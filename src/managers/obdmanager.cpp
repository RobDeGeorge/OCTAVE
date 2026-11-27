// OBDManager: construction, the per-parameter signal dispatch, the status
// properties and the small getters. The rest of the class is in
// obdmanager_connection.cpp (finding and holding the link),
// obdmanager_polling.cpp (what is polled, and the vehicle scan),
// obdmanager_diagnostics.cpp (trouble codes, VIN, the bus monitor) and
// obdmanager_android.cpp (Android's own Bluetooth LE path).
#include "obdmanager.h"
#include "elm327protocol.h"

#include <QDebug>
#include <QMetaMethod>
#include <QMetaObject>
#include <QProcess>

// ===========================================================================
// OBDManager
// ===========================================================================

OBDManager::OBDManager(SettingsManager *settingsManager, QObject *parent)
    : QObject(parent)
    , m_settingsManager(settingsManager)
{
    buildSignalDispatch();
    {
        const auto &table = ELM327Protocol::pidTable();
        for (auto it = table.constBegin(); it != table.constEnd(); ++it)
            m_signalToParamId.insert(it.value().signalName, it.value().commandName);
    }

    // Cache the latest status / detail / progress so the Q_PROPERTY readers
    // reflect whatever was last emitted. OBDManager's own emits go through
    // setConnectionStatus() & co., which store before emitting: QML bindings
    // are notified before any connected slot runs, so a cache filled only
    // here left connectionStatus / connectionState one change behind in QML
    // (a connected car read "Connecting"). These hooks keep the cache right
    // for a signal emitted from outside (the tests do).
    connect(this, &OBDManager::connectionStatusChanged, this,
            [this](const QString &status) { m_connectionStatus = status; });
    connect(this, &OBDManager::connectionStatusDetailChanged, this,
            [this](const QString &detail) { m_connectionDetail = detail; });
    connect(this, &OBDManager::connectionProgressChanged, this,
            [this](int progress) { m_connectionProgress = progress; });
    connect(this, &OBDManager::connectionLogLineAppended, this, [this](const QString &line) {
        m_connectionLog.append(line);
        while (m_connectionLog.size() > kConnectionLogLines)
            m_connectionLog.removeFirst();
        emit connectionLogChanged();
    });

    // --- Timers ---

    // Startup timer -- defer initial connection from constructor
    m_retryTimer.setSingleShot(true);
    QObject::connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (!m_connected)
            startConnection();
    });

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
        // Per-parameter enable settings no longer drive polling -- the poll
        // set follows setParameterDemand() (what is on screen).
    }

    m_demandDebounce.setSingleShot(true);
    m_demandDebounce.setInterval(250);
    QObject::connect(&m_demandDebounce, &QTimer::timeout, this, &OBDManager::applyPollList);

    // Start the deferred init
    m_startupTimer.start();
}

OBDManager::~OBDManager()
{
    // Don't let a still-running rfcomm bind call back into a dying manager
#if QT_CONFIG(process)
    if (m_rfcommBindProc) {
        m_rfcommBindProc->disconnect();
        m_rfcommBindProc->kill();
        m_rfcommBindProc->waitForFinished(1000);
    }
#endif
    close();
}

// ---------------------------------------------------------------------------
// Signal dispatch table -- maps ELM327Protocol signal names to member
// variable storage + signal emission.
// ---------------------------------------------------------------------------

void OBDManager::buildSignalDispatch()
{
    // Keyed by the signal's own name, which is what ELM327Protocol's PID
    // table calls it. The original 18 also keep their last value, for the
    // getters; nothing reads the others back, so they are only emitted.
    const auto add = [this](void (OBDManager::*sig)(float), float OBDManager::*store = nullptr) {
        m_signalDispatch.insert(QString::fromLatin1(QMetaMethod::fromSignal(sig).name()), {sig, store});
    };
    add(&OBDManager::coolantTempChanged, &OBDManager::m_coolantTemp);
    add(&OBDManager::voltageChanged, &OBDManager::m_voltage);
    add(&OBDManager::engineLoadChanged, &OBDManager::m_engineLoad);
    add(&OBDManager::throttlePositionChanged, &OBDManager::m_throttlePos);
    add(&OBDManager::intakeAirTempChanged, &OBDManager::m_intakeTemp);
    add(&OBDManager::timingAdvanceChanged, &OBDManager::m_timingAdvance);
    add(&OBDManager::massAirFlowChanged, &OBDManager::m_massAirflow);
    add(&OBDManager::speedMPHChanged, &OBDManager::m_speedMPH);
    add(&OBDManager::rpmChanged, &OBDManager::m_rpm);
    add(&OBDManager::airFuelRatioChanged, &OBDManager::m_airFuelRatio);
    add(&OBDManager::fuelLevelChanged, &OBDManager::m_fuelLevel);
    add(&OBDManager::intakeManifoldPressureChanged, &OBDManager::m_intakePressure);
    add(&OBDManager::shortTermFuelTrimChanged, &OBDManager::m_shortTermFuelTrim);
    add(&OBDManager::longTermFuelTrimChanged, &OBDManager::m_longTermFuelTrim);
    add(&OBDManager::oxygenSensorVoltageChanged, &OBDManager::m_o2SensorVoltage);
    add(&OBDManager::fuelPressureChanged, &OBDManager::m_fuelPressure);
    add(&OBDManager::engineOilTempChanged, &OBDManager::m_oilTemp);
    add(&OBDManager::runTimeChanged);
    add(&OBDManager::distanceWithMILChanged);
    add(&OBDManager::fuelRailPressureChanged);
    add(&OBDManager::fuelRailPressureDirectChanged);
    add(&OBDManager::barometricPressureChanged);
    add(&OBDManager::ambientAirTempChanged);
    add(&OBDManager::relativeThrottlePosChanged);
    add(&OBDManager::absoluteThrottlePosBChanged);
    add(&OBDManager::acceleratorPosChanged);
    add(&OBDManager::catalystTempB1S1Changed);
    add(&OBDManager::catalystTempB1S2Changed);
    add(&OBDManager::evapVaporPressureChanged);
    add(&OBDManager::shortFuelTrim2Changed);
    add(&OBDManager::longFuelTrim2Changed);
    add(&OBDManager::o2SensorB1S2Changed);
    add(&OBDManager::o2SensorB2S1Changed);
    add(&OBDManager::o2SensorB2S2Changed);
    add(&OBDManager::distanceSinceCodesCleared);
    add(&OBDManager::warmupsSinceCodesCleared);
    add(&OBDManager::absoluteLoadChanged);
    add(&OBDManager::commandedEGRChanged);
    add(&OBDManager::egrErrorChanged);
    add(&OBDManager::ethanoPercentChanged);
    add(&OBDManager::o2SensorB1S3Changed);
    add(&OBDManager::o2SensorB1S4Changed);
    add(&OBDManager::o2SensorB2S3Changed);
    add(&OBDManager::o2SensorB2S4Changed);
    add(&OBDManager::o2S1WRVoltageChanged);
    add(&OBDManager::o2S2WRVoltageChanged);
    add(&OBDManager::o2S3WRVoltageChanged);
    add(&OBDManager::o2S4WRVoltageChanged);
    add(&OBDManager::o2S5WRVoltageChanged);
    add(&OBDManager::o2S6WRVoltageChanged);
    add(&OBDManager::o2S7WRVoltageChanged);
    add(&OBDManager::o2S8WRVoltageChanged);
    add(&OBDManager::o2S1WRCurrentChanged);
    add(&OBDManager::o2S2WRCurrentChanged);
    add(&OBDManager::o2S3WRCurrentChanged);
    add(&OBDManager::o2S4WRCurrentChanged);
    add(&OBDManager::o2S5WRCurrentChanged);
    add(&OBDManager::o2S6WRCurrentChanged);
    add(&OBDManager::o2S7WRCurrentChanged);
    add(&OBDManager::o2S8WRCurrentChanged);
    add(&OBDManager::catalystTempB2S1Changed);
    add(&OBDManager::catalystTempB2S2Changed);
    add(&OBDManager::throttlePosCChanged);
    add(&OBDManager::acceleratorPosEChanged);
    add(&OBDManager::acceleratorPosFChanged);
    add(&OBDManager::throttleActuatorChanged);
    add(&OBDManager::evaporativePurgeChanged);
    add(&OBDManager::fuelRailPressureAbsChanged);
    add(&OBDManager::fuelInjectTimingChanged);
    add(&OBDManager::fuelRateChanged);
    add(&OBDManager::runTimeMILChanged);
    add(&OBDManager::timeSinceDTCClearedChanged);
    add(&OBDManager::maxMAFChanged);
    add(&OBDManager::fuelTypeChanged);
    add(&OBDManager::evapVaporPressureAbsChanged);
    add(&OBDManager::evapVaporPressureAltChanged);
    add(&OBDManager::shortO2TrimB1Changed);
    add(&OBDManager::longO2TrimB1Changed);
    add(&OBDManager::shortO2TrimB2Changed);
    add(&OBDManager::longO2TrimB2Changed);
    add(&OBDManager::relativeAccelPosChanged);
    add(&OBDManager::hybridBatteryRemainingChanged);
    add(&OBDManager::elmVoltageChanged);
}

void OBDManager::emitParameterSignal(const QString &signalName, float value)
{
    auto it = m_signalDispatch.constFind(signalName);
    if (it != m_signalDispatch.constEnd()) {
        // IGNITION_TIMING is TIMING_ADVANCE under its other name: one
        // reading, both values and both signals
        const bool timing = it->emitter == &OBDManager::timingAdvanceChanged;
        if (it->store)
            this->*(it->store) = value;
        if (timing)
            m_ignitionTiming = value;
        emit (this->*(it->emitter))(value);
        if (timing)
            emit ignitionTimingChanged(value);
    }
    const auto id = m_signalToParamId.constFind(signalName);
    if (id != m_signalToParamId.constEnd())
        publishSample(id.value(), value);
}

bool OBDManager::invokeWorker(const char *method)
{
    if (!m_worker) {
        qDebug() << "[OBD]" << method << "-- no worker thread on this transport, ignoring";
        return false;
    }
    QMetaObject::invokeMethod(m_worker, method, Qt::QueuedConnection);
    return true;
}

// ---------------------------------------------------------------------------
// Connection status
// ---------------------------------------------------------------------------

bool OBDManager::isConnected() const
{
    return m_connected;
}

void OBDManager::setConnectionStatus(const QString &status)
{
    m_connectionStatus = status;
    emit connectionStatusChanged(status);
}

void OBDManager::setConnectionDetail(const QString &detail)
{
    m_connectionDetail = detail;
    emit connectionStatusDetailChanged(detail);
}

void OBDManager::setConnectionProgress(int progress)
{
    m_connectionProgress = progress;
    emit connectionProgressChanged(progress);
}

QString OBDManager::get_connection_status() const
{
    // Same vocabulary as connectionStatusChanged:
    // "Disconnected", "Connecting", "Connected", "No Vehicle", ... so the
    // QML status colour map keys on one set of strings.
    return m_connectionStatus;
}

QString OBDManager::getConnectionStatus() const
{
    return m_connectionStatus;
}

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

void OBDManager::clearConnectionLog()
{
    if (m_connectionLog.isEmpty())
        return;
    m_connectionLog.clear();
    emit connectionLogChanged();
}

OBDManager::ConnectionState OBDManager::stateFor(const QString &status)
{
    // Every string connectionStatusChanged carries (tst_obdmanager lists them)
    static const QHash<QString, ConnectionState> table{
        {QStringLiteral("Disconnected"), ConnectionState::Disconnected},
        {QStringLiteral("Connecting"), ConnectionState::Connecting},
        {QStringLiteral("Reconnecting"), ConnectionState::Connecting},
        {QStringLiteral("Stale Connection"), ConnectionState::Connecting},
        {QStringLiteral("Connecting BLE..."), ConnectionState::Connecting},
        {QStringLiteral("Discovering services..."), ConnectionState::Connecting},
        {QStringLiteral("Initializing ELM327..."), ConnectionState::Connecting},
        {QStringLiteral("Querying PIDs..."), ConnectionState::Connecting},
        {QStringLiteral("Connected"), ConnectionState::Connected},
        {QStringLiteral("No Vehicle"), ConnectionState::NoVehicle},
        {QStringLiteral("Device Not Found"), ConnectionState::Failed},
        {QStringLiteral("Connection Failed"), ConnectionState::Failed},
        {QStringLiteral("Error"), ConnectionState::Failed},
        {QStringLiteral("No MAC Address"), ConnectionState::Failed},
        {QStringLiteral("BLE connect failed"), ConnectionState::Failed},
        {QStringLiteral("BLE error"), ConnectionState::Failed},
    };
    return table.value(status, status.isEmpty() ? ConnectionState::Disconnected : ConnectionState::Failed);
}
