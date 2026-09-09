#include "obdmanager.h"

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

#ifdef Q_OS_ANDROID
#include <QBluetoothLocalDevice>
#include <QPermissions>
#include <QJniObject>
#include <QtCore/private/qandroidextras_p.h>
#include <QCoreApplication>
#endif

// ===========================================================================
// OBDManager
// ===========================================================================

OBDManager::OBDManager(SettingsManager *settingsManager, QObject *parent)
    : QObject(parent)
    , m_settingsManager(settingsManager)
{
    buildSignalDispatch();

    // Cache the latest detail / progress so the Q_PROPERTY readers reflect
    // whatever was last emitted (emit sites are numerous; hook once here).
    connect(this, &OBDManager::connectionStatusDetailChanged, this,
            [this](const QString &detail) { m_connectionDetail = detail; });
    connect(this, &OBDManager::connectionProgressChanged, this,
            [this](int progress) { m_connectionProgress = progress; });

    // --- Timers ---

    // Startup timer -- defer initial connection from constructor
    m_startupTimer.setSingleShot(true);
    m_startupTimer.setInterval(50);
    QObject::connect(&m_startupTimer, &QTimer::timeout, this, &OBDManager::onStartupTimer);

    // Data watchdog -- detect stale connections
    m_dataWatchdogTimer.setInterval(5000);
    QObject::connect(&m_dataWatchdogTimer, &QTimer::timeout, this, &OBDManager::onDataWatchdog);

    // Device scanner -- periodically scan for new ports when disconnected
    m_deviceScannerTimer.setInterval(10000);
    QObject::connect(&m_deviceScannerTimer, &QTimer::timeout, this, &OBDManager::onDeviceScanTimer);

    // Port change debounce
    m_portChangeTimer.setSingleShot(true);
    m_portChangeTimer.setInterval(1000);
    QObject::connect(&m_portChangeTimer, &QTimer::timeout, this, &OBDManager::onPortChangeDebounce);

    // Connect to settings changes
    if (m_settingsManager) {
        QObject::connect(m_settingsManager, &SettingsManager::obdBluetoothPortChanged,
                         this, &OBDManager::onSettingsPortChanged);
        QObject::connect(m_settingsManager, &SettingsManager::obdParametersChanged,
                         this, &OBDManager::onSettingsParametersChanged);
    }

    // Start the deferred init
    m_startupTimer.start();
}

OBDManager::~OBDManager()
{
    close();
}

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

QString OBDManager::getConfiguredPort() const
{
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
    if (detectPlatform() == Platform::Android) {
        // On Android, "port" is a Bluetooth MAC address. We don't probe the
        // adapter here -- a real reachability test happens at connectToService
        // time via QBluetoothSocket::error(). Just validate format.
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
// Signal dispatch table -- maps ELM327Protocol signal names to member
// variable storage + signal emission.
// ---------------------------------------------------------------------------

void OBDManager::buildSignalDispatch()
{
    // Original 18
    m_signalDispatch[QStringLiteral("coolantTempChanged")] = [this](float v) {
        m_coolantTemp = v; emit coolantTempChanged(v);
    };
    m_signalDispatch[QStringLiteral("voltageChanged")] = [this](float v) {
        m_voltage = v; emit voltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("engineLoadChanged")] = [this](float v) {
        m_engineLoad = v; emit engineLoadChanged(v);
    };
    m_signalDispatch[QStringLiteral("throttlePositionChanged")] = [this](float v) {
        m_throttlePos = v; emit throttlePositionChanged(v);
    };
    m_signalDispatch[QStringLiteral("intakeAirTempChanged")] = [this](float v) {
        m_intakeTemp = v; emit intakeAirTempChanged(v);
    };
    m_signalDispatch[QStringLiteral("timingAdvanceChanged")] = [this](float v) {
        m_timingAdvance = v;
        m_ignitionTiming = v;
        emit timingAdvanceChanged(v);
        emit ignitionTimingChanged(v);
    };
    m_signalDispatch[QStringLiteral("massAirFlowChanged")] = [this](float v) {
        m_massAirflow = v; emit massAirFlowChanged(v);
    };
    m_signalDispatch[QStringLiteral("speedMPHChanged")] = [this](float v) {
        m_speedMPH = v; emit speedMPHChanged(v);
    };
    m_signalDispatch[QStringLiteral("rpmChanged")] = [this](float v) {
        m_rpm = v; emit rpmChanged(v);
    };
    m_signalDispatch[QStringLiteral("airFuelRatioChanged")] = [this](float v) {
        m_airFuelRatio = v; emit airFuelRatioChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelLevelChanged")] = [this](float v) {
        m_fuelLevel = v; emit fuelLevelChanged(v);
    };
    m_signalDispatch[QStringLiteral("intakeManifoldPressureChanged")] = [this](float v) {
        m_intakePressure = v; emit intakeManifoldPressureChanged(v);
    };
    m_signalDispatch[QStringLiteral("shortTermFuelTrimChanged")] = [this](float v) {
        m_shortTermFuelTrim = v; emit shortTermFuelTrimChanged(v);
    };
    m_signalDispatch[QStringLiteral("longTermFuelTrimChanged")] = [this](float v) {
        m_longTermFuelTrim = v; emit longTermFuelTrimChanged(v);
    };
    m_signalDispatch[QStringLiteral("oxygenSensorVoltageChanged")] = [this](float v) {
        m_o2SensorVoltage = v; emit oxygenSensorVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelPressureChanged")] = [this](float v) {
        m_fuelPressure = v; emit fuelPressureChanged(v);
    };
    m_signalDispatch[QStringLiteral("engineOilTempChanged")] = [this](float v) {
        m_oilTemp = v; emit engineOilTempChanged(v);
    };

    // Batch 1
    m_signalDispatch[QStringLiteral("runTimeChanged")] = [this](float v) {
        m_runTime = v; emit runTimeChanged(v);
    };
    m_signalDispatch[QStringLiteral("distanceWithMILChanged")] = [this](float v) {
        m_distanceWithMIL = v; emit distanceWithMILChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelRailPressureChanged")] = [this](float v) {
        m_fuelRailPressure = v; emit fuelRailPressureChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelRailPressureDirectChanged")] = [this](float v) {
        m_fuelRailPressureDirect = v; emit fuelRailPressureDirectChanged(v);
    };
    m_signalDispatch[QStringLiteral("barometricPressureChanged")] = [this](float v) {
        m_barometricPressure = v; emit barometricPressureChanged(v);
    };
    m_signalDispatch[QStringLiteral("ambientAirTempChanged")] = [this](float v) {
        m_ambientAirTemp = v; emit ambientAirTempChanged(v);
    };
    m_signalDispatch[QStringLiteral("relativeThrottlePosChanged")] = [this](float v) {
        m_relativeThrottlePos = v; emit relativeThrottlePosChanged(v);
    };
    m_signalDispatch[QStringLiteral("absoluteThrottlePosBChanged")] = [this](float v) {
        m_absoluteThrottlePosB = v; emit absoluteThrottlePosBChanged(v);
    };
    m_signalDispatch[QStringLiteral("acceleratorPosChanged")] = [this](float v) {
        m_acceleratorPos = v; emit acceleratorPosChanged(v);
    };
    m_signalDispatch[QStringLiteral("catalystTempB1S1Changed")] = [this](float v) {
        m_catalystTempB1S1 = v; emit catalystTempB1S1Changed(v);
    };
    m_signalDispatch[QStringLiteral("catalystTempB1S2Changed")] = [this](float v) {
        m_catalystTempB1S2 = v; emit catalystTempB1S2Changed(v);
    };
    m_signalDispatch[QStringLiteral("evapVaporPressureChanged")] = [this](float v) {
        m_evapVaporPressure = v; emit evapVaporPressureChanged(v);
    };
    m_signalDispatch[QStringLiteral("shortFuelTrim2Changed")] = [this](float v) {
        m_shortFuelTrim2 = v; emit shortFuelTrim2Changed(v);
    };
    m_signalDispatch[QStringLiteral("longFuelTrim2Changed")] = [this](float v) {
        m_longFuelTrim2 = v; emit longFuelTrim2Changed(v);
    };
    m_signalDispatch[QStringLiteral("o2SensorB1S2Changed")] = [this](float v) {
        m_o2SensorB1S2 = v; emit o2SensorB1S2Changed(v);
    };
    m_signalDispatch[QStringLiteral("o2SensorB2S1Changed")] = [this](float v) {
        m_o2SensorB2S1 = v; emit o2SensorB2S1Changed(v);
    };
    m_signalDispatch[QStringLiteral("o2SensorB2S2Changed")] = [this](float v) {
        m_o2SensorB2S2 = v; emit o2SensorB2S2Changed(v);
    };
    m_signalDispatch[QStringLiteral("distanceSinceCodesCleared")] = [this](float v) {
        m_distanceSinceCodesCleared = v; emit distanceSinceCodesCleared(v);
    };
    m_signalDispatch[QStringLiteral("warmupsSinceCodesCleared")] = [this](float v) {
        m_warmupsSinceCodesCleared = v; emit warmupsSinceCodesCleared(v);
    };
    m_signalDispatch[QStringLiteral("absoluteLoadChanged")] = [this](float v) {
        m_absoluteLoad = v; emit absoluteLoadChanged(v);
    };
    m_signalDispatch[QStringLiteral("commandedEGRChanged")] = [this](float v) {
        m_commandedEGR = v; emit commandedEGRChanged(v);
    };
    m_signalDispatch[QStringLiteral("egrErrorChanged")] = [this](float v) {
        m_egrError = v; emit egrErrorChanged(v);
    };
    m_signalDispatch[QStringLiteral("ethanoPercentChanged")] = [this](float v) {
        m_ethanolPercent = v; emit ethanoPercentChanged(v);
    };

    // Batch 2 -- Additional O2 sensors
    m_signalDispatch[QStringLiteral("o2SensorB1S3Changed")] = [this](float v) {
        m_o2SensorB1S3 = v; emit o2SensorB1S3Changed(v);
    };
    m_signalDispatch[QStringLiteral("o2SensorB1S4Changed")] = [this](float v) {
        m_o2SensorB1S4 = v; emit o2SensorB1S4Changed(v);
    };
    m_signalDispatch[QStringLiteral("o2SensorB2S3Changed")] = [this](float v) {
        m_o2SensorB2S3 = v; emit o2SensorB2S3Changed(v);
    };
    m_signalDispatch[QStringLiteral("o2SensorB2S4Changed")] = [this](float v) {
        m_o2SensorB2S4 = v; emit o2SensorB2S4Changed(v);
    };

    // Wide-range O2 sensors voltage
    m_signalDispatch[QStringLiteral("o2S1WRVoltageChanged")] = [this](float v) {
        m_o2S1WRVoltage = v; emit o2S1WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S2WRVoltageChanged")] = [this](float v) {
        m_o2S2WRVoltage = v; emit o2S2WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S3WRVoltageChanged")] = [this](float v) {
        m_o2S3WRVoltage = v; emit o2S3WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S4WRVoltageChanged")] = [this](float v) {
        m_o2S4WRVoltage = v; emit o2S4WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S5WRVoltageChanged")] = [this](float v) {
        m_o2S5WRVoltage = v; emit o2S5WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S6WRVoltageChanged")] = [this](float v) {
        m_o2S6WRVoltage = v; emit o2S6WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S7WRVoltageChanged")] = [this](float v) {
        m_o2S7WRVoltage = v; emit o2S7WRVoltageChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S8WRVoltageChanged")] = [this](float v) {
        m_o2S8WRVoltage = v; emit o2S8WRVoltageChanged(v);
    };

    // Wide-range O2 sensors current
    m_signalDispatch[QStringLiteral("o2S1WRCurrentChanged")] = [this](float v) {
        m_o2S1WRCurrent = v; emit o2S1WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S2WRCurrentChanged")] = [this](float v) {
        m_o2S2WRCurrent = v; emit o2S2WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S3WRCurrentChanged")] = [this](float v) {
        m_o2S3WRCurrent = v; emit o2S3WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S4WRCurrentChanged")] = [this](float v) {
        m_o2S4WRCurrent = v; emit o2S4WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S5WRCurrentChanged")] = [this](float v) {
        m_o2S5WRCurrent = v; emit o2S5WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S6WRCurrentChanged")] = [this](float v) {
        m_o2S6WRCurrent = v; emit o2S6WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S7WRCurrentChanged")] = [this](float v) {
        m_o2S7WRCurrent = v; emit o2S7WRCurrentChanged(v);
    };
    m_signalDispatch[QStringLiteral("o2S8WRCurrentChanged")] = [this](float v) {
        m_o2S8WRCurrent = v; emit o2S8WRCurrentChanged(v);
    };

    // Bank 2 catalyst temps
    m_signalDispatch[QStringLiteral("catalystTempB2S1Changed")] = [this](float v) {
        m_catalystTempB2S1 = v; emit catalystTempB2S1Changed(v);
    };
    m_signalDispatch[QStringLiteral("catalystTempB2S2Changed")] = [this](float v) {
        m_catalystTempB2S2 = v; emit catalystTempB2S2Changed(v);
    };

    // Additional throttle/accelerator
    m_signalDispatch[QStringLiteral("throttlePosCChanged")] = [this](float v) {
        m_throttlePosC = v; emit throttlePosCChanged(v);
    };
    m_signalDispatch[QStringLiteral("acceleratorPosEChanged")] = [this](float v) {
        m_acceleratorPosE = v; emit acceleratorPosEChanged(v);
    };
    m_signalDispatch[QStringLiteral("acceleratorPosFChanged")] = [this](float v) {
        m_acceleratorPosF = v; emit acceleratorPosFChanged(v);
    };
    m_signalDispatch[QStringLiteral("throttleActuatorChanged")] = [this](float v) {
        m_throttleActuator = v; emit throttleActuatorChanged(v);
    };

    // Fuel system
    m_signalDispatch[QStringLiteral("evaporativePurgeChanged")] = [this](float v) {
        m_evaporativePurge = v; emit evaporativePurgeChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelRailPressureAbsChanged")] = [this](float v) {
        m_fuelRailPressureAbs = v; emit fuelRailPressureAbsChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelInjectTimingChanged")] = [this](float v) {
        m_fuelInjectTiming = v; emit fuelInjectTimingChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelRateChanged")] = [this](float v) {
        m_fuelRate = v; emit fuelRateChanged(v);
    };

    // Time-based
    m_signalDispatch[QStringLiteral("runTimeMILChanged")] = [this](float v) {
        m_runTimeMIL = v; emit runTimeMILChanged(v);
    };
    m_signalDispatch[QStringLiteral("timeSinceDTCClearedChanged")] = [this](float v) {
        m_timeSinceDTCCleared = v; emit timeSinceDTCClearedChanged(v);
    };

    // Other
    m_signalDispatch[QStringLiteral("maxMAFChanged")] = [this](float v) {
        m_maxMAF = v; emit maxMAFChanged(v);
    };
    m_signalDispatch[QStringLiteral("fuelTypeChanged")] = [this](float v) {
        m_fuelType = v; emit fuelTypeChanged(v);
    };
    m_signalDispatch[QStringLiteral("evapVaporPressureAbsChanged")] = [this](float v) {
        m_evapVaporPressureAbs = v; emit evapVaporPressureAbsChanged(v);
    };
    m_signalDispatch[QStringLiteral("evapVaporPressureAltChanged")] = [this](float v) {
        m_evapVaporPressureAlt = v; emit evapVaporPressureAltChanged(v);
    };
    m_signalDispatch[QStringLiteral("shortO2TrimB1Changed")] = [this](float v) {
        m_shortO2TrimB1 = v; emit shortO2TrimB1Changed(v);
    };
    m_signalDispatch[QStringLiteral("longO2TrimB1Changed")] = [this](float v) {
        m_longO2TrimB1 = v; emit longO2TrimB1Changed(v);
    };
    m_signalDispatch[QStringLiteral("shortO2TrimB2Changed")] = [this](float v) {
        m_shortO2TrimB2 = v; emit shortO2TrimB2Changed(v);
    };
    m_signalDispatch[QStringLiteral("longO2TrimB2Changed")] = [this](float v) {
        m_longO2TrimB2 = v; emit longO2TrimB2Changed(v);
    };
    m_signalDispatch[QStringLiteral("relativeAccelPosChanged")] = [this](float v) {
        m_relativeAccelPos = v; emit relativeAccelPosChanged(v);
    };
    m_signalDispatch[QStringLiteral("hybridBatteryRemainingChanged")] = [this](float v) {
        m_hybridBatteryRemaining = v; emit hybridBatteryRemainingChanged(v);
    };
    m_signalDispatch[QStringLiteral("elmVoltageChanged")] = [this](float v) {
        m_elmVoltage = v; emit elmVoltageChanged(v);
    };
}

void OBDManager::emitParameterSignal(const QString &signalName, float value)
{
    auto it = m_signalDispatch.constFind(signalName);
    if (it != m_signalDispatch.constEnd())
        it.value()(value);
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

void OBDManager::scanForDevices()
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
                emit connectionStatusDetailChanged(
                    QStringLiteral("Device at %1 not found. Available: %2")
                        .arg(configured, ports.join(QStringLiteral(", "))));
            }
        }
    }

    m_lastDeviceScan = QDateTime::currentMSecsSinceEpoch();
}

void OBDManager::onDeviceScanTimer()
{
    scanForDevices();
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

void OBDManager::startConnection()
{
    {
        QMutexLocker locker(&m_lock);
        if (m_isConnecting) {
            qDebug() << "[OBD] Already connecting -- skipping";
            return;
        }
        m_isConnecting = true;
    }

    m_connectionAttempts++;
    qDebug() << "[OBD] Starting connection attempt #" << m_connectionAttempts;

    emit connectionStatusChanged(QStringLiteral("Connecting"));
    emit connectionStatusDetailChanged(
        QStringLiteral("Attempt %1...").arg(m_connectionAttempts));
    emit connectionProgressChanged(10);

#ifdef Q_OS_ANDROID
    // Android: skip the QSerialPort + worker-thread flow entirely; the
    // QBluetoothSocket lives on this thread and is event-driven.
    startAndroidConnection();
    return;
#endif

    QString port = getConfiguredPort();
    bool fastMode = m_settingsManager ? m_settingsManager->obdFastMode() : true;

    // Check if device exists
    if (!checkPortExists(port)) {
        m_connected = false;
        emit connectionStatusChanged(QStringLiteral("Device Not Found"));
        emit connectionStatusDetailChanged(QStringLiteral("Port %1 not available").arg(port));
        emit connectionProgressChanged(0);
        emit devicePresenceChanged(false);
        qDebug() << "[OBD] Port" << port << "not found";
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        scheduleAutoReconnect();
        return;
    }

    emit devicePresenceChanged(true);
    emit connectionProgressChanged(15);
    emit connectionStatusDetailChanged(QStringLiteral("Found %1, connecting...").arg(port));

    // Clean up previous worker thread
    cleanupWorkerThread();

    // Create worker and thread
    m_worker = new OBDConnectionWorker();
    m_worker->setPort(port);
    m_worker->setFastMode(fastMode);
    m_worker->setTimeout(m_connectionTimeout);

    // Determine PIDs to watch based on settings
    QList<PidKey> pidsToWatch;
    const auto &pidTable = ELM327Protocol::pidTable();
    // Map from PID table signal name back to a command-name key that matches
    // settings. We use the same approach as Python: iterate pidTable, check settings.
    // Build a reverse map from signal->command name for settings lookup.
    // For simplicity, we use the PID table's human name as the command key.
    // The Python code uses command names like "COOLANT_TEMP", "RPM", etc.
    // We need a mapping from PidKey -> settings command name.
    static const QHash<PidKey, QString> pidToCommand = {
        {{1,0x05}, QStringLiteral("COOLANT_TEMP")},
        {{1,0x42}, QStringLiteral("CONTROL_MODULE_VOLTAGE")},
        {{1,0x04}, QStringLiteral("ENGINE_LOAD")},
        {{1,0x11}, QStringLiteral("THROTTLE_POS")},
        {{1,0x0F}, QStringLiteral("INTAKE_TEMP")},
        {{1,0x0E}, QStringLiteral("TIMING_ADVANCE")},
        {{1,0x10}, QStringLiteral("MAF")},
        {{1,0x0D}, QStringLiteral("SPEED")},
        {{1,0x0C}, QStringLiteral("RPM")},
        {{1,0x44}, QStringLiteral("COMMANDED_EQUIV_RATIO")},
        {{1,0x2F}, QStringLiteral("FUEL_LEVEL")},
        {{1,0x0B}, QStringLiteral("INTAKE_PRESSURE")},
        {{1,0x06}, QStringLiteral("SHORT_FUEL_TRIM_1")},
        {{1,0x07}, QStringLiteral("LONG_FUEL_TRIM_1")},
        {{1,0x14}, QStringLiteral("O2_B1S1")},
        {{1,0x0A}, QStringLiteral("FUEL_PRESSURE")},
        {{1,0x5C}, QStringLiteral("OIL_TEMP")},
        {{1,0x1F}, QStringLiteral("RUN_TIME")},
        {{1,0x21}, QStringLiteral("DISTANCE_W_MIL")},
        {{1,0x22}, QStringLiteral("FUEL_RAIL_PRESSURE_VAC")},
        {{1,0x23}, QStringLiteral("FUEL_RAIL_PRESSURE_DIRECT")},
        {{1,0x33}, QStringLiteral("BAROMETRIC_PRESSURE")},
        {{1,0x46}, QStringLiteral("AMBIANT_AIR_TEMP")},
        {{1,0x45}, QStringLiteral("RELATIVE_THROTTLE_POS")},
        {{1,0x47}, QStringLiteral("THROTTLE_POS_B")},
        {{1,0x49}, QStringLiteral("ACCELERATOR_POS_D")},
        {{1,0x3C}, QStringLiteral("CATALYST_TEMP_B1S1")},
        {{1,0x3D}, QStringLiteral("CATALYST_TEMP_B1S2")},
        {{1,0x32}, QStringLiteral("EVAP_VAPOR_PRESSURE")},
        {{1,0x08}, QStringLiteral("SHORT_FUEL_TRIM_2")},
        {{1,0x09}, QStringLiteral("LONG_FUEL_TRIM_2")},
        {{1,0x15}, QStringLiteral("O2_B1S2")},
        {{1,0x16}, QStringLiteral("O2_B2S1")},
        {{1,0x17}, QStringLiteral("O2_B2S2")},
        {{1,0x31}, QStringLiteral("DISTANCE_SINCE_DTC_CLEAR")},
        {{1,0x30}, QStringLiteral("WARMUPS_SINCE_DTC_CLEAR")},
        {{1,0x43}, QStringLiteral("ABSOLUTE_LOAD")},
        {{1,0x2C}, QStringLiteral("COMMANDED_EGR")},
        {{1,0x2D}, QStringLiteral("EGR_ERROR")},
        {{1,0x52}, QStringLiteral("ETHANOL_PERCENT")},
        {{1,0x3E}, QStringLiteral("CATALYST_TEMP_B2S1")},
        {{1,0x3F}, QStringLiteral("CATALYST_TEMP_B2S2")},
        {{1,0x4A}, QStringLiteral("THROTTLE_POS_C")},
        {{1,0x4B}, QStringLiteral("ACCELERATOR_POS_E")},
        {{1,0x4C}, QStringLiteral("ACCELERATOR_POS_F")},
        {{1,0x4D}, QStringLiteral("RUN_TIME_MIL")},
        {{1,0x4E}, QStringLiteral("TIME_SINCE_DTC_CLEARED")},
        {{1,0x50}, QStringLiteral("MAX_MAF")},
        {{1,0x51}, QStringLiteral("FUEL_TYPE")},
        {{1,0x54}, QStringLiteral("EVAP_VAPOR_PRESSURE_ABS")},
        {{1,0x55}, QStringLiteral("EVAP_VAPOR_PRESSURE_ALT")},
        {{1,0x56}, QStringLiteral("SHORT_O2_TRIM_B1")},
        {{1,0x57}, QStringLiteral("LONG_O2_TRIM_B1")},
        {{1,0x58}, QStringLiteral("SHORT_O2_TRIM_B2")},
        {{1,0x59}, QStringLiteral("LONG_O2_TRIM_B2")},
        {{1,0x5A}, QStringLiteral("RELATIVE_ACCEL_POS")},
        {{1,0x5B}, QStringLiteral("HYBRID_BATTERY_REMAINING")},
        {{1,0x2E}, QStringLiteral("EVAPORATIVE_PURGE")},
        {{1,0x5D}, QStringLiteral("FUEL_INJECT_TIMING")},
        {{1,0x5E}, QStringLiteral("FUEL_RATE")},
        {{1,0x4F}, QStringLiteral("THROTTLE_ACTUATOR")},
    };

    // Original 17 default-enabled command names
    static const QSet<QString> defaultEnabled = {
        QStringLiteral("COOLANT_TEMP"), QStringLiteral("CONTROL_MODULE_VOLTAGE"),
        QStringLiteral("ENGINE_LOAD"), QStringLiteral("THROTTLE_POS"),
        QStringLiteral("INTAKE_TEMP"), QStringLiteral("TIMING_ADVANCE"),
        QStringLiteral("MAF"), QStringLiteral("SPEED"), QStringLiteral("RPM"),
        QStringLiteral("COMMANDED_EQUIV_RATIO"), QStringLiteral("FUEL_LEVEL"),
        QStringLiteral("INTAKE_PRESSURE"), QStringLiteral("SHORT_FUEL_TRIM_1"),
        QStringLiteral("LONG_FUEL_TRIM_1"), QStringLiteral("O2_B1S1"),
        QStringLiteral("FUEL_PRESSURE"), QStringLiteral("OIL_TEMP"),
    };

    for (auto it = pidToCommand.constBegin(); it != pidToCommand.constEnd(); ++it) {
        const QString &cmdName = it.value();
        bool isDefault = defaultEnabled.contains(cmdName);
        bool shouldWatch = true;
        if (m_settingsManager) {
            shouldWatch = m_settingsManager->get_obd_parameter_enabled(cmdName, isDefault);
        }
        if (shouldWatch) {
            pidsToWatch.append(it.key());
        }
    }

    m_hasActiveWatchers = !pidsToWatch.isEmpty();
    m_worker->setPidsToWatch(pidsToWatch);

    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // Connect worker signals (queued connections -- cross-thread safe)
    QObject::connect(m_worker, &OBDConnectionWorker::initComplete,
                     this, &OBDManager::onWorkerInitComplete, Qt::QueuedConnection);
    QObject::connect(m_worker, &OBDConnectionWorker::dataReceived,
                     this, &OBDManager::onWorkerDataReceived, Qt::QueuedConnection);
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

    // Start connection on worker thread
    QObject::connect(m_workerThread, &QThread::started,
                     m_worker, &OBDConnectionWorker::doConnect);

    m_workerThread->start();
}

void OBDManager::cleanupConnection()
{
    m_dataWatchdogTimer.stop();
    m_lastDataReceived = 0;

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
}

void OBDManager::cleanupWorkerThread()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    if (m_workerThread) {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
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
        emit connectionStatusDetailChanged(QStringLiteral("Auto-reconnect disabled"));
        m_deviceScannerTimer.start();
        return;
    }

    if (m_connectionAttempts >= maxAttempts) {
        qDebug() << "[OBD] Max attempts reached, switching to passive scanning";
        emit connectionStatusDetailChanged(QStringLiteral("Max retries reached. Scanning..."));
        m_deviceScannerTimer.start();
        return;
    }

    int delay = qMin(10, 2 + m_connectionAttempts * 2);
    emit connectionStatusDetailChanged(
        QStringLiteral("Retry in %1s... (%2/%3)")
            .arg(delay).arg(m_connectionAttempts).arg(maxAttempts));
    qDebug() << "[OBD] Auto-reconnect in" << delay << "s";

    QTimer::singleShot(delay * 1000, this, &OBDManager::startConnection);
}

// ---------------------------------------------------------------------------
// Worker callbacks (called on main thread via queued connections)
// ---------------------------------------------------------------------------

void OBDManager::onWorkerInitComplete(bool success, const QString &message)
{
    qDebug() << "[OBD] Worker init complete:" << success << message;

    if (success) {
        m_connected = true;
        m_connectionAttempts = 0;
        m_reconnectingAfterDiagnostic = false;

        emit connectionStatusChanged(QStringLiteral("Connected"));
        emit connectionStatusDetailChanged(QStringLiteral("OBD interface connected successfully"));
        emit connectionProgressChanged(100);

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

        // Check for partial connection (adapter found but no vehicle)
        if (message.contains(QStringLiteral("adapter"))) {
            emit connectionStatusChanged(QStringLiteral("No Vehicle"));
            emit connectionStatusDetailChanged(
                QStringLiteral("Connected to adapter, waiting for vehicle..."));
            emit connectionProgressChanged(50);
        } else {
            emit connectionStatusChanged(QStringLiteral("Connection Failed"));
            emit connectionStatusDetailChanged(
                QStringLiteral("Could not connect to OBD adapter"));
            emit connectionProgressChanged(0);
        }

        // Post-diagnostic retry logic
        if (m_reconnectingAfterDiagnostic && m_connectionAttempts < 3) {
            qDebug() << "[OBD] Post-diagnostic retry" << (m_connectionAttempts + 1) << "/3";
            emit connectionStatusDetailChanged(
                QStringLiteral("Reconnecting after diagnostic mode... (%1/3)")
                    .arg(m_connectionAttempts + 1));
            QTimer::singleShot(2000, this, &OBDManager::startConnection);
        } else {
            m_reconnectingAfterDiagnostic = false;
            scheduleAutoReconnect();
        }
    }

    {
        QMutexLocker locker(&m_lock);
        m_isConnecting = false;
    }
}

void OBDManager::onWorkerDataReceived(const QString &signalName, float value)
{
    m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
    emitParameterSignal(signalName, value);
}

void OBDManager::onWorkerDtcResult(const QStringList &codes)
{
    QVariantList dtcList;
    for (const QString &code : codes) {
        QVariantMap item;
        item[QStringLiteral("code")] = code;
        item[QStringLiteral("description")] = QString();
        dtcList.append(item);
    }
    m_dtcCodes = dtcList;
    m_dtcCount = dtcList.size();
    emit dtcCodesChanged(dtcList);
    emit dtcCountChanged(m_dtcCount);
}

void OBDManager::onWorkerDtcCleared(bool success, const QString &message)
{
    if (success) {
        m_dtcCodes.clear();
        m_dtcCount = 0;
        m_milStatus = false;
        emit dtcCodesChanged(QVariantList());
        emit dtcCountChanged(0);
        emit milStatusChanged(false);
    }
    emit dtcClearResult(success, message);
}

void OBDManager::onWorkerMilStatus(bool mil, int dtcCount)
{
    m_milStatus = mil;
    m_dtcCount = dtcCount;
    emit milStatusChanged(mil);
    emit dtcCountChanged(dtcCount);
}

void OBDManager::onWorkerFreezeFrame(const QStringList &codes)
{
    QVariantList list;
    for (const QString &code : codes) {
        QVariantMap item;
        item[QStringLiteral("code")] = code;
        item[QStringLiteral("description")] = QString();
        list.append(item);
    }
    m_freezeFrameDtcs = list;
    emit freezeFrameChanged(list);
}

void OBDManager::onWorkerConnectionLost(const QString &reason)
{
    qDebug() << "[OBD] Connection lost:" << reason;
    m_connected = false;
    emit connectionStatusChanged(QStringLiteral("Disconnected"));
    emit connectionStatusDetailChanged(reason);
    emit connectionProgressChanged(0);
    m_dataWatchdogTimer.stop();
    m_deviceScannerTimer.start();
    scheduleAutoReconnect();
}

void OBDManager::onWorkerScanProgress(int progress, const QString &message)
{
    emit scanProgressChanged(progress, message);
}

void OBDManager::onWorkerScanComplete(const QStringList &supported)
{
    m_supportedCommands = supported;
    m_isScanning = false;

    QVariantList varList;
    for (const QString &s : supported)
        varList.append(s);

    emit supportedCommandsChanged(varList);
    emit scanCompleteChanged(varList);
    emit scanProgressChanged(100, QStringLiteral("Found %1 supported parameters").arg(supported.size()));

    // Persist scan results
    if (m_settingsManager) {
        m_settingsManager->save_supported_obd_parameters(varList);
    }

    qDebug() << "[OBD] Scan complete:" << supported.size() << "supported parameters";
}

void OBDManager::onWorkerScanOutput(const QString &line)
{
    emit scanOutputChanged(line);
}

// ---------------------------------------------------------------------------
// Data watchdog
// ---------------------------------------------------------------------------

void OBDManager::onDataWatchdog()
{
    if (!m_connected)
        return;
    if (!m_hasActiveWatchers)
        return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed = now - m_lastDataReceived;

    if (m_lastDataReceived > 0 && elapsed > DATA_WATCHDOG_TIMEOUT_MS) {
        qDebug() << "[OBD] Data watchdog triggered -- no data for" << elapsed << "ms";
        emit connectionStatusChanged(QStringLiteral("Stale Connection"));
        emit connectionStatusDetailChanged(QStringLiteral("No data received, reconnecting..."));
        m_dataWatchdogTimer.stop();
        cleanupConnection();
        m_connectionAttempts = 0;
        QTimer::singleShot(100, this, &OBDManager::startConnection);
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
    if (!newPort.isEmpty() && newPort != m_pendingPort) {
        m_pendingPort = newPort;
        m_portChangeTimer.start();
        qDebug() << "[OBD] Port change scheduled:" << newPort;
    }
}

void OBDManager::onPortChangeDebounce()
{
    if (!m_pendingPort.isEmpty()) {
        qDebug() << "[OBD] Port changed to:" << m_pendingPort;
        m_connectionAttempts = 0;
        reconnect();
        m_pendingPort.clear();
    }
}

void OBDManager::onSettingsParametersChanged()
{
    // If connected, tell worker to update its PID list (requires reconnect for now)
    qDebug() << "[OBD] OBD parameters changed -- will take effect on next connection";
}

// ---------------------------------------------------------------------------
// Public slots -- Connection management
// ---------------------------------------------------------------------------

void OBDManager::connect_obd()
{
    startConnection();
}

void OBDManager::reconnect()
{
    qDebug() << "[OBD] Manual reconnect requested";
    cleanupConnection();
    m_connectionAttempts = 0;
    startConnection();
}

void OBDManager::force_connect()
{
    qDebug() << "[OBD] Force connect requested";
    m_connectionAttempts = 0;
    // Always clear the in-progress flag — without this, a previous attempt
    // that wedged (BLE controller never reported back, GATT timed out
    // silently, etc.) would block every subsequent tap of Connect with
    // "Already connecting -- skipping".
    {
        QMutexLocker locker(&m_lock);
        m_isConnecting = false;
    }
    cleanupConnection();
    startConnection();
}

void OBDManager::close()
{
    m_forceStopReconnect = true;
    m_deviceScannerTimer.stop();
    m_dataWatchdogTimer.stop();
    cleanupConnection();
}

void OBDManager::reset_connection()
{
    force_connect();
}

bool OBDManager::isConnected() const
{
    return m_connected;
}

QString OBDManager::get_connection_status() const
{
    if (!m_connected)
        return QStringLiteral("Not Connected");
    return QStringLiteral("Connected");
}

QString OBDManager::getConnectionStatus() const
{
    return m_connectionStatus;
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
    static const QRegularExpression macRe(
        QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
    if (macRe.match(id).hasMatch())                     return QStringLiteral("ble");
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
    if (kind == QStringLiteral("ble"))           return QStringLiteral("Bluetooth · %1").arg(id);
    if (kind == QStringLiteral("serial-rfcomm")) return QStringLiteral("Bluetooth (bound) · %1").arg(id);
    if (kind == QStringLiteral("serial-usb"))    return QStringLiteral("USB ELM327 · %1").arg(id);
    if (kind == QStringLiteral("tty"))           return id;
    if (kind == QStringLiteral("com"))           return QStringLiteral("Serial · %1").arg(id);
    return id;
}

QVariantList OBDManager::buildAdapterList(const QStringList &ports) const
{
    QVariantList result;
    for (const QString &p : ports) {
        QVariantMap entry;
        entry[QStringLiteral("identifier")] = p;
        entry[QStringLiteral("name")]       = displayNameForIdentifier(p);
        entry[QStringLiteral("kind")]       = kindForIdentifier(p);
        result.append(entry);
    }
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
QString OBDManager::ensureRfcommBound(const QString &mac)
{
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
            return dev;
        }
    }

    // 2) Try `rfcomm bind 0 <mac> 1`. Channel 1 is the universal SPP channel
    //    for ELM327 clones; if the adapter advertises a different channel
    //    the user can pre-bind manually.
    emit connectionLogLineAppended(
        QStringLiteral("rfcomm: binding %1 → /dev/rfcomm0").arg(mac));
    QProcess proc;
    proc.start(QStringLiteral("rfcomm"),
               QStringList() << QStringLiteral("bind")
                             << QStringLiteral("0") << mac
                             << QStringLiteral("1"));
    if (!proc.waitForStarted(2000)) {
        emit connectionLogLineAppended(
            QStringLiteral("rfcomm bind failed: rfcomm binary not found "
                           "(install bluez-utils, or pre-bind and enter /dev/rfcomm0)"));
        return QString();
    }
    proc.waitForFinished(5000);
    if (proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        emit connectionLogLineAppended(
            QStringLiteral("rfcomm bind failed: %1")
                .arg(err.isEmpty() ? QStringLiteral("non-zero exit") : err));
        emit connectionLogLineAppended(
            QStringLiteral("tip: add user to bluetooth group, or run "
                           "`sudo rfcomm bind 0 %1` once and enter /dev/rfcomm0")
                .arg(mac));
        return QString();
    }
    // Confirm the node now exists (rfcomm sometimes succeeds asynchronously).
    for (int wait = 0; wait < 10; ++wait) {
        if (QDir().exists(QStringLiteral("/dev/rfcomm0"))) {
            emit connectionLogLineAppended(
                QStringLiteral("rfcomm: bound, opening /dev/rfcomm0"));
            return QStringLiteral("/dev/rfcomm0");
        }
        QThread::msleep(100);
    }
    emit connectionLogLineAppended(
        QStringLiteral("rfcomm bind reported success but /dev/rfcomm0 missing"));
    return QString();
}
#endif

void OBDManager::connect_to_adapter(const QString &identifier)
{
    const QString trimmed = identifier.trimmed();
    if (trimmed.isEmpty()) {
        qDebug() << "[OBD] connect_to_adapter: empty identifier, ignoring";
        return;
    }

    static const QRegularExpression macRe(
        QStringLiteral("^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$"));
    const bool isMac = macRe.match(trimmed).hasMatch();

    qDebug() << "[OBD] connect_to_adapter:" << trimmed
             << "(kind:" << kindForIdentifier(trimmed) << ")";
    emit connectionLogLineAppended(
        QStringLiteral("connect → %1").arg(displayNameForIdentifier(trimmed)));

    // Persist the choice so reconnects + restarts use it.
    if (m_settingsManager) {
        m_settingsManager->save_obd_bluetooth_port(trimmed);
        if (isMac) {
            // Re-add as a saved chip; SettingsManager dedupes.
            m_settingsManager->add_obd_saved_adapter(trimmed, trimmed);
        }
    }

#ifdef Q_OS_LINUX
    // On Linux/Pi, a MAC alone isn't openable — translate to /dev/rfcommN.
    // (Android takes the MAC straight to QBluetoothSocket; Windows would need
    // a MAC→COM lookup which we punt on for now and let the user enter COM.)
    if (isMac) {
        const QString bound = ensureRfcommBound(trimmed);
        if (!bound.isEmpty() && m_settingsManager) {
            // Save the dev path as the active port so the worker thread
            // opens the bound node, not the MAC string.
            m_settingsManager->save_obd_bluetooth_port(bound);
        } else if (bound.isEmpty()) {
            emit connectionStatusChanged(QStringLiteral("Error"));
            emit connectionStatusDetailChanged(
                QStringLiteral("rfcomm bind failed — see Connection Log"));
            return;
        }
    }
#endif

    force_connect();
}

// ---------------------------------------------------------------------------
// Vehicle scan
// ---------------------------------------------------------------------------

void OBDManager::scan_vehicle()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot scan -- not connected";
        emit scanProgressChanged(0, QStringLiteral("Not connected to vehicle"));
        return;
    }
    if (m_isScanning) {
        qDebug() << "[OBD] Scan already in progress";
        return;
    }
    m_isScanning = true;
    emit scanProgressChanged(0, QStringLiteral("Starting vehicle scan..."));

    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doScanVehicle", Qt::QueuedConnection);
}

bool OBDManager::is_scanning() const
{
    return m_isScanning;
}

QStringList OBDManager::get_all_parameter_names() const
{
    // Return command names matching the Python _get_all_commands() keys
    static const QStringList names = {
        QStringLiteral("COOLANT_TEMP"), QStringLiteral("CONTROL_MODULE_VOLTAGE"),
        QStringLiteral("ENGINE_LOAD"), QStringLiteral("THROTTLE_POS"),
        QStringLiteral("INTAKE_TEMP"), QStringLiteral("TIMING_ADVANCE"),
        QStringLiteral("MAF"), QStringLiteral("SPEED"), QStringLiteral("RPM"),
        QStringLiteral("COMMANDED_EQUIV_RATIO"), QStringLiteral("FUEL_LEVEL"),
        QStringLiteral("INTAKE_PRESSURE"), QStringLiteral("SHORT_FUEL_TRIM_1"),
        QStringLiteral("LONG_FUEL_TRIM_1"), QStringLiteral("O2_B1S1"),
        QStringLiteral("FUEL_PRESSURE"), QStringLiteral("OIL_TEMP"),
        QStringLiteral("RUN_TIME"), QStringLiteral("DISTANCE_W_MIL"),
        QStringLiteral("FUEL_RAIL_PRESSURE_VAC"), QStringLiteral("FUEL_RAIL_PRESSURE_DIRECT"),
        QStringLiteral("BAROMETRIC_PRESSURE"), QStringLiteral("AMBIANT_AIR_TEMP"),
        QStringLiteral("RELATIVE_THROTTLE_POS"), QStringLiteral("THROTTLE_POS_B"),
        QStringLiteral("ACCELERATOR_POS_D"), QStringLiteral("CATALYST_TEMP_B1S1"),
        QStringLiteral("CATALYST_TEMP_B1S2"), QStringLiteral("EVAP_VAPOR_PRESSURE"),
        QStringLiteral("SHORT_FUEL_TRIM_2"), QStringLiteral("LONG_FUEL_TRIM_2"),
        QStringLiteral("O2_B1S2"), QStringLiteral("O2_B2S1"), QStringLiteral("O2_B2S2"),
        QStringLiteral("DISTANCE_SINCE_DTC_CLEAR"), QStringLiteral("WARMUPS_SINCE_DTC_CLEAR"),
        QStringLiteral("ABSOLUTE_LOAD"), QStringLiteral("COMMANDED_EGR"),
        QStringLiteral("EGR_ERROR"), QStringLiteral("ETHANOL_PERCENT"),
        QStringLiteral("O2_B1S3"), QStringLiteral("O2_B1S4"),
        QStringLiteral("O2_B2S3"), QStringLiteral("O2_B2S4"),
        QStringLiteral("O2_S1_WR_VOLTAGE"), QStringLiteral("O2_S2_WR_VOLTAGE"),
        QStringLiteral("O2_S3_WR_VOLTAGE"), QStringLiteral("O2_S4_WR_VOLTAGE"),
        QStringLiteral("O2_S5_WR_VOLTAGE"), QStringLiteral("O2_S6_WR_VOLTAGE"),
        QStringLiteral("O2_S7_WR_VOLTAGE"), QStringLiteral("O2_S8_WR_VOLTAGE"),
        QStringLiteral("O2_S1_WR_CURRENT"), QStringLiteral("O2_S2_WR_CURRENT"),
        QStringLiteral("O2_S3_WR_CURRENT"), QStringLiteral("O2_S4_WR_CURRENT"),
        QStringLiteral("O2_S5_WR_CURRENT"), QStringLiteral("O2_S6_WR_CURRENT"),
        QStringLiteral("O2_S7_WR_CURRENT"), QStringLiteral("O2_S8_WR_CURRENT"),
        QStringLiteral("CATALYST_TEMP_B2S1"), QStringLiteral("CATALYST_TEMP_B2S2"),
        QStringLiteral("THROTTLE_POS_C"), QStringLiteral("ACCELERATOR_POS_E"),
        QStringLiteral("ACCELERATOR_POS_F"), QStringLiteral("THROTTLE_ACTUATOR"),
        QStringLiteral("EVAPORATIVE_PURGE"), QStringLiteral("FUEL_RAIL_PRESSURE_ABS"),
        QStringLiteral("FUEL_INJECT_TIMING"), QStringLiteral("FUEL_RATE"),
        QStringLiteral("RUN_TIME_MIL"), QStringLiteral("TIME_SINCE_DTC_CLEARED"),
        QStringLiteral("MAX_MAF"), QStringLiteral("FUEL_TYPE"),
        QStringLiteral("EVAP_VAPOR_PRESSURE_ABS"), QStringLiteral("EVAP_VAPOR_PRESSURE_ALT"),
        QStringLiteral("SHORT_O2_TRIM_B1"), QStringLiteral("LONG_O2_TRIM_B1"),
        QStringLiteral("SHORT_O2_TRIM_B2"), QStringLiteral("LONG_O2_TRIM_B2"),
        QStringLiteral("RELATIVE_ACCEL_POS"), QStringLiteral("HYBRID_BATTERY_REMAINING"),
        QStringLiteral("ELM_VOLTAGE"),
    };
    return names;
}

QVariantList OBDManager::get_supported_commands() const
{
    QVariantList result;
    for (const QString &cmd : m_supportedCommands)
        result.append(cmd);
    return result;
}

void OBDManager::enable_scanned_parameters(const QVariantList &paramNames)
{
    if (!m_settingsManager) return;
    for (const QVariant &v : paramNames)
        m_settingsManager->save_obd_parameter_enabled(v.toString(), true);
    qDebug() << "[OBD] Enabled" << paramNames.size() << "scanned parameters";
}

void OBDManager::enable_all_supported()
{
    if (!m_supportedCommands.isEmpty()) {
        QVariantList list;
        for (const QString &s : m_supportedCommands)
            list.append(s);
        enable_scanned_parameters(list);
    }
}

// ---------------------------------------------------------------------------
// Diagnostic mode
// ---------------------------------------------------------------------------

void OBDManager::enter_diagnostic_mode()
{
    {
        QMutexLocker locker(&m_diagnosticModeLock);
        if (m_diagnosticMode) {
            qDebug() << "[OBD] Already in diagnostic mode";
            return;
        }
        if (m_diagnosticModeTransitioning) {
            qDebug() << "[OBD] Diagnostic mode transition in progress";
            return;
        }
        if (!m_connected) {
            qDebug() << "[OBD] Cannot enter diagnostic mode -- not connected";
            return;
        }
        m_diagnosticModeTransitioning = true;
    }

    qDebug() << "[OBD] Entering diagnostic mode -- stopping polling...";

    m_dataWatchdogTimer.stop();

    // Stop polling on worker thread
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stopPolling", Qt::QueuedConnection);

    {
        QMutexLocker locker(&m_diagnosticModeLock);
        m_diagnosticMode = true;
        m_diagnosticModeTransitioning = false;
    }

    qDebug() << "[OBD] Diagnostic mode active";
}

void OBDManager::exit_diagnostic_mode()
{
    {
        QMutexLocker locker(&m_diagnosticModeLock);
        if (!m_diagnosticMode && !m_diagnosticModeTransitioning) {
            qDebug() << "[OBD] Not in diagnostic mode";
            return;
        }
        if (m_diagnosticModeTransitioning && !m_diagnosticMode) {
            qDebug() << "[OBD] Cancelling diagnostic mode entry";
            m_diagnosticModeTransitioning = false;
            return;
        }
        m_diagnosticModeTransitioning = true;
    }

    qDebug() << "[OBD] Exiting diagnostic mode -- performing full reconnect...";

    {
        QMutexLocker locker(&m_diagnosticModeLock);
        m_diagnosticMode = false;
        m_diagnosticModeTransitioning = false;
    }

    // Full reconnect to restore clean state
    bool wasConnected = m_connected;
    if (wasConnected) {
        m_connected = false;
        emit connectionStatusChanged(QStringLiteral("Reconnecting"));
        emit connectionStatusDetailChanged(QStringLiteral("Reconnecting after diagnostic mode..."));

        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }

        m_connectionAttempts = 0;
        m_reconnectingAfterDiagnostic = true;
        QTimer::singleShot(1000, this, &OBDManager::delayedReconnectAfterDiagnostic);
    }
}

void OBDManager::delayedReconnectAfterDiagnostic()
{
    qDebug() << "[OBD] Executing delayed reconnect after diagnostic mode...";
    if (m_diagnosticMode) {
        qDebug() << "[OBD] Back in diagnostic mode, skipping reconnect";
        m_reconnectingAfterDiagnostic = false;
        return;
    }

    {
        QMutexLocker locker(&m_lock);
        m_isConnecting = false;
    }

    cleanupConnection();
    m_reconnectingAfterDiagnostic = true;
    startConnection();
}

bool OBDManager::is_diagnostic_mode() const
{
    return m_diagnosticMode;
}

// ---------------------------------------------------------------------------
// DTC reading / clearing -- delegate to worker thread
// ---------------------------------------------------------------------------

void OBDManager::read_dtc()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read DTCs -- not connected";
        emit dtcCodesChanged(QVariantList());
        return;
    }
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doReadDtc", Qt::QueuedConnection);
}

void OBDManager::read_current_dtc()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read current DTCs -- not connected";
        return;
    }
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doReadCurrentDtc", Qt::QueuedConnection);
}

void OBDManager::read_status()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read status -- not connected";
        return;
    }
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doReadStatus", Qt::QueuedConnection);
}

void OBDManager::clear_dtc()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot clear DTCs -- not connected";
        emit dtcClearResult(false, QStringLiteral("Not connected to vehicle"));
        return;
    }
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doClearDtc", Qt::QueuedConnection);
}

void OBDManager::read_freeze_frame()
{
    if (!m_connected) {
        qDebug() << "[OBD] Cannot read freeze frame -- not connected";
        return;
    }
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "doReadFreezeFrame", Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Diagnostic data getters
// ---------------------------------------------------------------------------

QVariantList OBDManager::get_dtc_codes() const { return m_dtcCodes; }
int OBDManager::get_dtc_count() const { return m_dtcCount; }
bool OBDManager::get_mil_status() const { return m_milStatus; }
QVariantList OBDManager::get_freeze_frame() const { return m_freezeFrameDtcs; }

// ---------------------------------------------------------------------------
// Parameter value getters (Original 18)
// ---------------------------------------------------------------------------

float OBDManager::coolantTemp() const { return m_coolantTemp; }
float OBDManager::voltage() const { return m_voltage; }
float OBDManager::engineLoad() const { return m_engineLoad; }
float OBDManager::throttlePosition() const { return m_throttlePos; }
float OBDManager::intakeTemp() const { return m_intakeTemp; }
float OBDManager::timingAdvance() const { return m_timingAdvance; }
float OBDManager::massAirFlow() const { return m_massAirflow; }
float OBDManager::speedMPH() const { return m_speedMPH; }
float OBDManager::rpm() const { return m_rpm; }
float OBDManager::airFuelRatio() const { return m_airFuelRatio; }
float OBDManager::fuelLevel() const { return m_fuelLevel; }
float OBDManager::intakeManifoldPressure() const { return m_intakePressure; }
float OBDManager::shortTermFuelTrim() const { return m_shortTermFuelTrim; }
float OBDManager::longTermFuelTrim() const { return m_longTermFuelTrim; }
float OBDManager::oxygenSensorVoltage() const { return m_o2SensorVoltage; }
float OBDManager::fuelPressure() const { return m_fuelPressure; }
float OBDManager::engineOilTemp() const { return m_oilTemp; }
float OBDManager::ignitionTiming() const { return m_ignitionTiming; }

// ---------------------------------------------------------------------------
// Misc slots
// ---------------------------------------------------------------------------

void OBDManager::refresh_values()
{
    // Force an immediate poll cycle (no-op if not connected or if already polling)
    if (m_connected && m_worker)
        QMetaObject::invokeMethod(m_worker, "onPollTimer", Qt::QueuedConnection);
}

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


// ===========================================================================
// ===========================================================================
// OBDConnectionWorker implementation
// ===========================================================================
// ===========================================================================

OBDConnectionWorker::OBDConnectionWorker(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(0);  // Poll as fast as possible (like delay_cmds=0)
    QObject::connect(&m_pollTimer, &QTimer::timeout, this, &OBDConnectionWorker::onPollTimer);
}

OBDConnectionWorker::~OBDConnectionWorker()
{
    doDisconnect();
}

void OBDConnectionWorker::setPort(const QString &portName) { m_portName = portName; }
void OBDConnectionWorker::setFastMode(bool fast) { m_fastMode = fast; }
void OBDConnectionWorker::setTimeout(int seconds) { m_timeout = seconds; }
void OBDConnectionWorker::setPidsToWatch(const QList<PidKey> &pids) { m_pidsToWatch = pids; }

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void OBDConnectionWorker::doConnect()
{
    qDebug() << "[OBD Worker] Connecting to" << m_portName;

    // Create serial port on this thread
    m_serial = new QSerialPort(this);
    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(QSerialPort::Baud38400);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    QObject::connect(m_serial, &QSerialPort::errorOccurred,
                     this, &OBDConnectionWorker::onSerialError);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit initComplete(false, QStringLiteral("Failed to open port: %1").arg(m_serial->errorString()));
        return;
    }

    // Set read timeout
    m_serial->setReadBufferSize(4096);

    // Run ELM327 init sequence
    if (!sendInitSequence()) {
        m_serial->close();
        emit initComplete(false, QStringLiteral("ELM327 init failed -- adapter not responding"));
        return;
    }

    // Try to read a PID to verify vehicle connection
    // Send RPM request as a quick check
    QString response = sendCommand(ELM327Protocol::formatPidRequest(1, 0x0C), 3000);
    if (response.isEmpty() || response.toUpper().contains(QStringLiteral("NO DATA")) ||
        response.toUpper().contains(QStringLiteral("UNABLE"))) {
        // Adapter is connected but no vehicle
        emit initComplete(false, QStringLiteral("Connected to adapter, no vehicle response"));
        return;
    }

    m_initialized = true;
    emit initComplete(true, QStringLiteral("Connected"));
}

void OBDConnectionWorker::doDisconnect()
{
    stopPolling();
    if (m_serial) {
        if (m_serial->isOpen())
            m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    m_initialized = false;
    m_responseBuffer.clear();
}

bool OBDConnectionWorker::sendInitSequence()
{
    const auto cmds = ELM327Protocol::initCommands();
    for (const InitCommand &cmd : cmds) {
        QString response = sendCommand(cmd.command, cmd.timeoutMs);
        qDebug() << "[OBD Worker] Init:" << cmd.command.trimmed() << "->" << response;
        // ATZ response usually contains "ELM327" -- verify
        if (cmd.command.startsWith("ATZ") && !response.toUpper().contains(QStringLiteral("ELM"))) {
            // Not necessarily fatal -- some clones don't report ELM in reset response
            qDebug() << "[OBD Worker] Warning: ATZ response doesn't contain 'ELM'";
        }
    }
    return true;  // Init commands sent; adapter at least responded to serial
}

QString OBDConnectionWorker::sendCommand(const QByteArray &cmd, int timeoutMs)
{
    if (!m_serial || !m_serial->isOpen())
        return QString();

    m_responseBuffer.clear();
    m_serial->write(cmd);
    m_serial->flush();

    // Wait for response with timeout (blocking within worker thread is OK)
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        if (m_serial->waitForReadyRead(100)) {
            m_responseBuffer.feed(m_serial->readAll());
            auto resp = m_responseBuffer.getResponse();
            if (resp.has_value())
                return resp.value();
        }
    }

    // Timeout -- return whatever we have
    auto resp = m_responseBuffer.getResponse();
    return resp.value_or(QString());
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

void OBDConnectionWorker::startPolling()
{
    if (m_pidsToWatch.isEmpty()) {
        qDebug() << "[OBD Worker] No PIDs to watch";
        return;
    }
    m_currentPidIndex = 0;
    m_polling = true;

    if (m_fastMode) {
        m_pollTimer.setInterval(0);  // Maximum speed
    } else {
        m_pollTimer.setInterval(50);
    }
    m_pollTimer.start();
    qDebug() << "[OBD Worker] Polling started," << m_pidsToWatch.size() << "PIDs";
}

void OBDConnectionWorker::stopPolling()
{
    m_polling = false;
    m_pollTimer.stop();
}

void OBDConnectionWorker::onPollTimer()
{
    if (!m_polling || !m_serial || !m_serial->isOpen())
        return;

    if (m_pidsToWatch.isEmpty())
        return;

    // Get next PID to poll
    const PidKey &pid = m_pidsToWatch[m_currentPidIndex];
    m_currentPidIndex = (m_currentPidIndex + 1) % m_pidsToWatch.size();

    // Send request and get response
    QByteArray request = ELM327Protocol::formatPidRequest(pid.first, pid.second);
    QString response = sendCommand(request, 500);

    if (response.isEmpty())
        return;

    // Parse response
    auto parsed = ELM327Protocol::parseResponse(response);
    if (!parsed.has_value())
        return;

    // Decode PID
    auto decoded = ELM327Protocol::decodePid(parsed->mode, parsed->pid, parsed->dataBytes);
    if (!decoded.has_value())
        return;

    // Emit the data
    emit dataReceived(decoded->signalName, static_cast<float>(decoded->value));
}

void OBDConnectionWorker::onSerialReadyRead()
{
    // Not used in polling mode (we use blocking reads in sendCommand)
    // Kept as placeholder for potential async mode
}

void OBDConnectionWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    qDebug() << "[OBD Worker] Serial error:" << error << m_serial->errorString();

    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError) {
        stopPolling();
        emit connectionLost(QStringLiteral("Serial port error: %1").arg(m_serial->errorString()));
    }
}

// ---------------------------------------------------------------------------
// Diagnostic commands (run on worker thread, blocking)
// ---------------------------------------------------------------------------

void OBDConnectionWorker::doReadDtc()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 03 request (stored DTCs)
    QString response = sendCommand(QByteArrayLiteral("03\r"), 5000);
    QStringList codes;
    if (!response.isEmpty()) {
        codes = ELM327Protocol::parseDtcResponse(response);
    }
    emit dtcResult(codes);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doReadCurrentDtc()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 07 request (current/pending DTCs)
    QString response = sendCommand(QByteArrayLiteral("07\r"), 5000);
    QStringList codes;
    if (!response.isEmpty()) {
        codes = ELM327Protocol::parseDtcResponse(response);
    }
    emit dtcResult(codes);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doClearDtc()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 04 request (clear DTCs)
    QString response = sendCommand(QByteArrayLiteral("04\r"), 5000);
    bool success = !response.isEmpty() && !response.toUpper().contains(QStringLiteral("ERROR"));
    emit dtcCleared(success,
                    success ? QStringLiteral("DTCs cleared successfully")
                            : QStringLiteral("Clear command failed"));

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doReadStatus()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 01 PID 01 (Monitor status since DTCs cleared)
    QString response = sendCommand(ELM327Protocol::formatPidRequest(1, 0x01), 3000);
    bool mil = false;
    int dtcCount = 0;

    auto parsed = ELM327Protocol::parseResponse(response);
    if (parsed.has_value() && parsed->dataBytes.size() >= 4) {
        // Byte A, bit 7 = MIL, bits 0-6 = DTC count
        uint8_t byteA = parsed->dataBytes[0];
        mil = (byteA & 0x80) != 0;
        dtcCount = byteA & 0x7F;
    }

    emit milStatus(mil, dtcCount);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doReadFreezeFrame()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 02 PID 02 (freeze frame DTC)
    QString response = sendCommand(QByteArrayLiteral("0202\r"), 5000);
    QStringList codes;
    if (!response.isEmpty()) {
        codes = ELM327Protocol::parseDtcResponse(response);
    }
    emit freezeFrame(codes);

    if (wasPolling) startPolling();
}

// ---------------------------------------------------------------------------
// Vehicle scan
// ---------------------------------------------------------------------------

QSet<int> OBDConnectionWorker::querySupportedPids()
{
    QSet<int> allSupported;

    // Query supported PIDs in ranges: 00, 20, 40, 60, 80, A0, C0, E0
    const int ranges[] = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0};
    for (int base : ranges) {
        QString response = sendCommand(ELM327Protocol::formatPidRequest(1, base), 3000);
        auto parsed = ELM327Protocol::parseResponse(response);
        if (!parsed.has_value() || parsed->dataBytes.size() < 4)
            break;  // Vehicle doesn't support this range

        QSet<int> supported = ELM327Protocol::parseSupportedPids(parsed->dataBytes);
        for (int pid : supported) {
            allSupported.insert(base + pid);
        }

        // If the last PID in the bitmap (bit 32 = base+0x20) is not set,
        // the vehicle doesn't support the next range query
        if (!supported.contains(32))
            break;
    }

    return allSupported;
}

void OBDConnectionWorker::doScanVehicle()
{
    emit scanOutput(QStringLiteral("[SCAN] Starting vehicle PID scan..."));
    emit scanOutput(QStringLiteral("[INFO] Querying supported PIDs from vehicle..."));

    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    QSet<int> supportedPids = querySupportedPids();

    if (supportedPids.isEmpty()) {
        emit scanOutput(QStringLiteral("[WARN] No supported PIDs returned from vehicle"));
        emit scanProgress(100, QStringLiteral("No supported PIDs found"));
        emit scanComplete(QStringList());
        if (wasPolling) startPolling();
        return;
    }

    emit scanOutput(QStringLiteral("[INFO] Vehicle reports %1 supported PIDs")
                        .arg(supportedPids.size()));

    // Check which PIDs from our table are supported
    const auto &table = ELM327Protocol::pidTable();
    QStringList supportedNames;

    // We need to map PidKey -> command name for the result
    // Reuse the same mapping from OBDManager::startConnection
    static const QHash<PidKey, QString> pidToCommand = {
        {{1,0x05}, QStringLiteral("COOLANT_TEMP")},
        {{1,0x42}, QStringLiteral("CONTROL_MODULE_VOLTAGE")},
        {{1,0x04}, QStringLiteral("ENGINE_LOAD")},
        {{1,0x11}, QStringLiteral("THROTTLE_POS")},
        {{1,0x0F}, QStringLiteral("INTAKE_TEMP")},
        {{1,0x0E}, QStringLiteral("TIMING_ADVANCE")},
        {{1,0x10}, QStringLiteral("MAF")},
        {{1,0x0D}, QStringLiteral("SPEED")},
        {{1,0x0C}, QStringLiteral("RPM")},
        {{1,0x44}, QStringLiteral("COMMANDED_EQUIV_RATIO")},
        {{1,0x2F}, QStringLiteral("FUEL_LEVEL")},
        {{1,0x0B}, QStringLiteral("INTAKE_PRESSURE")},
        {{1,0x06}, QStringLiteral("SHORT_FUEL_TRIM_1")},
        {{1,0x07}, QStringLiteral("LONG_FUEL_TRIM_1")},
        {{1,0x14}, QStringLiteral("O2_B1S1")},
        {{1,0x0A}, QStringLiteral("FUEL_PRESSURE")},
        {{1,0x5C}, QStringLiteral("OIL_TEMP")},
        {{1,0x1F}, QStringLiteral("RUN_TIME")},
        {{1,0x21}, QStringLiteral("DISTANCE_W_MIL")},
        {{1,0x22}, QStringLiteral("FUEL_RAIL_PRESSURE_VAC")},
        {{1,0x23}, QStringLiteral("FUEL_RAIL_PRESSURE_DIRECT")},
        {{1,0x33}, QStringLiteral("BAROMETRIC_PRESSURE")},
        {{1,0x46}, QStringLiteral("AMBIANT_AIR_TEMP")},
        {{1,0x45}, QStringLiteral("RELATIVE_THROTTLE_POS")},
        {{1,0x47}, QStringLiteral("THROTTLE_POS_B")},
        {{1,0x49}, QStringLiteral("ACCELERATOR_POS_D")},
        {{1,0x3C}, QStringLiteral("CATALYST_TEMP_B1S1")},
        {{1,0x3D}, QStringLiteral("CATALYST_TEMP_B1S2")},
        {{1,0x32}, QStringLiteral("EVAP_VAPOR_PRESSURE")},
        {{1,0x08}, QStringLiteral("SHORT_FUEL_TRIM_2")},
        {{1,0x09}, QStringLiteral("LONG_FUEL_TRIM_2")},
        {{1,0x15}, QStringLiteral("O2_B1S2")},
        {{1,0x16}, QStringLiteral("O2_B2S1")},
        {{1,0x17}, QStringLiteral("O2_B2S2")},
        {{1,0x31}, QStringLiteral("DISTANCE_SINCE_DTC_CLEAR")},
        {{1,0x30}, QStringLiteral("WARMUPS_SINCE_DTC_CLEAR")},
        {{1,0x43}, QStringLiteral("ABSOLUTE_LOAD")},
        {{1,0x2C}, QStringLiteral("COMMANDED_EGR")},
        {{1,0x2D}, QStringLiteral("EGR_ERROR")},
        {{1,0x52}, QStringLiteral("ETHANOL_PERCENT")},
        {{1,0x3E}, QStringLiteral("CATALYST_TEMP_B2S1")},
        {{1,0x3F}, QStringLiteral("CATALYST_TEMP_B2S2")},
        {{1,0x4A}, QStringLiteral("THROTTLE_POS_C")},
        {{1,0x4B}, QStringLiteral("ACCELERATOR_POS_E")},
        {{1,0x4C}, QStringLiteral("ACCELERATOR_POS_F")},
        {{1,0x4D}, QStringLiteral("RUN_TIME_MIL")},
        {{1,0x4E}, QStringLiteral("TIME_SINCE_DTC_CLEARED")},
        {{1,0x50}, QStringLiteral("MAX_MAF")},
        {{1,0x51}, QStringLiteral("FUEL_TYPE")},
        {{1,0x54}, QStringLiteral("EVAP_VAPOR_PRESSURE_ABS")},
        {{1,0x55}, QStringLiteral("EVAP_VAPOR_PRESSURE_ALT")},
        {{1,0x56}, QStringLiteral("SHORT_O2_TRIM_B1")},
        {{1,0x57}, QStringLiteral("LONG_O2_TRIM_B1")},
        {{1,0x58}, QStringLiteral("SHORT_O2_TRIM_B2")},
        {{1,0x59}, QStringLiteral("LONG_O2_TRIM_B2")},
        {{1,0x5A}, QStringLiteral("RELATIVE_ACCEL_POS")},
        {{1,0x5B}, QStringLiteral("HYBRID_BATTERY_REMAINING")},
        {{1,0x2E}, QStringLiteral("EVAPORATIVE_PURGE")},
        {{1,0x5D}, QStringLiteral("FUEL_INJECT_TIMING")},
        {{1,0x5E}, QStringLiteral("FUEL_RATE")},
        {{1,0x4F}, QStringLiteral("THROTTLE_ACTUATOR")},
    };

    int total = pidToCommand.size();
    int i = 0;
    for (auto it = pidToCommand.constBegin(); it != pidToCommand.constEnd(); ++it, ++i) {
        int progress = static_cast<int>((static_cast<float>(i) / total) * 100.0f);
        emit scanProgress(progress, QStringLiteral("Checking %1...").arg(it.value()));

        // Check if this PID is in the vehicle's supported set (mode 1, pid number)
        if (supportedPids.contains(it.key().second)) {
            supportedNames.append(it.value());
            emit scanOutput(QStringLiteral("[OK] %1").arg(it.value()));
        }
    }

    // Summary
    int unsupported = total - supportedNames.size();
    emit scanOutput(QString());
    emit scanOutput(QStringLiteral("========================================"));
    emit scanOutput(QStringLiteral("[DONE] Scan complete!"));
    emit scanOutput(QStringLiteral("[INFO] Supported: %1 parameters").arg(supportedNames.size()));
    emit scanOutput(QStringLiteral("[INFO] Unsupported: %1 parameters").arg(unsupported));
    emit scanOutput(QStringLiteral("========================================"));

    emit scanComplete(supportedNames);

    if (wasPolling) startPolling();
}

// ===========================================================================
// Android Bluetooth RFCOMM implementation
//
// On Android the QSerialPort/worker-thread flow is bypassed. QBluetoothSocket
// runs on the main thread and is fully event-driven: connectToService()
// returns immediately and we react to connected()/readyRead()/error() signals.
// ELM327 init runs as a state machine via QTimer between AT commands; PID
// polling is response-driven (next PID requested when the previous response
// arrives). Mirrors the deleted Python AndroidOBDManager (commit 75b1d3f^).
// ===========================================================================
#ifdef Q_OS_ANDROID

void OBDManager::logAndroid(const QString &line)
{
    qDebug().noquote() << "[OBD]" << line;
    emit connectionLogLineAppended(line);
    // Mirror to the existing detail text element on the OBD page so the user
    // sees the latest state transition without needing a new QML widget.
    emit connectionStatusDetailChanged(line);
}

QStringList OBDManager::listAndroidPairedDevices()
{
    // Listing paired devices requires QBluetoothLocalDevice, which Qt's
    // permission helper guards behind QBluetoothPermission::Access. If we
    // haven't been granted Bluetooth at runtime yet, instantiating the
    // local device just spams a warning every poll cycle. Skip until the
    // user has actually triggered the permission flow via force_connect.
    QStringList macs;
    if (!m_androidPermissionGranted)
        return macs;
    QBluetoothLocalDevice local;
    if (!local.isValid())
        return macs;
    const auto bonded = local.connectedDevices();
    for (const QBluetoothAddress &addr : bonded)
        macs.append(addr.toString());
    return macs;
}

void OBDManager::requestAndroidBluetoothPermission(std::function<void(bool)> done)
{
    if (m_androidPermissionGranted) {
        done(true);
        return;
    }

    // Qt 6.7's QBluetoothPermission helper has been observed to return
    // PermissionStatus::Denied (and immediately fail requestPermission()
    // synchronously without showing a dialog) even when the underlying
    // Android grants for BLUETOOTH_CONNECT/BLUETOOTH_SCAN are present.
    // Bypass it: check the system permission grants directly via
    // QJniObject/Android Context, and if either is missing fall back to
    // Qt's request flow (which DOES correctly pop the system dialog when
    // the permission is first-time-undetermined).
    auto isSystemGranted = [](const QString &perm) -> bool {
#ifdef Q_OS_ANDROID
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return false;
        QJniObject jPerm = QJniObject::fromString(perm);
        // ContextCompat.checkSelfPermission would be cleaner but pulls in
        // androidx; Context.checkSelfPermission has the same semantics on
        // API 23+ (we target API 30+ via manifest).
        jint result = context.callMethod<jint>(
            "checkSelfPermission", "(Ljava/lang/String;)I", jPerm.object<jstring>());
        // PackageManager.PERMISSION_GRANTED == 0
        return result == 0;
#else
        Q_UNUSED(perm);
        return true;
#endif
    };

    bool connectGranted = isSystemGranted(QStringLiteral("android.permission.BLUETOOTH_CONNECT"));
    bool scanGranted    = isSystemGranted(QStringLiteral("android.permission.BLUETOOTH_SCAN"));
    bool advertiseGranted = isSystemGranted(QStringLiteral("android.permission.BLUETOOTH_ADVERTISE"));
    logAndroid(QStringLiteral("system perms — CONNECT:%1 SCAN:%2 ADVERTISE:%3")
        .arg(connectGranted ? QStringLiteral("yes") : QStringLiteral("NO"),
             scanGranted    ? QStringLiteral("yes") : QStringLiteral("NO"),
             advertiseGranted ? QStringLiteral("yes") : QStringLiteral("NO")));

    // Compare against Qt's view via QtAndroidPrivate::checkPermission so we
    // can see exactly which name Qt thinks is denied.
    auto qtCheck = [](const QString &p) -> QString {
#ifdef Q_OS_ANDROID
        auto fut = QtAndroidPrivate::checkPermission(p);
        fut.waitForFinished();
        QtAndroidPrivate::PermissionResult r = fut.result();
        if (r == QtAndroidPrivate::Authorized) return QStringLiteral("granted");
        return QStringLiteral("DENIED");
#else
        Q_UNUSED(p);
        return QStringLiteral("?");
#endif
    };
    logAndroid(QStringLiteral("Qt perms — CONNECT:%1 SCAN:%2 ADVERTISE:%3")
        .arg(qtCheck(QStringLiteral("android.permission.BLUETOOTH_CONNECT")),
             qtCheck(QStringLiteral("android.permission.BLUETOOTH_SCAN")),
             qtCheck(QStringLiteral("android.permission.BLUETOOTH_ADVERTISE"))));
    logAndroid(QStringLiteral("Qt sdk version: %1")
        .arg(QtAndroidPrivate::androidSdkVersion()));

    // Always go through Qt's requestPermission(), even when the system grant
    // is already present. Qt 6.7's QBluetoothPermission/QBluetoothLocalDevice
    // have an internal cache that doesn't get updated by external `pm grant`
    // or by silent grants from a previous request — they only flip to
    // Granted when requestPermission() resolves to Granted in *this* process.
    // QLowEnergyController checks the same cache before initializing the
    // BLE adapter, so without this re-trigger every BLE connect fails with
    // "Permissions not authorized" / "invalid adapter" / "Bluetooth disabled
    // on phone" even when dumpsys clearly shows the runtime grants. When the
    // system already says granted, requestPermission resolves synchronously
    // with Granted (no dialog is shown), so this is essentially free.
    QBluetoothPermission perm;
    perm.setCommunicationModes(QBluetoothPermission::Access);
    qApp->requestPermission(perm, this, [this, done, isSystemGranted](const QPermission &p) {
        bool qtGranted = (p.status() == Qt::PermissionStatus::Granted);
        // Backstop with a direct system check in case Qt's API is buggy.
        bool sysGranted =
            isSystemGranted(QStringLiteral("android.permission.BLUETOOTH_CONNECT"))
         && isSystemGranted(QStringLiteral("android.permission.BLUETOOTH_SCAN"));
        bool granted = qtGranted || sysGranted;
        m_androidPermissionGranted = granted;
        logAndroid(QStringLiteral("permission resolved — qt:%1 system:%2")
            .arg(qtGranted  ? QStringLiteral("yes") : QStringLiteral("NO"),
                 sysGranted ? QStringLiteral("yes") : QStringLiteral("NO")));
        if (!granted) {
            emit connectionStatusChanged(QStringLiteral("Bluetooth permission denied"));
            emit connectionStatusDetailChanged(
                QStringLiteral("Grant Bluetooth in Settings -> Apps -> OCTAVE -> Permissions"));
        }
        done(granted);
    });
}

void OBDManager::startAndroidConnection()
{
    QString address = getConfiguredPort();
    if (!checkPortExists(address)) {
        m_connected = false;
        emit connectionStatusChanged(QStringLiteral("No MAC Address"));
        emit connectionStatusDetailChanged(
            QStringLiteral("Enter your ELM327 MAC in OBD Settings, then tap Connect"));
        emit connectionProgressChanged(0);
        emit devicePresenceChanged(false);
        qDebug() << "[OBD] Android: no/invalid MAC configured:" << address;
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        return;
    }

    cleanupAndroidConnection();

    m_btResponseBuffer.clear();
    m_androidElmInitialized = false;
    m_androidInitStep = 0;
    m_androidPolling = false;
    m_androidStaleCount = 0;
    m_androidSupportedPids.clear();
    m_bleLastJavaState = -1;

    emit connectionProgressChanged(20);
    emit connectionStatusChanged(QStringLiteral("Connecting BLE..."));
    logAndroid(QStringLiteral("BLE connect → %1 (via OctaveOBDBridge)").arg(address));

    // Hand off to the Java bridge — bypasses Qt's broken BLE permission gate.
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    QJniObject jMac = QJniObject::fromString(address);
    jboolean ok = QJniObject::callStaticMethod<jboolean>(
        "org/octave/app/OctaveOBDBridge",
        "connect",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        ctx.object<jobject>(),
        jMac.object<jstring>());
    if (!ok) {
        QString err = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "lastError",
            "()Ljava/lang/String;").toString();
        logAndroid(QStringLiteral("connect() failed: %1").arg(err));
        emit connectionStatusChanged(QStringLiteral("BLE connect failed"));
        emit connectionProgressChanged(0);
        QMutexLocker locker(&m_lock);
        m_isConnecting = false;
        return;
    }

    // Poll the Java bridge for state transitions and notification bytes.
    // 50 ms is fast enough that BLE notifications feel real-time without
    // burning CPU when idle.
    m_bleNotifyPoller.disconnect();
    m_bleNotifyPoller.setInterval(50);
    QObject::connect(&m_bleNotifyPoller, &QTimer::timeout,
                     this, &OBDManager::onAndroidBlePoll);
    m_bleNotifyPoller.start();
}

void OBDManager::cleanupAndroidConnection()
{
    m_androidInitTimer.stop();
    m_androidPollWatchdog.stop();
    m_bleNotifyPoller.stop();
    m_androidPolling = false;
    m_androidElmInitialized = false;
    QJniObject::callStaticMethod<void>(
        "org/octave/app/OctaveOBDBridge", "disconnect", "()V");
    m_btResponseBuffer.clear();
}

void OBDManager::onAndroidBlePoll()
{
    // 1) Drain any pending FFE1 notification bytes.
    while (true) {
        QJniObject jArr = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "pollResponse", "()[B");
        if (!jArr.isValid()) break;
        jbyteArray rawArr = jArr.object<jbyteArray>();
        if (!rawArr) break;
        QJniEnvironment env;
        jsize len = env->GetArrayLength(rawArr);
        if (len <= 0) break;
        QByteArray chunk(len, 0);
        env->GetByteArrayRegion(rawArr, 0, len,
            reinterpret_cast<jbyte *>(chunk.data()));
        m_btResponseBuffer.feed(chunk);
        while (true) {
            auto resp = m_btResponseBuffer.getResponse();
            if (!resp.has_value()) break;
            processAndroidResponse(resp.value());
        }
    }

    // 2) Watch state transitions and react.
    jint state = QJniObject::callStaticMethod<jint>(
        "org/octave/app/OctaveOBDBridge", "stateCode", "()I");
    if (state == m_bleLastJavaState) return;

    // 0=DISCONNECTED 1=CONNECTING 2=DISCOVERING 3=READY 4=ERROR
    switch (state) {
    case 0: { // DISCONNECTED
        QString ev = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "lastEvent",
            "()Ljava/lang/String;").toString();
        logAndroid(QStringLiteral("disconnected (%1)").arg(ev));
        bool wasConnected = m_connected;
        m_connected = false;
        m_androidElmInitialized = false;
        m_androidPolling = false;
        m_bleNotifyPoller.stop();
        m_androidPollWatchdog.stop();
        emit connectionStatusChanged(QStringLiteral("Disconnected"));
        emit connectionProgressChanged(0);
        if (wasConnected && m_androidReconnectAttempts < m_androidMaxReconnect) {
            ++m_androidReconnectAttempts;
            int delay = qMin(2000 * m_androidReconnectAttempts, 10000);
            QTimer::singleShot(delay, this, &OBDManager::startAndroidConnection);
        }
        break;
    }
    case 2: { // DISCOVERING (GATT connected, services pending)
        logAndroid(QStringLiteral("GATT connected, discovering services…"));
        emit connectionProgressChanged(50);
        emit connectionStatusChanged(QStringLiteral("Discovering services..."));
        break;
    }
    case 3: { // READY (FFE0 + FFE1 + notifications enabled)
        logAndroid(QStringLiteral("FFE0/FFE1 ready, notifications enabled"));
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        emit connectionProgressChanged(70);
        emit connectionStatusChanged(QStringLiteral("Initializing ELM327..."));

        // Don't re-save the MAC back to settings here — it was already
        // persisted when the user entered it. Saving the same value triggers
        // SettingsManager::obdBluetoothPortChanged → onSettingsPortChanged →
        // port-change debounce → force_connect → which would tear this very
        // session down mid-ELM-init.

        m_androidElmInitialized = false;
        m_androidInitStep = 0;
        m_btResponseBuffer.clear();

        m_androidInitTimer.disconnect();
        m_androidInitTimer.setSingleShot(true);
        QObject::connect(&m_androidInitTimer, &QTimer::timeout,
                         this, &OBDManager::onAndroidInitStep);

        QTimer::singleShot(500, this, &OBDManager::sendNextAndroidInitCommand);
        break;
    }
    case 4: { // ERROR
        QString err = QJniObject::callStaticObjectMethod(
            "org/octave/app/OctaveOBDBridge", "lastError",
            "()Ljava/lang/String;").toString();
        logAndroid(QStringLiteral("BLE error: %1").arg(err));
        emit connectionStatusChanged(QStringLiteral("BLE error"));
        emit connectionProgressChanged(0);
        m_bleNotifyPoller.stop();
        {
            QMutexLocker locker(&m_lock);
            m_isConnecting = false;
        }
        if (m_androidReconnectAttempts < m_androidMaxReconnect) {
            ++m_androidReconnectAttempts;
            int delay = qMin(2000 * m_androidReconnectAttempts, 10000);
            QTimer::singleShot(delay, this, &OBDManager::startAndroidConnection);
        }
        break;
    }
    default:
        break;
    }
    m_bleLastJavaState = state;
}

void OBDManager::writeAndroidBytes(const QByteArray &data)
{
    if (m_bleLastJavaState != 3) {
        qDebug() << "[OBD] Android: write while not READY (state="
                 << m_bleLastJavaState << "), dropping" << data;
        return;
    }
    // BLE default ATT MTU is 23 bytes (20 byte payload). Chunk to be safe.
    constexpr int CHUNK = 20;
    QJniEnvironment env;
    for (int i = 0; i < data.size(); i += CHUNK) {
        QByteArray slice = data.mid(i, CHUNK);
        jbyteArray jArr = env->NewByteArray(slice.size());
        env->SetByteArrayRegion(jArr, 0, slice.size(),
            reinterpret_cast<const jbyte *>(slice.constData()));
        QJniObject::callStaticMethod<jboolean>(
            "org/octave/app/OctaveOBDBridge", "write", "([B)Z", jArr);
        env->DeleteLocalRef(jArr);
    }
}

void OBDManager::sendNextAndroidInitCommand()
{
    const auto &cmds = ELM327Protocol::initCommands();
    if (m_androidInitStep >= cmds.size()) {
        m_androidElmInitialized = true;
        qDebug() << "[OBD] Android: ELM327 init complete, querying supported PIDs";
        emit connectionProgressChanged(80);
        emit connectionStatusChanged(QStringLiteral("Querying PIDs..."));
        queryAndroidSupportedPids();
        return;
    }
    const InitCommand &c = cmds[m_androidInitStep];
    qDebug() << "[OBD] Android: init"
             << (m_androidInitStep + 1) << "/" << cmds.size()
             << ":" << c.command.trimmed();
    writeAndroidBytes(c.command);
    ++m_androidInitStep;
    m_androidInitTimer.start(c.timeoutMs);
}

void OBDManager::onAndroidInitStep()
{
    // Init-step timeout: send the next command regardless of response.
    sendNextAndroidInitCommand();
}

void OBDManager::queryAndroidSupportedPids()
{
    writeAndroidBytes(QByteArrayLiteral("0100\r"));
    QTimer::singleShot(2000, this, &OBDManager::finalizeAndroidConnection);
}

void OBDManager::finalizeAndroidConnection()
{
    m_connected = true;
    m_androidReconnectAttempts = 0;
    qDebug() << "[OBD] Android: connected with"
             << m_androidSupportedPids.size() << "supported PIDs";
    emit connectionStatusChanged(QStringLiteral("Connected"));
    emit connectionProgressChanged(100);
    emit devicePresenceChanged(true);

    // Build poll list from settings (same logic as desktop) intersected with
    // what the vehicle reports supporting via mode 01 PID 00/20/40/60.
    static const QHash<PidKey, QString> pidToCommand = {
        {{1,0x05}, QStringLiteral("COOLANT_TEMP")},
        {{1,0x42}, QStringLiteral("CONTROL_MODULE_VOLTAGE")},
        {{1,0x04}, QStringLiteral("ENGINE_LOAD")},
        {{1,0x11}, QStringLiteral("THROTTLE_POS")},
        {{1,0x0F}, QStringLiteral("INTAKE_TEMP")},
        {{1,0x0E}, QStringLiteral("TIMING_ADVANCE")},
        {{1,0x10}, QStringLiteral("MAF")},
        {{1,0x0D}, QStringLiteral("SPEED")},
        {{1,0x0C}, QStringLiteral("RPM")},
        {{1,0x44}, QStringLiteral("COMMANDED_EQUIV_RATIO")},
        {{1,0x2F}, QStringLiteral("FUEL_LEVEL")},
        {{1,0x0B}, QStringLiteral("INTAKE_PRESSURE")},
        {{1,0x06}, QStringLiteral("SHORT_FUEL_TRIM_1")},
        {{1,0x07}, QStringLiteral("LONG_FUEL_TRIM_1")},
        {{1,0x14}, QStringLiteral("O2_B1S1")},
        {{1,0x0A}, QStringLiteral("FUEL_PRESSURE")},
        {{1,0x5C}, QStringLiteral("OIL_TEMP")},
    };
    static const QSet<QString> defaultEnabled = {
        QStringLiteral("COOLANT_TEMP"), QStringLiteral("CONTROL_MODULE_VOLTAGE"),
        QStringLiteral("ENGINE_LOAD"), QStringLiteral("THROTTLE_POS"),
        QStringLiteral("INTAKE_TEMP"), QStringLiteral("TIMING_ADVANCE"),
        QStringLiteral("MAF"), QStringLiteral("SPEED"), QStringLiteral("RPM"),
        QStringLiteral("COMMANDED_EQUIV_RATIO"), QStringLiteral("FUEL_LEVEL"),
        QStringLiteral("INTAKE_PRESSURE"), QStringLiteral("SHORT_FUEL_TRIM_1"),
        QStringLiteral("LONG_FUEL_TRIM_1"), QStringLiteral("O2_B1S1"),
        QStringLiteral("FUEL_PRESSURE"), QStringLiteral("OIL_TEMP"),
    };

    m_androidEnabledPids.clear();
    for (auto it = pidToCommand.constBegin(); it != pidToCommand.constEnd(); ++it) {
        bool isDefault = defaultEnabled.contains(it.value());
        bool watch = m_settingsManager
            ? m_settingsManager->get_obd_parameter_enabled(it.value(), isDefault)
            : isDefault;
        if (!watch) continue;
        // If the vehicle reported a supported set, filter to only those PIDs.
        // (mode-1 PID number is the second of the pair.)
        if (!m_androidSupportedPids.isEmpty() &&
            !m_androidSupportedPids.contains(it.key().second))
            continue;
        m_androidEnabledPids.append(it.key());
    }
    if (m_androidEnabledPids.isEmpty()) {
        // Vehicle didn't answer 0100 -- fall back to the default set so the
        // user still sees readings. ELM327 will return NO DATA for unsupported
        // PIDs which our code paths already ignore.
        for (auto it = pidToCommand.constBegin(); it != pidToCommand.constEnd(); ++it) {
            if (defaultEnabled.contains(it.value()))
                m_androidEnabledPids.append(it.key());
        }
    }

    m_androidPollIndex = 0;
    m_androidPolling = true;
    m_androidStaleCount = 0;

    m_androidPollWatchdog.disconnect();
    m_androidPollWatchdog.setSingleShot(true);
    QObject::connect(&m_androidPollWatchdog, &QTimer::timeout,
                     this, &OBDManager::onAndroidPollWatchdog);

    pollNextAndroidPid();
}

void OBDManager::pollNextAndroidPid()
{
    if (!m_connected || !m_androidPolling || m_androidEnabledPids.isEmpty())
        return;
    const PidKey &pid = m_androidEnabledPids[m_androidPollIndex];
    m_androidPollIndex = (m_androidPollIndex + 1) % m_androidEnabledPids.size();
    writeAndroidBytes(ELM327Protocol::formatPidRequest(pid.first, pid.second));
    m_androidPollWatchdog.start(500);
}

void OBDManager::onAndroidPollWatchdog()
{
    if (!m_androidPolling || !m_connected)
        return;
    ++m_androidStaleCount;
    if (m_androidStaleCount >= 10) {
        qDebug() << "[OBD] Android: data stale 5s, flushing buffer";
        m_btResponseBuffer.clear();
        m_androidStaleCount = 0;
    }
    pollNextAndroidPid();
}

void OBDManager::processAndroidResponse(const QString &response)
{
    if (response.isEmpty()) return;

    if (!m_androidElmInitialized) {
        qDebug() << "[OBD] Android: init response:" << response;
        return;
    }

    m_androidStaleCount = 0;

    auto parsed = ELM327Protocol::parseResponse(response);
    if (!parsed.has_value()) {
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    if (parsed->mode == 1 &&
        (parsed->pid == 0x00 || parsed->pid == 0x20 ||
         parsed->pid == 0x40 || parsed->pid == 0x60)) {
        const auto pids = ELM327Protocol::parseSupportedPids(parsed->dataBytes);
        for (int p : pids)
            m_androidSupportedPids.insert(p + parsed->pid);
        if (m_androidPolling)
            pollNextAndroidPid();
        return;
    }

    auto decoded = ELM327Protocol::decodePid(parsed->mode, parsed->pid, parsed->dataBytes);
    if (decoded.has_value()) {
        emitParameterSignal(decoded->signalName, static_cast<float>(decoded->value));
        m_lastDataReceived = QDateTime::currentMSecsSinceEpoch();
    }

    if (m_androidPolling)
        pollNextAndroidPid();
}

#endif // Q_OS_ANDROID
