#include "derivedparameters.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Petrol: 745 g/L, 3.78541 L to the US gallon
constexpr double kGramsPerLitre = 745.0;
constexpr double kGramsPerGallon = kGramsPerLitre * 3.78541;
constexpr double kStoichAfr = 14.7;
// Full-throttle petrol engine: a gram of air a second burns 1/14.7 g of
// fuel at 44 kJ/g, about 30 % of which reaches the crank (0.9 kW, 1.2 hp).
// At a cruise the engine is less efficient, so this reads high there
constexpr double kHpPerGramSecond = 1.2;
constexpr double kNmToLbFt = 0.737562;
constexpr double kKpaToPsi = 0.1450377;
constexpr double kSeaLevelKpa = 101.325;
constexpr double kSixtyMph = 60.0;
constexpr qint64 kRunTimeoutMs = 60000;
constexpr qint64 kForever = std::numeric_limits<qint64>::max();

enum Quantity { Air, Fuel, Torque, Power, Boost, QuantityCount };
constexpr int kAny = -1;    // no scan result: whichever way has fresh inputs
constexpr int kNone = -2;   // this car has none of the ways

struct Method {
    QStringList inputs;
    bool needsDisplacement = false;
};

const QStringList kSpeedDensity{QStringLiteral("INTAKE_PRESSURE"), QStringLiteral("INTAKE_TEMP"), QStringLiteral("RPM")};

// The ways to work each quantity out, best first. The index is what
// DerivedParameters::method() returns and the instance switches on
const QList<Method> &methods(int quantity)
{
    static const QList<Method> table[QuantityCount] = {
        // Air
        {{{QStringLiteral("MAF")}}, {kSpeedDensity, true}},
        // Fuel
        {{{QStringLiteral("FUEL_RATE")}},
         {{QStringLiteral("ENGINE_FUEL_RATE_GS")}},
         {{QStringLiteral("MAF")}},
         {kSpeedDensity, true}},
        // Torque
        {{{QStringLiteral("ACTUAL_ENGINE_TORQUE"), QStringLiteral("REFERENCE_TORQUE")}},
         {{QStringLiteral("MAF"), QStringLiteral("RPM")}},
         {kSpeedDensity, true}},
        // Power
        {{{QStringLiteral("ACTUAL_ENGINE_TORQUE"), QStringLiteral("REFERENCE_TORQUE"), QStringLiteral("RPM")}},
         {{QStringLiteral("MAF")}},
         {kSpeedDensity, true}},
        // Boost
        {{{QStringLiteral("INTAKE_PRESSURE"), QStringLiteral("BAROMETRIC_PRESSURE")}},
         {{QStringLiteral("INTAKE_PRESSURE")}}},
    };
    return table[quantity];
}

struct Output {
    QString id;
    int quantity;        // -1: none
    QStringList extra;   // inputs besides the quantity's
    bool needsTank = false;
};

const QList<Output> &outputs()
{
    const QString speed = QStringLiteral("SPEED");
    static const QList<Output> list{
        {QStringLiteral("ENGINE_POWER"), Power, {}},
        {QStringLiteral("ENGINE_TORQUE"), Torque, {}},
        {QStringLiteral("FUEL_ECONOMY"), Fuel, {speed}},
        {QStringLiteral("FUEL_FLOW"), Fuel, {}},
        {QStringLiteral("BOOST"), Boost, {}},
        {QStringLiteral("TRIP_DISTANCE"), -1, {speed}},
        {QStringLiteral("TRIP_FUEL_USED"), Fuel, {}},
        {QStringLiteral("TRIP_FUEL_ECONOMY"), Fuel, {speed}},
        {QStringLiteral("FUEL_RANGE"), Fuel, {speed, QStringLiteral("FUEL_LEVEL")}, true},
        {QStringLiteral("ZERO_TO_SIXTY"), -1, {speed}},
    };
    return list;
}

const Output *findOutput(const QString &id)
{
    for (const Output &o : outputs())
        if (o.id == id)
            return &o;
    return nullptr;
}

bool configAllows(const Method &m, const DerivedParameters::Config &config)
{
    return !m.needsDisplacement || config.displacementL > 0.0;
}

bool hasAll(const QSet<QString> &supported, const QStringList &ids)
{
    return std::all_of(ids.cbegin(), ids.cend(), [&supported](const QString &id) { return supported.contains(id); });
}

// The way a car reporting `supported` uses for `quantity`, or kNone
int pick(int quantity, const QSet<QString> &supported, const DerivedParameters::Config &config)
{
    const QList<Method> &list = methods(quantity);
    for (int i = 0; i < list.size(); ++i)
        if (configAllows(list.at(i), config) && hasAll(supported, list.at(i).inputs))
            return i;
    return kNone;
}

float rounded(double v, int decimals)
{
    const double f = std::pow(10.0, decimals);
    return static_cast<float>(std::round(v * f) / f);
}

} // namespace

// ── What a car can have, and what to poll for it ─────────────────────

QStringList DerivedParameters::ids()
{
    QStringList l;
    for (const Output &o : outputs())
        l.append(o.id);
    return l;
}

bool DerivedParameters::isDerived(const QString &id)
{
    return findOutput(id) != nullptr;
}

QStringList DerivedParameters::available(const QSet<QString> &supported, const Config &config)
{
    QStringList l;
    if (supported.isEmpty())
        return l;
    for (const Output &o : outputs())
        if (!inputs(o.id, supported, config).isEmpty())
            l.append(o.id);
    return l;
}

QSet<QString> DerivedParameters::inputs(const QString &id, const QSet<QString> &supported, const Config &config)
{
    const Output *o = findOutput(id);
    if (!o || (o->needsTank && config.tankGallons <= 0.0))
        return {};
    QSet<QString> in(o->extra.cbegin(), o->extra.cend());
    const bool torqueFigures = o->quantity == Torque || o->quantity == Power;
    const QString friction = QStringLiteral("ENGINE_FRICTION_TORQUE");

    if (supported.isEmpty()) {
        // Nothing known about the car: ask for every way's inputs
        if (o->quantity >= 0) {
            bool any = false;
            for (const Method &m : methods(o->quantity)) {
                if (!configAllows(m, config))
                    continue;
                any = true;
                in.unite(QSet<QString>(m.inputs.cbegin(), m.inputs.cend()));
            }
            if (!any)
                return {};
            if (torqueFigures)
                in.insert(friction);
        }
        return in;
    }

    if (!hasAll(supported, o->extra))
        return {};
    if (o->quantity >= 0) {
        const int i = pick(o->quantity, supported, config);
        if (i == kNone)
            return {};
        const QStringList &m = methods(o->quantity).at(i).inputs;
        in.unite(QSet<QString>(m.cbegin(), m.cend()));
        if (torqueFigures && i == 0 && supported.contains(friction))
            in.insert(friction);
    }
    return in;
}

// ── The arithmetic ───────────────────────────────────────────────────

double DerivedParameters::speedDensityAirflow(double mapKpa, double intakeC, double rpm, double displacementL)
{
    // A four-stroke fills its cylinders once every two turns: litres a
    // second at manifold pressure, then moles by the gas law (kPa * L = J),
    // then grams (dry air, 28.97 g/mol)
    const double litresPerSecond = displacementL * rpm / 120.0 * kVolumetricEfficiency;
    const double kelvin = intakeC + 273.15;
    if (kelvin <= 0.0 || litresPerSecond <= 0.0 || mapKpa <= 0.0)
        return 0.0;
    return mapKpa * litresPerSecond / (8.314 * kelvin) * 28.97;
}

double DerivedParameters::powerFromTorque(double torqueNm, double rpm)
{
    return torqueNm * rpm * 2.0 * M_PI / 60.0 / 745.7;
}

double DerivedParameters::powerFromAirflow(double airGs)
{
    return std::max(0.0, airGs) * kHpPerGramSecond;
}

double DerivedParameters::fuelFromAirflow(double airGs)
{
    return std::max(0.0, airGs) / kStoichAfr;
}

double DerivedParameters::mpg(double speedMph, double fuelGs)
{
    if (speedMph < 0.5)
        return 0.0;
    const double gallonsPerHour = std::max(0.0, fuelGs) * 3600.0 / kGramsPerGallon;
    if (gallonsPerHour * kMaxMpg <= speedMph)
        return kMaxMpg;   // coasting with the fuel cut
    return speedMph / gallonsPerHour;
}

// ── The instance ─────────────────────────────────────────────────────

void DerivedParameters::setConfig(const Config &config)
{
    if (config == m_config)
        return;
    m_config = config;
    m_triggersBuilt = false;
}

void DerivedParameters::setSupported(const QSet<QString> &supported)
{
    if (supported == m_supported)
        return;
    m_supported = supported;
    m_triggersBuilt = false;
}

void DerivedParameters::rebuildTriggers()
{
    m_triggers.clear();
    for (const Output &o : outputs()) {
        const QSet<QString> in = inputs(o.id, m_supported, m_config);
        for (const QString &id : in)
            m_triggers[id].append(o.id);
    }
    m_triggersBuilt = true;
}

void DerivedParameters::resetTrip()
{
    m_tripMiles = 0.0;
    m_tripGallons = 0.0;
}

void DerivedParameters::connected(qint64 nowMs)
{
    if (m_lastIngestMs < 0 || nowMs - m_lastIngestMs > kSameTripMs)
        resetTrip();
    m_readings.clear();
    m_lastSpeedMs = -1;
    m_lastFuelMs = -1;
    m_lastStillMs = -1;
    m_run = Run::Idle;
}

std::optional<double> DerivedParameters::fresh(const char *id, qint64 nowMs, qint64 maxAgeMs) const
{
    const auto it = m_readings.constFind(QLatin1String(id));
    if (it == m_readings.constEnd() || it->atMs < 0)
        return std::nullopt;
    if (maxAgeMs != kForever && nowMs - it->atMs > maxAgeMs)
        return std::nullopt;
    return static_cast<double>(it->value);
}

int DerivedParameters::method(int quantity) const
{
    return m_supported.isEmpty() ? kAny : pick(quantity, m_supported, m_config);
}

std::optional<double> DerivedParameters::airflow(qint64 nowMs) const
{
    const int m = method(Air);
    if (m == kAny || m == 0) {
        if (const auto maf = fresh("MAF", nowMs))
            return maf;
    }
    if ((m == kAny || m == 1) && m_config.displacementL > 0.0) {
        const auto map = fresh("INTAKE_PRESSURE", nowMs);
        const auto rpm = fresh("RPM", nowMs);
        const auto iat = fresh("INTAKE_TEMP", nowMs, kSlowMaxAgeMs);
        if (map && rpm && iat)
            return speedDensityAirflow(*map, *iat, *rpm, m_config.displacementL);
    }
    return std::nullopt;
}

std::optional<double> DerivedParameters::fuelFlow(qint64 nowMs) const
{
    const int m = method(Fuel);
    if (m == kAny || m == 0) {
        if (const auto litresPerHour = fresh("FUEL_RATE", nowMs))
            return *litresPerHour * kGramsPerLitre / 3600.0;
    }
    if (m == kAny || m == 1) {
        if (const auto gs = fresh("ENGINE_FUEL_RATE_GS", nowMs))
            return gs;
    }
    if (m == kAny || m >= 2) {
        // The air methods in the same order: fuel's 2 and 3 are air's 0 and 1
        if (const auto air = airflow(nowMs))
            return fuelFromAirflow(*air);
    }
    return std::nullopt;
}

std::optional<double> DerivedParameters::torqueNm(qint64 nowMs) const
{
    const int m = method(Torque);
    if (m == kAny || m == 0) {
        const auto percent = fresh("ACTUAL_ENGINE_TORQUE", nowMs);
        const auto reference = fresh("REFERENCE_TORQUE", nowMs, kForever);   // a constant of the engine
        if (percent && reference) {
            // The engine reports what its cylinders make; what reaches the
            // crank is that less its own friction, when it reports that too
            const double friction = fresh("ENGINE_FRICTION_TORQUE", nowMs, kSlowMaxAgeMs).value_or(0.0);
            return std::max(0.0, (*percent - std::max(0.0, friction)) * *reference / 100.0);
        }
    }
    if (m == kAny || m >= 1) {
        const auto air = airflow(nowMs);
        const auto rpm = fresh("RPM", nowMs);
        if (air && rpm) {
            if (*rpm < 300.0)
                return 0.0;
            return powerFromAirflow(*air) * 745.7 / (*rpm * 2.0 * M_PI / 60.0);
        }
    }
    return std::nullopt;
}

std::optional<double> DerivedParameters::powerHp(qint64 nowMs) const
{
    const int m = method(Power);
    if (m == kAny || m == 0) {
        const auto rpm = fresh("RPM", nowMs);
        const auto percent = fresh("ACTUAL_ENGINE_TORQUE", nowMs);
        const auto reference = fresh("REFERENCE_TORQUE", nowMs, kForever);
        if (rpm && percent && reference) {
            const double friction = fresh("ENGINE_FRICTION_TORQUE", nowMs, kSlowMaxAgeMs).value_or(0.0);
            const double nm = std::max(0.0, (*percent - std::max(0.0, friction)) * *reference / 100.0);
            return powerFromTorque(nm, *rpm);
        }
    }
    if (m == kAny || m >= 1) {
        if (const auto air = airflow(nowMs))
            return powerFromAirflow(*air);
    }
    return std::nullopt;
}

std::optional<double> DerivedParameters::boostPsi(qint64 nowMs) const
{
    const auto map = fresh("INTAKE_PRESSURE", nowMs);
    if (!map)
        return std::nullopt;
    const double baro = fresh("BAROMETRIC_PRESSURE", nowMs, kSlowMaxAgeMs).value_or(kSeaLevelKpa);
    return (*map - baro) * kKpaToPsi;
}

void DerivedParameters::addSpeedSample(double mph, qint64 nowMs, QList<Sample> &out)
{
    const double previous = m_lastSpeed;
    const qint64 previousMs = m_lastSpeedMs;
    const bool continuous = previousMs >= 0 && nowMs > previousMs && nowMs - previousMs <= kMaxAgeMs;
    if (continuous)
        m_tripMiles += (previous + mph) / 2.0 * static_cast<double>(nowMs - previousMs) / 3.6e6;

    // 0-60: from a standstill, timed to the moment the speed crosses 60 mph
    // (between two readings, so placed on the line between them). The start
    // is half way between the last reading at rest and the first one moving
    if (mph < 1.0) {
        m_run = Run::Armed;
        m_lastStillMs = nowMs;
    } else if (m_run == Run::Armed) {
        m_run = Run::Running;
        m_runStartMs = (m_lastStillMs + nowMs) / 2;
    } else if (m_run == Run::Running) {
        // Given up: a gap in the readings, a lift, or it is taking for ever
        if (!continuous || mph < previous - 3.0 || nowMs - m_runStartMs > kRunTimeoutMs)
            m_run = Run::Idle;
    }
    if (m_run == Run::Running && mph >= kSixtyMph) {
        double crossedMs = static_cast<double>(nowMs);
        if (continuous && previous < kSixtyMph && mph > previous)
            crossedMs = static_cast<double>(previousMs)
                        + (kSixtyMph - previous) / (mph - previous) * static_cast<double>(nowMs - previousMs);
        out.append({QStringLiteral("ZERO_TO_SIXTY"),
                    rounded((crossedMs - static_cast<double>(m_runStartMs)) / 1000.0, 2)});
        m_run = Run::Idle;
    }

    m_lastSpeed = mph;
    m_lastSpeedMs = nowMs;
}

QList<DerivedParameters::Sample> DerivedParameters::ingest(const QString &id, float value, qint64 nowMs)
{
    m_lastIngestMs = nowMs;
    if (!m_triggersBuilt)
        rebuildTriggers();
    const auto trigger = m_triggers.constFind(id);
    if (trigger == m_triggers.constEnd())
        return {};
    m_readings[id] = {value, nowMs};

    QList<Sample> out;
    if (id == QLatin1String("SPEED"))
        addSpeedSample(static_cast<double>(value), nowMs, out);

    // Fuel burnt since the last time the flow was worked out
    std::optional<double> fuel;
    if (trigger->contains(QLatin1String("FUEL_FLOW"))) {
        fuel = fuelFlow(nowMs);
        if (fuel) {
            if (m_lastFuelMs >= 0 && nowMs > m_lastFuelMs && nowMs - m_lastFuelMs <= kMaxAgeMs)
                m_tripGallons += (m_lastFuelGs + *fuel) / 2.0 * static_cast<double>(nowMs - m_lastFuelMs) / 1000.0
                                 / kGramsPerGallon;
            m_lastFuelGs = *fuel;
            m_lastFuelMs = nowMs;
        }
    } else {
        fuel = fuelFlow(nowMs);
    }

    const bool tripAverageReady = m_tripMiles >= kMinTripMiles && m_tripGallons > 0.0;
    const double tripMpg = tripAverageReady ? std::min(m_tripMiles / m_tripGallons, kMaxMpg) : 0.0;

    for (const QString &derived : *trigger) {
        if (derived == QLatin1String("ENGINE_POWER")) {
            if (const auto hp = powerHp(nowMs))
                out.append({derived, rounded(*hp, 1)});
        } else if (derived == QLatin1String("ENGINE_TORQUE")) {
            if (const auto nm = torqueNm(nowMs))
                out.append({derived, rounded(*nm * kNmToLbFt, 1)});
        } else if (derived == QLatin1String("FUEL_FLOW")) {
            if (fuel)
                out.append({derived, rounded(*fuel * 3600.0 / kGramsPerGallon, 2)});
        } else if (derived == QLatin1String("FUEL_ECONOMY")) {
            const auto speed = fresh("SPEED", nowMs);
            if (fuel && speed)
                out.append({derived, rounded(mpg(*speed, *fuel), 1)});
        } else if (derived == QLatin1String("BOOST")) {
            if (const auto psi = boostPsi(nowMs))
                out.append({derived, rounded(*psi, 2)});
        } else if (derived == QLatin1String("TRIP_DISTANCE")) {
            out.append({derived, rounded(m_tripMiles, 2)});
        } else if (derived == QLatin1String("TRIP_FUEL_USED")) {
            if (m_lastFuelMs >= 0)
                out.append({derived, rounded(m_tripGallons, 3)});
        } else if (derived == QLatin1String("TRIP_FUEL_ECONOMY")) {
            if (tripAverageReady)
                out.append({derived, rounded(tripMpg, 1)});
        } else if (derived == QLatin1String("FUEL_RANGE")) {
            const auto level = fresh("FUEL_LEVEL", nowMs, kSlowMaxAgeMs);
            if (tripAverageReady && level)
                out.append({derived, rounded(std::clamp(*level, 0.0, 100.0) / 100.0 * m_config.tankGallons * tripMpg, 0)});
        }
        // ZERO_TO_SIXTY comes from addSpeedSample(), once per run
    }
    return out;
}
