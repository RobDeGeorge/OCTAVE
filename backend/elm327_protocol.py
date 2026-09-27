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

import datetime
import math
import re

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

def _evap_pressure_alt(a):
    # PID 0x54: unscaled Pa, offset by 32767 (J1979; matches python-obd)
    return (a[0] * 256.0 + a[1]) - 32767.0

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
    (1, 0x54): ("Evap Vapor Pressure Alt", "evapVaporPressureAltChanged", "EVAP_VAPOR_PRESSURE_ALT", _evap_pressure_alt, 2),
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


_FRAME_RE = re.compile(r"^([0-9A-F]):([0-9A-F]*)$")
_HEX_RE = re.compile(r"^[0-9A-F]+$")


def decode_dtc(byte1, byte2):
    prefix = DTC_PREFIXES.get((byte1 >> 6) & 0x03, "P")
    return f"{prefix}{(byte1 >> 4) & 0x03}{byte1 & 0x0F:X}{(byte2 >> 4) & 0x0F:X}{byte2 & 0x0F:X}"


def split_messages(lines):
    """Split an ELM327 reply (headers off, spaces off) into one hex string per
    ECU message. CAN multi-frame replies ("00A", "0:4304...", "1:...") are
    joined into a single message and trimmed to the announced byte count."""
    messages = []
    multi = ""
    multi_len = -1
    for line in lines:
        line = line.replace(" ", "").upper()
        m = _FRAME_RE.match(line)
        if m:
            multi += m.group(2)
            continue
        if not _HEX_RE.match(line):
            continue  # SEARCHING..., NO DATA, echo, OK
        if len(line) == 3:
            # Byte-count header of a CAN multi-frame reply
            if multi:
                messages.append(multi[:multi_len * 2] if multi_len > 0 else multi)
                multi = ""
            multi_len = int(line, 16)
            continue
        messages.append(line)
    if multi:
        messages.append(multi[:multi_len * 2] if multi_len > 0 else multi)
    return messages


def parse_dtc_response(lines, mode=0x03):
    """Parse a Mode 03 / 07 / 0A DTC reply (every line of it, headers off).

    Handles CAN (count byte, multi-frame) and pre-CAN replies, several ECUs.
    Returns list of DTC code strings, e.g., ["P0301", "P0420"]
    """
    if isinstance(lines, str):
        lines = lines.splitlines()
    response_mode = f"{mode + 0x40:02X}"
    codes = []
    for msg in split_messages(lines):
        if not msg.startswith(response_mode):
            continue
        data = msg[2:]
        # Pre-CAN protocols always send 6 data bytes (3 DTC slots, zero
        # padded). ISO 15765 (CAN) puts a DTC-count byte first, which makes
        # the data an odd number of bytes; drop it so it isn't read as a code.
        if (len(data) // 2) % 2 == 1:
            data = data[2:]
        for i in range(0, len(data) - 3, 4):
            byte1 = int(data[i:i + 2], 16)
            byte2 = int(data[i + 2:i + 4], 16)
            if byte1 == 0 and byte2 == 0:
                continue
            code = decode_dtc(byte1, byte2)
            if code not in codes:  # several ECUs can report the same code
                codes.append(code)
    return codes


def parse_freeze_frame_dtc(lines):
    """Parse the Mode 02 PID 02 reply: the DTC that stored freeze frame 0
    (42 02 00 <DTC hi> <DTC lo>)."""
    if isinstance(lines, str):
        lines = lines.splitlines()
    codes = []
    for msg in split_messages(lines):
        if not msg.startswith("4202") or len(msg) < 10:
            continue
        byte1 = int(msg[6:8], 16)
        byte2 = int(msg[8:10], 16)
        if byte1 == 0 and byte2 == 0:
            continue
        code = decode_dtc(byte1, byte2)
        if code not in codes:
            codes.append(code)
    return codes


# ---------------------------------------------------------------------------
# Multi-value PIDs (SAE J1979 Mode 01 above 0x5E). Byte layouts and scaling
# cross-checked against the OBD-II PIDs table on Wikipedia and OBDb's SAEJ1979
# signal set. Index 0 is data byte A. support_bit is the bit of A (0 = LSB)
# the ECU sets when that value is present, or None.
# Parity twin of ELM327Protocol::extendedPidTable() in C++; the running Python
# app wraps these decoders in python-obd commands (obd_manager.py).
# ---------------------------------------------------------------------------

def _u16(a, i):
    return a[i] * 256.0 + a[i + 1]


def _s16(a, i):
    v = a[i] * 256 + a[i + 1]
    return float(v - 65536 if v > 32767 else v)


def _u32(a, i):
    return a[i] * 16777216.0 + a[i + 1] * 65536.0 + a[i + 2] * 256.0 + a[i + 3]


def _r2(v):
    # Half away from zero, like std::round in the C++ twin
    return math.floor(abs(v) * 100.0 + 0.5) / 100.0 * (1 if v >= 0 else -1)


def _xpct(a, i):
    return _r2(a[i] * 100.0 / 255.0)


def _xtemp(a, i):
    return a[i] - 40.0


def _xtorque(a, i):
    return a[i] - 125.0


# (mode, pid): [(param_id, name, support_bit, min_bytes, decoder), ...]
EXTENDED_PID_TABLE = {
    (1, 0x61): [("DEMAND_ENGINE_TORQUE", "Driver Demand Torque", None, 1, lambda a: _xtorque(a, 0))],
    (1, 0x62): [("ACTUAL_ENGINE_TORQUE", "Actual Engine Torque", None, 1, lambda a: _xtorque(a, 0))],
    (1, 0x63): [("REFERENCE_TORQUE", "Engine Reference Torque", None, 2, lambda a: _u16(a, 0))],
    (1, 0x65): [("RECOMMENDED_GEAR", "Recommended Gear", 4, 2, lambda a: float(a[1] >> 4))],
    (1, 0x66): [
        ("MAF_SENSOR_A", "MAF Sensor A", 0, 3, lambda a: _r2(_u16(a, 1) / 32.0)),
        ("MAF_SENSOR_B", "MAF Sensor B", 1, 5, lambda a: _r2(_u16(a, 3) / 32.0)),
    ],
    (1, 0x67): [
        ("COOLANT_TEMP_SENSOR_1", "Coolant Temp Sensor 1", 0, 2, lambda a: _xtemp(a, 1)),
        ("COOLANT_TEMP_SENSOR_2", "Coolant Temp Sensor 2", 1, 3, lambda a: _xtemp(a, 2)),
    ],
    (1, 0x68): [
        ("INTAKE_TEMP_B1S1", "Intake Air Temp B1S1", 0, 2, lambda a: _xtemp(a, 1)),
        ("INTAKE_TEMP_B1S2", "Intake Air Temp B1S2", 1, 3, lambda a: _xtemp(a, 2)),
        ("INTAKE_TEMP_B2S1", "Intake Air Temp B2S1", 3, 5, lambda a: _xtemp(a, 4)),
    ],
    (1, 0x69): [
        ("EGR_A_COMMANDED", "Commanded EGR A", 0, 2, lambda a: _xpct(a, 1)),
        ("EGR_A_ACTUAL", "Actual EGR A", 1, 3, lambda a: _xpct(a, 2)),
        ("EGR_A_ERROR", "EGR A Error", 2, 4, lambda a: _r2(a[3] * 100.0 / 128.0 - 100.0)),
    ],
    (1, 0x6C): [
        ("THROTTLE_ACTUATOR_A_COMMANDED", "Commanded Throttle A", 0, 2, lambda a: _xpct(a, 1)),
        ("RELATIVE_THROTTLE_A", "Relative Throttle A", 1, 3, lambda a: _xpct(a, 2)),
    ],
    (1, 0x6D): [
        ("FUEL_RAIL_PRESSURE_A_COMMANDED", "Commanded Fuel Rail Pressure A", 0, 3, lambda a: _u16(a, 1) * 10.0),
        ("FUEL_RAIL_PRESSURE_A", "Fuel Rail Pressure A", 1, 5, lambda a: _u16(a, 3) * 10.0),
        ("FUEL_RAIL_TEMP_A", "Fuel Rail Temp A", 2, 6, lambda a: _xtemp(a, 5)),
    ],
    (1, 0x70): [
        ("BOOST_PRESSURE_A_COMMANDED", "Commanded Boost A", 0, 3, lambda a: _r2(_u16(a, 1) / 32.0)),
        ("BOOST_PRESSURE_A", "Boost Pressure A", 1, 5, lambda a: _r2(_u16(a, 3) / 32.0)),
    ],
    (1, 0x72): [
        ("WASTEGATE_A_COMMANDED", "Commanded Wastegate A", 0, 2, lambda a: _xpct(a, 1)),
        ("WASTEGATE_A", "Wastegate A Position", 1, 3, lambda a: _xpct(a, 2)),
    ],
    (1, 0x7F): [
        ("ENGINE_RUN_TIME_TOTAL", "Total Engine Run Time", 0, 5, lambda a: _u32(a, 1)),
        ("ENGINE_IDLE_TIME_TOTAL", "Total Idle Time", 1, 9, lambda a: _u32(a, 5)),
    ],
    (1, 0x84): [("MANIFOLD_SURFACE_TEMP", "Manifold Surface Temp", None, 1, lambda a: _xtemp(a, 0))],
    (1, 0x8D): [("THROTTLE_POS_G", "Throttle Position G", None, 1, lambda a: _xpct(a, 0))],
    (1, 0x8E): [("ENGINE_FRICTION_TORQUE", "Engine Friction Torque", None, 1, lambda a: _xtorque(a, 0))],
    (1, 0x9A): [
        ("HYBRID_BATTERY_VOLTAGE", "Hybrid Battery Voltage", 1, 4, lambda a: _r2(_u16(a, 2) / 64.0)),
        ("HYBRID_BATTERY_CURRENT", "Hybrid Battery Current", 2, 6, lambda a: _r2(_s16(a, 4) / 10.0)),
    ],
    (1, 0x9D): [
        ("ENGINE_FUEL_RATE_GS", "Engine Fuel Rate", None, 2, lambda a: _r2(_u16(a, 0) / 50.0)),
        ("VEHICLE_FUEL_RATE_GS", "Vehicle Fuel Rate", None, 4, lambda a: _r2(_u16(a, 2) / 50.0)),
    ],
    (1, 0x9E): [("EXHAUST_FLOW_RATE", "Exhaust Flow Rate", None, 2, lambda a: _r2(_u16(a, 0) / 5.0))],
    (1, 0xA2): [("CYLINDER_FUEL_RATE", "Cylinder Fuel Rate", None, 2, lambda a: _r2(_u16(a, 0) / 32.0))],
    (1, 0xA4): [("TRANSMISSION_GEAR_RATIO", "Transmission Gear Ratio", 1, 4, lambda a: _u16(a, 2) / 1000.0)],
    (1, 0xA6): [("ODOMETER", "Odometer", None, 4, lambda a: _u32(a, 0) / 10.0)],
    (1, 0xB2): [("EV_BATTERY_HEALTH", "EV Battery Health", None, 1, lambda a: _xpct(a, 0))],
}


def decode_extended_pid(mode, pid, data_bytes):
    """Decode a multi-value PID into [(param_id, value), ...]; values whose
    "supported" bit is clear or whose bytes are missing are left out."""
    values = []
    for param_id, _name, support_bit, min_bytes, decoder in EXTENDED_PID_TABLE.get((mode, pid), []):
        if len(data_bytes) < min_bytes:
            continue
        if support_bit is not None and not (data_bytes and data_bytes[0] & (1 << support_bit)):
            continue
        values.append((param_id, decoder(data_bytes)))
    return values


def parse_response_lines(lines, mode, pid):
    """Parse every line of a reply (joining CAN multi-frame answers) and
    return (mode, pid, data_bytes) for the first message answering mode/pid."""
    prefix = f"{mode + 0x40:02X}{pid:02X}"
    for msg in split_messages(lines):
        if msg.startswith(prefix):
            raw = bytes.fromhex(msg)
            return (raw[0] - 0x40, raw[1], list(raw[2:]))
    return None


def parse_vin(lines):
    """Parse a Mode 09 PID 02 reply into the 17-character VIN ("" if none).

    CAN: one multi-frame message "4902 01 <17 bytes>" (01 = item count).
    Pre-CAN: five messages "4902 <seq> <4 bytes>", the first left-padded with
    00. Either way one byte follows "4902" before the characters."""
    if isinstance(lines, str):
        lines = lines.splitlines()
    parts = {}
    for msg in split_messages(lines):
        if not msg.startswith("4902") or len(msg) < 6:
            continue
        try:
            raw = bytes.fromhex(msg[4:])
        except ValueError:
            continue
        if raw and raw[0] not in parts:
            parts[raw[0]] = raw[1:]
    text = "".join(chr(c).upper() for seq in sorted(parts) for c in parts[seq] if chr(c).isalnum() and c < 128)
    return text[-17:] if len(text) >= 17 else ""


_VIN_RE = re.compile(r"^[A-HJ-NPR-Z0-9]{17}$")
_VIN_YEAR_CODES = "ABCDEFGHJKLMNPRSTVWXY123456789"

# World manufacturer identifiers (same table as ELM327Protocol::decodeVin)
_WMI3 = {
    "1J4": "Jeep", "1J8": "Jeep", "1C4": "Chrysler/Dodge/Jeep", "1C3": "Chrysler/Dodge",
    "1C6": "Ram", "3C6": "Ram", "3C7": "Ram", "1D7": "Dodge", "1B3": "Dodge", "1B7": "Dodge",
    "2C3": "Chrysler/Dodge", "2C4": "Chrysler/Dodge", "3C4": "Chrysler/Dodge/Jeep",
    "1G1": "Chevrolet", "1GC": "Chevrolet", "1GN": "Chevrolet", "1GB": "Chevrolet",
    "2G1": "Chevrolet", "3GN": "Chevrolet", "3GC": "Chevrolet", "1GT": "GMC", "1GK": "GMC",
    "3GT": "GMC", "1G6": "Cadillac", "1G4": "Buick", "1G2": "Pontiac", "1FA": "Ford",
    "1FB": "Ford", "1FC": "Ford", "1FD": "Ford", "1FM": "Ford", "1FT": "Ford", "2FM": "Ford",
    "3FA": "Ford", "1LN": "Lincoln", "5LM": "Lincoln", "1ME": "Mercury", "1HG": "Honda",
    "2HG": "Honda", "5FN": "Honda", "5J6": "Honda", "19U": "Acura", "5J8": "Acura", "JH4": "Acura",
    "1N4": "Nissan", "1N6": "Nissan", "5N1": "Nissan", "3N1": "Nissan", "JN1": "Nissan",
    "JN8": "Nissan", "4T1": "Toyota", "4T3": "Toyota", "5TD": "Toyota", "5TF": "Toyota",
    "2T1": "Toyota", "2T3": "Toyota", "JTD": "Toyota", "JTE": "Toyota", "JTM": "Toyota",
    "JTN": "Toyota", "JTH": "Lexus", "JTJ": "Lexus", "2T2": "Lexus", "4S3": "Subaru",
    "4S4": "Subaru", "JF1": "Subaru", "JF2": "Subaru", "JM1": "Mazda", "JM3": "Mazda",
    "5NP": "Hyundai", "5NM": "Hyundai", "KMH": "Hyundai", "KM8": "Hyundai", "5XY": "Kia",
    "5XX": "Kia", "KNA": "Kia", "KND": "Kia", "5YJ": "Tesla", "7SA": "Tesla", "LRW": "Tesla",
    "WBA": "BMW", "WBS": "BMW M", "5UX": "BMW", "4US": "BMW", "WMW": "MINI",
    "WDD": "Mercedes-Benz", "WDB": "Mercedes-Benz", "W1K": "Mercedes-Benz", "W1N": "Mercedes-Benz",
    "4JG": "Mercedes-Benz", "WVW": "Volkswagen", "WV1": "Volkswagen", "WV2": "Volkswagen",
    "3VW": "Volkswagen", "1VW": "Volkswagen", "WAU": "Audi", "WA1": "Audi", "WP0": "Porsche",
    "WP1": "Porsche", "SAJ": "Jaguar", "SAL": "Land Rover", "YV1": "Volvo", "YV4": "Volvo",
    "ZFF": "Ferrari", "ZAR": "Alfa Romeo", "ZFA": "Fiat", "3C3": "Fiat", "ZHW": "Lamborghini",
    "JA3": "Mitsubishi", "JA4": "Mitsubishi", "ML3": "Mitsubishi", "JS1": "Suzuki",
    "JS2": "Suzuki", "1HD": "Harley-Davidson", "VF1": "Renault", "VF3": "Peugeot",
    "VF7": "Citroen", "SCC": "Lotus", "SCF": "Aston Martin", "1YV": "Mazda", "4F2": "Mazda",
}
_WMI2 = {
    "1G": "General Motors", "2G": "General Motors", "3G": "General Motors", "1F": "Ford",
    "2F": "Ford", "3F": "Ford", "1C": "Chrysler", "2C": "Chrysler", "3C": "Chrysler", "1J": "Jeep",
    "1D": "Dodge", "2D": "Dodge", "3D": "Dodge", "1H": "Honda", "2H": "Honda", "JH": "Honda",
    "1N": "Nissan", "JN": "Nissan", "JT": "Toyota", "4T": "Toyota", "5T": "Toyota", "JM": "Mazda",
    "JF": "Subaru", "JS": "Suzuki", "KM": "Hyundai", "KN": "Kia", "WB": "BMW",
    "WD": "Mercedes-Benz", "WV": "Volkswagen", "WA": "Audi", "WP": "Porsche", "YV": "Volvo",
    "SA": "Jaguar/Land Rover",
}


def decode_vin(vin):
    """Return {"vin", "make", "modelYear"} for a VIN ("" / 0 when unknown)."""
    info = {"vin": "", "make": "", "modelYear": 0}
    vin = (vin or "").strip().upper()
    if not _VIN_RE.match(vin):
        return info
    info["vin"] = vin
    info["make"] = _WMI3.get(vin[:3], _WMI2.get(vin[:2], ""))
    # Position 10: a 30-year cycle. Position 7 alphabetic marks the 2010+
    # cycle (North America); a year in the future falls back 30 years.
    idx = _VIN_YEAR_CODES.find(vin[9])
    if idx >= 0:
        year = 1980 + idx + (30 if vin[6].isalpha() else 0)
        max_year = datetime.date.today().year + 1
        while year > max_year:
            year -= 30
        info["modelYear"] = year
    return info


class ResponseBuffer:
    """Accumulates bytes from the ELM327 until a complete response is available."""

    def __init__(self):
        self._buffer = b""
        # Every non-empty line of the last response get_response() returned
        # (multi-line replies: several ECUs, CAN multi-frame DTC lists)
        self.last_lines = []

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
        self.last_lines = lines
        # Return the last meaningful line (skip echo, prompts)
        for line in reversed(lines):
            if line and not line.startswith("AT") and line != "OK":
                return line
        return lines[-1] if lines else ""

    def clear(self):
        self._buffer = b""
        self.last_lines = []
