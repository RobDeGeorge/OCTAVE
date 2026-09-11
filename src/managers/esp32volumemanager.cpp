/*
 * ESP32 Volume Manager for OCTAVE (C++ port)
 * Handles USB Serial connection to ESP32-S3 receiver dongle for volume control.
 * The ESP32-S3 receives commands from a remote ESP32 encoder via ESP-NOW.
 *
 * Text protocol:
 *   '+' or '+N'  — volume up (N ticks)
 *   '-' or '-N'  — volume down (N ticks)
 *   'M'          — mute toggle (encoder button press)
 *
 * Sends to ESP32:
 *   'V##\n'        — volume level (0-100) for LED ring
 *   'M0\n' / 'M1\n' — mute state
 *   'C###,###,###\n' — RGB theme color
 */

#include "esp32volumemanager.h"

#ifndef Q_OS_MOBILE

#include "settingsmanager.h"

#include <QVariant>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEsp32, "octave.esp32volume")

// ==================== Construction / Destruction ====================

ESP32VolumeManager::ESP32VolumeManager(SettingsManager *settingsManager, QObject *parent)
    : QObject(parent)
    , m_settingsManager(nullptr)
    , m_serialPort(nullptr)
    , m_connected(false)
    , m_connectionStatus(QStringLiteral("Disconnected"))
    , m_connectionDetail(QStringLiteral("Not connected"))
    , m_stepSize(1.0)
    , m_reconnectDelay(3000)
    , m_reconnectAttempts(0)
    , m_maxReconnectAttempts(5)
    , m_pendingVolume(0)
    , m_hasPendingVolume(false)
{
    // Reconnect timer
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &ESP32VolumeManager::onReconnectTimeout);

    // Port scanner timer (background scanning when disconnected)
    m_scanTimer.setInterval(10000);
    connect(&m_scanTimer, &QTimer::timeout, this, &ESP32VolumeManager::onScanTimeout);

    // Startup timer — delay initial connection
    m_startupTimer.setSingleShot(true);
    m_startupTimer.setInterval(3000);
    connect(&m_startupTimer, &QTimer::timeout, this, &ESP32VolumeManager::onStartupTimeout);

    // Volume update rate limiting to prevent LED flickering
    m_volumeSendTimer.setSingleShot(true);
    m_volumeSendTimer.setInterval(50); // 50ms debounce = max 20 updates/sec
    connect(&m_volumeSendTimer, &QTimer::timeout, this, &ESP32VolumeManager::onVolumeSendTimeout);

    // Keepalive timer — sends periodic volume updates to prevent ESP32 timeout
    m_keepaliveTimer.setInterval(2000); // Every 2 seconds (ESP32 timeout is 5s)
    connect(&m_keepaliveTimer, &QTimer::timeout, this, &ESP32VolumeManager::onKeepaliveTimeout);

    // Connect to settings if provided
    if (settingsManager) {
        connect_settings_manager(settingsManager);
    }
}

ESP32VolumeManager::~ESP32VolumeManager()
{
    cleanup();
}

// ==================== Settings Manager ====================

void ESP32VolumeManager::connect_settings_manager(SettingsManager *settingsManager)
{
    if (m_settingsManager)
        return;
    m_settingsManager = settingsManager;
    connectSettingsSignals();
    m_startupTimer.start();
}

void ESP32VolumeManager::connectSettingsSignals()
{
    if (!m_settingsManager)
        return;

    // These connect to signals that SettingsManager must declare.
    // The slot names match the Python signal names exactly.
    connect(m_settingsManager, SIGNAL(esp32VolumePortChanged()),
            this, SLOT(onPortChanged()));
    connect(m_settingsManager, SIGNAL(esp32VolumeEnabledChanged()),
            this, SLOT(onEnabledChanged()));
    connect(m_settingsManager, SIGNAL(esp32VolumeStepSizeChanged()),
            this, SLOT(onStepSizeChanged()));
    connect(m_settingsManager, SIGNAL(esp32AutoReconnectChanged()),
            this, SLOT(onAutoReconnectChanged()));
    connect(m_settingsManager, SIGNAL(esp32LedSleepEnabledChanged()),
            this, SLOT(onLedSleepChanged()));

    // Load initial step size via property or method — we use dynamic property
    // access so we don't need to include the full SettingsManager header.
    QVariant stepVar = m_settingsManager->property("esp32VolumeStepSize");
    if (stepVar.isValid())
        m_stepSize = stepVar.toDouble();
}

// ==================== Platform Detection ====================

QString ESP32VolumeManager::detectPlatform() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

// ==================== Device Discovery ====================

QStringList ESP32VolumeManager::scanForDevices()
{
    QStringList ports;
    m_esp32S3Port.clear();

    const auto portInfos = QSerialPortInfo::availablePorts();

    // OBD port to exclude
    QString obdPort;
    if (m_settingsManager) {
        QVariant v = m_settingsManager->property("obdBluetoothPort");
        if (v.isValid())
            obdPort = v.toString();
    }

    for (const QSerialPortInfo &info : portInfos) {
        const QString desc = info.description().toLower();
        const QString portName = info.portName();
        const QString systemLocation = info.systemLocation();

        // Skip OBD port
        if (!obdPort.isEmpty() && systemLocation == obdPort)
            continue;

        // Check for ESP32-S3 (Espressif VID = 0x303A)
        bool isEsp32S3 = false;
        if (info.vendorIdentifier() == 0x303A) {
            isEsp32S3 = true;
        } else if (desc.contains(QStringLiteral("jtag"))
                   || desc.contains(QStringLiteral("esp32-s3"))
                   || desc.contains(QStringLiteral("esp32s3"))) {
            isEsp32S3 = true;
        }

        if (isEsp32S3) {
            ports.prepend(systemLocation);
            m_esp32S3Port = systemLocation;
            qCInfo(lcEsp32) << "detected ESP32-S3 on" << systemLocation;
        } else {
            // Accept common USB-serial chips
            bool isUsbSerial = desc.contains(QStringLiteral("cp210"))
                || desc.contains(QStringLiteral("ch340"))
                || desc.contains(QStringLiteral("ch910"))
                || desc.contains(QStringLiteral("ftdi"))
                || desc.contains(QStringLiteral("usb serial"))
                || desc.contains(QStringLiteral("silicon labs"))
                || desc.contains(QStringLiteral("prolific"));

            if (isUsbSerial || !systemLocation.isEmpty()) {
                ports.append(systemLocation);
            }
        }
    }

    ports.removeDuplicates();
    ports.sort();

    // Convert to QVariantList for comparison / emission
    QVariantList portsList;
    for (const QString &p : ports)
        portsList.append(p);

    if (portsList != m_availablePorts) {
        m_availablePorts = portsList;
        emit availablePortsChanged(m_availablePorts);
        qCDebug(lcEsp32) << "discovered ports:" << ports;
    }

    return ports;
}

// ==================== Settings Change Handlers ====================

void ESP32VolumeManager::onPortChanged()
{
    if (!m_settingsManager)
        return;
    QString newPort = m_settingsManager->property("esp32VolumePort").toString();
    qCInfo(lcEsp32) << "port changed to" << newPort;

    QVariant enabledVar = m_settingsManager->property("esp32VolumeEnabled");
    if (enabledVar.toBool() && !newPort.isEmpty()) {
        disconnect_device();
        m_reconnectAttempts = 0;
        QTimer::singleShot(500, this, &ESP32VolumeManager::connect_device);
    }
}

void ESP32VolumeManager::onEnabledChanged()
{
    if (!m_settingsManager)
        return;
    bool enabled = m_settingsManager->property("esp32VolumeEnabled").toBool();
    qCInfo(lcEsp32) << "enabled changed to" << enabled;
    if (enabled) {
        connect_device();
    } else {
        disconnect_device();
        m_scanTimer.stop();
    }
}

void ESP32VolumeManager::onStepSizeChanged()
{
    if (!m_settingsManager)
        return;
    m_stepSize = m_settingsManager->property("esp32VolumeStepSize").toDouble();
    qCDebug(lcEsp32) << "step size changed to" << m_stepSize;
}

void ESP32VolumeManager::onAutoReconnectChanged()
{
    if (!m_settingsManager)
        return;
    if (!m_settingsManager->property("esp32AutoReconnect").toBool()) {
        m_reconnectTimer.stop();
    }
}

void ESP32VolumeManager::onLedSleepChanged()
{
    if (!m_settingsManager)
        return;
    bool sleepEnabled = m_settingsManager->property("esp32LedSleepEnabled").toBool();
    qCInfo(lcEsp32) << "LED sleep setting changed to" << sleepEnabled;

    if (m_connected) {
        if (sleepEnabled) {
            m_keepaliveTimer.stop();
            qCInfo(lcEsp32) << "keepalive timer stopped (sleep enabled)";
        } else {
            m_keepaliveTimer.start();
            qCInfo(lcEsp32) << "keepalive timer started (sleep disabled)";
        }
    }
}

// ==================== Connection Management ====================

void ESP32VolumeManager::initialConnect()
{
    if (!m_settingsManager)
        return;

    scanForDevices();

    bool enabled = m_settingsManager->property("esp32VolumeEnabled").toBool();
    if (!enabled) {
        qCDebug(lcEsp32) << "disabled, skipping initial connection";
        return;
    }

    QString port = m_settingsManager->property("esp32VolumePort").toString();

    // Auto-select ESP32-S3 if detected and no port configured
    if (port.isEmpty() && !m_esp32S3Port.isEmpty()) {
        qCInfo(lcEsp32) << "auto-selecting ESP32-S3 on" << m_esp32S3Port;
        QMetaObject::invokeMethod(m_settingsManager, "save_esp32_volume_port",
                                  Q_ARG(QString, m_esp32S3Port));
        port = m_esp32S3Port;
    }
    // Fallback: try first available USB serial port
    else if (port.isEmpty() && !m_availablePorts.isEmpty()) {
        QString obdPort = m_settingsManager->property("obdBluetoothPort").toString();
        for (const QVariant &v : m_availablePorts) {
            QString p = v.toString();
            if (p != obdPort) {
                qCInfo(lcEsp32) << "auto-selecting port" << p;
                QMetaObject::invokeMethod(m_settingsManager, "save_esp32_volume_port",
                                          Q_ARG(QString, p));
                port = p;
                break;
            }
        }
    }

    if (!port.isEmpty()) {
        qCInfo(lcEsp32) << "starting initial connection...";
        connect_device();
    } else {
        qCInfo(lcEsp32) << "enabled but no port configured";
        m_connectionDetail = QStringLiteral("No port configured - select one above");
        emit connectionDetailChanged(m_connectionDetail);
    }
}

void ESP32VolumeManager::connect_device()
{
    qCInfo(lcEsp32) << "connect_device() called";

    {
        QMutexLocker locker(&m_lock);
        if (m_connected) {
            qCDebug(lcEsp32) << "already connected";
            return;
        }
    }

    if (!m_settingsManager) {
        qCWarning(lcEsp32) << "no settings manager!";
        return;
    }

    bool enabled = m_settingsManager->property("esp32VolumeEnabled").toBool();
    QString port = m_settingsManager->property("esp32VolumePort").toString();
    qCInfo(lcEsp32) << "enabled=" << enabled << "port=" << port;

    if (!enabled) {
        qCDebug(lcEsp32) << "disabled, not connecting";
        return;
    }

    if (port.isEmpty()) {
        m_connectionStatus = QStringLiteral("Not Configured");
        m_connectionDetail = QStringLiteral("No port configured");
        emit connectionStatusChanged(m_connectionStatus);
        emit connectionDetailChanged(m_connectionDetail);
        m_scanTimer.start();
        return;
    }

    m_connectionStatus = QStringLiteral("Connecting");
    m_connectionDetail = QStringLiteral("Connecting to %1...").arg(port);
    emit connectionStatusChanged(m_connectionStatus);
    emit connectionDetailChanged(m_connectionDetail);

    // Create serial port
    if (m_serialPort) {
        m_serialPort->close();
        m_serialPort->deleteLater();
        m_serialPort = nullptr;
    }

    m_serialPort = new QSerialPort(this);
    m_serialPort->setPortName(port);
    m_serialPort->setBaudRate(115200);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_serialPort, &QSerialPort::readyRead,
            this, &ESP32VolumeManager::onSerialReadyRead);
    connect(m_serialPort, &QSerialPort::errorOccurred,
            this, &ESP32VolumeManager::onSerialError);

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        qCWarning(lcEsp32) << "connection failed:" << m_serialPort->errorString();
        {
            QMutexLocker locker(&m_lock);
            m_connected = false;
        }
        m_connectionStatus = QStringLiteral("Error");
        m_connectionDetail = QStringLiteral("Connection failed: %1").arg(m_serialPort->errorString());
        emit connectionStatusChanged(m_connectionStatus);
        emit connectionDetailChanged(m_connectionDetail);
        scheduleReconnect();
        return;
    }

    {
        QMutexLocker locker(&m_lock);
        m_connected = true;
    }

    m_connectionStatus = QStringLiteral("Connected");
    m_connectionDetail = QStringLiteral("Connected to ESP32 on %1").arg(port);
    emit connectionStatusChanged(m_connectionStatus);
    emit connectionDetailChanged(m_connectionDetail);
    qCInfo(lcEsp32) << "connected to" << port;

    m_reconnectAttempts = 0;
    m_readBuffer.clear();

    // Start keepalive (only if sleep is disabled)
    bool sleepEnabled = m_settingsManager
        ? m_settingsManager->property("esp32LedSleepEnabled").toBool()
        : false;
    if (!sleepEnabled) {
        m_keepaliveTimer.start();
        qCInfo(lcEsp32) << "keepalive timer started (sleep disabled)";
    } else {
        qCInfo(lcEsp32) << "keepalive timer NOT started (sleep enabled)";
    }

    // Stop scanning while connected
    m_scanTimer.stop();
}

void ESP32VolumeManager::disconnect_device()
{
    qCInfo(lcEsp32) << "disconnecting...";

    m_reconnectTimer.stop();
    m_keepaliveTimer.stop();

    if (m_serialPort) {
        m_serialPort->close();
        m_serialPort->deleteLater();
        m_serialPort = nullptr;
    }

    {
        QMutexLocker locker(&m_lock);
        m_connected = false;
    }

    m_connectionStatus = QStringLiteral("Disconnected");
    m_connectionDetail = QStringLiteral("Disconnected from ESP32");
    emit connectionStatusChanged(m_connectionStatus);
    emit connectionDetailChanged(m_connectionDetail);
}

void ESP32VolumeManager::scheduleReconnect()
{
    if (!m_settingsManager)
        return;

    if (!m_settingsManager->property("esp32AutoReconnect").toBool()) {
        qCDebug(lcEsp32) << "auto-reconnect disabled";
        m_scanTimer.start();
        return;
    }

    if (m_reconnectAttempts >= m_maxReconnectAttempts) {
        qCWarning(lcEsp32) << "max reconnect attempts (" << m_maxReconnectAttempts << ") reached";
        m_connectionDetail = QStringLiteral("Max retries reached. Check connection.");
        emit connectionDetailChanged(m_connectionDetail);
        m_scanTimer.start();
        return;
    }

    m_reconnectAttempts++;
    int delay = qMin(10000, m_reconnectDelay * m_reconnectAttempts);

    qCInfo(lcEsp32) << "scheduling reconnect in" << delay << "ms (attempt"
                     << m_reconnectAttempts << "/" << m_maxReconnectAttempts << ")";
    m_connectionDetail = QStringLiteral("Reconnecting in %1s... (%2/%3)")
        .arg(delay / 1000)
        .arg(m_reconnectAttempts)
        .arg(m_maxReconnectAttempts);
    emit connectionDetailChanged(m_connectionDetail);

    m_reconnectTimer.setInterval(delay);
    m_reconnectTimer.start();
}

void ESP32VolumeManager::attemptReconnect()
{
    if (m_connected)
        return;
    qCInfo(lcEsp32) << "attempting reconnect...";
    connect_device();
}

void ESP32VolumeManager::handleDisconnect()
{
    {
        QMutexLocker locker(&m_lock);
        if (!m_connected)
            return;
        m_connected = false;
    }

    m_connectionStatus = QStringLiteral("Disconnected");
    m_connectionDetail = QStringLiteral("Connection lost");
    emit connectionStatusChanged(m_connectionStatus);
    emit connectionDetailChanged(m_connectionDetail);

    qCWarning(lcEsp32) << "connection lost, scheduling reconnect";
    scheduleReconnect();
}

// ==================== Serial I/O ====================

void ESP32VolumeManager::onSerialReadyRead()
{
    if (!m_serialPort)
        return;

    QByteArray data = m_serialPort->readAll();
    m_readBuffer.append(QString::fromUtf8(data));

    // Process complete lines
    while (m_readBuffer.contains(QLatin1Char('\n'))) {
        int idx = m_readBuffer.indexOf(QLatin1Char('\n'));
        QString line = m_readBuffer.left(idx).trimmed();
        m_readBuffer = m_readBuffer.mid(idx + 1);
        if (!line.isEmpty()) {
            processCommand(line);
        }
    }
}

void ESP32VolumeManager::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    switch (error) {
    // ResourceError typically means the device was unplugged; the others
    // are the SerialException-class failures the Python backend treats as a
    // lost link (pyserial raises for a vanished/unreadable/unwritable port).
    case QSerialPort::ResourceError:
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::PermissionError:
    case QSerialPort::ReadError:
    case QSerialPort::WriteError:
        qCWarning(lcEsp32) << "serial error:" << error << "-"
                           << (m_serialPort ? m_serialPort->errorString() : QString());
        // No-op while not connected (e.g. a failed open(), which schedules
        // its own reconnect); otherwise routes into the reconnect path.
        handleDisconnect();
        break;
    default:
        qCWarning(lcEsp32) << "serial error:" << error;
        break;
    }
}

bool ESP32VolumeManager::writeCommand(const QByteArray &data, const char *what)
{
    if (!m_serialPort)
        return false;
    const qint64 written = m_serialPort->write(data);
    if (written != data.size()) {
        qCWarning(lcEsp32) << "failed to send" << what << ":"
                           << m_serialPort->errorString();
        return false;
    }
    return true;
}

void ESP32VolumeManager::processCommand(const QString &command)
{
    QString cmd = command.trimmed().toUpper();
    qCDebug(lcEsp32) << "received command:" << cmd;

    if (cmd == QLatin1String("+")) {
        // Single tick clockwise
        emit volumeChangeRequested(static_cast<float>(m_stepSize));
    } else if (cmd == QLatin1String("-")) {
        // Single tick counter-clockwise
        emit volumeChangeRequested(static_cast<float>(-m_stepSize));
    } else if (cmd.startsWith(QLatin1Char('+')) && cmd.length() > 1) {
        // Multiple ticks clockwise: +3
        bool ok = false;
        int ticks = cmd.mid(1).toInt(&ok);
        if (ok) {
            emit volumeChangeRequested(static_cast<float>(m_stepSize * ticks));
        } else {
            qCWarning(lcEsp32) << "invalid command format:" << cmd;
        }
    } else if (cmd.startsWith(QLatin1Char('-')) && cmd.length() > 1) {
        // Multiple ticks counter-clockwise: -2
        bool ok = false;
        int ticks = cmd.mid(1).toInt(&ok);
        if (ok) {
            emit volumeChangeRequested(static_cast<float>(-m_stepSize * ticks));
        } else {
            qCWarning(lcEsp32) << "invalid command format:" << cmd;
        }
    } else if (cmd == QLatin1String("M")) {
        // Mute toggle (encoder button press)
        emit muteToggleRequested();
    } else if (cmd == QLatin1String("P")) {
        // Play/pause (optional - double-click)
        qCDebug(lcEsp32) << "play/pause command (not implemented)";
    } else {
        qCDebug(lcEsp32) << "unknown command:" << cmd;
    }
}

// ==================== Public Slots for QML ====================

QVariantList ESP32VolumeManager::get_available_ports()
{
    scanForDevices();
    return m_availablePorts;
}

QString ESP32VolumeManager::get_ports_with_descriptions()
{
    QJsonArray portsArray;
    QString obdPort;
    if (m_settingsManager)
        obdPort = m_settingsManager->property("obdBluetoothPort").toString();

    const auto portInfos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portInfos) {
        QString device = info.systemLocation();

        // Skip OBD port
        if (device == obdPort)
            continue;

        QString desc = info.description();
        if (desc.isEmpty())
            desc = QStringLiteral("Unknown Device");

        // Identify ESP32-S3
        bool isEsp32S3 = (info.vendorIdentifier() == 0x303A)
            || desc.toLower().contains(QStringLiteral("jtag"))
            || desc.toLower().contains(QStringLiteral("esp32-s3"));

        // Build display label
        QString label;
        if (isEsp32S3) {
            label = QStringLiteral("%1 - ESP32-S3 (Recommended)").arg(device);
        } else {
            label = QStringLiteral("%1 - %2").arg(device, desc);
        }

        QJsonObject obj;
        obj[QStringLiteral("port")] = device;
        obj[QStringLiteral("label")] = label;
        obj[QStringLiteral("description")] = desc;
        obj[QStringLiteral("isEsp32S3")] = isEsp32S3;
        portsArray.append(obj);
    }

    // Sort: ESP32-S3 first, then by port name
    // QJsonArray doesn't support in-place sort, so we sort via a QList
    QList<QJsonObject> sorted;
    for (const QJsonValue &v : portsArray)
        sorted.append(v.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        bool aEsp = a[QStringLiteral("isEsp32S3")].toBool();
        bool bEsp = b[QStringLiteral("isEsp32S3")].toBool();
        if (aEsp != bEsp)
            return aEsp; // ESP32-S3 first
        return a[QStringLiteral("port")].toString() < b[QStringLiteral("port")].toString();
    });

    QJsonArray sortedArray;
    for (const QJsonObject &obj : sorted)
        sortedArray.append(obj);

    return QString::fromUtf8(QJsonDocument(sortedArray).toJson(QJsonDocument::Compact));
}

bool ESP32VolumeManager::is_connected()
{
    return m_connected;
}

QString ESP32VolumeManager::get_connection_status()
{
    return m_connectionStatus;
}

QString ESP32VolumeManager::get_connection_detail()
{
    return m_connectionDetail;
}

void ESP32VolumeManager::refresh_ports()
{
    scanForDevices();
}

void ESP32VolumeManager::reset_reconnect_attempts()
{
    m_reconnectAttempts = 0;
}

// ==================== Properties ====================

bool ESP32VolumeManager::getConnected() const
{
    return m_connected;
}

QString ESP32VolumeManager::getConnectionStatusProp() const
{
    return m_connectionStatus;
}

QString ESP32VolumeManager::getConnectionDetailProp() const
{
    return m_connectionDetail;
}

QVariantList ESP32VolumeManager::getAvailablePortsProp() const
{
    return m_availablePorts;
}

// ==================== Send Commands to ESP32 ====================

void ESP32VolumeManager::send_volume_update(int volume)
{
    if (!m_connected || !m_serialPort)
        return;

    m_pendingVolume = volume;
    m_hasPendingVolume = true;

    if (!m_volumeSendTimer.isActive())
        m_volumeSendTimer.start();
}

void ESP32VolumeManager::sendPendingVolume()
{
    if (!m_hasPendingVolume)
        return;

    if (!m_connected || !m_serialPort) {
        m_hasPendingVolume = false;
        return;
    }

    QString cmd = QStringLiteral("V%1\n").arg(m_pendingVolume);
    qCInfo(lcEsp32) << "sending volume command:" << cmd.trimmed();
    writeCommand(cmd.toUtf8(), "volume");
    m_hasPendingVolume = false;
}

void ESP32VolumeManager::sendKeepalive()
{
    if (!m_connected || !m_serialPort)
        return;

    if (m_settingsManager) {
        QVariant volVar = m_settingsManager->property("currentVolume");
        int volume = volVar.isValid() ? volVar.toInt() : 50;
        QString cmd = QStringLiteral("V%1\n").arg(volume);
        // Don't log successful keepalives to avoid spam. A rejected keepalive
        // write means the port is gone — treat as a lost link, like Python.
        if (!writeCommand(cmd.toUtf8(), "keepalive"))
            handleDisconnect();
    }
}

void ESP32VolumeManager::send_mute_state(bool muted)
{
    if (!m_connected || !m_serialPort) {
        qCInfo(lcEsp32) << "send_mute_state(" << muted << ") - not connected, skipping";
        return;
    }

    QString cmd = QStringLiteral("M%1\n").arg(muted ? 1 : 0);
    qCInfo(lcEsp32) << "sending mute command:" << cmd.trimmed();
    writeCommand(cmd.toUtf8(), "mute state");
}

void ESP32VolumeManager::send_theme_color(int r, int g, int b)
{
    if (!m_connected || !m_serialPort) {
        qCInfo(lcEsp32) << "send_theme_color(" << r << g << b << ") - not connected, skipping";
        return;
    }

    QString cmd = QStringLiteral("C%1,%2,%3\n").arg(r).arg(g).arg(b);
    qCInfo(lcEsp32) << "sending color command:" << cmd.trimmed();
    if (writeCommand(cmd.toUtf8(), "theme color"))
        qCDebug(lcEsp32) << "sent theme color: RGB(" << r << g << b << ")";
}

// ==================== Timer Slots ====================

void ESP32VolumeManager::onReconnectTimeout()
{
    attemptReconnect();
}

void ESP32VolumeManager::onScanTimeout()
{
    scanForDevices();
}

void ESP32VolumeManager::onStartupTimeout()
{
    initialConnect();
}

void ESP32VolumeManager::onVolumeSendTimeout()
{
    sendPendingVolume();
}

void ESP32VolumeManager::onKeepaliveTimeout()
{
    sendKeepalive();
}

// ==================== Cleanup ====================

void ESP32VolumeManager::cleanup()
{
    qCInfo(lcEsp32) << "cleaning up...";
    m_reconnectTimer.stop();
    m_scanTimer.stop();
    m_startupTimer.stop();
    m_volumeSendTimer.stop();
    m_keepaliveTimer.stop();
    disconnect_device();
}

#endif // Q_OS_MOBILE
