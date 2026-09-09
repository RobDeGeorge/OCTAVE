#ifndef OBDMANAGER_H
#define OBDMANAGER_H

#include <QObject>
#include <QMutex>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstdint>

#include "elm327protocol.h"
#include "settingsmanager.h"

// Android-only OBD: we deliberately bypass Qt's QLowEnergyController stack
// (Qt 6.7's QBluetoothPermission aggregator is broken on Android 16/API 36
// and refuses to initialize the BLE adapter even with all underlying perms
// granted). All BLE work runs through OctaveOBDBridge.java via JNI instead.

class OBDConnectionWorker;

// ===========================================================================
// OBDManager -- desktop (USB/serial) OBD-II manager using QSerialPort.
//
// Port of backend/obd_manager.py.
// Signal names match EXACTLY what OBDParameterModel.qml expects.
// ===========================================================================
class OBDManager : public QObject
{
    Q_OBJECT

    // Expose connection state as QML-readable properties
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionStatusChanged)
    Q_PROPERTY(QString connectionStatus READ getConnectionStatus NOTIFY connectionStatusChanged)
    // Last detail message / progress percent, so the OBD settings connection
    // card can bind declaratively instead of tracking the signals by hand.
    Q_PROPERTY(QString connectionDetail READ connectionDetail NOTIFY connectionStatusDetailChanged)
    Q_PROPERTY(int connectionProgress READ connectionProgress NOTIFY connectionProgressChanged)

    // Unified adapter list — list of {name, identifier, kind} on every OS.
    // QML binds to this directly so the OBD settings page renders identically
    // on Android / Linux / Pi / Windows / macOS.
    Q_PROPERTY(QVariantList availableAdapters READ availableAdapters NOTIFY availableAdaptersChanged)

signals:
    // ======================================================================
    // OBD parameter signals -- Original 18
    // ======================================================================
    void coolantTempChanged(float value);
    void voltageChanged(float value);
    void engineLoadChanged(float value);
    void throttlePositionChanged(float value);
    void intakeAirTempChanged(float value);
    void timingAdvanceChanged(float value);
    void massAirFlowChanged(float value);
    void speedMPHChanged(float value);
    void rpmChanged(float value);
    void airFuelRatioChanged(float value);
    void fuelLevelChanged(float value);
    void intakeManifoldPressureChanged(float value);
    void shortTermFuelTrimChanged(float value);
    void longTermFuelTrimChanged(float value);
    void oxygenSensorVoltageChanged(float value);
    void fuelPressureChanged(float value);
    void engineOilTempChanged(float value);
    void ignitionTimingChanged(float value);

    // ======================================================================
    // OBD parameter signals -- Batch 1 (additional)
    // ======================================================================
    void runTimeChanged(float value);
    void distanceWithMILChanged(float value);
    void fuelRailPressureChanged(float value);
    void fuelRailPressureDirectChanged(float value);
    void barometricPressureChanged(float value);
    void ambientAirTempChanged(float value);
    void relativeThrottlePosChanged(float value);
    void absoluteThrottlePosBChanged(float value);
    void acceleratorPosChanged(float value);
    void catalystTempB1S1Changed(float value);
    void catalystTempB1S2Changed(float value);
    void evapVaporPressureChanged(float value);
    void shortFuelTrim2Changed(float value);
    void longFuelTrim2Changed(float value);
    void o2SensorB1S2Changed(float value);
    void o2SensorB2S1Changed(float value);
    void o2SensorB2S2Changed(float value);
    void distanceSinceCodesCleared(float value);
    void warmupsSinceCodesCleared(float value);
    void absoluteLoadChanged(float value);
    void commandedEGRChanged(float value);
    void egrErrorChanged(float value);
    void ethanoPercentChanged(float value);

    // ======================================================================
    // OBD parameter signals -- Batch 2 (remaining PIDs)
    // ======================================================================
    // Additional O2 sensors
    void o2SensorB1S3Changed(float value);
    void o2SensorB1S4Changed(float value);
    void o2SensorB2S3Changed(float value);
    void o2SensorB2S4Changed(float value);
    // Wide-range O2 sensors voltage
    void o2S1WRVoltageChanged(float value);
    void o2S2WRVoltageChanged(float value);
    void o2S3WRVoltageChanged(float value);
    void o2S4WRVoltageChanged(float value);
    void o2S5WRVoltageChanged(float value);
    void o2S6WRVoltageChanged(float value);
    void o2S7WRVoltageChanged(float value);
    void o2S8WRVoltageChanged(float value);
    // Wide-range O2 sensors current
    void o2S1WRCurrentChanged(float value);
    void o2S2WRCurrentChanged(float value);
    void o2S3WRCurrentChanged(float value);
    void o2S4WRCurrentChanged(float value);
    void o2S5WRCurrentChanged(float value);
    void o2S6WRCurrentChanged(float value);
    void o2S7WRCurrentChanged(float value);
    void o2S8WRCurrentChanged(float value);
    // Bank 2 catalyst temps
    void catalystTempB2S1Changed(float value);
    void catalystTempB2S2Changed(float value);
    // Additional throttle/accelerator
    void throttlePosCChanged(float value);
    void acceleratorPosEChanged(float value);
    void acceleratorPosFChanged(float value);
    void throttleActuatorChanged(float value);
    // Fuel system
    void evaporativePurgeChanged(float value);
    void fuelRailPressureAbsChanged(float value);
    void fuelInjectTimingChanged(float value);
    void fuelRateChanged(float value);
    // Time-based
    void runTimeMILChanged(float value);
    void timeSinceDTCClearedChanged(float value);
    // Other
    void maxMAFChanged(float value);
    void fuelTypeChanged(float value);
    void evapVaporPressureAbsChanged(float value);
    void evapVaporPressureAltChanged(float value);
    void shortO2TrimB1Changed(float value);
    void longO2TrimB1Changed(float value);
    void shortO2TrimB2Changed(float value);
    void longO2TrimB2Changed(float value);
    void relativeAccelPosChanged(float value);
    void hybridBatteryRemainingChanged(float value);
    void elmVoltageChanged(float value);

    // ======================================================================
    // Connection status signals
    // ======================================================================
    void connectionStatusChanged(const QString &status);
    void connectionStatusDetailChanged(const QString &detail);
    void connectionProgressChanged(int progress);
    void devicePresenceChanged(bool present);
    void availablePortsChanged(const QVariantList &ports);
    // Emitted alongside availablePortsChanged but with the richer
    // {name, identifier, kind} shape the unified QML binds to.
    void availableAdaptersChanged(const QVariantList &adapters);
    void supportedCommandsChanged(const QVariantList &commands);
    void scanProgressChanged(int progress, const QString &message);
    void scanCompleteChanged(const QVariantList &supportedParams);
    void scanOutputChanged(const QString &output);

    // ======================================================================
    // Diagnostic signals
    // ======================================================================
    void dtcCodesChanged(const QVariantList &codes);
    void dtcCountChanged(int count);
    void milStatusChanged(bool status);
    void dtcClearResult(bool success, const QString &message);
    void freezeFrameChanged(const QVariantList &data);

    // Live log line for the QML OBD page's connection log/terminal element.
    // One signal = one new line appended; QML can keep a rolling buffer.
    void connectionLogLineAppended(const QString &line);

public:
    explicit OBDManager(SettingsManager *settingsManager = nullptr,
                        QObject *parent = nullptr);
    ~OBDManager() override;

    // --- Property getters (Q_INVOKABLE so QML can call them) ---
    Q_INVOKABLE bool isConnected() const;
    QString connectionDetail() const { return m_connectionDetail; }
    int connectionProgress() const { return m_connectionProgress; }
    // snake_case alias — the shared QML (OBDHome.qml etc.) calls is_connected()
    // to match the Python backend's @Slot name. Keep both in lockstep.
    Q_INVOKABLE bool is_connected() const { return isConnected(); }

public slots:
    // ======================================================================
    // Connection management slots
    // ======================================================================
    void connect_obd();       // named connect_obd to avoid clash with QObject::connect
    void reconnect();
    void force_connect();
    void close();
    void reset_connection();

    QString get_connection_status() const;
    QString getConnectionStatus() const; // alias for Q_PROPERTY

    // ======================================================================
    // Device discovery
    // ======================================================================
    void refresh_ports();
    QVariantList get_available_ports();

    // ======================================================================
    // Unified adapter API — same shape on every OS. The QML OBD settings
    // page calls these instead of branching on isAndroid / platformHint.
    // ======================================================================
    // Alias of refresh_ports() so QML reads consistently as `obdManager.scan()`.
    void scan();
    // Rich list of {name, identifier, kind} maps. `kind` is one of
    // "ble", "serial-rfcomm", "serial-usb", "com", "tty", "serial".
    QVariantList availableAdapters() const;
    // One-shot connect: normalises the identifier (MAC vs /dev path vs COM),
    // saves it to settings, runs rfcomm bind on Linux when given a MAC, and
    // kicks off force_connect(). The single entry point QML uses for both
    // adapter-row taps and manual entry "Go". snake_case to match Python.
    void connect_to_adapter(const QString &identifier);
    // Returns "windows" | "macos" | "linux" | "android" | "ios". QML uses
    // this only for the per-OS hint string under the entry field — never
    // for structural branching.
    QString platform_hint() const;

    // ======================================================================
    // Vehicle scan
    // ======================================================================
    void scan_vehicle();
    bool is_scanning() const;
    QStringList get_all_parameter_names() const;
    QVariantList get_supported_commands() const;
    void enable_scanned_parameters(const QVariantList &paramNames);
    void enable_all_supported();

    // ======================================================================
    // Diagnostic mode
    // ======================================================================
    void enter_diagnostic_mode();
    void exit_diagnostic_mode();
    bool is_diagnostic_mode() const;

    // ======================================================================
    // DTC reading / clearing
    // ======================================================================
    void read_dtc();
    void read_current_dtc();
    void read_status();
    void clear_dtc();
    void read_freeze_frame();

    // ======================================================================
    // Diagnostic data getters
    // ======================================================================
    QVariantList get_dtc_codes() const;
    int get_dtc_count() const;
    bool get_mil_status() const;
    QVariantList get_freeze_frame() const;

    // ======================================================================
    // Parameter value getters (Original 18)
    // ======================================================================
    float coolantTemp() const;
    float voltage() const;
    float engineLoad() const;
    float throttlePosition() const;
    float intakeTemp() const;
    float timingAdvance() const;
    float massAirFlow() const;
    float speedMPH() const;
    float rpm() const;
    float airFuelRatio() const;
    float fuelLevel() const;
    float intakeManifoldPressure() const;
    float shortTermFuelTrim() const;
    float longTermFuelTrim() const;
    float oxygenSensorVoltage() const;
    float fuelPressure() const;
    float engineOilTemp() const;
    float ignitionTiming() const;

    // ======================================================================
    // Misc slots called from QML
    // ======================================================================
    void refresh_values();
    void open_bluetooth_settings();
    void set_target_address(const QString &address);
    void set_auto_reconnect(bool enabled);
    void set_connection_timeout(int timeoutSeconds);
    bool check_device_presence();

private slots:
    // Worker thread callbacks
    void onWorkerInitComplete(bool success, const QString &message);
    void onWorkerDataReceived(const QString &signalName, float value);
    void onWorkerDtcResult(const QStringList &codes);
    void onWorkerDtcCleared(bool success, const QString &message);
    void onWorkerMilStatus(bool mil, int dtcCount);
    void onWorkerFreezeFrame(const QStringList &codes);
    void onWorkerConnectionLost(const QString &reason);
    void onWorkerScanProgress(int progress, const QString &message);
    void onWorkerScanComplete(const QStringList &supported);
    void onWorkerScanOutput(const QString &line);

    // Internal timers
    void onStartupTimer();
    void onDataWatchdog();
    void onDeviceScanTimer();
    void onPortChangeDebounce();
    void onSettingsPortChanged();
    void onSettingsParametersChanged();

    // Post-diagnostic reconnect
    void delayedReconnectAfterDiagnostic();

#ifdef Q_OS_ANDROID
    // Android BLE state-machine slots. Transport runs through
    // OctaveOBDBridge.java (native BluetoothGatt); we poll for state
    // transitions and notification bytes here on a Qt timer.
    void onAndroidBlePoll();
    void onAndroidInitStep();
    void onAndroidPollWatchdog();
#endif

private:
    // Platform detection
    enum class Platform { Windows, macOS, Linux, Android };
    Platform detectPlatform() const;

    // Configuration helpers
    QString getConfiguredPort() const;
    bool checkPortExists(const QString &port) const;
    int getMaxReconnectAttempts() const;
    bool isAutoReconnectEnabled() const;

    // Connection lifecycle
    void startConnection();
    void cleanupConnection();
    void cleanupWorkerThread();
    void scheduleAutoReconnect();

    // Port scanning
    void scanForDevices();

    // Unified-adapter helpers — classify an identifier (MAC/COM/dev path)
    // into the {name, kind} the QML chip / row renders.
    QString kindForIdentifier(const QString &id) const;
    QString displayNameForIdentifier(const QString &id) const;
    QVariantList buildAdapterList(const QStringList &ports) const;

#ifdef Q_OS_LINUX
    // Linux/Pi only: when the user gives us a MAC, run `rfcomm bind 0 <mac> 1`
    // so the kernel hands us a /dev/rfcommN node we can open like any serial
    // port. Returns the resulting device path on success, empty string on
    // failure (and emits the failure reason to the connection log).
    QString ensureRfcommBound(const QString &mac);
#endif

    // Build the signal-name -> emit-lambda dispatch table
    void buildSignalDispatch();

    // Emit a parameter signal by name
    void emitParameterSignal(const QString &signalName, float value);

    // ======================================================================
    // Member variables
    // ======================================================================
    SettingsManager *m_settingsManager = nullptr;

    // Serial port (owned by worker thread when polling)
    QSerialPort *m_serialPort = nullptr;

    // Worker thread
    QThread *m_workerThread = nullptr;
    OBDConnectionWorker *m_worker = nullptr;

    // Connection state
    mutable QMutex m_lock;
    bool m_connected = false;
    bool m_isConnecting = false;
    int m_connectionAttempts = 0;
    QString m_connectionStatus;
    QString m_connectionDetail;
    int m_connectionProgress = 0;
    int m_connectionTimeout = 5;  // seconds
    bool m_forceStopReconnect = false;

    // Diagnostic mode
    bool m_diagnosticMode = false;
    QMutex m_diagnosticModeLock;
    bool m_diagnosticModeTransitioning = false;
    bool m_reconnectingAfterDiagnostic = false;

    // Scanning
    bool m_isScanning = false;
    QStringList m_supportedCommands;

    // Diagnostic data
    QVariantList m_dtcCodes;
    int m_dtcCount = 0;
    bool m_milStatus = false;
    QVariantList m_freezeFrameDtcs;

    // Parameter values -- Original 18
    float m_coolantTemp = 0.0f;
    float m_voltage = 0.0f;
    float m_engineLoad = 0.0f;
    float m_throttlePos = 0.0f;
    float m_intakeTemp = 0.0f;
    float m_timingAdvance = 0.0f;
    float m_massAirflow = 0.0f;
    float m_speedMPH = 0.0f;
    float m_rpm = 0.0f;
    float m_airFuelRatio = 0.0f;
    float m_fuelLevel = 0.0f;
    float m_intakePressure = 0.0f;
    float m_shortTermFuelTrim = 0.0f;
    float m_longTermFuelTrim = 0.0f;
    float m_o2SensorVoltage = 0.0f;
    float m_fuelPressure = 0.0f;
    float m_oilTemp = 0.0f;
    float m_ignitionTiming = 0.0f;

    // Parameter values -- Batch 1
    float m_runTime = 0.0f;
    float m_distanceWithMIL = 0.0f;
    float m_fuelRailPressure = 0.0f;
    float m_fuelRailPressureDirect = 0.0f;
    float m_barometricPressure = 0.0f;
    float m_ambientAirTemp = 0.0f;
    float m_relativeThrottlePos = 0.0f;
    float m_absoluteThrottlePosB = 0.0f;
    float m_acceleratorPos = 0.0f;
    float m_catalystTempB1S1 = 0.0f;
    float m_catalystTempB1S2 = 0.0f;
    float m_evapVaporPressure = 0.0f;
    float m_shortFuelTrim2 = 0.0f;
    float m_longFuelTrim2 = 0.0f;
    float m_o2SensorB1S2 = 0.0f;
    float m_o2SensorB2S1 = 0.0f;
    float m_o2SensorB2S2 = 0.0f;
    float m_distanceSinceCodesCleared = 0.0f;
    float m_warmupsSinceCodesCleared = 0.0f;
    float m_absoluteLoad = 0.0f;
    float m_commandedEGR = 0.0f;
    float m_egrError = 0.0f;
    float m_ethanolPercent = 0.0f;

    // Parameter values -- Batch 2
    float m_o2SensorB1S3 = 0.0f;
    float m_o2SensorB1S4 = 0.0f;
    float m_o2SensorB2S3 = 0.0f;
    float m_o2SensorB2S4 = 0.0f;
    float m_o2S1WRVoltage = 0.0f;
    float m_o2S2WRVoltage = 0.0f;
    float m_o2S3WRVoltage = 0.0f;
    float m_o2S4WRVoltage = 0.0f;
    float m_o2S5WRVoltage = 0.0f;
    float m_o2S6WRVoltage = 0.0f;
    float m_o2S7WRVoltage = 0.0f;
    float m_o2S8WRVoltage = 0.0f;
    float m_o2S1WRCurrent = 0.0f;
    float m_o2S2WRCurrent = 0.0f;
    float m_o2S3WRCurrent = 0.0f;
    float m_o2S4WRCurrent = 0.0f;
    float m_o2S5WRCurrent = 0.0f;
    float m_o2S6WRCurrent = 0.0f;
    float m_o2S7WRCurrent = 0.0f;
    float m_o2S8WRCurrent = 0.0f;
    float m_catalystTempB2S1 = 0.0f;
    float m_catalystTempB2S2 = 0.0f;
    float m_throttlePosC = 0.0f;
    float m_acceleratorPosE = 0.0f;
    float m_acceleratorPosF = 0.0f;
    float m_throttleActuator = 0.0f;
    float m_evaporativePurge = 0.0f;
    float m_fuelRailPressureAbs = 0.0f;
    float m_fuelInjectTiming = 0.0f;
    float m_fuelRate = 0.0f;
    float m_runTimeMIL = 0.0f;
    float m_timeSinceDTCCleared = 0.0f;
    float m_maxMAF = 0.0f;
    float m_fuelType = 0.0f;
    float m_evapVaporPressureAbs = 0.0f;
    float m_evapVaporPressureAlt = 0.0f;
    float m_shortO2TrimB1 = 0.0f;
    float m_longO2TrimB1 = 0.0f;
    float m_shortO2TrimB2 = 0.0f;
    float m_longO2TrimB2 = 0.0f;
    float m_relativeAccelPos = 0.0f;
    float m_hybridBatteryRemaining = 0.0f;
    float m_elmVoltage = 0.0f;

    // Device discovery
    QStringList m_availablePorts;
    qint64 m_lastDeviceScan = 0;

    // Signal dispatch table: signalName -> lambda that stores + emits
    QHash<QString, std::function<void(float)>> m_signalDispatch;

    // Timers
    QTimer m_startupTimer;
    QTimer m_dataWatchdogTimer;
    QTimer m_deviceScannerTimer;
    QTimer m_portChangeTimer;

    // Data watchdog
    qint64 m_lastDataReceived = 0;
    static constexpr int DATA_WATCHDOG_TIMEOUT_MS = 30000;
    bool m_hasActiveWatchers = false;

    // Pending port change
    QString m_pendingPort;

#ifdef Q_OS_ANDROID
    // Android-only BLE GATT state -- on Android the worker thread is bypassed
    // entirely; the main thread owns the QLowEnergyController directly.
    void startAndroidConnection();
    void cleanupAndroidConnection();
    void requestAndroidBluetoothPermission(std::function<void(bool)> done);
    QStringList listAndroidPairedDevices();
    // Logs to qDebug + emits to the QML connection-log element.
    void logAndroid(const QString &line);
    void writeAndroidBytes(const QByteArray &data);
    void processAndroidResponse(const QString &response);
    void sendNextAndroidInitCommand();
    void queryAndroidSupportedPids();
    void finalizeAndroidConnection();
    void pollNextAndroidPid();

    // Java-side state we mirror via JNI polling. m_bleLastJavaState tracks
    // the most recent stateCode() value we've seen so we only emit log lines
    // on actual transitions (poll runs every 50ms).
    int m_bleLastJavaState = 0;
    QTimer m_bleNotifyPoller;
    ResponseBuffer m_btResponseBuffer;
    int m_androidInitStep = 0;
    bool m_androidElmInitialized = false;
    QList<PidKey> m_androidEnabledPids;
    int m_androidPollIndex = 0;
    bool m_androidPolling = false;
    int m_androidStaleCount = 0;
    int m_androidReconnectAttempts = 0;
    int m_androidMaxReconnect = 3;
    QSet<int> m_androidSupportedPids;
    QTimer m_androidInitTimer;
    QTimer m_androidPollWatchdog;
    bool m_androidPermissionGranted = false;
#endif
};

// ===========================================================================
// OBDConnectionWorker -- runs on a dedicated QThread, owns the QSerialPort.
//
// Handles ELM327 initialization, PID polling, and diagnostic commands.
// Communicates results back to OBDManager via signals (thread-safe).
// ===========================================================================
class OBDConnectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit OBDConnectionWorker(QObject *parent = nullptr);
    ~OBDConnectionWorker() override;

    void setPort(const QString &portName);
    void setFastMode(bool fast);
    void setTimeout(int seconds);
    void setPidsToWatch(const QList<PidKey> &pids);

signals:
    // Connection
    void initComplete(bool success, const QString &message);
    void connectionLost(const QString &reason);

    // Live data
    void dataReceived(const QString &signalName, float value);

    // Diagnostics
    void dtcResult(const QStringList &codes);
    void dtcCleared(bool success, const QString &message);
    void milStatus(bool mil, int dtcCount);
    void freezeFrame(const QStringList &codes);

    // Scan
    void scanProgress(int progress, const QString &message);
    void scanComplete(const QStringList &supported);
    void scanOutput(const QString &line);

public slots:
    // Called from OBDManager (via queued connection)
    void doConnect();
    void doDisconnect();
    void releasePort();   // close + delete the QSerialPort; nothing may hold it open while idle
    void startPolling();
    void stopPolling();
    void doReadDtc();
    void doReadCurrentDtc();
    void doClearDtc();
    void doReadStatus();
    void doReadFreezeFrame();
    void doScanVehicle();

private slots:
    void onPollTimer();
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    // ELM327 init sequence
    bool sendInitSequence();

    // Send a command and wait for response (blocking, with timeout)
    QString sendCommand(const QByteArray &cmd, int timeoutMs = 2000);

    // Query supported PIDs from vehicle
    QSet<int> querySupportedPids();

    // Serial port (created on worker thread)
    QSerialPort *m_serial = nullptr;
    ResponseBuffer m_responseBuffer;

    // Configuration
    QString m_portName;
    bool m_fastMode = true;
    int m_timeout = 5;

    // PIDs to poll
    QList<PidKey> m_pidsToWatch;
    int m_currentPidIndex = 0;

    // Poll timer
    QTimer m_pollTimer;
    bool m_polling = false;

    // State
    bool m_initialized = false;
    bool m_waitingForResponse = false;
    PidKey m_pendingPid;

    // Supported PIDs (discovered from vehicle)
    QSet<int> m_supportedPids;
};

#endif // OBDMANAGER_H
