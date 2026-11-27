#ifndef DERIVEDPARAMETERS_H
#define DERIVEDPARAMETERS_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

/**
 * @brief Readings the car does not report, worked out from the ones it does:
 *        horsepower, torque, fuel economy, fuel flow, boost, the trip's
 *        distance / fuel / average, range and the 0-60 time.
 *
 * Each is a ParameterCatalog row with source Derived and comes out of
 * OBDManager::parameterSampled like a polled PID, so the value store, the
 * gauges, the black box and the service see it with no code of their own.
 *
 * A reading can have several ways to be worked out, best first (horsepower:
 * the engine's own torque figures, else the air it breathes, else that air
 * estimated from manifold pressure and the engine's size). The way used is
 * the first whose inputs this car has, so one car always uses one way.
 *
 * OBDManager owns one: the static half says which derived readings a car
 * can have and which PIDs each needs polled, the instance does the sums.
 * No Qt object, no thread of its own (called on OBDManager's).
 */
class DerivedParameters
{
public:
    struct Config {
        double displacementL = 0.0;   // engine size; 0: not known
        double tankGallons = 0.0;     // fuel tank; 0: not known
        bool operator==(const Config &o) const
        {
            return displacementL == o.displacementL && tankGallons == o.tankGallons;
        }
    };
    using Sample = QPair<QString, float>;

    // Every derived id, in catalog order
    static QStringList ids();
    static bool isDerived(const QString &id);
    // The derived readings a car reporting `supported` can have. Empty for
    // an empty set (no scan result: nothing is known about the car)
    static QStringList available(const QSet<QString> &supported, const Config &config);
    // The PIDs to poll for `id`: the inputs of the way this car uses, or of
    // every way when `supported` is empty. Empty when the car can't have it
    static QSet<QString> inputs(const QString &id, const QSet<QString> &supported, const Config &config);

    void setConfig(const Config &config);
    // This session's scan result (OBDManager::setVehicleSupport)
    void setSupported(const QSet<QString> &supported);
    // A connection was made. The trip sums start again unless the last
    // reading is under kSameTripMs old (a reconnect in the same drive)
    void connected(qint64 nowMs);
    // A reading arrived: the derived readings it moves, with their values
    QList<Sample> ingest(const QString &id, float value, qint64 nowMs);

    // An input older than this is not used (kSlowMaxAgeMs for the ones that
    // barely move: barometric pressure, intake temperature, fuel level)
    static constexpr qint64 kMaxAgeMs = 5000;
    static constexpr qint64 kSlowMaxAgeMs = 120000;
    static constexpr qint64 kSameTripMs = 5 * 60 * 1000;
    // Trip average and range wait for this much of a drive
    static constexpr double kMinTripMiles = 0.25;

    // The arithmetic, public for the tests
    // Air into the engine, g/s, from manifold pressure (kPa), intake
    // temperature (°C), engine speed and size, at kVolumetricEfficiency
    static double speedDensityAirflow(double mapKpa, double intakeC, double rpm, double displacementL);
    static double powerFromTorque(double torqueNm, double rpm);   // hp
    static double powerFromAirflow(double airGs);                 // hp, petrol
    static double fuelFromAirflow(double airGs);                  // g/s, petrol at 14.7:1
    static double mpg(double speedMph, double fuelGs);            // capped at kMaxMpg

    static constexpr double kVolumetricEfficiency = 0.85;
    static constexpr double kMaxMpg = 99.9;

private:
    struct Reading {
        float value = 0;
        qint64 atMs = -1;
    };
    std::optional<double> fresh(const char *id, qint64 nowMs, qint64 maxAgeMs = kMaxAgeMs) const;
    int method(int quantity) const;   // index into the quantity's methods, -1: by what is fresh
    std::optional<double> airflow(qint64 nowMs) const;      // g/s
    std::optional<double> fuelFlow(qint64 nowMs) const;     // g/s
    std::optional<double> torqueNm(qint64 nowMs) const;
    std::optional<double> powerHp(qint64 nowMs) const;
    std::optional<double> boostPsi(qint64 nowMs) const;
    void rebuildTriggers();
    void addSpeedSample(double mph, qint64 nowMs, QList<Sample> &out);
    void resetTrip();

    Config m_config;
    QSet<QString> m_supported;
    QHash<QString, Reading> m_readings;
    QHash<QString, QStringList> m_triggers;   // input id -> derived ids it moves
    bool m_triggersBuilt = false;
    qint64 m_lastIngestMs = -1;

    // Trip sums
    double m_tripMiles = 0.0;
    double m_tripGallons = 0.0;
    double m_lastSpeed = 0.0;
    qint64 m_lastSpeedMs = -1;
    double m_lastFuelGs = 0.0;
    qint64 m_lastFuelMs = -1;

    // 0-60: armed at a standstill, running from the first movement
    enum class Run { Idle, Armed, Running };
    Run m_run = Run::Idle;
    qint64 m_runStartMs = 0;
    qint64 m_lastStillMs = -1;
};

#endif // DERIVEDPARAMETERS_H
