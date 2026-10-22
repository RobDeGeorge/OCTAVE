pragma Singleton
import QtQuick 2.15

QtObject {
    id: root

    // ── All parameters with display metadata ─────────────────────────
    // From the C++ ParameterCatalog (src/managers/parametercatalog.h), the
    // one table for every frontend: {id, title, shortTitle, unit, min, max,
    // kind, decimals, source ("obd" | "imu"), aliasOf}. `kind` declares
    // widget compatibility:
    //   "percentage"    — 0–100 % unidirectional
    //   "temperature"   — °C temperature (may go negative, but treated by theme)
    //   "voltage"       — volts
    //   "pressure"      — kPa or Pa
    //   "bidirectional" — signed value that straddles zero (trim, timing, error)
    //   "numeric"       — everything else (speed, RPM, distance, time, flow…)
    // Widgets declare `octaveSupportedKinds` as an array (e.g. ["bidirectional"]
    // for LinearGauge, ["*"] for generic numeric widgets). The PID picker in the
    // dashboard editor filters based on this.
    readonly property var allParameters:
        (typeof parameterCatalog !== "undefined" && parameterCatalog) ? parameterCatalog.parameters : []

    // id -> parameter, for the per-id helpers below
    readonly property var _byId: {
        var m = {};
        for (var i = 0; i < allParameters.length; i++)
            m[allParameters[i].id] = allParameters[i];
        return m;
    }

    // ── Original 18 parameter IDs (default-enabled) ──────────────────
    readonly property var originalParameters: [
        "SPEED", "RPM", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE",
        "ENGINE_LOAD", "THROTTLE_POS", "INTAKE_TEMP", "TIMING_ADVANCE",
        "MAF", "COMMANDED_EQUIV_RATIO", "FUEL_LEVEL", "INTAKE_PRESSURE",
        "SHORT_FUEL_TRIM_1", "LONG_FUEL_TRIM_1", "O2_B1S1", "FUEL_PRESSURE",
        "OIL_TEMP", "IGNITION_TIMING"
    ]

    // ── parameterInfo map (keyed by ID) for HomeOBDView compatibility ─
    // Home cards show the short title where there is one ("SPD", "CLT")
    property var parameterInfo: {
        var info = {};
        for (var i = 0; i < allParameters.length; i++) {
            var p = allParameters[i];
            info[p.id] = {
                title: p.shortTitle || p.title,
                unit: p.unit,
                minValue: p.min,
                maxValue: p.max
            };
        }
        return info;
    }

    // ── Live parameter values (updated via signal connections) ────────
    // Using a plain object that gets replaced to trigger QML bindings.
    //
    // Writes are coalesced: incoming values land in _pendingValues and are
    // folded into paramValues by _flushTimer at most ~30x/sec. Replacing
    // paramValues re-evaluates every gauge binding that reads it, and the
    // IMU alone fires ~440 updates/sec (7 signals at 60 Hz). Copying the
    // whole dict and rebinding on each one pegged the QML thread at 100%
    // CPU while idle on the Orange Pi (V4 string-alloc + GC churn), so the
    // per-write path must stay allocation-free.
    property var paramValues: ({})
    property var _pendingValues: ({})

    function updateParamValue(paramId, value) {
        if (simulationActive) return;   // demo mode owns paramValues
        _pendingValues[paramId] = value;
        if (!_flushTimer.running) _flushTimer.start();
    }

    function _flushPendingValues() {
        if (simulationActive) { _pendingValues = ({}); return; }
        var changed = _pendingValues;
        var newValues = Object.assign({}, paramValues, changed);
        _pendingValues = ({});
        paramValues = newValues;   // fresh object so bindings re-evaluate
        for (var id in changed) {
            if (_holders[id])
                _holders[id].value = changed[id];
        }
    }

    // ── Per-parameter value holders ───────────────────────────────────
    // paramValues is replaced as a whole, so anything that reads it
    // re-evaluates on every fold, whichever value changed. A view with many
    // values on screen (OBDMenu's Parameter Cards: one card per supported
    // PID) binds to valueHolder(id).value instead: only the holders whose
    // value was in the fold change, so only those cards re-evaluate and
    // animate. Created on first use and kept for the session.
    property var _holders: ({})
    property Component _holderComponent: Component {
        QtObject { property var value }
    }
    function valueHolder(paramId) {
        var h = _holders[paramId];
        if (!h) {
            h = _holderComponent.createObject(root, { value: paramValues[paramId] });
            _holders[paramId] = h;
        }
        return h;
    }
    // After paramValues was replaced wholesale (demo mode, reset)
    function _syncAllHolders() {
        for (var id in _holders)
            _holders[id].value = paramValues[id];
    }

    property var _flushTimer: Timer {
        interval: 33            // ~30 Hz; plenty for needle animation
        repeat: false           // re-armed by the next write, idle otherwise
        onTriggered: root._flushPendingValues()
    }

    function getParamValue(paramId) {
        return paramValues[paramId] || 0;
    }

    // Decimal places for a plain-text readout (catalog `decimals`: 0 for
    // whole-number readings like speed, RPM and counters, else 1)
    function decimalsFor(paramId) {
        var p = _byId[paramId];
        return p ? p.decimals : 1;
    }

    // ── Simulated data (dashboard-editor demo mode) ───────────────────
    // Pure-QML fake data so dashboards can be built/tested without a live
    // OBD connection: every parameter sweeps a sine between its min/max,
    // with period and phase varied per parameter so the screen doesn't
    // pulse in lockstep. While active, real backend updates are ignored
    // (see guard above); on deactivation paramValues resets so stale fake
    // readings can never linger in the real OBD views.
    property bool simulationActive: false

    onSimulationActiveChanged: {
        if (simulationActive) {
            _simEpoch = Date.now();
            _simTick();
        } else {
            _pendingValues = ({});
            paramValues = ({});
            _syncAllHolders();
        }
    }

    property real _simEpoch: 0

    function _simTick() {
        var t = (Date.now() - _simEpoch) / 1000.0;
        var newValues = {};
        for (var i = 0; i < allParameters.length; i++) {
            var p = allParameters[i];
            var period = 4 + (i % 7) * 2;          // 4–16 s sweeps
            var phase = i * 0.7;
            var norm = 0.5 * (1 + Math.sin(2 * Math.PI * t / period + phase));
            var v = p.min + (p.max - p.min) * norm;
            // Round the big-range integers so readouts look like real data
            newValues[p.id] = (p.max - p.min) > 20 ? Math.round(v) : Math.round(v * 100) / 100;
        }
        paramValues = newValues;
        _syncAllHolders();
    }

    property var _simTimer: Timer {
        interval: 100
        repeat: true
        running: root.simulationActive
        onTriggered: root._simTick()
    }

    // ── Helper functions ─────────────────────────────────────────────
    function isOriginalParameter(command) {
        return originalParameters.indexOf(command) !== -1;
    }

    function getParamInfo(paramId) {
        return parameterInfo[paramId] || { title: paramId, unit: "", minValue: 0, maxValue: 100 };
    }

    // True if `paramId` is one of the declared parameters. getParamInfo()
    // returns a permissive fallback for unknown ids, so validation callers
    // (DashboardRenderer, the editor) use this instead.
    function hasParameter(paramId) {
        return parameterInfo[paramId] !== undefined;
    }

    // ── Vehicle support gate ──────────────────────────────────────────
    // obdManager scans the connected vehicle's supported-PID bitmaps on
    // every connect and publishes the result (model-id vocabulary) as
    // `vehicleSupportedParameters`; `vehicleScanComplete` is true from the
    // end of that scan until the connection drops. Neither is persisted —
    // the gate only ever describes the car that is answering right now.
    //
    //   vehicleKnown    — a vehicle has been scanned this session
    //   isSupported(id) — true for sensor params (not OBD), and for OBD
    //                     params the scanned vehicle reports. False for
    //                     everything else, including when no vehicle is
    //                     known (callers that want "anything goes while
    //                     offline", like the editor's PID picker, check
    //                     vehicleKnown first).
    readonly property var sensorParameters:
        allParameters.filter(function(p) { return p.source === "imu" }).map(function(p) { return p.id })
    function isSensorParameter(paramId) {
        var p = _byId[paramId];
        return p !== undefined && p.source === "imu";
    }

    readonly property bool _obdAvailable: typeof obdManager !== "undefined" && obdManager !== null
    readonly property bool vehicleKnown: _obdAvailable && obdManager.vehicleScanComplete === true
    readonly property var vehicleSupportedParameters:
        (_obdAvailable && obdManager.vehicleSupportedParameters) ? obdManager.vehicleSupportedParameters : []

    // id → true lookup, rebuilt only when the scan result changes.
    readonly property var _supportedSet: {
        var s = {};
        for (var i = 0; i < vehicleSupportedParameters.length; i++)
            s[vehicleSupportedParameters[i]] = true;
        return s;
    }

    function isSupported(paramId) {
        if (isSensorParameter(paramId)) return true;
        return vehicleKnown && _supportedSet[paramId] === true;
    }

    // Parameters a live vehicle can actually feed: the scanned OBD set in
    // allParameters order, without aliases (IGNITION_TIMING would be a second
    // Timing Advance card). Empty until a vehicle has been scanned.
    readonly property var vehicleParameters: {
        if (!vehicleKnown) return [];
        return allParameters.filter(function(p) {
            return !root.isSensorParameter(p.id) && !p.aliasOf && root._supportedSet[p.id] === true;
        });
    }

    // ── Signal connections to berryIMU for the sensor parameters ─────
    // Same pattern as _obdConnections below; guarded because the IMU
    // manager may legitimately be absent (e.g. stripped-down builds).
    property var _imuConnections: Connections {
        target: (typeof berryIMU !== "undefined" && berryIMU) ? berryIMU : null
        ignoreUnknownSignals: true

        function onPitchChanged(v) { root.updateParamValue("PITCH", v); }
        function onRollChanged(v) { root.updateParamValue("ROLL", v); }
        function onHeadingChanged(v) { root.updateParamValue("HEADING", v); }
        function onAltitudeChanged(v) { root.updateParamValue("ALTITUDE", v); }
        function onAccelMagnitudeChanged(v) { root.updateParamValue("ACCEL_MAG", v); }
        function onLateralGChanged(v) { root.updateParamValue("LATERAL_G", v); }
        function onLongitudinalGChanged(v) { root.updateParamValue("LONGITUDINAL_G", v); }
        function onBaroTempChanged(v) { root.updateParamValue("BARO_TEMP", v); }
    }

    // ── Signal connections to obdManager for all 92 parameters (84 OBD + 8 IMU) ───────
    property var _obdConnections: Connections {
        target: obdManager

        // Original 18 parameters
        function onCoolantTempChanged(value) { root.updateParamValue("COOLANT_TEMP", value); }
        function onVoltageChanged(value) { root.updateParamValue("CONTROL_MODULE_VOLTAGE", value); }
        function onEngineLoadChanged(value) { root.updateParamValue("ENGINE_LOAD", value); }
        function onThrottlePositionChanged(value) { root.updateParamValue("THROTTLE_POS", value); }
        function onIntakeAirTempChanged(value) { root.updateParamValue("INTAKE_TEMP", value); }
        function onTimingAdvanceChanged(value) { root.updateParamValue("TIMING_ADVANCE", value); }
        function onMassAirFlowChanged(value) { root.updateParamValue("MAF", value); }
        function onSpeedMPHChanged(value) { root.updateParamValue("SPEED", value); }
        function onRpmChanged(value) { root.updateParamValue("RPM", value); }
        function onAirFuelRatioChanged(value) { root.updateParamValue("COMMANDED_EQUIV_RATIO", value); }
        function onFuelLevelChanged(value) { root.updateParamValue("FUEL_LEVEL", value); }
        function onIntakeManifoldPressureChanged(value) { root.updateParamValue("INTAKE_PRESSURE", value); }
        function onShortTermFuelTrimChanged(value) { root.updateParamValue("SHORT_FUEL_TRIM_1", value); }
        function onLongTermFuelTrimChanged(value) { root.updateParamValue("LONG_FUEL_TRIM_1", value); }
        function onOxygenSensorVoltageChanged(value) { root.updateParamValue("O2_B1S1", value); }
        function onFuelPressureChanged(value) { root.updateParamValue("FUEL_PRESSURE", value); }
        function onEngineOilTempChanged(value) { root.updateParamValue("OIL_TEMP", value); }
        function onIgnitionTimingChanged(value) { root.updateParamValue("IGNITION_TIMING", value); }

        // Multi-value PIDs 0x61+ (torque, boost, odometer, ...): one generic
        // signal carries the parameter id with the value
        function onObdParameterChanged(paramId, value) { root.updateParamValue(paramId, value); }

        // Additional parameters - Batch 1
        function onRunTimeChanged(value) { root.updateParamValue("RUN_TIME", value); }
        function onDistanceWithMILChanged(value) { root.updateParamValue("DISTANCE_W_MIL", value); }
        function onFuelRailPressureChanged(value) { root.updateParamValue("FUEL_RAIL_PRESSURE_VAC", value); }
        function onFuelRailPressureDirectChanged(value) { root.updateParamValue("FUEL_RAIL_PRESSURE_DIRECT", value); }
        function onBarometricPressureChanged(value) { root.updateParamValue("BAROMETRIC_PRESSURE", value); }
        function onAmbientAirTempChanged(value) { root.updateParamValue("AMBIANT_AIR_TEMP", value); }
        function onRelativeThrottlePosChanged(value) { root.updateParamValue("RELATIVE_THROTTLE_POS", value); }
        function onAbsoluteThrottlePosBChanged(value) { root.updateParamValue("THROTTLE_POS_B", value); }
        function onAcceleratorPosChanged(value) { root.updateParamValue("ACCELERATOR_POS_D", value); }
        function onCatalystTempB1S1Changed(value) { root.updateParamValue("CATALYST_TEMP_B1S1", value); }
        function onCatalystTempB1S2Changed(value) { root.updateParamValue("CATALYST_TEMP_B1S2", value); }
        function onEvapVaporPressureChanged(value) { root.updateParamValue("EVAP_VAPOR_PRESSURE", value); }
        function onShortFuelTrim2Changed(value) { root.updateParamValue("SHORT_FUEL_TRIM_2", value); }
        function onLongFuelTrim2Changed(value) { root.updateParamValue("LONG_FUEL_TRIM_2", value); }
        function onO2SensorB1S2Changed(value) { root.updateParamValue("O2_B1S2", value); }
        function onO2SensorB2S1Changed(value) { root.updateParamValue("O2_B2S1", value); }
        function onO2SensorB2S2Changed(value) { root.updateParamValue("O2_B2S2", value); }
        function onDistanceSinceCodesCleared(value) { root.updateParamValue("DISTANCE_SINCE_DTC_CLEAR", value); }
        function onWarmupsSinceCodesCleared(value) { root.updateParamValue("WARMUPS_SINCE_DTC_CLEAR", value); }
        function onAbsoluteLoadChanged(value) { root.updateParamValue("ABSOLUTE_LOAD", value); }
        function onCommandedEGRChanged(value) { root.updateParamValue("COMMANDED_EGR", value); }
        function onEgrErrorChanged(value) { root.updateParamValue("EGR_ERROR", value); }
        function onEthanoPercentChanged(value) { root.updateParamValue("ETHANOL_PERCENT", value); }

        // Batch 2 - Additional O2 sensors
        function onO2SensorB1S3Changed(value) { root.updateParamValue("O2_B1S3", value); }
        function onO2SensorB1S4Changed(value) { root.updateParamValue("O2_B1S4", value); }
        function onO2SensorB2S3Changed(value) { root.updateParamValue("O2_B2S3", value); }
        function onO2SensorB2S4Changed(value) { root.updateParamValue("O2_B2S4", value); }
        // Wide-range O2 sensors voltage
        function onO2S1WRVoltageChanged(value) { root.updateParamValue("O2_S1_WR_VOLTAGE", value); }
        function onO2S2WRVoltageChanged(value) { root.updateParamValue("O2_S2_WR_VOLTAGE", value); }
        function onO2S3WRVoltageChanged(value) { root.updateParamValue("O2_S3_WR_VOLTAGE", value); }
        function onO2S4WRVoltageChanged(value) { root.updateParamValue("O2_S4_WR_VOLTAGE", value); }
        function onO2S5WRVoltageChanged(value) { root.updateParamValue("O2_S5_WR_VOLTAGE", value); }
        function onO2S6WRVoltageChanged(value) { root.updateParamValue("O2_S6_WR_VOLTAGE", value); }
        function onO2S7WRVoltageChanged(value) { root.updateParamValue("O2_S7_WR_VOLTAGE", value); }
        function onO2S8WRVoltageChanged(value) { root.updateParamValue("O2_S8_WR_VOLTAGE", value); }
        // Wide-range O2 sensors current
        function onO2S1WRCurrentChanged(value) { root.updateParamValue("O2_S1_WR_CURRENT", value); }
        function onO2S2WRCurrentChanged(value) { root.updateParamValue("O2_S2_WR_CURRENT", value); }
        function onO2S3WRCurrentChanged(value) { root.updateParamValue("O2_S3_WR_CURRENT", value); }
        function onO2S4WRCurrentChanged(value) { root.updateParamValue("O2_S4_WR_CURRENT", value); }
        function onO2S5WRCurrentChanged(value) { root.updateParamValue("O2_S5_WR_CURRENT", value); }
        function onO2S6WRCurrentChanged(value) { root.updateParamValue("O2_S6_WR_CURRENT", value); }
        function onO2S7WRCurrentChanged(value) { root.updateParamValue("O2_S7_WR_CURRENT", value); }
        function onO2S8WRCurrentChanged(value) { root.updateParamValue("O2_S8_WR_CURRENT", value); }
        // Bank 2 catalyst temps
        function onCatalystTempB2S1Changed(value) { root.updateParamValue("CATALYST_TEMP_B2S1", value); }
        function onCatalystTempB2S2Changed(value) { root.updateParamValue("CATALYST_TEMP_B2S2", value); }
        // Additional throttle/accelerator
        function onThrottlePosCChanged(value) { root.updateParamValue("THROTTLE_POS_C", value); }
        function onAcceleratorPosEChanged(value) { root.updateParamValue("ACCELERATOR_POS_E", value); }
        function onAcceleratorPosFChanged(value) { root.updateParamValue("ACCELERATOR_POS_F", value); }
        function onThrottleActuatorChanged(value) { root.updateParamValue("THROTTLE_ACTUATOR", value); }
        // Fuel system
        function onEvaporativePurgeChanged(value) { root.updateParamValue("EVAPORATIVE_PURGE", value); }
        function onFuelRailPressureAbsChanged(value) { root.updateParamValue("FUEL_RAIL_PRESSURE_ABS", value); }
        function onFuelInjectTimingChanged(value) { root.updateParamValue("FUEL_INJECT_TIMING", value); }
        function onFuelRateChanged(value) { root.updateParamValue("FUEL_RATE", value); }
        // Time-based
        function onRunTimeMILChanged(value) { root.updateParamValue("RUN_TIME_MIL", value); }
        function onTimeSinceDTCClearedChanged(value) { root.updateParamValue("TIME_SINCE_DTC_CLEARED", value); }
        // Other
        function onMaxMAFChanged(value) { root.updateParamValue("MAX_MAF", value); }
        function onFuelTypeChanged(value) { root.updateParamValue("FUEL_TYPE", value); }
        function onEvapVaporPressureAbsChanged(value) { root.updateParamValue("EVAP_VAPOR_PRESSURE_ABS", value); }
        function onEvapVaporPressureAltChanged(value) { root.updateParamValue("EVAP_VAPOR_PRESSURE_ALT", value); }
        function onShortO2TrimB1Changed(value) { root.updateParamValue("SHORT_O2_TRIM_B1", value); }
        function onLongO2TrimB1Changed(value) { root.updateParamValue("LONG_O2_TRIM_B1", value); }
        function onShortO2TrimB2Changed(value) { root.updateParamValue("SHORT_O2_TRIM_B2", value); }
        function onLongO2TrimB2Changed(value) { root.updateParamValue("LONG_O2_TRIM_B2", value); }
        function onRelativeAccelPosChanged(value) { root.updateParamValue("RELATIVE_ACCEL_POS", value); }
        function onHybridBatteryRemainingChanged(value) { root.updateParamValue("HYBRID_BATTERY_REMAINING", value); }
        function onElmVoltageChanged(value) { root.updateParamValue("ELM_VOLTAGE", value); }
    }
}
