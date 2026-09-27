#include "elm327protocol.h"

#include <QHash>
#include <QRegularExpression>
#include <cctype>
#include <QMap>
#include <QDate>

#include <cmath>
#include <exception>

// Protocol transcript and parse failures; TX/RX lines come from the OBD worker
Q_LOGGING_CATEGORY(lcElm327, "octave.elm327")

// ===========================================================================
// Decoder functions
// ===========================================================================

static double decSimple(const QVector<uint8_t> &a) {
    return static_cast<double>(a[0]);
}

static double decOffset40(const QVector<uint8_t> &a) {
    return static_cast<double>(a[0]) - 40.0;
}

static double decPct(const QVector<uint8_t> &a) {
    return std::round(a[0] * 100.0 / 255.0 * 10.0) / 10.0;
}

static double decPctCentered(const QVector<uint8_t> &a) {
    return std::round((static_cast<double>(a[0]) - 128.0) * 100.0 / 128.0 * 10.0) / 10.0;
}

static double decRpm(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 4.0 * 10.0) / 10.0;
}

static double decKpa(const QVector<uint8_t> &a) {
    return static_cast<double>(a[0]);
}

static double decFuelPressure(const QVector<uint8_t> &a) {
    return static_cast<double>(a[0] * 3);
}

static double decTiming(const QVector<uint8_t> &a) {
    return std::round((a[0] / 2.0 - 64.0) * 10.0) / 10.0;
}

static double decMaf(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 100.0 * 100.0) / 100.0;
}

static double decO2Voltage(const QVector<uint8_t> &a) {
    return std::round(a[0] / 200.0 * 1000.0) / 1000.0;
}

static double decFuelRailVac(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) * 0.079 * 10.0) / 10.0;
}

static double decFuelRailDirect(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) * 10.0 * 10.0) / 10.0;
}

static double decEvapPressure(const QVector<uint8_t> &a) {
    int val = a[0] * 256 + a[1];
    if (val > 32767) val -= 65536;
    return std::round(val / 4.0 * 10.0) / 10.0;
}

// PID 0x54: unscaled Pa, offset by 32767 (J1979; matches python-obd)
static double decEvapPressureAlt(const QVector<uint8_t> &a) {
    return (a[0] * 256.0 + a[1]) - 32767.0;
}

static double decCatalystTemp(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 10.0 - 40.0);  // round to 0.1
}

static double decVoltage(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 1000.0 * 100.0) / 100.0;
}

// Commanded equivalence ratio (lambda, ~1.0 at stoich) scaled to a gasoline
// air/fuel ratio, matching the Python backend (`_update_afr` multiplies
// python-obd's ratio by 14.7). OBDParameterModel's AFR gauge spans 10-18.
static double decAirFuelRatio(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 32768.0 * 14.7 * 100.0) / 100.0;
}

static double decUint16(const QVector<uint8_t> &a) {
    return static_cast<double>(a[0] * 256 + a[1]);
}

static double decFuelRate(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 20.0 * 100.0) / 100.0;
}

static double decInjectTiming(const QVector<uint8_t> &a) {
    return std::round(((a[0] * 256.0 + a[1]) / 128.0 - 210.0) * 100.0) / 100.0;
}

static double decAbsEvap(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) / 200.0 * 100.0) / 100.0;
}

// Wide-range O2 (PIDs 0x24-0x2B / 0x34-0x3B): bytes A,B carry the lambda
// ratio; the voltage / current live in bytes C,D. Same formulas as
// python-obd's sensor_voltage_big and current_centered.
static double decWrO2Voltage(const QVector<uint8_t> &a) {
    return std::round((a[2] * 256.0 + a[3]) * 8.0 / 65535.0 * 10000.0) / 10000.0;
}

static double decWrO2Current(const QVector<uint8_t> &a) {
    return std::round(((a[2] * 256.0 + a[3]) / 256.0 - 128.0) * 10000.0) / 10000.0;
}

static double decFuelRailAbs(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) * 10.0 * 10.0) / 10.0;
}

static double decSpeedMPH(const QVector<uint8_t> &a) {
    return std::round(a[0] * 0.621371 * 10.0) / 10.0;  // km/h to mph
}

static double decAbsoluteLoad(const QVector<uint8_t> &a) {
    return std::round((a[0] * 256.0 + a[1]) * 100.0 / 255.0 * 10.0) / 10.0;
}

static double decMaxMAF(const QVector<uint8_t> &a) {
    return static_cast<double>(a[0] * 10);
}

// ===========================================================================
// PID Table (built once, cached)
// ===========================================================================

QHash<PidKey, PidEntry> ELM327Protocol::buildPidTable()
{
    // PID numbers, command names and decoders follow python-obd's
    // commands.py (the Python backend's authoritative table) so both
    // backends read the same bytes into the same signal.
    QHash<PidKey, PidEntry> t;

    // --- Default enabled PIDs (the 16+1 most common) ---
    t[{1, 0x05}] = {"Coolant Temp",           "coolantTempChanged",             "COOLANT_TEMP",              decOffset40,       1};
    t[{1, 0x42}] = {"Control Module Voltage",  "voltageChanged",                "CONTROL_MODULE_VOLTAGE",    decVoltage,        2};
    t[{1, 0x04}] = {"Engine Load",             "engineLoadChanged",             "ENGINE_LOAD",               decPct,            1};
    t[{1, 0x11}] = {"Throttle Position",       "throttlePositionChanged",       "THROTTLE_POS",              decPct,            1};
    t[{1, 0x0F}] = {"Intake Air Temp",         "intakeAirTempChanged",          "INTAKE_TEMP",               decOffset40,       1};
    t[{1, 0x0E}] = {"Timing Advance",          "timingAdvanceChanged",          "TIMING_ADVANCE",            decTiming,         1};
    t[{1, 0x10}] = {"MAF",                     "massAirFlowChanged",            "MAF",                       decMaf,            2};
    t[{1, 0x0D}] = {"Speed",                   "speedMPHChanged",               "SPEED",                     decSpeedMPH,       1};
    t[{1, 0x0C}] = {"RPM",                     "rpmChanged",                    "RPM",                       decRpm,            2};
    t[{1, 0x44}] = {"Air/Fuel Ratio",          "airFuelRatioChanged",           "COMMANDED_EQUIV_RATIO",     decAirFuelRatio,   2};
    t[{1, 0x2F}] = {"Fuel Level",              "fuelLevelChanged",              "FUEL_LEVEL",                decPct,            1};
    t[{1, 0x0B}] = {"Intake Manifold Pressure","intakeManifoldPressureChanged", "INTAKE_PRESSURE",           decKpa,            1};
    t[{1, 0x06}] = {"Short Term Fuel Trim 1",  "shortTermFuelTrimChanged",      "SHORT_FUEL_TRIM_1",         decPctCentered,    1};
    t[{1, 0x07}] = {"Long Term Fuel Trim 1",   "longTermFuelTrimChanged",       "LONG_FUEL_TRIM_1",          decPctCentered,    1};
    t[{1, 0x14}] = {"O2 Sensor B1S1",          "oxygenSensorVoltageChanged",    "O2_B1S1",                   decO2Voltage,      2};
    t[{1, 0x0A}] = {"Fuel Pressure",           "fuelPressureChanged",           "FUEL_PRESSURE",             decFuelPressure,   1};
    t[{1, 0x5C}] = {"Oil Temp",                "engineOilTempChanged",          "OIL_TEMP",                  decOffset40,       1};

    // --- Extended PIDs ---
    t[{1, 0x1F}] = {"Run Time",                "runTimeChanged",                "RUN_TIME",                  decUint16,         2};
    t[{1, 0x21}] = {"Distance w/ MIL",         "distanceWithMILChanged",        "DISTANCE_W_MIL",            decUint16,         2};
    t[{1, 0x22}] = {"Fuel Rail Pressure (vac)", "fuelRailPressureChanged",      "FUEL_RAIL_PRESSURE_VAC",    decFuelRailVac,    2};
    t[{1, 0x23}] = {"Fuel Rail Pressure (direct)","fuelRailPressureDirectChanged","FUEL_RAIL_PRESSURE_DIRECT",decFuelRailDirect, 2};
    t[{1, 0x33}] = {"Barometric Pressure",     "barometricPressureChanged",     "BAROMETRIC_PRESSURE",       decKpa,            1};
    t[{1, 0x46}] = {"Ambient Air Temp",        "ambientAirTempChanged",         "AMBIANT_AIR_TEMP",          decOffset40,       1};
    t[{1, 0x45}] = {"Relative Throttle Pos",   "relativeThrottlePosChanged",    "RELATIVE_THROTTLE_POS",     decPct,            1};
    t[{1, 0x47}] = {"Throttle Pos B",          "absoluteThrottlePosBChanged",   "THROTTLE_POS_B",            decPct,            1};
    t[{1, 0x49}] = {"Accelerator Pos D",       "acceleratorPosChanged",         "ACCELERATOR_POS_D",         decPct,            1};
    t[{1, 0x3C}] = {"Catalyst Temp B1S1",      "catalystTempB1S1Changed",       "CATALYST_TEMP_B1S1",        decCatalystTemp,   2};
    t[{1, 0x3E}] = {"Catalyst Temp B1S2",      "catalystTempB1S2Changed",       "CATALYST_TEMP_B1S2",        decCatalystTemp,   2};
    t[{1, 0x32}] = {"Evap Vapor Pressure",     "evapVaporPressureChanged",      "EVAP_VAPOR_PRESSURE",       decEvapPressure,   2};
    t[{1, 0x08}] = {"Short Fuel Trim 2",       "shortFuelTrim2Changed",         "SHORT_FUEL_TRIM_2",         decPctCentered,    1};
    t[{1, 0x09}] = {"Long Fuel Trim 2",        "longFuelTrim2Changed",          "LONG_FUEL_TRIM_2",          decPctCentered,    1};
    t[{1, 0x15}] = {"O2 B1S2",                 "o2SensorB1S2Changed",           "O2_B1S2",                   decO2Voltage,      2};
    t[{1, 0x18}] = {"O2 B2S1",                 "o2SensorB2S1Changed",           "O2_B2S1",                   decO2Voltage,      2};
    t[{1, 0x19}] = {"O2 B2S2",                 "o2SensorB2S2Changed",           "O2_B2S2",                   decO2Voltage,      2};
    t[{1, 0x31}] = {"Distance Since Codes Cleared","distanceSinceCodesCleared", "DISTANCE_SINCE_DTC_CLEAR",  decUint16,         2};
    t[{1, 0x30}] = {"Warmups Since Codes Cleared","warmupsSinceCodesCleared",   "WARMUPS_SINCE_DTC_CLEAR",   decSimple,         1};
    t[{1, 0x43}] = {"Absolute Load",           "absoluteLoadChanged",           "ABSOLUTE_LOAD",             decAbsoluteLoad,   2};
    t[{1, 0x2C}] = {"Commanded EGR",           "commandedEGRChanged",           "COMMANDED_EGR",             decPct,            1};
    t[{1, 0x2D}] = {"EGR Error",               "egrErrorChanged",               "EGR_ERROR",                 decPctCentered,    1};
    t[{1, 0x52}] = {"Ethanol Percent",         "ethanoPercentChanged",          "ETHANOL_PERCENT",           decPct,            1};

    // --- Additional narrowband O2 sensors (0x14-0x1B: B1S1..B1S4, B2S1..B2S4) ---
    t[{1, 0x16}] = {"O2 B1S3",                 "o2SensorB1S3Changed",           "O2_B1S3",                   decO2Voltage,      2};
    t[{1, 0x17}] = {"O2 B1S4",                 "o2SensorB1S4Changed",           "O2_B1S4",                   decO2Voltage,      2};
    t[{1, 0x1A}] = {"O2 B2S3",                 "o2SensorB2S3Changed",           "O2_B2S3",                   decO2Voltage,      2};
    t[{1, 0x1B}] = {"O2 B2S4",                 "o2SensorB2S4Changed",           "O2_B2S4",                   decO2Voltage,      2};

    // --- Wide-range O2 sensors: voltage (0x24-0x2B), current (0x34-0x3B) ---
    for (int n = 1; n <= 8; ++n) {
        t[{1, 0x23 + n}] = {QStringLiteral("O2 S%1 WR Voltage").arg(n),
                            QStringLiteral("o2S%1WRVoltageChanged").arg(n),
                            QStringLiteral("O2_S%1_WR_VOLTAGE").arg(n),
                            decWrO2Voltage, 4};
        t[{1, 0x33 + n}] = {QStringLiteral("O2 S%1 WR Current").arg(n),
                            QStringLiteral("o2S%1WRCurrentChanged").arg(n),
                            QStringLiteral("O2_S%1_WR_CURRENT").arg(n),
                            decWrO2Current, 4};
    }

    t[{1, 0x3D}] = {"Catalyst Temp B2S1",      "catalystTempB2S1Changed",       "CATALYST_TEMP_B2S1",        decCatalystTemp,   2};
    t[{1, 0x3F}] = {"Catalyst Temp B2S2",      "catalystTempB2S2Changed",       "CATALYST_TEMP_B2S2",        decCatalystTemp,   2};
    t[{1, 0x48}] = {"Throttle Pos C",          "throttlePosCChanged",           "THROTTLE_POS_C",            decPct,            1};
    t[{1, 0x4A}] = {"Accelerator Pos E",       "acceleratorPosEChanged",        "ACCELERATOR_POS_E",         decPct,            1};
    t[{1, 0x4B}] = {"Accelerator Pos F",       "acceleratorPosFChanged",        "ACCELERATOR_POS_F",         decPct,            1};
    t[{1, 0x4C}] = {"Throttle Actuator",       "throttleActuatorChanged",       "THROTTLE_ACTUATOR",         decPct,            1};
    t[{1, 0x4D}] = {"Run Time MIL",            "runTimeMILChanged",             "RUN_TIME_MIL",              decUint16,         2};
    t[{1, 0x4E}] = {"Time Since DTC Cleared",  "timeSinceDTCClearedChanged",    "TIME_SINCE_DTC_CLEARED",    decUint16,         2};
    t[{1, 0x50}] = {"Max MAF",                 "maxMAFChanged",                 "MAX_MAF",                   decMaxMAF,         1};
    t[{1, 0x51}] = {"Fuel Type",               "fuelTypeChanged",               "FUEL_TYPE",                 decSimple,         1};
    t[{1, 0x53}] = {"Evap Vapor Pressure Abs", "evapVaporPressureAbsChanged",   "EVAP_VAPOR_PRESSURE_ABS",   decAbsEvap,        2};
    t[{1, 0x54}] = {"Evap Vapor Pressure Alt", "evapVaporPressureAltChanged",   "EVAP_VAPOR_PRESSURE_ALT",   decEvapPressureAlt, 2};
    t[{1, 0x55}] = {"Short O2 Trim B1",        "shortO2TrimB1Changed",          "SHORT_O2_TRIM_B1",          decPctCentered,    1};
    t[{1, 0x56}] = {"Long O2 Trim B1",         "longO2TrimB1Changed",           "LONG_O2_TRIM_B1",           decPctCentered,    1};
    t[{1, 0x57}] = {"Short O2 Trim B2",        "shortO2TrimB2Changed",          "SHORT_O2_TRIM_B2",          decPctCentered,    1};
    t[{1, 0x58}] = {"Long O2 Trim B2",         "longO2TrimB2Changed",           "LONG_O2_TRIM_B2",           decPctCentered,    1};
    t[{1, 0x59}] = {"Fuel Rail Pressure Abs",  "fuelRailPressureAbsChanged",    "FUEL_RAIL_PRESSURE_ABS",    decFuelRailAbs,    2};
    t[{1, 0x5A}] = {"Relative Accel Pos",      "relativeAccelPosChanged",       "RELATIVE_ACCEL_POS",        decPct,            1};
    t[{1, 0x5B}] = {"Hybrid Battery",          "hybridBatteryRemainingChanged", "HYBRID_BATTERY_REMAINING",  decPct,            1};
    t[{1, 0x2E}] = {"Evaporative Purge",       "evaporativePurgeChanged",       "EVAPORATIVE_PURGE",         decPct,            1};
    t[{1, 0x5D}] = {"Fuel Inject Timing",      "fuelInjectTimingChanged",       "FUEL_INJECT_TIMING",        decInjectTiming,   2};
    t[{1, 0x5E}] = {"Fuel Rate",               "fuelRateChanged",               "FUEL_RATE",                 decFuelRate,       2};

    // --- Adapter voltage ("ATRV"), decoded from text by parseElmVoltage() ---
    t[kElmVoltageKey] = {"ELM Voltage",         "elmVoltageChanged",             "ELM_VOLTAGE",               nullptr,           0};

    return t;
}

// ===========================================================================
// Static accessors
// ===========================================================================

const QHash<PidKey, PidEntry> &ELM327Protocol::pidTable()
{
    static const QHash<PidKey, PidEntry> table = buildPidTable();
    return table;
}

QList<InitCommand> ELM327Protocol::initCommands()
{
    return {
        {QByteArrayLiteral("ATZ\r"),   1500},
        {QByteArrayLiteral("ATE0\r"),   500},
        {QByteArrayLiteral("ATL0\r"),   300},
        {QByteArrayLiteral("ATS0\r"),   300},
        {QByteArrayLiteral("ATH0\r"),   300},
        {QByteArrayLiteral("ATAT2\r"),  300},
        {QByteArrayLiteral("ATSP0\r"),  500},
    };
}

const QStringList &ELM327Protocol::errorResponses()
{
    static const QStringList errors = {
        QStringLiteral("NO DATA"),
        QStringLiteral("UNABLE TO CONNECT"),
        QStringLiteral("ERROR"),
        QStringLiteral("?"),
        QStringLiteral("STOPPED"),
        QStringLiteral("BUS INIT: ...ERROR"),
    };
    return errors;
}

QByteArray ELM327Protocol::formatPidRequest(int mode, int pid)
{
    return QStringLiteral("%1%2\r")
        .arg(mode, 2, 16, QLatin1Char('0'))
        .arg(pid, 2, 16, QLatin1Char('0'))
        .toUpper()
        .toLatin1();
}

std::optional<ParsedResponse> ELM327Protocol::parseResponse(const QString &raw)
{
    QString line = raw.trimmed();
    const QString upper = line.toUpper();

    for (const QString &err : errorResponses()) {
        if (upper.contains(err)) {
            qCDebug(lcElm327) << "adapter error response:" << line.left(200);
            return std::nullopt;
        }
    }

    // Remove whitespace
    line.remove(QLatin1Char(' '));

    // Response format: 41 0C 1A F8 (mode+0x40, pid, data...)
    QByteArray hexBytes = QByteArray::fromHex(line.toLatin1());
    if (hexBytes.size() < 2) {
        qCDebug(lcElm327) << "unparseable response:" << raw.trimmed().left(200);
        return std::nullopt;
    }

    int respMode = static_cast<uint8_t>(hexBytes[0]);
    int pid      = static_cast<uint8_t>(hexBytes[1]);

    QVector<uint8_t> data;
    data.reserve(hexBytes.size() - 2);
    for (int i = 2; i < hexBytes.size(); ++i)
        data.append(static_cast<uint8_t>(hexBytes[i]));

    return ParsedResponse{respMode - 0x40, pid, data};
}

std::optional<ParsedResponse> ELM327Protocol::parseResponseLines(const QStringList &lines,
                                                                 int mode, int pid)
{
    const QString prefix = QStringLiteral("%1%2")
                               .arg(mode + 0x40, 2, 16, QLatin1Char('0'))
                               .arg(pid, 2, 16, QLatin1Char('0'))
                               .toUpper();
    for (const QString &msg : splitMessages(lines)) {
        if (msg.startsWith(prefix))
            return parseResponse(msg);
    }
    return std::nullopt;
}

std::optional<DecodedPid> ELM327Protocol::decodePid(int mode, int pid,
                                                     const QVector<uint8_t> &dataBytes)
{
    const auto &table = pidTable();
    auto it = table.constFind({mode, pid});
    if (it == table.constEnd())
        return std::nullopt;

    const PidEntry &entry = it.value();
    if (!entry.decoder)
        return std::nullopt;  // kElmVoltageKey: text reply, see parseElmVoltage()
    if (dataBytes.size() < entry.expectedBytes) {
        qCWarning(lcElm327).nospace() << "short response for " << entry.signalName << " (mode " << Qt::hex << mode
                                      << " pid " << pid << Qt::dec << "): " << dataBytes.size() << " of "
                                      << entry.expectedBytes << " bytes";
        return std::nullopt;
    }

    try {
        double value = entry.decoder(dataBytes);
        return DecodedPid{entry.signalName, value};
    } catch (const std::exception &e) {
        qCWarning(lcElm327) << "decode failed for" << entry.signalName << ":" << e.what();
        return std::nullopt;
    } catch (...) {
        qCWarning(lcElm327) << "decode failed for" << entry.signalName;
        return std::nullopt;
    }
}

QSet<int> ELM327Protocol::parseSupportedPids(const QVector<uint8_t> &dataBytes)
{
    QSet<int> supported;
    if (dataBytes.size() < 4)
        return supported;

    uint32_t bitmap = (static_cast<uint32_t>(dataBytes[0]) << 24)
                    | (static_cast<uint32_t>(dataBytes[1]) << 16)
                    | (static_cast<uint32_t>(dataBytes[2]) <<  8)
                    |  static_cast<uint32_t>(dataBytes[3]);

    for (int i = 0; i < 32; ++i) {
        if (bitmap & (1u << (31 - i)))
            supported.insert(i + 1);
    }
    return supported;
}

QByteArray ELM327Protocol::elmVoltageRequest()
{
    return QByteArrayLiteral("ATRV\r");
}

std::optional<double> ELM327Protocol::parseElmVoltage(const QString &raw)
{
    // python-obd's elm_voltage(): lower-case, strip the 'v', float().
    static const QRegularExpression voltRe(
        QStringLiteral("^\\s*(\\d+(?:\\.\\d+)?)\\s*[vV]?\\s*$"));
    const auto m = voltRe.match(raw);
    if (!m.hasMatch()) {
        qCDebug(lcElm327) << "unparseable ATRV reply:" << raw.trimmed().left(200);
        return std::nullopt;
    }
    bool ok = false;
    const double volts = m.captured(1).toDouble(&ok);
    if (!ok)
        return std::nullopt;
    return volts;
}

QChar ELM327Protocol::dtcPrefix(int code)
{
    switch (code) {
    case 0: return QLatin1Char('P');
    case 1: return QLatin1Char('C');
    case 2: return QLatin1Char('B');
    case 3: return QLatin1Char('U');
    default: return QLatin1Char('P');
    }
}

QString ELM327Protocol::decodeDtc(int byte1, int byte2)
{
    const QChar prefix = dtcPrefix((byte1 >> 6) & 0x03);
    return QStringLiteral("%1%2%3%4%5")
        .arg(prefix)
        .arg((byte1 >> 4) & 0x03)
        .arg(byte1 & 0x0F, 1, 16)
        .arg((byte2 >> 4) & 0x0F, 1, 16)
        .arg(byte2 & 0x0F, 1, 16)
        .toUpper();
}

// Split an ELM327 reply (headers off, spaces off) into one hex string per
// ECU message. CAN multi-frame replies ("00A", "0:4304...", "1:...") are
// joined into a single message and trimmed to the announced byte count.
QStringList ELM327Protocol::splitMessages(const QStringList &lines, bool *isMultiFrame)
{
    static const QRegularExpression frameRe(QStringLiteral("^([0-9A-F]):([0-9A-F]*)$"));
    static const QRegularExpression hexRe(QStringLiteral("^[0-9A-F]+$"));

    QStringList messages;
    QString multi;
    int multiLen = -1;
    bool sawFrame = false;

    for (QString line : lines) {
        line.remove(QLatin1Char(' '));
        line = line.toUpper();
        const auto m = frameRe.match(line);
        if (m.hasMatch()) {
            sawFrame = true;
            multi += m.captured(2);
            continue;
        }
        if (!hexRe.match(line).hasMatch())
            continue;   // SEARCHING..., NO DATA, echo, OK
        if (line.size() == 3) {
            // Byte-count header of a CAN multi-frame reply
            if (!multi.isEmpty()) {
                messages.append(multiLen > 0 ? multi.left(multiLen * 2) : multi);
                multi.clear();
            }
            bool ok = false;
            multiLen = line.toInt(&ok, 16);
            if (!ok) multiLen = -1;
            continue;
        }
        messages.append(line);
    }
    if (!multi.isEmpty())
        messages.append(multiLen > 0 ? multi.left(multiLen * 2) : multi);
    if (isMultiFrame)
        *isMultiFrame = sawFrame;
    return messages;
}

QStringList ELM327Protocol::parseDtcResponse(const QStringList &lines, int mode)
{
    const QString responseMode = QStringLiteral("%1").arg(mode + 0x40, 2, 16, QLatin1Char('0')).toUpper();
    QStringList codes;

    const QStringList messages = splitMessages(lines);
    for (const QString &msg : messages) {
        if (!msg.startsWith(responseMode))
            continue;
        QString data = msg.mid(2);
        // Pre-CAN protocols always send 6 data bytes (3 DTC slots, zero
        // padded). ISO 15765 (CAN) puts a DTC-count byte first, which makes
        // the data an odd number of bytes; drop it so it isn't read as a code.
        if ((data.size() / 2) % 2 == 1)
            data = data.mid(2);

        for (int i = 0; i + 3 < data.size(); i += 4) {
            bool ok1 = false, ok2 = false;
            const int byte1 = data.mid(i, 2).toInt(&ok1, 16);
            const int byte2 = data.mid(i + 2, 2).toInt(&ok2, 16);
            if (!ok1 || !ok2 || (byte1 == 0 && byte2 == 0))
                continue;
            const QString code = decodeDtc(byte1, byte2);
            if (!codes.contains(code))   // several ECUs can report the same code
                codes.append(code);
        }
    }
    return codes;
}

QStringList ELM327Protocol::parseFreezeFrameDtc(const QStringList &lines)
{
    // Mode 02 PID 02 frame 00 reply: 42 02 00 <DTC hi> <DTC lo>
    QStringList codes;
    for (const QString &msg : splitMessages(lines)) {
        if (!msg.startsWith(QStringLiteral("4202")) || msg.size() < 10)
            continue;
        bool ok1 = false, ok2 = false;
        const int byte1 = msg.mid(6, 2).toInt(&ok1, 16);
        const int byte2 = msg.mid(8, 2).toInt(&ok2, 16);
        if (!ok1 || !ok2 || (byte1 == 0 && byte2 == 0))
            continue;
        const QString code = decodeDtc(byte1, byte2);
        if (!codes.contains(code))
            codes.append(code);
    }
    return codes;
}

// ---------------------------------------------------------------------------
// Multi-value PIDs (SAE J1979 Mode 01 above 0x5E). Byte layouts and scaling
// cross-checked against the OBD-II PIDs table on Wikipedia and OBDb's
// SAEJ1979 signal set. Byte 0 is data byte A. `supportBit` is the bit of A
// (0 = LSB) the ECU sets when that value is present.
// Mirrored in backend/elm327_protocol.py (EXTENDED_PID_TABLE).
// ---------------------------------------------------------------------------

namespace {
double u8(const QVector<uint8_t> &a, int i) { return a[i]; }
double u16(const QVector<uint8_t> &a, int i) { return a[i] * 256.0 + a[i + 1]; }
double s16(const QVector<uint8_t> &a, int i)
{
    const int v = a[i] * 256 + a[i + 1];
    return v > 32767 ? v - 65536 : v;
}
double u32(const QVector<uint8_t> &a, int i)
{
    return a[i] * 16777216.0 + a[i + 1] * 65536.0 + a[i + 2] * 256.0 + a[i + 3];
}
double round2(double v) { return std::round(v * 100.0) / 100.0; }
double pct(const QVector<uint8_t> &a, int i) { return round2(u8(a, i) * 100.0 / 255.0); }
double temp(const QVector<uint8_t> &a, int i) { return u8(a, i) - 40.0; }
double torque(const QVector<uint8_t> &a, int i) { return u8(a, i) - 125.0; }
} // namespace

const QHash<PidKey, QList<ExtendedSignal>> &ELM327Protocol::extendedPidTable()
{
    static const QHash<PidKey, QList<ExtendedSignal>> table = [] {
        QHash<PidKey, QList<ExtendedSignal>> t;
        auto sig = [](const char *id, const char *name, int supportBit, int minBytes, PidDecoder d) {
            return ExtendedSignal{QString::fromLatin1(id), QString::fromLatin1(name), supportBit, minBytes, std::move(d)};
        };
        t[{1, 0x61}] = {sig("DEMAND_ENGINE_TORQUE", "Driver Demand Torque", -1, 1, [](auto &a) { return torque(a, 0); })};
        t[{1, 0x62}] = {sig("ACTUAL_ENGINE_TORQUE", "Actual Engine Torque", -1, 1, [](auto &a) { return torque(a, 0); })};
        t[{1, 0x63}] = {sig("REFERENCE_TORQUE", "Engine Reference Torque", -1, 2, [](auto &a) { return u16(a, 0); })};
        t[{1, 0x65}] = {sig("RECOMMENDED_GEAR", "Recommended Gear", 4, 2, [](auto &a) { return double(a[1] >> 4); })};
        t[{1, 0x66}] = {
            sig("MAF_SENSOR_A", "MAF Sensor A", 0, 3, [](auto &a) { return round2(u16(a, 1) / 32.0); }),
            sig("MAF_SENSOR_B", "MAF Sensor B", 1, 5, [](auto &a) { return round2(u16(a, 3) / 32.0); }),
        };
        t[{1, 0x67}] = {
            sig("COOLANT_TEMP_SENSOR_1", "Coolant Temp Sensor 1", 0, 2, [](auto &a) { return temp(a, 1); }),
            sig("COOLANT_TEMP_SENSOR_2", "Coolant Temp Sensor 2", 1, 3, [](auto &a) { return temp(a, 2); }),
        };
        t[{1, 0x68}] = {
            sig("INTAKE_TEMP_B1S1", "Intake Air Temp B1S1", 0, 2, [](auto &a) { return temp(a, 1); }),
            sig("INTAKE_TEMP_B1S2", "Intake Air Temp B1S2", 1, 3, [](auto &a) { return temp(a, 2); }),
            sig("INTAKE_TEMP_B2S1", "Intake Air Temp B2S1", 3, 5, [](auto &a) { return temp(a, 4); }),
        };
        t[{1, 0x69}] = {
            sig("EGR_A_COMMANDED", "Commanded EGR A", 0, 2, [](auto &a) { return pct(a, 1); }),
            sig("EGR_A_ACTUAL", "Actual EGR A", 1, 3, [](auto &a) { return pct(a, 2); }),
            sig("EGR_A_ERROR", "EGR A Error", 2, 4, [](auto &a) { return round2(u8(a, 3) * 100.0 / 128.0 - 100.0); }),
        };
        t[{1, 0x6C}] = {
            sig("THROTTLE_ACTUATOR_A_COMMANDED", "Commanded Throttle A", 0, 2, [](auto &a) { return pct(a, 1); }),
            sig("RELATIVE_THROTTLE_A", "Relative Throttle A", 1, 3, [](auto &a) { return pct(a, 2); }),
        };
        t[{1, 0x6D}] = {
            sig("FUEL_RAIL_PRESSURE_A_COMMANDED", "Commanded Fuel Rail Pressure A", 0, 3, [](auto &a) { return u16(a, 1) * 10.0; }),
            sig("FUEL_RAIL_PRESSURE_A", "Fuel Rail Pressure A", 1, 5, [](auto &a) { return u16(a, 3) * 10.0; }),
            sig("FUEL_RAIL_TEMP_A", "Fuel Rail Temp A", 2, 6, [](auto &a) { return temp(a, 5); }),
        };
        t[{1, 0x70}] = {
            sig("BOOST_PRESSURE_A_COMMANDED", "Commanded Boost A", 0, 3, [](auto &a) { return round2(u16(a, 1) / 32.0); }),
            sig("BOOST_PRESSURE_A", "Boost Pressure A", 1, 5, [](auto &a) { return round2(u16(a, 3) / 32.0); }),
        };
        t[{1, 0x72}] = {
            sig("WASTEGATE_A_COMMANDED", "Commanded Wastegate A", 0, 2, [](auto &a) { return pct(a, 1); }),
            sig("WASTEGATE_A", "Wastegate A Position", 1, 3, [](auto &a) { return pct(a, 2); }),
        };
        t[{1, 0x7F}] = {
            sig("ENGINE_RUN_TIME_TOTAL", "Total Engine Run Time", 0, 5, [](auto &a) { return u32(a, 1); }),
            sig("ENGINE_IDLE_TIME_TOTAL", "Total Idle Time", 1, 9, [](auto &a) { return u32(a, 5); }),
        };
        t[{1, 0x84}] = {sig("MANIFOLD_SURFACE_TEMP", "Manifold Surface Temp", -1, 1, [](auto &a) { return temp(a, 0); })};
        t[{1, 0x8D}] = {sig("THROTTLE_POS_G", "Throttle Position G", -1, 1, [](auto &a) { return pct(a, 0); })};
        t[{1, 0x8E}] = {sig("ENGINE_FRICTION_TORQUE", "Engine Friction Torque", -1, 1, [](auto &a) { return torque(a, 0); })};
        t[{1, 0x9A}] = {
            sig("HYBRID_BATTERY_VOLTAGE", "Hybrid Battery Voltage", 1, 4, [](auto &a) { return round2(u16(a, 2) / 64.0); }),
            sig("HYBRID_BATTERY_CURRENT", "Hybrid Battery Current", 2, 6, [](auto &a) { return round2(s16(a, 4) / 10.0); }),
        };
        t[{1, 0x9D}] = {
            sig("ENGINE_FUEL_RATE_GS", "Engine Fuel Rate", -1, 2, [](auto &a) { return round2(u16(a, 0) / 50.0); }),
            sig("VEHICLE_FUEL_RATE_GS", "Vehicle Fuel Rate", -1, 4, [](auto &a) { return round2(u16(a, 2) / 50.0); }),
        };
        t[{1, 0x9E}] = {sig("EXHAUST_FLOW_RATE", "Exhaust Flow Rate", -1, 2, [](auto &a) { return round2(u16(a, 0) / 5.0); })};
        t[{1, 0xA2}] = {sig("CYLINDER_FUEL_RATE", "Cylinder Fuel Rate", -1, 2, [](auto &a) { return round2(u16(a, 0) / 32.0); })};
        t[{1, 0xA4}] = {sig("TRANSMISSION_GEAR_RATIO", "Transmission Gear Ratio", 1, 4, [](auto &a) { return u16(a, 2) / 1000.0; })};
        t[{1, 0xA6}] = {sig("ODOMETER", "Odometer", -1, 4, [](auto &a) { return u32(a, 0) / 10.0; })};
        t[{1, 0xB2}] = {sig("EV_BATTERY_HEALTH", "EV Battery Health", -1, 1, [](auto &a) { return pct(a, 0); })};
        return t;
    }();
    return table;
}

QList<QPair<QString, double>> ELM327Protocol::decodeExtendedPid(int mode, int pid,
                                                                const QVector<uint8_t> &dataBytes)
{
    QList<QPair<QString, double>> values;
    const auto &table = extendedPidTable();
    auto it = table.constFind({mode, pid});
    if (it == table.constEnd())
        return values;
    for (const ExtendedSignal &s : it.value()) {
        if (dataBytes.size() < s.minBytes)
            continue;
        if (s.supportBit >= 0 && (dataBytes.isEmpty() || !(dataBytes[0] & (1 << s.supportBit))))
            continue;
        values.append({s.paramId, s.decoder(dataBytes)});
    }
    return values;
}

QString ELM327Protocol::parseVin(const QStringList &lines)
{
    // CAN: one multi-frame message "4902 01 <17 bytes>" (01 = item count).
    // Pre-CAN: five messages "4902 <seq> <4 bytes>", the first left-padded
    // with 00. Either way one byte follows "4902" before the characters.
    QMap<int, QByteArray> parts;
    for (const QString &msg : splitMessages(lines)) {
        if (!msg.startsWith(QStringLiteral("4902")) || msg.size() < 6)
            continue;
        const QByteArray bytes = QByteArray::fromHex(msg.mid(4).toLatin1());
        if (bytes.isEmpty())
            continue;
        const int seq = static_cast<uint8_t>(bytes[0]);
        if (!parts.contains(seq))
            parts.insert(seq, bytes.mid(1));
    }
    QString text;
    for (const QByteArray &part : std::as_const(parts)) {
        for (char c : part) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                text.append(QChar(QLatin1Char(c)).toUpper());
        }
    }
    if (text.size() < 17)
        return QString();
    return text.right(17);
}

VinInfo ELM327Protocol::decodeVin(const QString &vinIn)
{
    VinInfo info;
    const QString vin = vinIn.trimmed().toUpper();
    static const QRegularExpression vinRe(QStringLiteral("^[A-HJ-NPR-Z0-9]{17}$"));
    if (!vinRe.match(vin).hasMatch())
        return info;
    info.vin = vin;

    // World manufacturer identifier: exact 3-character match, then the
    // 2-character prefix
    static const QHash<QString, QString> wmi3 = {
        {"1J4", "Jeep"}, {"1J8", "Jeep"}, {"1C4", "Chrysler/Dodge/Jeep"}, {"1C3", "Chrysler/Dodge"},
        {"1C6", "Ram"}, {"3C6", "Ram"}, {"3C7", "Ram"}, {"1D7", "Dodge"}, {"1B3", "Dodge"},
        {"1B7", "Dodge"}, {"2C3", "Chrysler/Dodge"}, {"2C4", "Chrysler/Dodge"}, {"3C4", "Chrysler/Dodge/Jeep"},
        {"1G1", "Chevrolet"}, {"1GC", "Chevrolet"}, {"1GN", "Chevrolet"}, {"1GB", "Chevrolet"},
        {"2G1", "Chevrolet"}, {"3GN", "Chevrolet"}, {"3GC", "Chevrolet"}, {"1GT", "GMC"},
        {"1GK", "GMC"}, {"3GT", "GMC"}, {"1G6", "Cadillac"}, {"1G4", "Buick"}, {"1G2", "Pontiac"},
        {"1FA", "Ford"}, {"1FB", "Ford"}, {"1FC", "Ford"}, {"1FD", "Ford"}, {"1FM", "Ford"},
        {"1FT", "Ford"}, {"2FM", "Ford"}, {"3FA", "Ford"}, {"1LN", "Lincoln"}, {"5LM", "Lincoln"},
        {"1ME", "Mercury"}, {"1HG", "Honda"}, {"2HG", "Honda"}, {"5FN", "Honda"}, {"5J6", "Honda"},
        {"19U", "Acura"}, {"5J8", "Acura"}, {"JH4", "Acura"}, {"1N4", "Nissan"}, {"1N6", "Nissan"},
        {"5N1", "Nissan"}, {"3N1", "Nissan"}, {"JN1", "Nissan"}, {"JN8", "Nissan"}, {"4T1", "Toyota"},
        {"4T3", "Toyota"}, {"5TD", "Toyota"}, {"5TF", "Toyota"}, {"2T1", "Toyota"}, {"2T3", "Toyota"},
        {"JTD", "Toyota"}, {"JTE", "Toyota"}, {"JTM", "Toyota"}, {"JTN", "Toyota"}, {"JTH", "Lexus"},
        {"JTJ", "Lexus"}, {"2T2", "Lexus"}, {"4S3", "Subaru"}, {"4S4", "Subaru"}, {"JF1", "Subaru"},
        {"JF2", "Subaru"}, {"JM1", "Mazda"}, {"JM3", "Mazda"}, {"5NP", "Hyundai"}, {"5NM", "Hyundai"},
        {"KMH", "Hyundai"}, {"KM8", "Hyundai"}, {"5XY", "Kia"}, {"5XX", "Kia"}, {"KNA", "Kia"},
        {"KND", "Kia"}, {"5YJ", "Tesla"}, {"7SA", "Tesla"}, {"LRW", "Tesla"}, {"WBA", "BMW"},
        {"WBS", "BMW M"}, {"5UX", "BMW"}, {"4US", "BMW"}, {"WMW", "MINI"}, {"WDD", "Mercedes-Benz"},
        {"WDB", "Mercedes-Benz"}, {"W1K", "Mercedes-Benz"}, {"W1N", "Mercedes-Benz"}, {"4JG", "Mercedes-Benz"},
        {"WVW", "Volkswagen"}, {"WV1", "Volkswagen"}, {"WV2", "Volkswagen"}, {"3VW", "Volkswagen"},
        {"1VW", "Volkswagen"}, {"WAU", "Audi"}, {"WA1", "Audi"}, {"WP0", "Porsche"}, {"WP1", "Porsche"},
        {"SAJ", "Jaguar"}, {"SAL", "Land Rover"}, {"YV1", "Volvo"}, {"YV4", "Volvo"}, {"ZFF", "Ferrari"},
        {"ZAR", "Alfa Romeo"}, {"ZFA", "Fiat"}, {"3C3", "Fiat"}, {"ZHW", "Lamborghini"}, {"JA3", "Mitsubishi"},
        {"JA4", "Mitsubishi"}, {"ML3", "Mitsubishi"}, {"JS1", "Suzuki"}, {"JS2", "Suzuki"},
        {"1HD", "Harley-Davidson"}, {"VF1", "Renault"}, {"VF3", "Peugeot"}, {"VF7", "Citroen"},
        {"SCC", "Lotus"}, {"SCF", "Aston Martin"}, {"1YV", "Mazda"}, {"4F2", "Mazda"},
    };
    static const QHash<QString, QString> wmi2 = {
        {"1G", "General Motors"}, {"2G", "General Motors"}, {"3G", "General Motors"}, {"1F", "Ford"},
        {"2F", "Ford"}, {"3F", "Ford"}, {"1C", "Chrysler"}, {"2C", "Chrysler"}, {"3C", "Chrysler"},
        {"1J", "Jeep"}, {"1D", "Dodge"}, {"2D", "Dodge"}, {"3D", "Dodge"}, {"1H", "Honda"}, {"2H", "Honda"},
        {"JH", "Honda"}, {"1N", "Nissan"}, {"JN", "Nissan"}, {"JT", "Toyota"}, {"4T", "Toyota"},
        {"5T", "Toyota"}, {"JM", "Mazda"}, {"JF", "Subaru"}, {"JS", "Suzuki"}, {"KM", "Hyundai"},
        {"KN", "Kia"}, {"WB", "BMW"}, {"WD", "Mercedes-Benz"}, {"WV", "Volkswagen"}, {"WA", "Audi"},
        {"WP", "Porsche"}, {"YV", "Volvo"}, {"SA", "Jaguar/Land Rover"},
    };
    info.make = wmi3.value(vin.left(3), wmi2.value(vin.left(2)));

    // Model year, position 10: A-Y (no I, O, Q, U, Z) then 1-9, a 30-year
    // cycle. Position 7 alphabetic marks the 2010+ cycle (North America);
    // a year that would be in the future falls back 30 years.
    static const QString codes = QStringLiteral("ABCDEFGHJKLMNPRSTVWXY123456789");
    const int idx = codes.indexOf(vin.at(9));
    if (idx >= 0) {
        int year = 1980 + idx;
        if (vin.at(6).isLetter())
            year += 30;
        const int maxYear = QDate::currentDate().year() + 1;
        while (year > maxYear)
            year -= 30;
        info.modelYear = year;
    }
    return info;
}

QList<PidKey> ELM327Protocol::defaultPids()
{
    return {
        {1, 0x0C},  // RPM
        {1, 0x0D},  // Speed
        {1, 0x05},  // Coolant Temp
        {1, 0x04},  // Engine Load
        {1, 0x11},  // Throttle Position
        {1, 0x0F},  // Intake Air Temp
        {1, 0x0B},  // Intake Manifold Pressure
        {1, 0x42},  // Control Module Voltage
        {1, 0x2F},  // Fuel Level
        {1, 0x10},  // MAF
        {1, 0x0E},  // Timing Advance
        {1, 0x06},  // Short Fuel Trim 1
        {1, 0x07},  // Long Fuel Trim 1
        {1, 0x14},  // O2 B1S1
        {1, 0x0A},  // Fuel Pressure
        {1, 0x5C},  // Oil Temp
    };
}

QStringList ELM327Protocol::supportedCommandNames(const QSet<int> &supportedPids)
{
    QStringList names;
    const auto &table = pidTable();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        const PidKey &key = it.key();
        if (key == kElmVoltageKey || (key.first == 1 && supportedPids.contains(key.second)))
            names.append(it.value().commandName);
    }
    const auto &ext = extendedPidTable();
    for (auto it = ext.constBegin(); it != ext.constEnd(); ++it) {
        if (it.key().first == 1 && supportedPids.contains(it.key().second)) {
            for (const ExtendedSignal &s : it.value())
                names.append(s.paramId);
        }
    }
    names.sort();
    return names;
}

QStringList ELM327Protocol::allParameterNames()
{
    QStringList names;
    const auto &table = pidTable();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        names.append(it.value().name);
    }
    const auto &ext = extendedPidTable();
    for (auto it = ext.constBegin(); it != ext.constEnd(); ++it) {
        for (const ExtendedSignal &s : it.value())
            names.append(s.name);
    }
    names.sort();
    return names;
}

// ===========================================================================
// ResponseBuffer
// ===========================================================================

void ResponseBuffer::feed(const QByteArray &data)
{
    m_buffer.append(data);
    // Safety: if buffer grows huge without a '>' prompt, trim
    if (m_buffer.size() > 4096)
        m_buffer = m_buffer.right(1024);
}

std::optional<QString> ResponseBuffer::getResponse()
{
    int idx = m_buffer.indexOf('>');
    if (idx == -1)
        return std::nullopt;

    QByteArray raw = m_buffer.left(idx);
    m_buffer = m_buffer.mid(idx + 1);

    QString response = QString::fromLatin1(raw).trimmed();

    // Filter out echo and empty lines, return the data line
    QStringList lines;
    const auto parts = response.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            lines.append(trimmed);
    }
    m_lastLines = lines;

    // Return the last meaningful line (skip echo, prompts, AT/OK)
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString &l = lines[i];
        if (!l.isEmpty() && !l.startsWith(QStringLiteral("AT")) && l != QStringLiteral("OK"))
            return l;
    }

    return lines.isEmpty() ? QString() : lines.last();
}

void ResponseBuffer::clear()
{
    m_buffer.clear();
    m_lastLines.clear();
}
