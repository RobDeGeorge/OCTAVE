#ifndef OBDMANAGER_H
#define OBDMANAGER_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QMutex>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <optional>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstdint>
#include <functional>

#include "derivedparameters.h"
#include "elm327protocol.h"
#include "obdconnectionworker.h"
#include "settingsmanager.h"

// Android-only OBD: we deliberately bypass Qt's QLowEnergyController stack
// (Qt 6.7's QBluetoothPermission aggregator is broken on Android 16/API 36
// and refuses to initialize the BLE adapter even with all underlying perms
// granted). All BLE work runs through OctaveOBDBridge.java via JNI instead.

class QProcess;

// Where the messages of the listen-only bus monitor go (busmonitor.h).
// Called on the manager's thread.
class BusSink
{
public:
    virtual ~BusSink() = default;
    // `overruns`: times the adapter's buffer filled since the last call
    // (messages were lost each time)
    virtual void busFrames(const QList<BusFrame> &frames, int overruns) = 0;
    // `note` says why monitoring ended when it wasn't asked to
    virtual void busMonitorChanged(bool active, const QString &note) = 0;
};

// ===========================================================================
// OBDManager -- desktop (USB/serial) OBD-II manager using QSerialPort.
//
// Signal names match EXACTLY what OBDParameterModel.qml expects.
// ===========================================================================
class OBDManager : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "vehicle")
    Q_CLASSINFO("octave.scope.read",
                "airFuelRatio availableAdapters coolantTemp engineLoad engineOilTemp fuelLevel fuelPressure "
                "getAllParameterNames getAvailablePorts getConnectionStatus getDtcCodes getDtcCount "
                "getFreezeFrame getMilStatus getSupportedCommands get_all_parameter_names get_available_ports "
                "get_connection_status get_dtc_codes get_dtc_count get_freeze_frame get_mil_status "
                "get_supported_commands ignitionTiming intakeManifoldPressure intakeTemp isConnected "
                "isDiagnosticMode isScanning is_connected is_diagnostic_mode is_scanning longTermFuelTrim "
                "massAirFlow oxygenSensorVoltage platformHint platform_hint rpm setBackgroundDemand "
                "setParameterDemand shortTermFuelTrim speedMPH throttlePosition timingAdvance voltage")
    Q_CLASSINFO("octave.scope.system", "openBluetoothSettings open_bluetooth_settings")
    // Deprecated names (src/service/metadescribe.h): the camelCase one beside each stays
    Q_CLASSINFO("octave.deprecated.connect_obd", "use connectObd; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.force_connect", "use forceConnect; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.reset_connection", "use resetConnection; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_connection_status", "use getConnectionStatus (or the connectionStatus property); goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.refresh_ports", "use refreshPorts; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_available_ports", "use getAvailablePorts; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.connect_to_adapter", "use connectToAdapter; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.platform_hint", "use platformHint; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.scan_vehicle", "use scanVehicle; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_scanning", "use isScanning; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_all_parameter_names", "use getAllParameterNames; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_supported_commands", "use getSupportedCommands; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.enable_scanned_parameters", "use enableScannedParameters; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.enable_all_supported", "use enableAllSupported; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.enter_diagnostic_mode", "use enterDiagnosticMode; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.exit_diagnostic_mode", "use exitDiagnosticMode; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_diagnostic_mode", "use isDiagnosticMode; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.read_dtc", "use readDtc; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.read_current_dtc", "use readCurrentDtc; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.read_status", "use readStatus; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.clear_dtc", "use clearDtc; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.read_freeze_frame", "use readFreezeFrame; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.read_vin", "use readVin; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_dtc_codes", "use getDtcCodes; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_dtc_count", "use getDtcCount; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_mil_status", "use getMilStatus; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_freeze_frame", "use getFreezeFrame; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.refresh_values", "use refreshValues; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.open_bluetooth_settings", "use openBluetoothSettings; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.set_target_address", "use setTargetAddress; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.connect_direct", "use connectDirect; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.set_auto_reconnect", "use setAutoReconnect; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.set_connection_timeout", "use setConnectionTimeout; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.check_device_presence", "use checkDevicePresence; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.is_connected", "use isConnected (or the connected property); goes in protocol 5")

    // Expose connection state as QML-readable properties
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionStatusChanged)
    // The selected adapter is the built-in demo; never persisted as a real port.
    Q_PROPERTY(bool simulated READ isSimulated NOTIFY simulatedChanged)
    Q_PROPERTY(QString connectionStatus READ getConnectionStatus NOTIFY connectionStatusChanged)
    // connectionStatus classified (stateFor): what a frontend colours and
    // animates by; the text stays the label. QML: import OCTAVE.OBD 1.0 for
    // OBDManager.Connecting etc.
    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionStatusChanged)
    // Last detail message / progress percent, so the OBD settings connection
    // card can bind declaratively instead of tracking the signals by hand.
    Q_PROPERTY(QString connectionDetail READ connectionDetail NOTIFY connectionStatusDetailChanged)
    Q_PROPERTY(int connectionProgress READ connectionProgress NOTIFY connectionProgressChanged)
    // The last kConnectionLogLines connection log lines, oldest first (every
    // connectionLogLineAppended since start), so each page shows the same
    // history whenever it was built
    Q_PROPERTY(QStringList connectionLog READ connectionLog NOTIFY connectionLogChanged)

    // Unified adapter list — list of {name, identifier, kind} on every OS.
    // QML binds to this directly so the OBD settings page renders identically
    // on Android / Linux / Pi / Windows / macOS.
    Q_PROPERTY(QVariantList availableAdapters READ availableAdapters NOTIFY availableAdaptersChanged)

    // Vehicle identity from Mode 09 PID 02, read once per connection
    // ("" / 0 when the vehicle does not report a VIN, as many pre-2005 cars don't)
    Q_PROPERTY(QString vin READ vin NOTIFY vinChanged)
    Q_PROPERTY(QString vehicleMake READ vehicleMake NOTIFY vinChanged)
    Q_PROPERTY(int vehicleModelYear READ vehicleModelYear NOTIFY vinChanged)

    // This session's vehicle scan result (OBDParameterModel ids) and whether
    // the scan has finished. Both reset on disconnect -- never loaded from the
    // persisted list, so they always describe the car that is connected now.
    Q_PROPERTY(QStringList vehicleSupportedParameters READ vehicleSupportedParameters
                   NOTIFY vehicleSupportedParametersChanged)
    Q_PROPERTY(bool vehicleScanComplete READ vehicleScanComplete
                   NOTIFY vehicleSupportedParametersChanged)

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
    void simulatedChanged();
    void simulatedAdapterRequested();
    // A live link dropped on its own (adapter unplugged or out of range, port
    // error, BLE drop). Not emitted for disconnect(), port changes,
    // diagnostic-mode switches or shutdown, which tear the worker down first.
    // Main.qml turns it into the "OBD connection lost" notice.
    void connectionLostUnexpectedly(const QString &reason);
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
    void vehicleSupportedParametersChanged();

    // ======================================================================
    // Diagnostic signals
    // ======================================================================
    void dtcCodesChanged(const QVariantList &codes);
    void dtcCountChanged(int count);
    void milStatusChanged(bool status);
    void dtcClearResult(bool success, const QString &message);
    void freezeFrameChanged(const QVariantList &data);

    // Multi-value PIDs (0x61 and up: torque, boost, odometer, ...) arrive
    // through this one signal instead of a signal per parameter; QML's
    // OBDParameterModel routes it into paramValues by id.
    void obdParameterChanged(const QString &paramId, float value);
    // Every decoded value, base or multi-value, keyed by its OBDParameterModel
    // id ("COOLANT_TEMP", "BOOST_PRESSURE_A", ...). One emission per reading,
    // in arrival order; the black-box recorder (TripRecorder) taps this so it
    // never has to know about the 84 per-parameter signals above.
    void parameterSampled(const QString &paramId, float value);
    void vinChanged(const QString &vin);

    // One connection log line (the connection steps, most useful on the
    // Android BLE path); connectionLog keeps the recent ones
    void connectionLogLineAppended(const QString &line);
    void connectionLogChanged();

public:
    // Disconnected: no link, or it went away; Connecting: any step toward a
    // link, reconnects and a stale link about to be retried included;
    // Connected; NoVehicle: the adapter answers but the vehicle doesn't
    // (ignition off); Failed: the attempt ended in an error
    enum class ConnectionState { Disconnected, Connecting, Connected, NoVehicle, Failed };
    Q_ENUM(ConnectionState)
    static ConnectionState stateFor(const QString &status);
    ConnectionState connectionState() const { return stateFor(m_connectionStatus); }

    explicit OBDManager(SettingsManager *settingsManager = nullptr,
                        QObject *parent = nullptr);
    ~OBDManager() override;

    // --- Property getters (Q_INVOKABLE so QML can call them) ---
    Q_INVOKABLE bool isConnected() const;
    QString connectionDetail() const { return m_connectionDetail; }
    QStringList connectionLog() const { return m_connectionLog; }
    static constexpr int kConnectionLogLines = 60;
    Q_INVOKABLE void clearConnectionLog();
    int connectionProgress() const { return m_connectionProgress; }
    // snake_case alias — QML (OBDHome.qml etc.) calls is_connected().
    // Keep both in lockstep.
    Q_INVOKABLE bool is_connected() const { return isConnected(); }
    QString vin() const { return m_vin.vin; }
    QString vehicleMake() const { return m_vin.make; }
    int vehicleModelYear() const { return m_vin.modelYear; }
    QStringList vehicleSupportedParameters() const { return m_vehicleSupportedParameters; }
    bool vehicleScanComplete() const { return m_vehicleScanComplete; }

    // Demand-driven polling: replace `consumer`'s wanted parameter ids
    // ("dashboard", "cards", "home", "editor", ...); an empty list removes the
    // consumer. The poll set is the union of every consumer, filtered to this
    // session's scan result, plus the RPM heartbeat (ELM_VOLTAGE when the car
    // has no RPM). BerryIMU and unknown ids are ignored. Applied live after a
    // 250 ms debounce.
    Q_INVOKABLE void setParameterDemand(const QString &consumer, const QStringList &paramIds);

    // Low-priority demand: ids wanted "eventually", not on screen. They are
    // polled one PID per pass of the foreground list (half the bandwidth when
    // nothing but the heartbeat is in the foreground), so a long background
    // sweep never slows the live gauges. Same consumer / empty-list rules as
    // setParameterDemand(); ids already in the foreground are polled there.
    Q_INVOKABLE void setBackgroundDemand(const QString &consumer, const QStringList &paramIds);

    // The manufacturer-specific readings of the user's custom PID file
    // (custompids.h). Once, before the first connection. Each is polled
    // while its paramId is demanded, whatever the vehicle scan found (the
    // scan can't see them), and counts as supported by the vehicle.
    void setCustomPids(const QList<CustomPidRequest> &pids);

    // What the derived readings (derivedparameters.h: horsepower, fuel
    // economy, range, ...) can't learn from the car: the engine's size in
    // litres and the tank's in US gallons, 0 for not known. Changes which
    // of them this car has (vehicleSupportedParameters) and what is polled
    // for them.
    void setDerivedConfig(double displacementL, double tankGallons);

    // The bus monitor (busmonitor.h): the adapter only listens ("ATMA") and
    // every message it hears goes to the sink. Polling pauses meanwhile and
    // resumes when it stops; any other request (trouble codes, a scan) ends
    // it. `filter` is ELM327Protocol::busFilter() text or empty. False when
    // there is no connection that can do it (none, or the Android BLE path).
    void setBusSink(BusSink *sink) { m_busSink = sink; }
    bool canMonitorBus() const;
    bool startBusMonitor(const QString &filter);
    void stopBusMonitor();

    // The poll list for a demand set (consumer -> parameter ids) and this
    // session's scan result: what buildPidsToWatch() uses. Static so the
    // rules can be tested without a connection.
    // A derived reading is polled as the inputs `derived` gives it.
    static QList<PidKey> pidsToWatch(const QHash<QString, QSet<QString>> &demand,
                                     const QStringList &vehicleSupported,
                                     const DerivedParameters::Config &derived = {});
    // The background poll list for a background demand set: the same rules,
    // minus the heartbeat and minus every PID already in `foreground`.
    static QList<PidKey> backgroundPidsToWatch(const QHash<QString, QSet<QString>> &demand,
                                               const QList<PidKey> &foreground,
                                               const QStringList &vehicleSupported,
                                               const DerivedParameters::Config &derived = {});
    // A scan result plus every catalog alias whose target it contains
    // (TIMING_ADVANCE -> also IGNITION_TIMING); vehicleSupportedParameters
    // is published through this
    static QStringList withAliases(const QStringList &names);

    // Use `port` instead of the saved obdBluetoothPort, without writing it
    // to settings (dev tooling points this at the simulated adapter, see
    // src/dev/). Takes effect on the next connection attempt.
    void setPortOverride(const QString &port);
    bool isSimulated() const;
    static QVariantMap simulatedAdapter();

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
    // adapter-row taps and manual entry "Go".
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
    void read_vin();

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
    // Linux: connect over a direct Bluetooth RFCOMM socket (no rfcomm bind,
    // no /dev node). Saves "rfcomm://<MAC>[/<channel>]" as the OBD port.
    void connect_direct(const QString &mac, int channel = 1);
    void set_auto_reconnect(bool enabled);
    void set_connection_timeout(int timeoutSeconds);
    bool check_device_presence();

    // camelCase names for the snake_case methods above (protocol 3 keeps both;
    // the old names are marked octave.deprecated and go in protocol 5)
    void connectObd() { connect_obd(); }
    void forceConnect() { force_connect(); }
    void resetConnection() { reset_connection(); }
    void refreshPorts() { refresh_ports(); }
    QVariantList getAvailablePorts() { return get_available_ports(); }
    void connectToAdapter(const QString &identifier) { connect_to_adapter(identifier); }
    QString platformHint() const { return platform_hint(); }
    void scanVehicle() { scan_vehicle(); }
    bool isScanning() const { return is_scanning(); }
    QStringList getAllParameterNames() const { return get_all_parameter_names(); }
    QVariantList getSupportedCommands() const { return get_supported_commands(); }
    void enableScannedParameters(const QVariantList &paramNames) { enable_scanned_parameters(paramNames); }
    void enableAllSupported() { enable_all_supported(); }
    void enterDiagnosticMode() { enter_diagnostic_mode(); }
    void exitDiagnosticMode() { exit_diagnostic_mode(); }
    bool isDiagnosticMode() const { return is_diagnostic_mode(); }
    void readDtc() { read_dtc(); }
    void readCurrentDtc() { read_current_dtc(); }
    void readStatus() { read_status(); }
    void clearDtc() { clear_dtc(); }
    void readFreezeFrame() { read_freeze_frame(); }
    void readVin() { read_vin(); }
    QVariantList getDtcCodes() const { return get_dtc_codes(); }
    int getDtcCount() const { return get_dtc_count(); }
    bool getMilStatus() const { return get_mil_status(); }
    QVariantList getFreezeFrame() const { return get_freeze_frame(); }
    void refreshValues() { refresh_values(); }
    void openBluetoothSettings() { open_bluetooth_settings(); }
    void setTargetAddress(const QString &address) { set_target_address(address); }
    void connectDirect(const QString &mac, int channel = 1) { connect_direct(mac, channel); }
    void setAutoReconnect(bool enabled) { set_auto_reconnect(enabled); }
    void setConnectionTimeout(int timeoutSeconds) { set_connection_timeout(timeoutSeconds); }
    bool checkDevicePresence() { return check_device_presence(); }

private slots:
    // Worker thread callbacks
    void onWorkerInitComplete(bool success, const QString &message);
    void onWorkerDataReceived(const QString &signalName, float value);
    void onWorkerExtendedData(const QString &paramId, float value);
    void onWorkerVin(const QString &vin);
    void onWorkerDtcResult(const QStringList &codes);
    void onWorkerDtcCleared(bool success, const QString &message);
    void onWorkerMilStatus(bool mil, int dtcCount);
    void onWorkerFreezeFrame(const QStringList &codes, const QVariantMap &values);
    void onWorkerConnectionLost(const QString &reason);
    void onWorkerScanProgress(int progress, const QString &message);
    void onWorkerScanComplete(const QStringList &supported);
    void onWorkerScanOutput(const QString &line);
    void onWorkerBusFrames(const QList<BusFrame> &frames, int overruns);
    void onWorkerBusMonitor(bool active, const QString &note);

    // Internal timers
    void onStartupTimer();
    void onDataWatchdog();
    void onDeviceScanTimer();
    void onPortChangeDebounce();
    void onSettingsPortChanged();
    // Swap the poll list in place (no reconnect) after a demand or scan change.
    void applyPollList();

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
    // Store, then emit: what QML reads on the change must be the new value
    void setConnectionStatus(const QString &status);
    void setConnectionDetail(const QString &detail);
    void setConnectionProgress(int progress);

    // Port scanning
    // `passiveRetry`: the scan timer's tick, which may also retry an adapter
    // that is there and not answering; a ports query passes false
    void scanForDevices(bool passiveRetry = false);

    // Unified-adapter helpers — classify an identifier (MAC/COM/dev path)
    // into the {name, kind} the QML chip / row renders.
    QString kindForIdentifier(const QString &id) const;
    QString displayNameForIdentifier(const QString &id) const;
    QVariantList buildAdapterList(const QStringList &ports) const;

#ifdef Q_OS_LINUX
    // Linux/Pi only: when the user gives us a MAC, run `rfcomm bind 0 <mac> 1`
    // so the kernel hands us a /dev/rfcommN node we can open like any serial
    // port. Runs asynchronously (the bind can take seconds) and ends in
    // finishRfcommBind() with the device path, or an empty string on failure
    // (the reason goes to the connection log).
    void startRfcommBind(const QString &mac);
    void waitForRfcommNode(quint64 generation, int attemptsLeft);
    void finishRfcommBind(quint64 generation, const QString &bound);
    void releaseRfcommProc(QProcess *proc);
#endif

    // Build the signal-name -> emit-lambda dispatch table
    void buildSignalDispatch();

    // The poll list: the union of every consumer's demand mapped to PID keys
    // (a multi-value PID when any of its signals is wanted), filtered to this
    // session's scan result, plus the heartbeat. Used on connect and live
    // from applyPollList().
    QList<PidKey> buildPidsToWatch() const;
    QList<PidKey> buildBackgroundPids(const QList<PidKey> &foreground) const;
    // {kCustomPidMode, index} of every custom PID `demand` asks for
    QList<PidKey> customKeys(const QHash<QString, QSet<QString>> &demand) const;
    QList<CustomPidRequest> m_customPids;
    BusSink *m_busSink = nullptr;
    bool m_busMonitoring = false;   // the data watchdog is off meanwhile

    QHash<QString, QSet<QString>> m_paramDemand;
    QHash<QString, QSet<QString>> m_backgroundDemand;
    // signalName ("coolantTempChanged") -> parameter id ("COOLANT_TEMP"),
    // from the PID table; feeds parameterSampled()
    QHash<QString, QString> m_signalToParamId;
    QTimer m_demandDebounce;
    QStringList m_vehicleSupportedParameters;
    bool m_vehicleScanComplete = false;
    void setVehicleSupport(const QStringList &names, bool complete);
    // The derived readings: the last scan as the car gave it (they are
    // added to it again when the engine or tank size changes), and every
    // reading on its way out through parameterSampled passes publishSample()
    DerivedParameters m_derived;
    DerivedParameters::Config m_derivedConfig;
    QStringList m_scannedParameters;
    void publishSample(const QString &paramId, float value);

    // Queue a worker slot by name; logs and returns false when there is no
    // worker (Android drives the adapter from this thread instead).
    bool invokeWorker(const char *method);

    // Emit a parameter signal by name
    void emitParameterSignal(const QString &signalName, float value);

    // ======================================================================
    // Member variables
    // ======================================================================
    SettingsManager *m_settingsManager = nullptr;

    // Bumped by every connect_to_adapter(); an rfcomm bind still running for
    // an older request is ignored when it finishes.
    quint64 m_rfcommBindGeneration = 0;
    QProcess *m_rfcommBindProc = nullptr;  // in-flight `rfcomm bind` (Linux)

    // Worker thread
    QThread *m_workerThread = nullptr;
    OBDConnectionWorker *m_worker = nullptr;

    // Connection state. Everything here is touched on the manager's thread
    // only: the worker reports back through queued signals.
    bool m_connected = false;
    bool m_isConnecting = false;
    int m_connectionAttempts = 0;
    QString m_connectionStatus = QStringLiteral("Disconnected");  // last connectionStatusChanged value
    QString m_connectionDetail;
    QStringList m_connectionLog;
    int m_connectionProgress = 0;
    int m_connectionTimeout = 5;  // seconds
    bool m_forceStopReconnect = false;
    QString m_activePort;   // port of the attempt in flight / the live connection

    // Diagnostic mode
    bool m_diagnosticMode = false;
    bool m_reconnectingAfterDiagnostic = false;

    // Scanning
    bool m_isScanning = false;
    QStringList m_supportedCommands;

    // Diagnostic data
    QVariantList m_dtcCodes;
    VinInfo m_vin;
    void setVin(const QString &vin);
    int m_dtcCount = 0;
    bool m_milStatus = false;
    QVariantList m_freezeFrameDtcs;

    // The last value of the original 18 parameters, for their getters
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

    // Device discovery
    QStringList m_availablePorts;

    // Signal dispatch table: signalName -> the signal to emit, and where its
    // last value is kept when a getter reads it back
    struct ParamSignal {
        void (OBDManager::*emitter)(float);
        float OBDManager::*store;
    };
    QHash<QString, ParamSignal> m_signalDispatch;

    // Timers
    QTimer m_startupTimer;
    QTimer m_dataWatchdogTimer;
    // The one pending "try again later" (auto-reconnect, post-diagnostic,
    // stale link). A member, so a connection made meanwhile cancels it: as
    // QTimer::singleShot calls these fired into a live link and rebuilt it.
    QTimer m_retryTimer;
    // The last attempt reached the adapter and no vehicle answered: the
    // passive scan keeps trying, as the ignition is probably just off
    bool m_waitingForVehicle = false;
    QTimer m_deviceScannerTimer;
    QTimer m_portChangeTimer;

    // Data watchdog
    qint64 m_lastDataReceived = 0;
    static constexpr int DATA_WATCHDOG_TIMEOUT_MS = 30000;
    bool m_hasActiveWatchers = false;

    // Pending port change
    QString m_pendingPort;
    QString m_portOverride;   // see setPortOverride()

#ifdef Q_OS_ANDROID
    // Android-only BLE state -- on Android the worker thread is bypassed
    // entirely; the main thread drives OctaveOBDBridge.java (native
    // BluetoothGatt) over JNI and polls it on a timer.
    // Taken around m_isConnecting by this path alone, which also runs on the
    // manager's thread; it goes when the path folds into the worker
    QMutex m_lock;
    void startAndroidConnection();
    void cleanupAndroidConnection();
    // Bonded devices from BluetoothAdapter.getBondedDevices() via the Java
    // bridge; empty when the runtime Bluetooth permission is missing (the
    // bridge requests it on the first Connect tap).
    QStringList listAndroidPairedDevices();
    // Logs to qDebug + emits to the QML connection-log element.
    void logAndroid(const QString &line);
    void writeAndroidBytes(const QByteArray &data);
    void processAndroidResponse(const QString &response);
    void sendNextAndroidInitCommand();
    void queryAndroidSupportedPids();
    void finalizeAndroidConnection();
    // buildPidsToWatch() filtered by the vehicle's supported bitmap; called
    // on connect and live from applyPollList().
    void rebuildAndroidPollList();
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
    // Background sweep (see setBackgroundDemand): one of these after each
    // full pass of m_androidEnabledPids
    QList<PidKey> m_androidBackgroundPids;
    int m_androidBackgroundIndex = 0;
    bool m_androidBackgroundNext = false;
    bool m_androidPolling = false;
    int m_androidStaleCount = 0;
    int m_androidReconnectAttempts = 0;
    int m_androidMaxReconnect = 3;
    QSet<int> m_androidSupportedPids;
    QTimer m_androidInitTimer;
    QTimer m_androidPollWatchdog;
    // True between writing "ATRV" and its (text) reply.
    bool m_androidAwaitingElmVoltage = false;
    bool m_androidVinRequested = false;   // next poll slot sends 0902
    bool m_androidAwaitingVin = false;
    // MAC -> friendly name from the last bonded-device listing.
    QHash<QString, QString> m_androidDeviceNames;
#endif
};

#endif // OBDMANAGER_H
