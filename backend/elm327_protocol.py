"""
ELM327/OBD-II Protocol Handler — pure Python, no I/O.

Handles command formatting, response parsing, and PID decoding.

Desktop reference / parity twin of the C++ ``ELM327Protocol``
(src/managers/elm327protocol.{h,cpp}). The running Python app talks to the
adapter through python-obd instead, so nothing imports this module at
runtime; it exists so the PID table, decoders and parsers can be diffed and
exercised in plain Python. PID numbers and command names follow python-obd's
commands.py — keep this file, the C++ table and that table in agreement.
"""

from backend.logging_config import get_logger
logger = get_logger(__name__)


# Standard OBD-II SPP UUID for Bluetooth RFCOMM
SPP_UUID = "00001101-0000-1000-8000-00805f9b34fb"

# ELM327 initialization sequence
INIT_COMMANDS = [
    (b"ATZ\r", 1.5),       # Reset (needs long timeout)
    (b"ATE0\r", 0.5),      # Echo off
    (b"ATL0\r", 0.3),      # Linefeeds off
    (b"ATS0\r", 0.3),      # Spaces off
    (b"ATH0\r", 0.3),      # Headers off
    (b"ATAT2\r", 0.3),     # Aggressive adaptive timing (fastest responses)
    (b"ATSP0\r", 0.5),     # Auto protocol
]

# ELM327 response indicators
PROMPT = b">"
NO_DATA = "NO DATA"
UNABLE = "UNABLE TO CONNECT"
SEARCHING = "SEARCHING..."
ERROR_RESPONSES = {"NO DATA", "UNABLE TO CONNECT", "ERROR", "?", "STOPPED", "BUS INIT: ...ERROR"}

# DTC type prefixes
DTC_PREFIXES = {0: "P", 1: "C", 2: "B", 3: "U"}


# ---------------------------------------------------------------------------
# PID Table: (mode, pid_hex) → (name, signal_name, decode_func, unit)
# signal_name matches the Signal name in StubOBDManager / OBDManager
# decode_func takes a list of data bytes [A, B, ...] and returns a float
# ---------------------------------------------------------------------------

def _simple(a):
    return float(a[0])

def _offset40(a):
    return float(a[0] - 40)

def _pct(a):
    return round(a[0] * 100.0 / 255.0, 1)

def _pct_centered(a):
    return round((a[0] - 128) * 100.0 / 128.0, 1)

def _rpm(a):
    return round((a[0] * 256.0 + a[1]) / 4.0, 1)

def _kpa(a):
    return float(a[0])

def _fuel_pressure(a):
    return float(a[0] * 3)

def _timing(a):
    return round(a[0] / 2.0 - 64.0, 1)

def _maf(a):
    return round((a[0] * 256.0 + a[1]) / 100.0, 2)

def _o2_voltage(a):
    return round(a[0] / 200.0, 3)

def _fuel_rail_vac(a):
    return round((a[0] * 256.0 + a[1]) * 0.079, 1)

def _fuel_rail_direct(a):
    return round((a[0] * 256.0 + a[1]) * 10.0, 1)

def _evap_pressure(a):
    val = a[0] * 256 + a[1]
    if val > 32767:
        val -= 65536
    return round(val / 4.0, 1)

def _catalyst_temp(a):
    return round((a[0] * 256.0 + a[1]) / 10.0 - 40.0, 1)

def _voltage(a):
    return round((a[0] * 256.0 + a[1]) / 1000.0, 2)

def _air_fuel_ratio(a):
    # Commanded equivalence ratio (lambda) scaled to a gasoline AFR, matching
    # OBDManager._update_afr (python-obd ratio * 14.7).
    return round((a[0] * 256.0 + a[1]) / 32768.0 * 14.7, 2)

def _uint16(a):
    return float(a[0] * 256 + a[1])

def _fuel_rate(a):
    return round((a[0] * 256.0 + a[1]) / 20.0, 2)

def _inject_timing(a):
    return round(((a[0] * 256.0 + a[1]) / 128.0) - 210.0, 2)

def _abs_evap(a):
    return round((a[0] * 256.0 + a[1]) / 200.0, 2)

# Wide-range O2 (PIDs 0x24-0x2B / 0x34-0x3B): bytes A,B carry the lambda
# ratio; the voltage / current live in bytes C,D (python-obd's
# sensor_voltage_big and current_centered).
def _wr_o2_voltage(a):
    return round((a[2] * 256.0 + a[3]) * 8.0 / 65535.0, 4)

def _wr_o2_current(a):
    return round((a[2] * 256.0 + a[3]) / 256.0 - 128.0, 4)

def _fuel_rail_abs(a):
    return round((a[0] * 256.0 + a[1]) * 10.0, 1)


# Core PID table: (mode, pid) → (name, signal_name, command_name, decoder, num_bytes)
# signal_name is the name of the Changed signal on the OBD manager (e.g., "rpmChanged");
# command_name is the python-obd command name (settings key / scan vocabulary).
PID_TABLE = {
    # --- Default enabled PIDs (the 16+1 most common) ---
    (1, 0x05): ("Coolant Temp", "coolantTempChanged", "COOLANT_TEMP", _offset40, 1),
    (1, 0x42): ("Control Module Voltage", "voltageChanged", "CONTROL_MODULE_VOLTAGE", _voltage, 2),
    (1, 0x04): ("Engine Load", "engineLoadChanged", "ENGINE_LOAD", _pct, 1),
    (1, 0x11): ("Throttle Position", "throttlePositionChanged", "THROTTLE_POS", _pct, 1),
    (1, 0x0F): ("Intake Air Temp", "intakeAirTempChanged", "INTAKE_TEMP", _offset40, 1),
    (1, 0x0E): ("Timing Advance", "timingAdvanceChanged", "TIMING_ADVANCE", _timing, 1),
    (1, 0x10): ("MAF", "massAirFlowChanged", "MAF", _maf, 2),
    (1, 0x0D): ("Speed", "speedMPHChanged", "SPEED", lambda a: round(a[0] * 0.621371, 1), 1),  # km/h → mph
    (1, 0x0C): ("RPM", "rpmChanged", "RPM", _rpm, 2),
    (1, 0x44): ("Air/Fuel Ratio", "airFuelRatioChanged", "COMMANDED_EQUIV_RATIO", _air_fuel_ratio, 2),
    (1, 0x2F): ("Fuel Level", "fuelLevelChanged", "FUEL_LEVEL", _pct, 1),
    (1, 0x0B): ("Intake Manifold Pressure", "intakeManifoldPressureChanged", "INTAKE_PRESSURE", _kpa, 1),
    (1, 0x06): ("Short Term Fuel Trim 1", "shortTermFuelTrimChanged", "SHORT_FUEL_TRIM_1", _pct_centered, 1),
    (1, 0x07): ("Long Term Fuel Trim 1", "longTermFuelTrimChanged", "LONG_FUEL_TRIM_1", _pct_centered, 1),
    (1, 0x14): ("O2 Sensor B1S1", "oxygenSensorVoltageChanged", "O2_B1S1", _o2_voltage, 2),
    (1, 0x0A): ("Fuel Pressure", "fuelPressureChanged", "FUEL_PRESSURE", _fuel_pressure, 1),
    (1, 0x5C): ("Oil Temp", "engineOilTempChanged", "OIL_TEMP", _offset40, 1),

    # --- Extended PIDs ---
    (1, 0x1F): ("Run Time", "runTimeChanged", "RUN_TIME", _uint16, 2),
    (1, 0x21): ("Distance w/ MIL", "distanceWithMILChanged", "DISTANCE_W_MIL", _uint16, 2),
    (1, 0x22): ("Fuel Rail Pressure (vac)", "fuelRailPressureChanged", "FUEL_RAIL_PRESSURE_VAC", _fuel_rail_vac, 2),
    (1, 0x23): ("Fuel Rail Pressure (direct)", "fuelRailPressureDirectChanged", "FUEL_RAIL_PRESSURE_DIRECT", _fuel_rail_direct, 2),
    (1, 0x33): ("Barometric Pressure", "barometricPressureChanged", "BAROMETRIC_PRESSURE", _kpa, 1),
    (1, 0x46): ("Ambient Air Temp", "ambientAirTempChanged", "AMBIANT_AIR_TEMP", _offset40, 1),
    (1, 0x45): ("Relative Throttle Pos", "relativeThrottlePosChanged", "RELATIVE_THROTTLE_POS", _pct, 1),
    (1, 0x47): ("Throttle Pos B", "absoluteThrottlePosBChanged", "THROTTLE_POS_B", _pct, 1),
    (1, 0x49): ("Accelerator Pos D", "acceleratorPosChanged", "ACCELERATOR_POS_D", _pct, 1),
    (1, 0x3C): ("Catalyst Temp B1S1", "catalystTempB1S1Changed", "CATALYST_TEMP_B1S1", _catalyst_temp, 2),
    (1, 0x3E): ("Catalyst Temp B1S2", "catalystTempB1S2Changed", "CATALYST_TEMP_B1S2", _catalyst_temp, 2),
    (1, 0x32): ("Evap Vapor Pressure", "evapVaporPressureChanged", "EVAP_VAPOR_PRESSURE", _evap_pressure, 2),
    (1, 0x08): ("Short Fuel Trim 2", "shortFuelTrim2Changed", "SHORT_FUEL_TRIM_2", _pct_centered, 1),
    (1, 0x09): ("Long Fuel Trim 2", "longFuelTrim2Changed", "LONG_FUEL_TRIM_2", _pct_centered, 1),
    (1, 0x15): ("O2 B1S2", "o2SensorB1S2Changed", "O2_B1S2", _o2_voltage, 2),
    (1, 0x18): ("O2 B2S1", "o2SensorB2S1Changed", "O2_B2S1", _o2_voltage, 2),
    (1, 0x19): ("O2 B2S2", "o2SensorB2S2Changed", "O2_B2S2", _o2_voltage, 2),
    (1, 0x31): ("Distance Since Codes Cleared", "distanceSinceCodesCleared", "DISTANCE_SINCE_DTC_CLEAR", _uint16, 2),
    (1, 0x30): ("Warmups Since Codes Cleared", "warmupsSinceCodesCleared", "WARMUPS_SINCE_DTC_CLEAR", _simple, 1),
    (1, 0x43): ("Absolute Load", "absoluteLoadChanged", "ABSOLUTE_LOAD", lambda a: round((a[0]*256+a[1])*100.0/255.0, 1), 2),
    (1, 0x2C): ("Commanded EGR", "commandedEGRChanged", "COMMANDED_EGR", _pct, 1),
    (1, 0x2D): ("EGR Error", "egrErrorChanged", "EGR_ERROR", _pct_centered, 1),
    (1, 0x52): ("Ethanol Percent", "ethanoPercentChanged", "ETHANOL_PERCENT", _pct, 1),

    # --- Additional narrowband O2 sensors (0x14-0x1B: B1S1..B1S4, B2S1..B2S4) ---
    (1, 0x16): ("O2 B1S3", "o2SensorB1S3Changed", "O2_B1S3", _o2_voltage, 2),
    (1, 0x17): ("O2 B1S4", "o2SensorB1S4Changed", "O2_B1S4", _o2_voltage, 2),
    (1, 0x1A): ("O2 B2S3", "o2SensorB2S3Changed", "O2_B2S3", _o2_voltage, 2),
    (1, 0x1B): ("O2 B2S4", "o2SensorB2S4Changed", "O2_B2S4", _o2_voltage, 2),

    (1, 0x3D): ("Catalyst Temp B2S1", "catalystTempB2S1Changed", "CATALYST_TEMP_B2S1", _catalyst_temp, 2),
    (1, 0x3F): ("Catalyst Temp B2S2", "catalystTempB2S2Changed", "CATALYST_TEMP_B2S2", _catalyst_temp, 2),
    (1, 0x48): ("Throttle Pos C", "throttlePosCChanged", "THROTTLE_POS_C", _pct, 1),
    (1, 0x4A): ("Accelerator Pos E", "acceleratorPosEChanged", "ACCELERATOR_POS_E", _pct, 1),
    (1, 0x4B): ("Accelerator Pos F", "acceleratorPosFChanged", "ACCELERATOR_POS_F", _pct, 1),
    (1, 0x4C): ("Throttle Actuator", "throttleActuatorChanged", "THROTTLE_ACTUATOR", _pct, 1),
    (1, 0x4D): ("Run Time MIL", "runTimeMILChanged", "RUN_TIME_MIL", _uint16, 2),
    (1, 0x4E): ("Time Since DTC Cleared", "timeSinceDTCClearedChanged", "TIME_SINCE_DTC_CLEARED", _uint16, 2),
    (1, 0x50): ("Max MAF", "maxMAFChanged", "MAX_MAF", lambda a: float(a[0] * 10), 1),
    (1, 0x51): ("Fuel Type", "fuelTypeChanged", "FUEL_TYPE", _simple, 1),
    (1, 0x53): ("Evap Vapor Pressure Abs", "evapVaporPressureAbsChanged", "EVAP_VAPOR_PRESSURE_ABS", _abs_evap, 2),
    (1, 0x54): ("Evap Vapor Pressure Alt", "evapVaporPressureAltChanged", "EVAP_VAPOR_PRESSURE_ALT", _evap_pressure, 2),
    (1, 0x55): ("Short O2 Trim B1", "shortO2TrimB1Changed", "SHORT_O2_TRIM_B1", _pct_centered, 1),
    (1, 0x56): ("Long O2 Trim B1", "longO2TrimB1Changed", "LONG_O2_TRIM_B1", _pct_centered, 1),
    (1, 0x57): ("Short O2 Trim B2", "shortO2TrimB2Changed", "SHORT_O2_TRIM_B2", _pct_centered, 1),
    (1, 0x58): ("Long O2 Trim B2", "longO2TrimB2Changed", "LONG_O2_TRIM_B2", _pct_centered, 1),
    (1, 0x59): ("Fuel Rail Pressure Abs", "fuelRailPressureAbsChanged", "FUEL_RAIL_PRESSURE_ABS", _fuel_rail_abs, 2),
    (1, 0x5A): ("Relative Accel Pos", "relativeAccelPosChanged", "RELATIVE_ACCEL_POS", _pct, 1),
    (1, 0x5B): ("Hybrid Battery", "hybridBatteryRemainingChanged", "HYBRID_BATTERY_REMAINING", _pct, 1),
    (1, 0x2E): ("Evaporative Purge", "evaporativePurgeChanged", "EVAPORATIVE_PURGE", _pct, 1),
    (1, 0x5D): ("Fuel Inject Timing", "fuelInjectTimingChanged", "FUEL_INJECT_TIMING", _inject_timing, 2),
    (1, 0x5E): ("Fuel Rate", "fuelRateChanged", "FUEL_RATE", _fuel_rate, 2),
}

# Wide-range O2 sensors: voltage (0x24-0x2B), current (0x34-0x3B)
for _n in range(1, 9):
    PID_TABLE[(1, 0x23 + _n)] = (f"O2 S{_n} WR Voltage", f"o2S{_n}WRVoltageChanged",
                                 f"O2_S{_n}_WR_VOLTAGE", _wr_o2_voltage, 4)
    PID_TABLE[(1, 0x33 + _n)] = (f"O2 S{_n} WR Current", f"o2S{_n}WRCurrentChanged",
                                 f"O2_S{_n}_WR_CURRENT", _wr_o2_current, 4)

# Pseudo-PID for python-obd's ELM_VOLTAGE: the adapter's own "ATRV" command,
# answered with text such as "12.6V" — see parse_elm_voltage(). Mirrors
# kElmVoltageKey in the C++ header.
ELM_VOLTAGE_KEY = (0, 0)
ELM_VOLTAGE_REQUEST = b"ATRV\r"
PID_TABLE[ELM_VOLTAGE_KEY] = ("ELM Voltage", "elmVoltageChanged", "ELM_VOLTAGE", None, 0)


# Default PIDs to poll (most commonly supported)
DEFAULT_PIDS = [
    (1, 0x0C),  # RPM
    (1, 0x0D),  # Speed
    (1, 0x05),  # Coolant Temp
    (1, 0x04),  # Engine Load
    (1, 0x11),  # Throttle Position
    (1, 0x0F),  # Intake Air Temp
    (1, 0x0B),  # Intake Manifold Pressure
    (1, 0x42),  # Control Module Voltage
    (1, 0x2F),  # Fuel Level
    (1, 0x10),  # MAF
    (1, 0x0E),  # Timing Advance
    (1, 0x06),  # Short Fuel Trim 1
    (1, 0x07),  # Long Fuel Trim 1
    (1, 0x14),  # O2 B1S1
    (1, 0x0A),  # Fuel Pressure
    (1, 0x5C),  # Oil Temp
]


def format_pid_request(mode, pid):
    """Format an OBD-II PID request. Returns bytes."""
    return f"{mode:02X}{pid:02X}\r".encode()


def parse_elm_voltage(raw):
    """Parse an ATRV reply ("12.6V", "12.6") into volts; None if not a voltage.

    Same rule as python-obd's elm_voltage(): lower-case, strip the 'v', float().
    """
    text = raw.strip().lower().rstrip("v").strip()
    try:
        return float(text)
    except ValueError:
        logger.debug(f"unparseable ATRV reply: {raw.strip()[:200]}")
        return None


def supported_command_names(supported_pids):
    """python-obd command names of every table entry whose mode-01 PID is in
    ``supported_pids``. ELM_VOLTAGE is always included (a python-obd base command)."""
    names = [entry[2] for key, entry in PID_TABLE.items()
             if key == ELM_VOLTAGE_KEY or (key[0] == 1 and key[1] in supported_pids)]
    return sorted(names)


def parse_response(raw):
    """Parse a raw ELM327 response string.

    Returns (mode, pid, data_bytes) or None if invalid/error.
    """
    # Clean the response
    line = raw.strip()
    for err in ERROR_RESPONSES:
        if err in line.upper():
            logger.debug(f"adapter error response: {line[:200]}")
            return None

    # Remove any whitespace
    line = line.replace(" ", "")

    # Response format: 41 0C 1A F8 (mode+0x40, pid, data...)
    try:
        raw_bytes = bytes.fromhex(line)
    except ValueError:
        logger.debug(f"unparseable response: {raw.strip()[:200]}")
        return None

    if len(raw_bytes) < 2:
        logger.debug(f"unparseable response: {raw.strip()[:200]}")
        return None

    resp_mode = raw_bytes[0]  # e.g., 0x41 for mode 01 response
    pid = raw_bytes[1]
    data = list(raw_bytes[2:])

    return (resp_mode - 0x40, pid, data)


def decode_pid(mode, pid, data_bytes):
    """Decode a PID response into (signal_name, value).

    Returns (signal_name, float_value) or None if PID unknown.
    """
    key = (mode, pid)
    if key not in PID_TABLE:
        return None

    name, signal_name, _command, decoder, expected_bytes = PID_TABLE[key]
    if decoder is None:
        return None  # ELM_VOLTAGE_KEY: text reply, see parse_elm_voltage()
    if len(data_bytes) < expected_bytes:
        logger.warning(f"short response for {signal_name} (mode {mode:02X} pid {pid:02X}): "
                       f"{len(data_bytes)} of {expected_bytes} bytes")
        return None

    try:
        value = decoder(data_bytes)
        return (signal_name, value)
    except Exception:
        logger.exception(f"decode failed for {signal_name}")
        return None


def parse_supported_pids(data_bytes):
    """Parse a Mode 01 PID 00/20/40/60 response (supported PIDs bitmap).

    Returns a set of supported PID numbers.
    """
    supported = set()
    if len(data_bytes) < 4:
        return supported

    bitmap = (data_bytes[0] << 24) | (data_bytes[1] << 16) | (data_bytes[2] << 8) | data_bytes[3]
    for i in range(32):
        if bitmap & (1 << (31 - i)):
            supported.add(i + 1)  # PIDs are 1-indexed relative to the base
    return supported


def parse_dtc_response(raw):
    """Parse Mode 03 (GET_DTC) response.

    Returns list of DTC code strings, e.g., ["P0301", "P0420"]
    """
    codes = []
    line = raw.strip().replace(" ", "")

    # Remove the mode byte (43 for mode 03 response)
    if line.startswith("43"):
        line = line[2:]

    # Each DTC is 2 bytes (4 hex chars)
    for i in range(0, len(line) - 3, 4):
        chunk = line[i:i+4]
        if chunk == "0000":
            continue
        try:
            byte1 = int(chunk[0:2], 16)
            byte2 = int(chunk[2:4], 16)
            prefix = DTC_PREFIXES.get((byte1 >> 6) & 0x03, "P")
            digit2 = (byte1 >> 4) & 0x03
            digit3 = byte1 & 0x0F
            digit4 = (byte2 >> 4) & 0x0F
            digit5 = byte2 & 0x0F
            code = f"{prefix}{digit2}{digit3:X}{digit4:X}{digit5:X}"
            codes.append(code)
        except (ValueError, IndexError):
            continue

    return codes


class ResponseBuffer:
    """Accumulates bytes from the ELM327 until a complete response is available."""

    def __init__(self):
        self._buffer = b""

    def feed(self, data):
        """Add incoming bytes to the buffer."""
        self._buffer += data
        # Safety: if buffer grows huge without a '>' prompt, it's corrupted
        if len(self._buffer) > 4096:
            self._buffer = self._buffer[-1024:]

    def get_response(self):
        """Extract a complete response (up to '>' prompt).

        Returns the response string (without prompt) or None if incomplete.
        """
        idx = self._buffer.find(b">")
        if idx == -1:
            return None

        response = self._buffer[:idx].decode("ascii", errors="ignore").strip()
        self._buffer = self._buffer[idx + 1:]

        # Filter out echo and empty lines, return the data line
        lines = [l.strip() for l in response.split("\r") if l.strip()]
        # Return the last meaningful line (skip echo, prompts)
        for line in reversed(lines):
            if line and not line.startswith("AT") and line != "OK":
                return line
        return lines[-1] if lines else ""

    def clear(self):
        self._buffer = b""
