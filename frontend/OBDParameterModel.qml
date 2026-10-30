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

    // The same rows in the units the gauges show (gaugeTempUnit,
    // gaugeSpeedUnit, sensorAltitudeUnit; ParameterCatalog converts). Gauges
    // read these through getParamInfo() and liveValue(); allParameters stays
    // in each parameter's own unit (Trip History, the PID lists).
    readonly property var displayParameters:
        (typeof parameterCatalog !== "undefined" && parameterCatalog) ? parameterCatalog.displayParameters : []
    // id -> [scale, offset] for the rows that convert; reassigned when a unit
    // setting changes, so every liveValue() binding re-evaluates
    readonly property var _conversion: {
        var m = {};
        for (var i = 0; i < displayParameters.length; i++) {
            var p = displayParameters[i];
            if (p.displayScale !== 1 || p.displayOffset !== 0)
                m[p.id] = [p.displayScale, p.displayOffset];
        }
        return m;
    }

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
        for (var i = 0; i < displayParameters.length; i++) {
            var p = displayParameters[i];
            info[p.id] = {
                title: p.shortTitle || p.title,
                unit: p.unit,
                minValue: p.min,
                maxValue: p.max
            };
        }
        return info;
    }

    // ── Live parameter values ─────────────────────────────────────────
    // The C++ ParameterStore (context property `paramStore`, fed from
    // obdManager.parameterSampled and the berryIMU signals in main.cpp) holds
    // the values, one notifying key per id, and coalesces writes to ~30 Hz.
    //
    // Readers bind liveValue(id): a binding depends only on the branch that
    // ran, so outside demo mode it re-evaluates only when that id changes.
    // undefined until the first reading (readouts show "--"), and at
    // shutdown, when the store goes before the engine.
    readonly property var _store: (typeof paramStore !== "undefined" && paramStore) ? paramStore : null
    // Values are in the display unit (see displayParameters); the store
    // keeps the parameter's own.
    function liveValue(paramId) {
        return toDisplay(paramId, rawValue(paramId));
    }

    // liveValue as a number gauges can bind a `real` to: NaN until there is
    // a finite reading, so "no sample yet" never looks like a real 0 (gauges
    // show "--", draw no fill and evaluate no threshold while !isFinite)
    function sample(paramId) {
        if (!paramId) return NaN;
        var v = liveValue(paramId);
        return (typeof v === "number" && isFinite(v)) ? v : NaN;
    }

    // The same reading in the parameter's own unit (MPH, °C, m), for code
    // that needs the native value whatever the gauges show (the 3D Jeep's
    // wheel speed)
    function rawValue(paramId) {
        return simulationActive ? _simValues[paramId] : (_store ? _store[paramId] : undefined);
    }

    // A value of paramId from its own unit to the gauges' and back (the
    // dashboard renderer and editor convert range props with these)
    function toDisplay(paramId, v) {
        var c = _conversion[paramId];
        return (c && typeof v === "number") ? v * c[0] + c[1] : v;
    }
    function fromDisplay(paramId, v) {
        var c = _conversion[paramId];
        return (c && typeof v === "number") ? Math.round((v - c[1]) / c[0] * 1e6) / 1e6 : v;
    }

    // A parameter's own unit (what the store and the black box hold)
    function nativeUnit(paramId) {
        var p = _byId[paramId];
        return p ? p.unit : "";
    }

    function getParamValue(paramId) {
        return liveValue(paramId) || 0;
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
    // pulse in lockstep. While active, liveValue() serves these instead of
    // the store; switching off puts the real readings back at once, so fake
    // ones never linger in the real OBD views.
    property bool simulationActive: false
    property var _simValues: ({})

    onSimulationActiveChanged: {
        if (simulationActive) {
            _simEpoch = Date.now();
            _simTick();
        } else {
            _simValues = ({});
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
        _simValues = newValues;   // fresh object: every demo reader re-evaluates
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
    // Timing Advance card), in display units (the Parameter Cards show unit,
    // min and max next to liveValue). Empty until a vehicle has been scanned.
    readonly property var vehicleParameters: {
        if (!vehicleKnown) return [];
        return displayParameters.filter(function(p) {
            return !root.isSensorParameter(p.id) && !p.aliasOf && root._supportedSet[p.id] === true;
        });
    }
}
