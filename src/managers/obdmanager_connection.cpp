// OBDManager: finding the adapter and holding the link. Port discovery, the
// connection attempt and its teardown, retries, the stale-data watchdog, the
// unified adapter API and the Linux rfcomm bind.
#include "obdmanager.h"
#include "../sim/simelm327.h"
#include "elm327protocol.h"
#include "rfcommsocket.h"
#include "../util/perfscope.h"
#include "../util/threadidle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QVariantMap>
#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------

OBDManager::Platform OBDManager::detectPlatform() const
{
#ifdef Q_OS_ANDROID
    return Platform::Android;
#elif defined(Q_OS_WIN)
    return Platform::Windows;
#elif defined(Q_OS_MACOS)
    return Platform::macOS;
#else
    return Platform::Linux;
#endif
}

// ---------------------------------------------------------------------------
// Configuration helpers
// ---------------------------------------------------------------------------

bool OBDManager::isSimulated() const
{
    return m_portOverride == QLatin1String(OctaveSim::kSimPort);
}

void OBDManager::setPortOverride(const QString &port)
{
    const bool wasSimulated = isSimulated();
    m_portOverride = port;
    // An explicit choice supersedes a pending settings change / rfcomm bind.
    m_portChangeTimer.stop();
    m_pendingPort.clear();
    ++m_rfcommBindGeneration;
    if (wasSimulated != isSimulated())
        emit simulatedChanged();
}

QVariantMap OBDManager::simulatedAdapter()
{
    const QString address = QString::fromLatin1(OctaveSim::kSimPort);
    return {{"address", address}, {"identifier", address}, {"name", "Simulated car"},
            {"rssi", 0}, {"kind", "sim"}, {"likelyObd", true}, {"paired", false},
            {"connectable", true}, {"note", "No hardware: a demo drive"}};
}

QString OBDManager::getConfiguredPort() const
{
    if (!m_portOverride.isEmpty())
        return m_portOverride;
    if (m_settingsManager) {
        QString port = m_settingsManager->obdBluetoothPort();
        if (!port.isEmpty())
            return port;
    }
    // Platform defaults
    switch (detectPlatform()) {
    case Platform::Windows: return QStringLiteral("COM3");
    case Platform::macOS:   return QStringLiteral("/dev/tty.OBD");
    case Platform::Android: return QString();  // user must enter MAC
    default:                return QStringLiteral("/dev/rfcomm0");
    }
}

bool OBDManager::checkPortExists(const QString &port) const
{
    // A direct Bluetooth link has no device node; reachability is only
    // known by connecting (RfcommSocket::connectTo reports why it failed)
    if (RfcommSocket::isUrl(port))
        return RfcommSocket::parseUrl(port, nullptr, nullptr);
    if (OBDConnectionWorker::transportClaims(port))
        return true;
    if (detectPlatform() == Platform::Android) {
        // On Android, "port" is a Bluetooth MAC address. We don't probe the
        // adapter here -- a real reachability test happens when
        // OctaveOBDBridge.connect() issues connectGatt(). Just validate format.
        static const QRegularExpression macRe(
            QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
        return macRe.match(port).hasMatch();
    }
    if (detectPlatform() == Platform::Windows) {
        const auto infos = QSerialPortInfo::availablePorts();
        for (const auto &info : infos) {
            if (info.portName() == port || info.systemLocation() == port)
                return true;
        }
        return false;
    }
    // Unix: check device file
    return QDir().exists(port);
}

int OBDManager::getMaxReconnectAttempts() const
{
    if (m_settingsManager)
        return m_settingsManager->obdAutoReconnectAttempts();
    return 0;
}

bool OBDManager::isAutoReconnectEnabled() const
{
    return getMaxReconnectAttempts() > 0;
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

void OBDManager::onStartupTimer()
{
    qDebug() << "[OBD] Starting initial connection...";
    scanForDevices();
    m_deviceScannerTimer.start();
    startConnection();
}

// ---------------------------------------------------------------------------
// Device scanning
// ---------------------------------------------------------------------------

void OBDManager::scanForDevices(bool passiveRetry)
{
    QStringList ports;

    switch (detectPlatform()) {
    case Platform::Windows: {
        const auto infos = QSerialPortInfo::availablePorts();
        for (const auto &info : infos) {
            const QString desc = info.description().toLower();
            if (desc.contains(QStringLiteral("bluetooth")) ||
                desc.contains(QStringLiteral("obd")) ||
                desc.contains(QStringLiteral("elm")) ||
                desc.contains(QStringLiteral("serial")) ||
                desc.contains(QStringLiteral("usb")) ||
                info.portName().startsWith(QStringLiteral("COM"))) {
                ports.append(info.portName());
            }
        }
        break;
    }
    case Platform::macOS: {
        QDir devDir(QStringLiteral("/dev"));
        const QStringList patterns = {
            QStringLiteral("tty.OBD*"), QStringLiteral("tty.Bluetooth*"),
            QStringLiteral("cu.OBD*"),  QStringLiteral("cu.Bluetooth*"),
            QStringLiteral("tty.usbserial*"), QStringLiteral("cu.usbserial*"),
            QStringLiteral("tty.*ELM*"), QStringLiteral("cu.*ELM*"),
        };
        for (const QString &pattern : patterns) {
            const auto matches = devDir.entryList({pattern}, QDir::System);
            for (const QString &m : matches)
                ports.append(QStringLiteral("/dev/") + m);
        }
        break;
    }
    case Platform::Linux: {
        QDir devDir(QStringLiteral("/dev"));
        const QStringList patterns = {
            QStringLiteral("rfcomm*"),
            QStringLiteral("ttyUSB*"),
            QStringLiteral("ttyACM*"),
        };
        for (const QString &pattern : patterns) {
            const auto matches = devDir.entryList({pattern}, QDir::System);
            for (const QString &m : matches)
                ports.append(QStringLiteral("/dev/") + m);
        }
        break;
    }
    case Platform::Android: {
#ifdef Q_OS_ANDROID
        // List paired Bluetooth devices (MAC addresses) -- the user must pair
        // their ELM327 in the Android Settings app first. We don't run an
        // active discovery here because Qt's QBluetoothDeviceDiscoveryAgent
        // has historically crashed on Android with NullPointerException; the
        // bonded list is enough for the typical flow (pair once, reconnect by
        // MAC thereafter).
        ports = listAndroidPairedDevices();
#endif
        break;
    }
    }

    // A port served by a registered transport (the dev simulator) has no
    // device node to find; list it so the adapter list shows it
    if (OBDConnectionWorker::transportClaims(getConfiguredPort()))
        ports.append(getConfiguredPort());

    // Remove duplicates and sort
    ports.removeDuplicates();
    ports.sort();

    if (ports != m_availablePorts) {
        m_availablePorts = ports;

        QVariantList varPorts;
        for (const QString &p : ports)
            varPorts.append(p);
        emit availablePortsChanged(varPorts);
        emit availableAdaptersChanged(buildAdapterList(ports));

        qDebug() << "[OBD] Discovered ports:" << ports;

        // If we found a port and we're not connected, try to connect
        if (!ports.isEmpty() && !m_connected && !m_isConnecting) {
            QString configured = getConfiguredPort();
            if (ports.contains(configured)) {
                qDebug() << "[OBD] Configured port" << configured << "available, connecting...";
                m_connectionAttempts = 0;
                startConnection();
            } else {
                setConnectionDetail(
                    QStringLiteral("Device at %1 not found. Available: %2")
                        .arg(configured, ports.join(QStringLiteral(", "))));
            }
        }
    }

    // A direct Bluetooth adapter never shows up as a new /dev node, so the
    // passive scan just retries it (every 10 s scan tick): the dongle is
    // usually only powered with the ignition on, and connectTo() fails
    // fast with "Host is down" while it is off
    const QString configured = getConfiguredPort();
    // The same for any adapter that answered while the car did not (ignition
    // off at boot, key off and on again): its /dev node never changes, so
    // without this it sat at "waiting for vehicle" until someone tapped Connect
    //
    // Only on the scan timer's own tick: a ports query (get_available_ports,
    // refresh_ports, scan: read scope) asks what is there and starts nothing.
    // And not while a timed retry is pending: this used to zero the attempt
    // count under it every 10 s, so obdAutoReconnectAttempts never ran out
    // in this state. Once the timed retries are used up they stay used up
    // (the count is kept) and this is one quiet attempt per tick.
    if (passiveRetry
        && (RfcommSocket::isUrl(configured) || OBDConnectionWorker::transportClaims(configured)
            || (m_waitingForVehicle && checkPortExists(configured)))
        && !m_connected && !m_isConnecting && !m_retryTimer.isActive()
        && !m_forceStopReconnect && !m_diagnosticMode) {
        qDebug() << "[OBD] Retrying direct Bluetooth adapter" << configured;
        if (!isAutoReconnectEnabled())
            m_connectionAttempts = 0;   // no timed retries to count: every tick is attempt 1
        startConnection();
    }

}

void OBDManager::onDeviceScanTimer()
{
    scanForDevices(true);
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

void OBDManager::startConnection()
{
    if (m_forceStopReconnect)
        return;
    // Callers that mean "again" (reconnect(), force_connect(), a port
    // change) clean the old connection up first; anything else arriving
    // while connected is late, and must not rebuild a working link
    if (m_connected) {
        qDebug() << "[OBD] Already connected -- skipping";
        return;
    }
    m_retryTimer.stop();
    if (m_isConnecting) {
        qDebug() << "[OBD] Already connecting -- skipping";
        return;
    }
    m_isConnecting = true;

    m_connectionAttempts++;
    qDebug() << "[OBD] Starting connection attempt #" << m_connectionAttempts;

    setConnectionStatus(QStringLiteral("Connecting"));
    setConnectionDetail(
        QStringLiteral("Attempt %1...").arg(m_connectionAttempts));
    setConnectionProgress(10);

#ifdef Q_OS_ANDROID
    // Real Android BLE uses Java; custom adapters (including the simulator)
    // use the same worker and ELM327 protocol as desktop and WebAssembly.
    if (!OBDConnectionWorker::transportClaims(getConfiguredPort())) {
        startAndroidConnection();
        return;
    }
#endif

    QString port = getConfiguredPort();
    m_activePort = port;
    bool fastMode = m_settingsManager ? m_settingsManager->obdFastMode() : true;

    // Check if device exists
    if (!checkPortExists(port)) {
        m_connected = false;
        setConnectionStatus(QStringLiteral("Device Not Found"));
        setConnectionDetail(QStringLiteral("Port %1 not available").arg(port));
        setConnectionProgress(0);
        emit devicePresenceChanged(false);
        qDebug() << "[OBD] Port" << port << "not found";
        m_isConnecting = false;
        scheduleAutoReconnect();
        return;
    }

    emit devicePresenceChanged(true);
    setConnectionProgress(15);
    setConnectionDetail(QStringLiteral("Found %1, connecting...").arg(port));

    // Clean up previous worker thread
    cleanupWorkerThread();

    // Create worker and thread
    m_worker = new OBDConnectionWorker();
    m_worker->setPort(port);
    m_worker->setFastMode(fastMode);
    m_worker->setTimeout(m_connectionTimeout);
    m_worker->setCustomPids(m_customPids);

    const QList<PidKey> pidsToWatch = buildPidsToWatch();
    const QList<PidKey> backgroundPids = buildBackgroundPids(pidsToWatch);
    m_hasActiveWatchers = !pidsToWatch.isEmpty() || !backgroundPids.isEmpty();
    m_worker->setPidsToWatch(pidsToWatch, backgroundPids);

    // No parent: cleanupWorkerThread() hands the thread its own deleteLater,
    // so a busy thread is never destroyed with the manager while running
    m_workerThread = new QThread();
    m_workerThread->setObjectName(QStringLiteral("obd-worker"));  // visible in top -H / ps
    m_worker->moveToThread(m_workerThread);
    sleepWhenIdle(m_workerThread);   // the browser build: see threadidle.h

    // Connect worker signals (queued connections -- cross-thread safe)
    QObject::connect(m_worker, &OBDConnectionWorker::initComplete,
                     this, &OBDManager::onWorkerInitComplete, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::dataReceived,
                     this, &OBDManager::onWorkerDataReceived, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::extendedDataReceived,
                     this, &OBDManager::onWorkerExtendedData, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::vinRead,
                     this, &OBDManager::onWorkerVin, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::connectionLost,
                     this, &OBDManager::onWorkerConnectionLost, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::dtcResult,
                     this, &OBDManager::onWorkerDtcResult, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::dtcCleared,
                     this, &OBDManager::onWorkerDtcCleared, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::milStatus,
                     this, &OBDManager::onWorkerMilStatus, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::freezeFrame,
                     this, &OBDManager::onWorkerFreezeFrame, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::scanProgress,
                     this, &OBDManager::onWorkerScanProgress, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::scanComplete,
                     this, &OBDManager::onWorkerScanComplete, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::scanOutput,
                     this, &OBDManager::onWorkerScanOutput, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::logLine,
                     this, &OBDManager::connectionLogLineAppended, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::busFrames,
                     this, &OBDManager::onWorkerBusFrames, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::busMonitorChanged,
                     this, &OBDManager::onWorkerBusMonitor, Qt::QueuedConnection);

    // Start connection on worker thread
    QObject::connect(m_workerThread, &QThread::started,
                     m_worker, &OBDConnectionWorker::doConnect);

    m_workerThread->start();
}

void OBDManager::cleanupConnection()
{
    // The worker's own "monitor ended" is disconnected below
    if (m_busMonitoring)
        onWorkerBusMonitor(false, QString());
    m_dataWatchdogTimer.stop();
    m_lastDataReceived = 0;
    setVehicleSupport({}, false);  // the next car may support a different set

#ifdef Q_OS_ANDROID
    cleanupAndroidConnection();
#endif

    // Tell worker to stop polling and disconnect
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "stopPolling", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_worker, "doDisconnect", Qt::QueuedConnection);
    }

    cleanupWorkerThread();
    m_connected = false;
    // A vehicle scan in flight died with the worker and its scanComplete is
    // disconnected: left set, every later scan_vehicle() said "already in
    // progress" until restart
    m_isScanning = false;
    // Any attempt in flight was just abandoned, and its initComplete is
    // disconnected, so nothing else would clear this. Left set, the
    // startConnection() that usually follows (reconnect(), the port-change
    // debounce) skipped as "Already connecting" and OBD sat in
    // "Connecting" for good (Pi, 2026-10-03).
    m_isConnecting = false;
}

void OBDManager::cleanupWorkerThread()
{
    if (!m_workerThread) {
        if (m_worker) {
            delete m_worker;
            m_worker = nullptr;
        }
        return;
    }

    // Detach first: a worker still unwinding a blocking doConnect() must not
    // deliver late results into the connection that replaces it.
    if (m_worker)
        QObject::disconnect(m_worker, nullptr, this, nullptr);

    // Never destroy a running QThread (Qt aborts the process). The thread
    // deletes itself and its worker once it has actually finished; a worker
    // stuck in doConnect() (adapter "SEARCHING...") sees the interruption
    // request at its next sendCommand() poll and unwinds within ~100 ms.
    QThread *thread = m_workerThread;
    OBDConnectionWorker *worker = m_worker;
    m_workerThread = nullptr;
    m_worker = nullptr;

    if (!thread->isRunning()) {
        delete worker;
        delete thread;
        return;
    }

    if (worker)
        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->requestInterruption();
    thread->quit();
    if (!thread->wait(2000))
        qWarning() << "[OBD] Worker thread still busy; it will clean up when it finishes";
}

void OBDManager::scheduleAutoReconnect()
{
    if (m_forceStopReconnect) {
        qDebug() << "[OBD] Reconnect stopped by force flag";
        return;
    }

    int maxAttempts = getMaxReconnectAttempts();

    if (!isAutoReconnectEnabled()) {
        qDebug() << "[OBD] Auto-reconnect disabled, switching to passive scanning";
        setConnectionDetail(QStringLiteral("Auto-reconnect disabled"));
        m_deviceScannerTimer.start();
        return;
    }

    if (m_connectionAttempts >= maxAttempts) {
        qDebug() << "[OBD] Max attempts reached, switching to passive scanning";
        setConnectionDetail(QStringLiteral("Max retries reached. Scanning..."));
        m_deviceScannerTimer.start();
        return;
    }

    int delay = qMin(10, 2 + m_connectionAttempts * 2);
    setConnectionDetail(
        QStringLiteral("Retry in %1s... (%2/%3)")
            .arg(delay).arg(m_connectionAttempts).arg(maxAttempts));
    qDebug() << "[OBD] Auto-reconnect in" << delay << "s";

    m_retryTimer.start(delay * 1000);
}

// ---------------------------------------------------------------------------
// Worker callbacks (called on main thread via queued connections)
// ---------------------------------------------------------------------------

void OBDManager::onWorkerInitComplete(bool success, const QString &message)
{
    qDebug() << "[OBD] Worker init complete:" << success << message;

    if (success) {
        m_connected = true;
        m_derived.connected(QDateTime::currentMSecsSinceEpoch());
        m_connectionAttempts = 0;
        m_reconnectingAfterDiagnostic = false;
        m_waitingForVehicle = false;
        m_retryTimer.stop();

        setConnectionStatus(QStringLiteral("Connected"));
        setConnectionDetail(QStringLiteral("OBD interface connected successfully"));
        setConnectionProgress(100);

        // Start polling
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "startPolling", Qt::QueuedConnection);

        // Start data watchdog
        m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
        m_dataWatchdogTimer.start();

        // Stop background scanning while connected
        m_deviceScannerTimer.stop();

        // Auto-scan after a short delay
        QTimer::singleShot(2000, this, &OBDManager::scan_vehicle);

    } else {
        m_connected = false;

        // Partial connection: the adapter answered and no vehicle did. Only
        // the worker's own message says so; "adapter" alone also matched
        // "adapter not responding" and any port error naming one.
        m_waitingForVehicle = message.contains(QStringLiteral("no vehicle response"));
        if (m_waitingForVehicle) {
            setConnectionStatus(QStringLiteral("No Vehicle"));
            setConnectionDetail(
                QStringLiteral("Connected to adapter, waiting for vehicle..."));
            setConnectionProgress(50);
        } else {
            setConnectionStatus(QStringLiteral("Connection Failed"));
            setConnectionDetail(
                QStringLiteral("Could not connect to OBD adapter"));
            setConnectionProgress(0);
        }

        // Post-diagnostic retry logic
        if (m_reconnectingAfterDiagnostic && m_connectionAttempts < 3) {
            qDebug() << "[OBD] Post-diagnostic retry" << (m_connectionAttempts + 1) << "/3";
            setConnectionDetail(
                QStringLiteral("Reconnecting after diagnostic mode... (%1/3)")
                    .arg(m_connectionAttempts + 1));
            m_retryTimer.start(2000);
        } else {
            m_reconnectingAfterDiagnostic = false;
            scheduleAutoReconnect();
        }
    }

    m_isConnecting = false;
}

void OBDManager::onWorkerConnectionLost(const QString &reason)
{
    qDebug() << "[OBD] Connection lost:" << reason;
    const bool wasConnected = m_connected;
    m_connected = false;
    setVehicleSupport({}, false);
    setConnectionStatus(QStringLiteral("Disconnected"));
    setConnectionDetail(reason);
    if (wasConnected)
        emit connectionLostUnexpectedly(reason);
    setConnectionProgress(0);
    m_dataWatchdogTimer.stop();
    m_deviceScannerTimer.start();
    scheduleAutoReconnect();
}

// ---------------------------------------------------------------------------
// Data watchdog
// ---------------------------------------------------------------------------

void OBDManager::onDataWatchdog()
{
    OCTAVE_PERF_SCOPE("obd._monitor_connection");
    if (!m_connected)
        return;
    if (!m_hasActiveWatchers)
        return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - m_lastDataReceived;

    if (m_lastDataReceived > 0 && elapsed > DATA_WATCHDOG_TIMEOUT_MS) {
        qCWarning(lcElm327) << "no data for" << elapsed << "ms; reconnecting";
        setConnectionStatus(QStringLiteral("Stale Connection"));
        setConnectionDetail(QStringLiteral("No data received, reconnecting..."));
        m_dataWatchdogTimer.stop();
        cleanupConnection();
        m_connectionAttempts = 0;
        m_retryTimer.start(100);
    }
}

// ---------------------------------------------------------------------------
// Settings callbacks
// ---------------------------------------------------------------------------

void OBDManager::onSettingsPortChanged()
{
    if (!m_settingsManager)
        return;
    QString newPort = m_settingsManager->obdBluetoothPort();
    if (isSimulated())
        setPortOverride(QString());
    if (!newPort.isEmpty() && newPort != m_pendingPort) {
        m_pendingPort = newPort;
        m_portChangeTimer.start();
        qDebug() << "[OBD] Port change scheduled:" << newPort;
    }
}

void OBDManager::onPortChangeDebounce()
{
    if (!m_pendingPort.isEmpty()) {
        // connect_to_adapter() saves the port and connects straight away;
        // the save lands here a second later. Reconnecting then would kill
        // that fresh attempt mid-handshake.
        if (m_pendingPort == m_activePort && (m_connected || m_isConnecting)) {
            qDebug() << "[OBD] Port change to" << m_pendingPort << "already in use";
            m_pendingPort.clear();
            return;
        }
        qDebug() << "[OBD] Port changed to:" << m_pendingPort;
        m_connectionAttempts = 0;
        reconnect();
        m_pendingPort.clear();
    }
}

// ---------------------------------------------------------------------------
// Public slots -- Connection management
// ---------------------------------------------------------------------------

void OBDManager::connect_obd()
{
    m_forceStopReconnect = false;
    startConnection();
}

void OBDManager::reconnect()
{
    m_forceStopReconnect = false;
    qCInfo(lcElm327) << "reconnect requested";
    cleanupConnection();
    m_connectionAttempts = 0;
    startConnection();
}

void OBDManager::force_connect()
{
    m_forceStopReconnect = false;
    qCInfo(lcElm327) << "connect requested";
    m_connectionAttempts = 0;
    // Always clear the in-progress flag — without this, a previous attempt
    // that wedged (BLE controller never reported back, GATT timed out
    // silently, etc.) would block every subsequent tap of Connect with
    // "Already connecting -- skipping".
    m_isConnecting = false;
    cleanupConnection();
    startConnection();
}

void OBDManager::close()
{
    m_forceStopReconnect = true;
    m_deviceScannerTimer.stop();
    m_dataWatchdogTimer.stop();
    m_retryTimer.stop();
    m_waitingForVehicle = false;
    cleanupConnection();
    if (isSimulated())
        setPortOverride(QString());
    // cleanupConnection() is silent (a reconnect says "Connecting" right
    // after it); here nothing follows, so say it, or connected,
    // connectionState and connectionStatus stay "Connected" for subscribers
    if (m_connectionStatus != QLatin1String("Disconnected")) {
        setConnectionStatus(QStringLiteral("Disconnected"));
        setConnectionDetail(QStringLiteral("Closed"));
        setConnectionProgress(0);
    }
}

void OBDManager::reset_connection()
{
    force_connect();
}

// ---------------------------------------------------------------------------
// Device discovery
// ---------------------------------------------------------------------------

void OBDManager::refresh_ports()
{
    scanForDevices();
}

QVariantList OBDManager::get_available_ports()
{
    scanForDevices();
    QVariantList result;
    for (const QString &p : m_availablePorts)
        result.append(p);
    return result;
}

// ---------------------------------------------------------------------------
// Unified adapter API
// ---------------------------------------------------------------------------
// The OBD settings page binds to these instead of forking on isAndroid /
// platform-specific port-string conventions. All variation lives here:
// scan() does the right discovery for the OS, availableAdapters returns a
// uniform {name, identifier, kind} list, connectToAdapter() classifies the
// identifier and (on Linux) auto-binds rfcomm before opening.

void OBDManager::scan()
{
    scanForDevices();
}

QString OBDManager::kindForIdentifier(const QString &id) const
{
    if (id == QLatin1String(OctaveSim::kSimPort)) return QStringLiteral("sim");
    static const QRegularExpression macRe(
        QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
    if (macRe.match(id).hasMatch())                     return QStringLiteral("ble");
    if (RfcommSocket::isUrl(id))                        return QStringLiteral("bt-direct");
    if (id.startsWith(QStringLiteral("/dev/rfcomm")))   return QStringLiteral("serial-rfcomm");
    if (id.startsWith(QStringLiteral("/dev/ttyUSB"))
        || id.startsWith(QStringLiteral("/dev/ttyACM"))) return QStringLiteral("serial-usb");
    if (id.startsWith(QStringLiteral("/dev/tty."))
        || id.startsWith(QStringLiteral("/dev/cu.")))    return QStringLiteral("tty");
    if (id.startsWith(QStringLiteral("COM")))            return QStringLiteral("com");
    return QStringLiteral("serial");
}

QString OBDManager::displayNameForIdentifier(const QString &id) const
{
    const QString kind = kindForIdentifier(id);
    if (kind == QLatin1String("sim")) return QStringLiteral("Simulated car");
#ifdef Q_OS_ANDROID
    if (kind == QStringLiteral("ble") && m_androidDeviceNames.contains(id))
        return QStringLiteral("%1 · %2").arg(m_androidDeviceNames.value(id), id);
#endif
    if (kind == QStringLiteral("ble"))           return QStringLiteral("Bluetooth · %1").arg(id);
    if (kind == QStringLiteral("serial-rfcomm")) return QStringLiteral("Bluetooth (bound) · %1").arg(id);
    if (kind == QStringLiteral("bt-direct")) {
        QString mac;
        int channel = 1;
        RfcommSocket::parseUrl(id, &mac, &channel);
        return channel == 1 ? QStringLiteral("Bluetooth (direct) · %1").arg(mac)
                            : QStringLiteral("Bluetooth (direct) · %1 ch %2").arg(mac).arg(channel);
    }
    if (kind == QStringLiteral("serial-usb"))    return QStringLiteral("USB ELM327 · %1").arg(id);
    if (kind == QStringLiteral("tty"))           return id;
    if (kind == QStringLiteral("com"))           return QStringLiteral("Serial · %1").arg(id);
    return id;
}

QVariantList OBDManager::buildAdapterList(const QStringList &ports) const
{
    QVariantList result;
    for (const QString &p : ports) {
        if (p == QLatin1String(OctaveSim::kSimPort))
            continue;
        QVariantMap entry;
        entry[QStringLiteral("identifier")] = p;
        entry[QStringLiteral("name")]       = displayNameForIdentifier(p);
        entry[QStringLiteral("kind")]       = kindForIdentifier(p);
        result.append(entry);
    }
    result.append(simulatedAdapter());
    return result;
}

QVariantList OBDManager::availableAdapters() const
{
    return buildAdapterList(m_availablePorts);
}

QString OBDManager::platform_hint() const
{
    switch (detectPlatform()) {
    case Platform::Windows: return QStringLiteral("windows");
    case Platform::macOS:   return QStringLiteral("macos");
    case Platform::Linux:   return QStringLiteral("linux");
    case Platform::Android: return QStringLiteral("android");
    }
    return QStringLiteral("unknown");
}

#ifdef Q_OS_LINUX
void OBDManager::startRfcommBind(const QString &mac)
{
    const quint64 generation = m_rfcommBindGeneration;

    // 1) If any /dev/rfcommN already exists, the user pre-bound it (or a
    //    previous run did) — just use the lowest-numbered one. We don't
    //    verify the bound MAC matches; the user typing a MAC into the field
    //    while a different rfcomm is already bound is exotic enough that
    //    surfacing the actual open() failure is better than guessing.
    for (int i = 0; i < 8; ++i) {
        const QString dev = QStringLiteral("/dev/rfcomm%1").arg(i);
        if (QDir().exists(dev)) {
            emit connectionLogLineAppended(
                QStringLiteral("rfcomm: using existing %1").arg(dev));
            finishRfcommBind(generation, dev);
            return;
        }
    }

    // 2) Try `rfcomm bind 0 <mac> 1`. Channel 1 is the universal SPP channel
    //    for ELM327 clones; if the adapter advertises a different channel
    //    the user can pre-bind manually. Async: a slow bind used to freeze
    //    the GUI thread for up to 7 s.
    emit connectionLogLineAppended(
        QStringLiteral("rfcomm: binding %1 → /dev/rfcomm0").arg(mac));

    if (m_rfcommBindProc) {
        // A newer connect superseded the running bind
        QProcess *old = m_rfcommBindProc;
        m_rfcommBindProc = nullptr;
        old->disconnect();
        old->kill();
        old->deleteLater();
    }

    auto *proc = new QProcess(this);
    m_rfcommBindProc = proc;

    auto *timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, proc, [this, proc]() {
        if (proc->state() == QProcess::NotRunning)
            return;
        proc->setProperty("timedOut", true);
        emit connectionLogLineAppended(QStringLiteral("rfcomm bind failed: timed out"));
        proc->kill();  // finished() follows with CrashExit
    });

    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, generation](QProcess::ProcessError error) {
        // Crashes and our timeout kill arrive via finished()
        if (error != QProcess::FailedToStart)
            return;
        emit connectionLogLineAppended(
            QStringLiteral("rfcomm bind failed: rfcomm binary not found "
                           "(install bluez-utils, or pre-bind and enter /dev/rfcomm0)"));
        releaseRfcommProc(proc);
        finishRfcommBind(generation, QString());
    });

    connect(proc, &QProcess::finished, this,
            [this, proc, mac, generation](int exitCode, QProcess::ExitStatus exitStatus) {
        const bool timedOut = proc->property("timedOut").toBool();
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        releaseRfcommProc(proc);

        // A timeout or crash is a failure even when exitCode() reads 0
        if (timedOut) {
            finishRfcommBind(generation, QString());
            return;
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            QString reason = err;
            if (reason.isEmpty()) {
                reason = exitStatus != QProcess::NormalExit
                    ? QStringLiteral("rfcomm crashed")
                    : QStringLiteral("non-zero exit");
            }
            emit connectionLogLineAppended(
                QStringLiteral("rfcomm bind failed: %1").arg(reason));
            emit connectionLogLineAppended(
                QStringLiteral("tip: add user to bluetooth group, or run "
                               "`sudo rfcomm bind 0 %1` once and enter /dev/rfcomm0")
                    .arg(mac));
            finishRfcommBind(generation, QString());
            return;
        }
        // Confirm the node now exists (rfcomm sometimes succeeds
        // asynchronously): 10 checks, 100 ms apart.
        waitForRfcommNode(generation, 9);
    });

    // OCTAVE_RFCOMM_PROGRAM stands in for rfcomm in tests (tests/cpp)
    const QString program = qEnvironmentVariableIsSet("OCTAVE_RFCOMM_PROGRAM")
        ? qEnvironmentVariable("OCTAVE_RFCOMM_PROGRAM") : QStringLiteral("rfcomm");
    proc->start(program,
                QStringList() << QStringLiteral("bind")
                              << QStringLiteral("0") << mac
                              << QStringLiteral("1"));
    timeout->start(5000);
}

void OBDManager::waitForRfcommNode(quint64 generation, int attemptsLeft)
{
    if (generation != m_rfcommBindGeneration)
        return;
    if (QDir().exists(QStringLiteral("/dev/rfcomm0"))) {
        emit connectionLogLineAppended(
            QStringLiteral("rfcomm: bound, opening /dev/rfcomm0"));
        finishRfcommBind(generation, QStringLiteral("/dev/rfcomm0"));
        return;
    }
    if (attemptsLeft <= 0) {
        emit connectionLogLineAppended(
            QStringLiteral("rfcomm bind reported success but /dev/rfcomm0 missing"));
        finishRfcommBind(generation, QString());
        return;
    }
    QTimer::singleShot(100, this, [this, generation, attemptsLeft]() {
        waitForRfcommNode(generation, attemptsLeft - 1);
    });
}

void OBDManager::finishRfcommBind(quint64 generation, const QString &bound)
{
    // Superseded by a newer connect_to_adapter() — it owns the outcome now
    if (generation != m_rfcommBindGeneration)
        return;

    if (bound.isEmpty()) {
        setConnectionStatus(QStringLiteral("Error"));
        setConnectionDetail(
            QStringLiteral("rfcomm bind failed — see Connection Log"));
        return;
    }
    if (m_settingsManager) {
        // Save the dev path as the active port so the worker thread
        // opens the bound node, not the MAC string.
        m_settingsManager->save_obd_bluetooth_port(bound);
    }
    force_connect();
}

void OBDManager::releaseRfcommProc(QProcess *proc)
{
    if (m_rfcommBindProc == proc)
        m_rfcommBindProc = nullptr;
    proc->disconnect(this);
    proc->deleteLater();
}
#endif

void OBDManager::connect_to_adapter(const QString &identifier)
{
    const QString trimmed = identifier.trimmed();
    if (trimmed.isEmpty()) {
        qDebug() << "[OBD] connect_to_adapter: empty identifier, ignoring";
        return;
    }

    if (trimmed == QLatin1String(OctaveSim::kSimPort)) {
        emit simulatedAdapterRequested();
        return;
    }
    // A hardware selection must take over from a previous demo override.
    if (isSimulated())
        setPortOverride(QString());

    static const QRegularExpression macRe(
        QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
    const bool isMac = macRe.match(trimmed).hasMatch();

    // Supersede any rfcomm bind still running for an earlier request
    ++m_rfcommBindGeneration;

    qDebug() << "[OBD] connect_to_adapter:" << trimmed
             << "(kind:" << kindForIdentifier(trimmed) << ")";
    emit connectionLogLineAppended(
        QStringLiteral("connect → %1").arg(displayNameForIdentifier(trimmed)));

    // Persist the choice so reconnects + restarts use it.
    if (m_settingsManager) {
        m_settingsManager->save_obd_bluetooth_port(trimmed);
        if (isMac || RfcommSocket::isUrl(trimmed)) {
            // Re-add as a saved chip; SettingsManager dedupes.
            m_settingsManager->add_obd_saved_adapter(trimmed, trimmed);
        }
    }

#ifdef Q_OS_LINUX
    // On Linux/Pi, a MAC alone isn't openable — translate to /dev/rfcommN.
    // (Android hands the MAC to OctaveOBDBridge.connect(); Windows would need
    // a MAC→COM lookup which we punt on for now and let the user enter COM.)
    // The bind runs async and finishRfcommBind() calls force_connect().
    if (isMac) {
        startRfcommBind(trimmed);
        return;
    }
#endif

    force_connect();
}

void OBDManager::connect_direct(const QString &mac, int channel)
{
    QString m = mac.trimmed();
    if (RfcommSocket::isUrl(m)) {
        connect_to_adapter(m);
        return;
    }
    const QString url = RfcommSocket::makeUrl(m, channel > 0 ? channel : 1);
    if (!RfcommSocket::parseUrl(url, nullptr, nullptr)) {
        emit connectionLogLineAppended(
            QStringLiteral("Direct connect needs a Bluetooth MAC like 88:1B:99:66:DD:5F"));
        return;
    }
    connect_to_adapter(url);
}

// ---------------------------------------------------------------------------
// Misc slots
// ---------------------------------------------------------------------------

void OBDManager::open_bluetooth_settings()
{
    // Desktop: no-op (only meaningful on Android)
    qDebug() << "[OBD] open_bluetooth_settings() -- desktop no-op";
}

void OBDManager::set_target_address(const QString &address)
{
    // Desktop: configure port name instead of BT address
    qDebug() << "[OBD] set_target_address:" << address;
    if (m_settingsManager)
        m_settingsManager->save_obd_bluetooth_port(address);
}

void OBDManager::set_auto_reconnect(bool enabled)
{
    Q_UNUSED(enabled);
    qDebug() << "[OBD] set_auto_reconnect() deprecated -- use settings";
}

void OBDManager::set_connection_timeout(int timeoutSeconds)
{
    m_connectionTimeout = qBound(5, timeoutSeconds, 60);
}

bool OBDManager::check_device_presence()
{
    QString port = getConfiguredPort();
    bool present = checkPortExists(port);
    emit devicePresenceChanged(present);
    return present;
}
