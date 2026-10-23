#include "dtcdescriptions.h"

#include <QHash>
#include <QRegularExpression>

namespace DtcDescriptions {

namespace {
const QHash<QString, QString> &table()
{
    // Common generic (SAE J2012) codes. Keep to codes whose generic meaning
    // is fixed; manufacturer-specific codes (P1xxx, P30xx-P33xx, ...) never
    // go here.
    static const QHash<QString, QString> t = [] {
        QHash<QString, QString> h{
            {"P0010", "Intake camshaft position actuator circuit (bank 1)"},
            {"P0011", "Intake camshaft position timing over-advanced or system performance (bank 1)"},
            {"P0016", "Crankshaft position / camshaft position correlation (bank 1 sensor A)"},
            {"P0030", "O2 sensor heater control circuit (bank 1 sensor 1)"},
            {"P0100", "Mass or volume air flow circuit"},
            {"P0101", "Mass or volume air flow circuit range / performance"},
            {"P0102", "Mass or volume air flow circuit low input"},
            {"P0103", "Mass or volume air flow circuit high input"},
            {"P0106", "Manifold absolute pressure / barometric pressure circuit range / performance"},
            {"P0107", "Manifold absolute pressure / barometric pressure circuit low input"},
            {"P0108", "Manifold absolute pressure / barometric pressure circuit high input"},
            {"P0110", "Intake air temperature circuit"},
            {"P0112", "Intake air temperature circuit low input"},
            {"P0113", "Intake air temperature circuit high input"},
            {"P0115", "Engine coolant temperature circuit"},
            {"P0116", "Engine coolant temperature circuit range / performance"},
            {"P0117", "Engine coolant temperature circuit low input"},
            {"P0118", "Engine coolant temperature circuit high input"},
            {"P0120", "Throttle / pedal position sensor A circuit"},
            {"P0121", "Throttle / pedal position sensor A circuit range / performance"},
            {"P0122", "Throttle / pedal position sensor A circuit low input"},
            {"P0123", "Throttle / pedal position sensor A circuit high input"},
            {"P0125", "Insufficient coolant temperature for closed loop fuel control"},
            {"P0128", "Coolant thermostat (coolant temperature below thermostat regulating temperature)"},
            {"P0130", "O2 sensor circuit (bank 1 sensor 1)"},
            {"P0131", "O2 sensor circuit low voltage (bank 1 sensor 1)"},
            {"P0132", "O2 sensor circuit high voltage (bank 1 sensor 1)"},
            {"P0133", "O2 sensor circuit slow response (bank 1 sensor 1)"},
            {"P0134", "O2 sensor circuit no activity detected (bank 1 sensor 1)"},
            {"P0135", "O2 sensor heater circuit (bank 1 sensor 1)"},
            {"P0136", "O2 sensor circuit (bank 1 sensor 2)"},
            {"P0141", "O2 sensor heater circuit (bank 1 sensor 2)"},
            {"P0171", "System too lean (bank 1)"},
            {"P0172", "System too rich (bank 1)"},
            {"P0174", "System too lean (bank 2)"},
            {"P0175", "System too rich (bank 2)"},
            {"P0200", "Injector circuit"},
            {"P0300", "Random / multiple cylinder misfire detected"},
            {"P0325", "Knock sensor 1 circuit (bank 1 or single sensor)"},
            {"P0335", "Crankshaft position sensor A circuit"},
            {"P0340", "Camshaft position sensor A circuit (bank 1 or single sensor)"},
            {"P0400", "Exhaust gas recirculation flow"},
            {"P0401", "Exhaust gas recirculation flow insufficient detected"},
            {"P0402", "Exhaust gas recirculation flow excessive detected"},
            {"P0420", "Catalyst system efficiency below threshold (bank 1)"},
            {"P0430", "Catalyst system efficiency below threshold (bank 2)"},
            {"P0440", "Evaporative emission system"},
            {"P0441", "Evaporative emission system incorrect purge flow"},
            {"P0442", "Evaporative emission system leak detected (small leak)"},
            {"P0446", "Evaporative emission system vent control circuit"},
            {"P0455", "Evaporative emission system leak detected (large leak)"},
            {"P0456", "Evaporative emission system leak detected (very small leak)"},
            {"P0500", "Vehicle speed sensor A"},
            {"P0505", "Idle air control system"},
            {"P0506", "Idle air control system RPM lower than expected"},
            {"P0507", "Idle air control system RPM higher than expected"},
            {"P0562", "System voltage low"},
            {"P0563", "System voltage high"},
            {"P0600", "Serial communication link"},
            {"P0606", "Control module processor"},
            {"P0700", "Transmission control system (MIL request)"},
            {"U0100", "Lost communication with ECM / PCM A"},
            {"U0101", "Lost communication with TCM"},
            {"U0121", "Lost communication with anti-lock brake system (ABS) control module"},
            {"U0140", "Lost communication with body control module"},
            {"U0155", "Lost communication with instrument panel cluster (IPC) control module"},
        };
        for (int c = 1; c <= 8; ++c) {
            h.insert(QStringLiteral("P020%1").arg(c), QStringLiteral("Injector circuit, cylinder %1").arg(c));
            h.insert(QStringLiteral("P030%1").arg(c), QStringLiteral("Cylinder %1 misfire detected").arg(c));
        }
        return h;
    }();
    return t;
}

QString powertrainArea(QChar third)
{
    switch (third.toUpper().toLatin1()) {
    case '0':
    case '1': return QStringLiteral("fuel and air metering");
    case '2': return QStringLiteral("fuel and air metering (injector circuit)");
    case '3': return QStringLiteral("ignition system or misfire");
    case '4': return QStringLiteral("auxiliary emission controls");
    case '5': return QStringLiteral("vehicle speed, idle control or auxiliary inputs");
    case '6': return QStringLiteral("computer or output circuits");
    case '7':
    case '8':
    case '9': return QStringLiteral("transmission");
    case 'A': return QStringLiteral("hybrid propulsion");
    default: return {};
    }
}
} // namespace

bool isKnown(const QString &code)
{
    return table().contains(code.trimmed().toUpper());
}

QString describe(const QString &codeIn)
{
    const QString code = codeIn.trimmed().toUpper();
    static const QRegularExpression shape(QStringLiteral("^[PCBU][0-3][0-9A-F]{3}$"));
    if (!shape.match(code).hasMatch())
        return {};
    if (const auto it = table().constFind(code); it != table().constEnd())
        return it.value();

    const QChar system = code.at(0);
    const int second = code.at(1).digitValue();
    // Generic (SAE) or manufacturer-specific, by the second character.
    // Powertrain: P0 and P2 generic, P1 manufacturer, P30-P33 manufacturer
    // and P34-P39 generic. Chassis, body, network: 0 and 3 generic, 1 and 2
    // manufacturer.
    bool generic = false;
    if (system == QLatin1Char('P'))
        generic = second == 0 || second == 2 || (second == 3 && code.at(2).isDigit() && code.at(2).digitValue() >= 4);
    else
        generic = second == 0 || second == 3;
    const QString kind = generic ? QStringLiteral("Generic") : QStringLiteral("Manufacturer-specific");
    if (system == QLatin1Char('P')) {
        const QString area = powertrainArea(code.at(2));
        return area.isEmpty() ? kind + QStringLiteral(" powertrain code")
                              : kind + QStringLiteral(" powertrain code: ") + area;
    }
    if (system == QLatin1Char('C'))
        return kind + QStringLiteral(" chassis code");
    if (system == QLatin1Char('B'))
        return kind + QStringLiteral(" body code");
    return kind + QStringLiteral(" network communication code");
}

} // namespace DtcDescriptions
