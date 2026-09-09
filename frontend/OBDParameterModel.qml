pragma Singleton
import QtQuick 2.15

QtObject {
    id: root

    // ── All OBD parameters with display metadata + semantic `kind` ───
    //
    // `kind` is used by widgets to declare PID compatibility. Recognized kinds:
    //   "percentage"    — 0–100 % unidirectional
    //   "temperature"   — °C temperature (may go negative, but treated by theme)
    //   "voltage"       — volts
    //   "pressure"      — kPa or Pa
    //   "bidirectional" — signed value that straddles zero (trim, timing, error)
    //   "numeric"       — everything else (speed, RPM, distance, time, flow…)
    // Widgets declare `octaveSupportedKinds` as an array (e.g. ["bidirectional"]
    // for LinearGauge, ["*"] for generic numeric widgets). The PID picker in the
    // dashboard editor filters based on this.
    readonly property var allParameters: [
        // Original 18 parameters
        { id: "SPEED", title: "Speed", unit: "MPH", min: 0, max: 160, kind: "numeric" },
        { id: "RPM", title: "Engine RPM", unit: "RPM", min: 0, max: 8000, kind: "numeric" },
        { id: "COOLANT_TEMP", title: "Coolant Temp", unit: "°C", min: 0, max: 120, kind: "temperature" },
        { id: "OIL_TEMP", title: "Oil Temp", unit: "°C", min: 0, max: 150, kind: "temperature" },
        { id: "COMMANDED_EQUIV_RATIO", title: "Air-Fuel Ratio", unit: ":1", min: 10, max: 18, kind: "numeric" },
        { id: "ENGINE_LOAD", title: "Engine Load", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "THROTTLE_POS", title: "Throttle", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "FUEL_LEVEL", title: "Fuel Level", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "SHORT_FUEL_TRIM_1", title: "Short Fuel Trim 1", unit: "%", min: -25, max: 25, kind: "bidirectional" },
        { id: "LONG_FUEL_TRIM_1", title: "Long Fuel Trim 1", unit: "%", min: -25, max: 25, kind: "bidirectional" },
        { id: "INTAKE_TEMP", title: "Intake Temp", unit: "°C", min: 0, max: 80, kind: "temperature" },
        { id: "INTAKE_PRESSURE", title: "Intake Pressure", unit: "kPa", min: 0, max: 255, kind: "pressure" },
        { id: "MAF", title: "Mass Air Flow", unit: "g/s", min: 0, max: 100, kind: "numeric" },
        { id: "TIMING_ADVANCE", title: "Timing Advance", unit: "°", min: -35, max: 35, kind: "bidirectional" },
        { id: "CONTROL_MODULE_VOLTAGE", title: "System Voltage", unit: "V", min: 10, max: 15, kind: "voltage" },
        { id: "O2_B1S1", title: "O2 Bank 1 Sensor 1", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "FUEL_PRESSURE", title: "Fuel Pressure", unit: "kPa", min: 0, max: 765, kind: "pressure" },
        { id: "IGNITION_TIMING", title: "Ignition Timing", unit: "°", min: -10, max: 60, kind: "bidirectional" },
        // Additional parameters
        { id: "RUN_TIME", title: "Run Time", unit: "sec", min: 0, max: 65535, kind: "numeric" },
        { id: "DISTANCE_W_MIL", title: "Distance w/ MIL", unit: "km", min: 0, max: 65535, kind: "numeric" },
        { id: "FUEL_RAIL_PRESSURE_VAC", title: "Fuel Rail Pressure", unit: "kPa", min: 0, max: 5177, kind: "pressure" },
        { id: "FUEL_RAIL_PRESSURE_DIRECT", title: "Fuel Rail Direct", unit: "kPa", min: 0, max: 655350, kind: "pressure" },
        { id: "BAROMETRIC_PRESSURE", title: "Barometric", unit: "kPa", min: 0, max: 255, kind: "pressure" },
        { id: "AMBIANT_AIR_TEMP", title: "Ambient Air", unit: "°C", min: -40, max: 215, kind: "temperature" },
        { id: "RELATIVE_THROTTLE_POS", title: "Rel. Throttle", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "THROTTLE_POS_B", title: "Throttle Pos B", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "ACCELERATOR_POS_D", title: "Accel. Pedal", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "CATALYST_TEMP_B1S1", title: "Cat Temp B1S1", unit: "°C", min: 0, max: 6513, kind: "temperature" },
        { id: "CATALYST_TEMP_B1S2", title: "Cat Temp B1S2", unit: "°C", min: 0, max: 6513, kind: "temperature" },
        { id: "EVAP_VAPOR_PRESSURE", title: "EVAP Pressure", unit: "Pa", min: -8192, max: 8191, kind: "bidirectional" },
        { id: "SHORT_FUEL_TRIM_2", title: "Short Fuel Trim 2", unit: "%", min: -25, max: 25, kind: "bidirectional" },
        { id: "LONG_FUEL_TRIM_2", title: "Long Fuel Trim 2", unit: "%", min: -25, max: 25, kind: "bidirectional" },
        { id: "O2_B1S2", title: "O2 Bank 1 Sensor 2", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "O2_B2S1", title: "O2 Bank 2 Sensor 1", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "O2_B2S2", title: "O2 Bank 2 Sensor 2", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "DISTANCE_SINCE_DTC_CLEAR", title: "Dist Since Clear", unit: "km", min: 0, max: 65535, kind: "numeric" },
        { id: "WARMUPS_SINCE_DTC_CLEAR", title: "Warmups Since Clear", unit: "", min: 0, max: 255, kind: "numeric" },
        { id: "ABSOLUTE_LOAD", title: "Absolute Load", unit: "%", min: 0, max: 25700, kind: "numeric" },
        { id: "COMMANDED_EGR", title: "Commanded EGR", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "EGR_ERROR", title: "EGR Error", unit: "%", min: -100, max: 100, kind: "bidirectional" },
        { id: "ETHANOL_PERCENT", title: "Ethanol %", unit: "%", min: 0, max: 100, kind: "percentage" },
        // Batch 2 - Additional O2 sensors
        { id: "O2_B1S3", title: "O2 Bank 1 Sensor 3", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "O2_B1S4", title: "O2 Bank 1 Sensor 4", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "O2_B2S3", title: "O2 Bank 2 Sensor 3", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        { id: "O2_B2S4", title: "O2 Bank 2 Sensor 4", unit: "V", min: 0, max: 1.0, kind: "voltage" },
        // Wide-range O2 sensors voltage
        { id: "O2_S1_WR_VOLTAGE", title: "O2 S1 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S2_WR_VOLTAGE", title: "O2 S2 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S3_WR_VOLTAGE", title: "O2 S3 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S4_WR_VOLTAGE", title: "O2 S4 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S5_WR_VOLTAGE", title: "O2 S5 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S6_WR_VOLTAGE", title: "O2 S6 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S7_WR_VOLTAGE", title: "O2 S7 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        { id: "O2_S8_WR_VOLTAGE", title: "O2 S8 WR Voltage", unit: "V", min: 0, max: 5.0, kind: "voltage" },
        // Wide-range O2 sensors current
        { id: "O2_S1_WR_CURRENT", title: "O2 S1 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S2_WR_CURRENT", title: "O2 S2 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S3_WR_CURRENT", title: "O2 S3 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S4_WR_CURRENT", title: "O2 S4 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S5_WR_CURRENT", title: "O2 S5 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S6_WR_CURRENT", title: "O2 S6 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S7_WR_CURRENT", title: "O2 S7 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        { id: "O2_S8_WR_CURRENT", title: "O2 S8 WR Current", unit: "mA", min: -128, max: 128, kind: "bidirectional" },
        // Bank 2 catalyst temps
        { id: "CATALYST_TEMP_B2S1", title: "Cat Temp B2S1", unit: "°C", min: 0, max: 6513, kind: "temperature" },
        { id: "CATALYST_TEMP_B2S2", title: "Cat Temp B2S2", unit: "°C", min: 0, max: 6513, kind: "temperature" },
        // Additional throttle/accelerator
        { id: "THROTTLE_POS_C", title: "Throttle Pos C", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "ACCELERATOR_POS_E", title: "Accel. Pos E", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "ACCELERATOR_POS_F", title: "Accel. Pos F", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "THROTTLE_ACTUATOR", title: "Throttle Actuator", unit: "%", min: 0, max: 100, kind: "percentage" },
        // Fuel system
        { id: "EVAPORATIVE_PURGE", title: "EVAP Purge", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "FUEL_RAIL_PRESSURE_ABS", title: "Fuel Rail Abs", unit: "kPa", min: 0, max: 655350, kind: "pressure" },
        { id: "FUEL_INJECT_TIMING", title: "Fuel Inject Timing", unit: "°", min: -210, max: 302, kind: "bidirectional" },
        { id: "FUEL_RATE", title: "Fuel Rate", unit: "L/h", min: 0, max: 3212, kind: "numeric" },
        // Time-based
        { id: "RUN_TIME_MIL", title: "Run Time w/ MIL", unit: "min", min: 0, max: 65535, kind: "numeric" },
        { id: "TIME_SINCE_DTC_CLEARED", title: "Time Since Clear", unit: "min", min: 0, max: 65535, kind: "numeric" },
        // Other
        { id: "MAX_MAF", title: "Max MAF", unit: "g/s", min: 0, max: 2550, kind: "numeric" },
        { id: "FUEL_TYPE", title: "Fuel Type", unit: "", min: 0, max: 23, kind: "numeric" },
        { id: "EVAP_VAPOR_PRESSURE_ABS", title: "EVAP Pressure Abs", unit: "kPa", min: 0, max: 327, kind: "pressure" },
        { id: "EVAP_VAPOR_PRESSURE_ALT", title: "EVAP Pressure Alt", unit: "Pa", min: -32768, max: 32767, kind: "bidirectional" },
        { id: "SHORT_O2_TRIM_B1", title: "Short O2 Trim B1", unit: "%", min: -100, max: 100, kind: "bidirectional" },
        { id: "LONG_O2_TRIM_B1", title: "Long O2 Trim B1", unit: "%", min: -100, max: 100, kind: "bidirectional" },
        { id: "SHORT_O2_TRIM_B2", title: "Short O2 Trim B2", unit: "%", min: -100, max: 100, kind: "bidirectional" },
        { id: "LONG_O2_TRIM_B2", title: "Long O2 Trim B2", unit: "%", min: -100, max: 100, kind: "bidirectional" },
        { id: "RELATIVE_ACCEL_POS", title: "Rel. Accel Pos", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "HYBRID_BATTERY_REMAINING", title: "Hybrid Battery", unit: "%", min: 0, max: 100, kind: "percentage" },
        { id: "ELM_VOLTAGE", title: "ELM Voltage", unit: "V", min: 0, max: 65, kind: "voltage" },
        // ── Sensor (BerryIMU) parameters ──────────────────────────────
        // Fed by the berryIMU manager (see _imuConnections below), not
        // obdManager. They flow through paramValues like any OBD PID, so
        // every gauge, the PID picker, and the editor's demo mode all work
        // with them unchanged.
        { id: "PITCH", title: "Pitch", unit: "°", min: -90, max: 90, kind: "bidirectional" },
        { id: "ROLL", title: "Roll", unit: "°", min: -180, max: 180, kind: "bidirectional" },
        { id: "HEADING", title: "Heading", unit: "°", min: 0, max: 360, kind: "numeric" },
        { id: "ALTITUDE", title: "Altitude", unit: "m", min: 0, max: 3000, kind: "numeric" },
        { id: "ACCEL_MAG", title: "G Magnitude", unit: "g", min: 0, max: 3, kind: "numeric" },
        { id: "LATERAL_G", title: "Lateral G", unit: "g", min: -2, max: 2, kind: "bidirectional" },
        { id: "LONGITUDINAL_G", title: "Longitudinal G", unit: "g", min: -2, max: 2, kind: "bidirectional" },
        { id: "BARO_TEMP", title: "Cabin Baro Temp", unit: "°C", min: -20, max: 60, kind: "temperature" }
    ]

    // ── Original 18 parameter IDs (default-enabled) ──────────────────
    readonly property var originalParameters: [
        "SPEED", "RPM", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE",
        "ENGINE_LOAD", "THROTTLE_POS", "INTAKE_TEMP", "TIMING_ADVANCE",
        "MAF", "COMMANDED_EQUIV_RATIO", "FUEL_LEVEL", "INTAKE_PRESSURE",
        "SHORT_FUEL_TRIM_1", "LONG_FUEL_TRIM_1", "O2_B1S1", "FUEL_PRESSURE",
        "OIL_TEMP", "IGNITION_TIMING"
    ]

    // ── Abbreviated titles for compact display ───────────────────────
    readonly property var shortTitles: ({
        "SPEED": "SPD", "RPM": "RPM", "COOLANT_TEMP": "CLT", "OIL_TEMP": "OIL",
        "COMMANDED_EQUIV_RATIO": "AFR", "ENGINE_LOAD": "LOAD", "THROTTLE_POS": "TPS",
        "FUEL_LEVEL": "FUEL", "SHORT_FUEL_TRIM_1": "STFT1", "LONG_FUEL_TRIM_1": "LTFT1",
        "INTAKE_TEMP": "IAT", "INTAKE_PRESSURE": "MAP", "MAF": "MAF",
        "TIMING_ADVANCE": "TIM", "CONTROL_MODULE_VOLTAGE": "VOLT",
        "O2_B1S1": "O2", "FUEL_PRESSURE": "FP", "IGNITION_TIMING": "IGN"
    })

    // ── parameterInfo map (keyed by ID) for HomeOBDView compatibility ─
    property var parameterInfo: {
        var info = {};
        for (var i = 0; i < allParameters.length; i++) {
            var p = allParameters[i];
            info[p.id] = {
                title: shortTitles[p.id] || p.title,
                unit: p.unit,
                minValue: p.min,
                maxValue: p.max
            };
        }
        return info;
    }

    // ── Live parameter values (updated via signal connections) ────────
    // Using a plain object that gets replaced to trigger QML bindings
    property var paramValues: ({})

    function updateParamValue(paramId, value) {
        if (simulationActive) return;   // demo mode owns paramValues
        var newValues = Object.assign({}, paramValues);
        newValues[paramId] = value;
        paramValues = newValues;
    }

    function getParamValue(paramId) {
        return paramValues[paramId] || 0;
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
            paramValues = ({});
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

    // ── Signal connections to obdManager for all 93 parameters ───────
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
