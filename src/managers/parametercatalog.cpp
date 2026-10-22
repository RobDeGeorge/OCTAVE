#include "parametercatalog.h"

#include <QHash>

#include <iterator>

namespace {

using Source = ParameterCatalog::Source;

struct Row {
    const char *id;
    const char *title;
    const char *shortTitle;
    const char *unit;   // UTF-8
    double min;
    double max;
    const char *kind;
    int decimals;
    Source source;
    const char *aliasOf;
};

// Titles, units and ranges are the ones the QML OBDParameterModel table used,
// which is what users have been seeing. Order is display order (the PID
// picker and the demo sweep follow it).
const Row kRows[] = {
        {"SPEED", "Speed", "SPD", "MPH", 0.0, 160.0, "numeric", 0, Source::Obd, ""},
        {"RPM", "Engine RPM", "RPM", "RPM", 0.0, 8000.0, "numeric", 0, Source::Obd, ""},
        {"COOLANT_TEMP", "Coolant Temp", "CLT", "°C", 0.0, 120.0, "temperature", 1, Source::Obd, ""},
        {"OIL_TEMP", "Oil Temp", "OIL", "°C", 0.0, 150.0, "temperature", 1, Source::Obd, ""},
        {"COMMANDED_EQUIV_RATIO", "Air-Fuel Ratio", "AFR", ":1", 10.0, 18.0, "numeric", 1, Source::Obd, ""},
        {"ENGINE_LOAD", "Engine Load", "LOAD", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"THROTTLE_POS", "Throttle", "TPS", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"FUEL_LEVEL", "Fuel Level", "FUEL", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"SHORT_FUEL_TRIM_1", "Short Fuel Trim 1", "STFT1", "%", -25.0, 25.0, "bidirectional", 1, Source::Obd, ""},
        {"LONG_FUEL_TRIM_1", "Long Fuel Trim 1", "LTFT1", "%", -25.0, 25.0, "bidirectional", 1, Source::Obd, ""},
        {"INTAKE_TEMP", "Intake Temp", "IAT", "°C", 0.0, 80.0, "temperature", 1, Source::Obd, ""},
        {"INTAKE_PRESSURE", "Intake Pressure", "MAP", "kPa", 0.0, 255.0, "pressure", 1, Source::Obd, ""},
        {"MAF", "Mass Air Flow", "MAF", "g/s", 0.0, 100.0, "numeric", 1, Source::Obd, ""},
        {"TIMING_ADVANCE", "Timing Advance", "TIM", "°", -35.0, 35.0, "bidirectional", 1, Source::Obd, ""},
        {"CONTROL_MODULE_VOLTAGE", "System Voltage", "VOLT", "V", 10.0, 15.0, "voltage", 1, Source::Obd, ""},
        {"O2_B1S1", "O2 Bank 1 Sensor 1", "O2", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"FUEL_PRESSURE", "Fuel Pressure", "FP", "kPa", 0.0, 765.0, "pressure", 1, Source::Obd, ""},
        {"IGNITION_TIMING", "Ignition Timing", "IGN", "°", -10.0, 60.0, "bidirectional", 1, Source::Obd, "TIMING_ADVANCE"},
        {"RUN_TIME", "Run Time", "", "sec", 0.0, 65535.0, "numeric", 0, Source::Obd, ""},
        {"DISTANCE_W_MIL", "Distance w/ MIL", "", "km", 0.0, 65535.0, "numeric", 0, Source::Obd, ""},
        {"FUEL_RAIL_PRESSURE_VAC", "Fuel Rail Pressure", "", "kPa", 0.0, 5177.0, "pressure", 1, Source::Obd, ""},
        {"FUEL_RAIL_PRESSURE_DIRECT", "Fuel Rail Direct", "", "kPa", 0.0, 655350.0, "pressure", 1, Source::Obd, ""},
        {"BAROMETRIC_PRESSURE", "Barometric", "", "kPa", 0.0, 255.0, "pressure", 1, Source::Obd, ""},
        {"AMBIANT_AIR_TEMP", "Ambient Air", "", "°C", -40.0, 215.0, "temperature", 1, Source::Obd, ""},
        {"RELATIVE_THROTTLE_POS", "Rel. Throttle", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"THROTTLE_POS_B", "Throttle Pos B", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"ACCELERATOR_POS_D", "Accel. Pedal", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"CATALYST_TEMP_B1S1", "Cat Temp B1S1", "", "°C", 0.0, 6513.0, "temperature", 1, Source::Obd, ""},
        {"CATALYST_TEMP_B1S2", "Cat Temp B1S2", "", "°C", 0.0, 6513.0, "temperature", 1, Source::Obd, ""},
        {"EVAP_VAPOR_PRESSURE", "EVAP Pressure", "", "Pa", -8192.0, 8191.0, "bidirectional", 1, Source::Obd, ""},
        {"SHORT_FUEL_TRIM_2", "Short Fuel Trim 2", "", "%", -25.0, 25.0, "bidirectional", 1, Source::Obd, ""},
        {"LONG_FUEL_TRIM_2", "Long Fuel Trim 2", "", "%", -25.0, 25.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_B1S2", "O2 Bank 1 Sensor 2", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"O2_B2S1", "O2 Bank 2 Sensor 1", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"O2_B2S2", "O2 Bank 2 Sensor 2", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"DISTANCE_SINCE_DTC_CLEAR", "Dist Since Clear", "", "km", 0.0, 65535.0, "numeric", 0, Source::Obd, ""},
        {"WARMUPS_SINCE_DTC_CLEAR", "Warmups Since Clear", "", "", 0.0, 255.0, "numeric", 0, Source::Obd, ""},
        {"ABSOLUTE_LOAD", "Absolute Load", "", "%", 0.0, 25700.0, "numeric", 1, Source::Obd, ""},
        {"COMMANDED_EGR", "Commanded EGR", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"EGR_ERROR", "EGR Error", "", "%", -100.0, 100.0, "bidirectional", 1, Source::Obd, ""},
        {"ETHANOL_PERCENT", "Ethanol %", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"O2_B1S3", "O2 Bank 1 Sensor 3", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"O2_B1S4", "O2 Bank 1 Sensor 4", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"O2_B2S3", "O2 Bank 2 Sensor 3", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"O2_B2S4", "O2 Bank 2 Sensor 4", "", "V", 0.0, 1.0, "voltage", 1, Source::Obd, ""},
        {"O2_S1_WR_VOLTAGE", "O2 S1 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S2_WR_VOLTAGE", "O2 S2 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S3_WR_VOLTAGE", "O2 S3 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S4_WR_VOLTAGE", "O2 S4 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S5_WR_VOLTAGE", "O2 S5 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S6_WR_VOLTAGE", "O2 S6 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S7_WR_VOLTAGE", "O2 S7 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S8_WR_VOLTAGE", "O2 S8 WR Voltage", "", "V", 0.0, 5.0, "voltage", 1, Source::Obd, ""},
        {"O2_S1_WR_CURRENT", "O2 S1 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S2_WR_CURRENT", "O2 S2 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S3_WR_CURRENT", "O2 S3 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S4_WR_CURRENT", "O2 S4 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S5_WR_CURRENT", "O2 S5 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S6_WR_CURRENT", "O2 S6 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S7_WR_CURRENT", "O2 S7 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"O2_S8_WR_CURRENT", "O2 S8 WR Current", "", "mA", -128.0, 128.0, "bidirectional", 1, Source::Obd, ""},
        {"CATALYST_TEMP_B2S1", "Cat Temp B2S1", "", "°C", 0.0, 6513.0, "temperature", 1, Source::Obd, ""},
        {"CATALYST_TEMP_B2S2", "Cat Temp B2S2", "", "°C", 0.0, 6513.0, "temperature", 1, Source::Obd, ""},
        {"THROTTLE_POS_C", "Throttle Pos C", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"ACCELERATOR_POS_E", "Accel. Pos E", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"ACCELERATOR_POS_F", "Accel. Pos F", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"THROTTLE_ACTUATOR", "Throttle Actuator", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"EVAPORATIVE_PURGE", "EVAP Purge", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"FUEL_RAIL_PRESSURE_ABS", "Fuel Rail Abs", "", "kPa", 0.0, 655350.0, "pressure", 1, Source::Obd, ""},
        {"FUEL_INJECT_TIMING", "Fuel Inject Timing", "", "°", -210.0, 302.0, "bidirectional", 1, Source::Obd, ""},
        {"FUEL_RATE", "Fuel Rate", "", "L/h", 0.0, 3212.0, "numeric", 1, Source::Obd, ""},
        {"RUN_TIME_MIL", "Run Time w/ MIL", "", "min", 0.0, 65535.0, "numeric", 0, Source::Obd, ""},
        {"TIME_SINCE_DTC_CLEARED", "Time Since Clear", "", "min", 0.0, 65535.0, "numeric", 0, Source::Obd, ""},
        {"MAX_MAF", "Max MAF", "", "g/s", 0.0, 2550.0, "numeric", 1, Source::Obd, ""},
        {"FUEL_TYPE", "Fuel Type", "", "", 0.0, 23.0, "numeric", 1, Source::Obd, ""},
        {"EVAP_VAPOR_PRESSURE_ABS", "EVAP Pressure Abs", "", "kPa", 0.0, 327.0, "pressure", 1, Source::Obd, ""},
        {"EVAP_VAPOR_PRESSURE_ALT", "EVAP Pressure Alt", "", "Pa", -32768.0, 32767.0, "bidirectional", 1, Source::Obd, ""},
        {"SHORT_O2_TRIM_B1", "Short O2 Trim B1", "", "%", -100.0, 100.0, "bidirectional", 1, Source::Obd, ""},
        {"LONG_O2_TRIM_B1", "Long O2 Trim B1", "", "%", -100.0, 100.0, "bidirectional", 1, Source::Obd, ""},
        {"SHORT_O2_TRIM_B2", "Short O2 Trim B2", "", "%", -100.0, 100.0, "bidirectional", 1, Source::Obd, ""},
        {"LONG_O2_TRIM_B2", "Long O2 Trim B2", "", "%", -100.0, 100.0, "bidirectional", 1, Source::Obd, ""},
        {"RELATIVE_ACCEL_POS", "Rel. Accel Pos", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"HYBRID_BATTERY_REMAINING", "Hybrid Battery", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"ELM_VOLTAGE", "ELM Voltage", "", "V", 0.0, 65.0, "voltage", 1, Source::Obd, ""},
        {"DEMAND_ENGINE_TORQUE", "Demand Torque", "", "%", -125.0, 130.0, "bidirectional", 1, Source::Obd, ""},
        {"ACTUAL_ENGINE_TORQUE", "Actual Torque", "", "%", -125.0, 130.0, "bidirectional", 1, Source::Obd, ""},
        {"ENGINE_FRICTION_TORQUE", "Friction Torque", "", "%", -125.0, 130.0, "bidirectional", 1, Source::Obd, ""},
        {"REFERENCE_TORQUE", "Reference Torque", "", "Nm", 0.0, 2000.0, "numeric", 1, Source::Obd, ""},
        {"BOOST_PRESSURE_A", "Boost Pressure (abs)", "", "kPa", 0.0, 300.0, "pressure", 1, Source::Obd, ""},
        {"BOOST_PRESSURE_A_COMMANDED", "Boost Commanded (abs)", "", "kPa", 0.0, 300.0, "pressure", 1, Source::Obd, ""},
        {"WASTEGATE_A", "Wastegate Position", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"WASTEGATE_A_COMMANDED", "Wastegate Commanded", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"MAF_SENSOR_A", "MAF Sensor A", "", "g/s", 0.0, 655.0, "numeric", 1, Source::Obd, ""},
        {"MAF_SENSOR_B", "MAF Sensor B", "", "g/s", 0.0, 655.0, "numeric", 1, Source::Obd, ""},
        {"INTAKE_TEMP_B1S1", "Intake Temp B1S1", "", "°C", -20.0, 100.0, "temperature", 1, Source::Obd, ""},
        {"INTAKE_TEMP_B1S2", "Intake Temp B1S2", "", "°C", -20.0, 100.0, "temperature", 1, Source::Obd, ""},
        {"INTAKE_TEMP_B2S1", "Intake Temp B2S1", "", "°C", -20.0, 100.0, "temperature", 1, Source::Obd, ""},
        {"MANIFOLD_SURFACE_TEMP", "Manifold Surface Temp", "", "°C", -40.0, 215.0, "temperature", 1, Source::Obd, ""},
        {"COOLANT_TEMP_SENSOR_1", "Coolant Temp 1", "", "°C", 0.0, 130.0, "temperature", 1, Source::Obd, ""},
        {"COOLANT_TEMP_SENSOR_2", "Coolant Temp 2", "", "°C", 0.0, 130.0, "temperature", 1, Source::Obd, ""},
        {"THROTTLE_POS_G", "Throttle Pos G", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"THROTTLE_ACTUATOR_A_COMMANDED", "Throttle A Commanded", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"RELATIVE_THROTTLE_A", "Rel. Throttle A", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"EGR_A_COMMANDED", "EGR A Commanded", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"EGR_A_ACTUAL", "EGR A Actual", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"EGR_A_ERROR", "EGR A Error", "", "%", -100.0, 100.0, "bidirectional", 1, Source::Obd, ""},
        {"FUEL_RAIL_PRESSURE_A", "Fuel Rail Pressure A", "", "kPa", 0.0, 250000.0, "pressure", 1, Source::Obd, ""},
        {"FUEL_RAIL_PRESSURE_A_COMMANDED", "Fuel Rail A Commanded", "", "kPa", 0.0, 250000.0, "pressure", 1, Source::Obd, ""},
        {"FUEL_RAIL_TEMP_A", "Fuel Rail Temp A", "", "°C", -40.0, 150.0, "temperature", 1, Source::Obd, ""},
        {"ENGINE_FUEL_RATE_GS", "Engine Fuel Rate", "", "g/s", 0.0, 100.0, "numeric", 1, Source::Obd, ""},
        {"VEHICLE_FUEL_RATE_GS", "Vehicle Fuel Rate", "", "g/s", 0.0, 100.0, "numeric", 1, Source::Obd, ""},
        {"CYLINDER_FUEL_RATE", "Cylinder Fuel Rate", "", "mg/stroke", 0.0, 200.0, "numeric", 1, Source::Obd, ""},
        {"EXHAUST_FLOW_RATE", "Exhaust Flow", "", "kg/h", 0.0, 2000.0, "numeric", 1, Source::Obd, ""},
        {"TRANSMISSION_GEAR_RATIO", "Gear Ratio", "", ":1", 0.0, 6.0, "numeric", 1, Source::Obd, ""},
        {"RECOMMENDED_GEAR", "Recommended Gear", "", "", 0.0, 10.0, "numeric", 1, Source::Obd, ""},
        {"ODOMETER", "Odometer", "", "km", 0.0, 1000000.0, "numeric", 1, Source::Obd, ""},
        {"ENGINE_RUN_TIME_TOTAL", "Total Engine Run Time", "", "sec", 0.0, 360000000.0, "numeric", 0, Source::Obd, ""},
        {"ENGINE_IDLE_TIME_TOTAL", "Total Idle Time", "", "sec", 0.0, 360000000.0, "numeric", 1, Source::Obd, ""},
        {"HYBRID_BATTERY_VOLTAGE", "Hybrid Battery Voltage", "", "V", 0.0, 900.0, "voltage", 1, Source::Obd, ""},
        {"HYBRID_BATTERY_CURRENT", "Hybrid Battery Current", "", "A", -500.0, 500.0, "bidirectional", 1, Source::Obd, ""},
        {"EV_BATTERY_HEALTH", "EV Battery Health", "", "%", 0.0, 100.0, "percentage", 1, Source::Obd, ""},
        {"PITCH", "Pitch", "", "°", -90.0, 90.0, "bidirectional", 1, Source::Imu, ""},
        {"ROLL", "Roll", "", "°", -180.0, 180.0, "bidirectional", 1, Source::Imu, ""},
        {"HEADING", "Heading", "", "°", 0.0, 360.0, "numeric", 1, Source::Imu, ""},
        {"ALTITUDE", "Altitude", "", "m", 0.0, 3000.0, "numeric", 1, Source::Imu, ""},
        {"ACCEL_MAG", "G Magnitude", "", "g", 0.0, 3.0, "numeric", 1, Source::Imu, ""},
        {"LATERAL_G", "Lateral G", "", "g", -2.0, 2.0, "bidirectional", 1, Source::Imu, ""},
        {"LONGITUDINAL_G", "Longitudinal G", "", "g", -2.0, 2.0, "bidirectional", 1, Source::Imu, ""},
        {"BARO_TEMP", "Cabin Baro Temp", "", "°C", -20.0, 60.0, "temperature", 1, Source::Imu, ""},

};

} // namespace

ParameterCatalog::ParameterCatalog(QObject *parent)
    : QObject(parent)
{
}

const QList<ParameterCatalog::Parameter> &ParameterCatalog::all()
{
    static const QList<Parameter> list = [] {
        QList<Parameter> l;
        l.reserve(int(std::size(kRows)));
        for (const Row &r : kRows) {
            Parameter p;
            p.id = QString::fromLatin1(r.id);
            p.title = QString::fromUtf8(r.title);
            p.shortTitle = QString::fromUtf8(r.shortTitle);
            p.unit = QString::fromUtf8(r.unit);
            p.min = r.min;
            p.max = r.max;
            p.kind = QString::fromLatin1(r.kind);
            p.decimals = r.decimals;
            p.source = r.source;
            p.aliasOf = QString::fromLatin1(r.aliasOf);
            l.append(p);
        }
        return l;
    }();
    return list;
}

const ParameterCatalog::Parameter *ParameterCatalog::find(const QString &id)
{
    static const QHash<QString, int> index = [] {
        QHash<QString, int> h;
        const auto &l = all();
        for (int i = 0; i < l.size(); ++i)
            h.insert(l.at(i).id, i);
        return h;
    }();
    const auto it = index.constFind(id);
    return it == index.constEnd() ? nullptr : &all().at(it.value());
}

QStringList ParameterCatalog::imuIds()
{
    QStringList ids;
    for (const Parameter &p : all())
        if (p.source == Source::Imu)
            ids.append(p.id);
    return ids;
}

QStringList ParameterCatalog::obdIds()
{
    QStringList ids;
    for (const Parameter &p : all())
        if (p.source == Source::Obd && p.aliasOf.isEmpty())
            ids.append(p.id);
    return ids;
}

QVariantMap ParameterCatalog::toVariant(const Parameter &p)
{
    return {
        {QStringLiteral("id"), p.id},
        {QStringLiteral("title"), p.title},
        {QStringLiteral("shortTitle"), p.shortTitle},
        {QStringLiteral("unit"), p.unit},
        {QStringLiteral("min"), p.min},
        {QStringLiteral("max"), p.max},
        {QStringLiteral("kind"), p.kind},
        {QStringLiteral("decimals"), p.decimals},
        {QStringLiteral("source"), p.source == Source::Imu ? QStringLiteral("imu") : QStringLiteral("obd")},
        {QStringLiteral("aliasOf"), p.aliasOf},
    };
}

QVariantList ParameterCatalog::parameters() const
{
    static const QVariantList list = [] {
        QVariantList l;
        for (const Parameter &p : all())
            l.append(toVariant(p));
        return l;
    }();
    return list;
}

bool ParameterCatalog::contains(const QString &id) const
{
    return find(id) != nullptr;
}

QVariantMap ParameterCatalog::info(const QString &id) const
{
    const Parameter *p = find(id);
    return p ? toVariant(*p) : QVariantMap();
}
