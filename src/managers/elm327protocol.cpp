#include "elm327protocol.h"

#include <QHash>
#include <QRegularExpression>

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
    t[{1, 0x54}] = {"Evap Vapor Pressure Alt", "evapVaporPressureAltChanged",   "EVAP_VAPOR_PRESSURE_ALT",   decEvapPressure,   2};
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

QStringList ELM327Protocol::parseDtcResponse(const QString &raw)
{
    QStringList codes;
    QString line = raw.trimmed();
    line.remove(QLatin1Char(' '));

    // Remove mode byte (43 for mode 03 response)
    if (line.startsWith(QStringLiteral("43")))
        line = line.mid(2);

    // Each DTC is 2 bytes (4 hex chars)
    for (int i = 0; i + 3 < line.size(); i += 4) {
        QString chunk = line.mid(i, 4);
        if (chunk == QStringLiteral("0000"))
            continue;

        bool ok1, ok2;
        int byte1 = chunk.mid(0, 2).toInt(&ok1, 16);
        int byte2 = chunk.mid(2, 2).toInt(&ok2, 16);
        if (!ok1 || !ok2)
            continue;

        QChar prefix = dtcPrefix((byte1 >> 6) & 0x03);
        int digit2 = (byte1 >> 4) & 0x03;
        int digit3 = byte1 & 0x0F;
        int digit4 = (byte2 >> 4) & 0x0F;
        int digit5 = byte2 & 0x0F;

        codes.append(QStringLiteral("%1%2%3%4%5")
                         .arg(prefix)
                         .arg(digit2)
                         .arg(digit3, 1, 16)
                         .arg(digit4, 1, 16)
                         .arg(digit5, 1, 16)
                         .toUpper());
    }
    return codes;
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
}
