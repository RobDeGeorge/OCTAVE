// DerivedParameters works out readings the car does not report (horsepower,
// torque, fuel economy, fuel flow, boost, the trip sums, range, 0-60) from
// the ones it does. Pins: which of them a car can have and what is polled
// for each (the best way the car has inputs for, one way per car), the
// arithmetic, stale inputs giving no value, the trip sums over time and
// across a reconnect, and that the catalog lists exactly these ids.

#include <QTest>

#include "managers/derivedparameters.h"
#include "managers/parametercatalog.h"

using Config = DerivedParameters::Config;

namespace {
QSet<QString> set(std::initializer_list<const char *> ids)
{
    QSet<QString> s;
    for (const char *id : ids)
        s.insert(QString::fromLatin1(id));
    return s;
}

// The value of `id` among what an ingest() returned, or NaN
double valueOf(const QList<DerivedParameters::Sample> &samples, const char *id)
{
    for (const auto &s : samples)
        if (s.first == QLatin1String(id))
            return s.second;
    return qQNaN();
}
} // namespace

class TstDerivedParameters : public QObject
{
    Q_OBJECT

private slots:
    void catalogListsTheSameIds()
    {
        QCOMPARE(ParameterCatalog::derivedIds(), DerivedParameters::ids());
        for (const QString &id : DerivedParameters::ids()) {
            QVERIFY(DerivedParameters::isDerived(id));
            QCOMPARE(ParameterCatalog::find(id)->group, QStringLiteral("Calculated"));
            // Not something to ask the adapter for
            QVERIFY(!ParameterCatalog::obdIds().contains(id));
        }
        QVERIFY(!DerivedParameters::isDerived("RPM"));
    }

    void whatACarCanHave()
    {
        // Nothing known about the car: nothing offered
        QVERIFY(DerivedParameters::available({}, {}).isEmpty());

        // A modern car: everything but range, which needs the tank's size
        const auto modern = set({"RPM", "SPEED", "MAF", "INTAKE_PRESSURE", "BAROMETRIC_PRESSURE", "FUEL_LEVEL",
                                 "ACTUAL_ENGINE_TORQUE", "REFERENCE_TORQUE", "FUEL_RATE"});
        QStringList all = DerivedParameters::ids();
        QCOMPARE(DerivedParameters::available(modern, Config{0.0, 15.0}), all);
        all.removeAll(QStringLiteral("FUEL_RANGE"));
        QCOMPARE(DerivedParameters::available(modern, {}), all);

        // An old speed-density car (no air flow sensor, no torque figures):
        // only what speed and manifold pressure give, until the engine's
        // size is known
        const auto old = set({"RPM", "SPEED", "INTAKE_PRESSURE", "INTAKE_TEMP", "FUEL_LEVEL"});
        QCOMPARE(DerivedParameters::available(old, Config{0.0, 19.0}),
                 (QStringList{"BOOST", "TRIP_DISTANCE", "ZERO_TO_SIXTY"}));
        QCOMPARE(DerivedParameters::available(old, Config{4.0, 19.0}), DerivedParameters::ids());

        // No speed: no economy
        QVERIFY(!DerivedParameters::available(set({"MAF", "RPM"}), {}).contains("FUEL_ECONOMY"));
        QVERIFY(DerivedParameters::available(set({"MAF", "RPM"}), {}).contains("ENGINE_POWER"));
    }

    void whatIsPolled()
    {
        // The best way the car has inputs for, and only that one
        const auto modern = set({"RPM", "SPEED", "MAF", "ACTUAL_ENGINE_TORQUE", "REFERENCE_TORQUE",
                                 "ENGINE_FRICTION_TORQUE", "FUEL_RATE", "INTAKE_PRESSURE"});
        QCOMPARE(DerivedParameters::inputs("ENGINE_POWER", modern, {}),
                 set({"ACTUAL_ENGINE_TORQUE", "REFERENCE_TORQUE", "RPM", "ENGINE_FRICTION_TORQUE"}));
        QCOMPARE(DerivedParameters::inputs("FUEL_ECONOMY", modern, {}), set({"FUEL_RATE", "SPEED"}));
        QCOMPARE(DerivedParameters::inputs("BOOST", modern, {}), set({"INTAKE_PRESSURE"}));

        const auto maf = set({"RPM", "SPEED", "MAF"});
        QCOMPARE(DerivedParameters::inputs("ENGINE_POWER", maf, {}), set({"MAF"}));
        QCOMPARE(DerivedParameters::inputs("ENGINE_TORQUE", maf, {}), set({"MAF", "RPM"}));
        QCOMPARE(DerivedParameters::inputs("TRIP_FUEL_ECONOMY", maf, {}), set({"MAF", "SPEED"}));

        const auto old = set({"RPM", "SPEED", "INTAKE_PRESSURE", "INTAKE_TEMP"});
        QVERIFY(DerivedParameters::inputs("ENGINE_POWER", old, {}).isEmpty());
        QCOMPARE(DerivedParameters::inputs("ENGINE_POWER", old, Config{4.0, 0.0}),
                 set({"INTAKE_PRESSURE", "INTAKE_TEMP", "RPM"}));

        // No scan result: every way's inputs (the adapter answers NO DATA
        // for the ones the car lacks)
        QCOMPARE(DerivedParameters::inputs("ENGINE_POWER", {}, {}),
                 set({"ACTUAL_ENGINE_TORQUE", "REFERENCE_TORQUE", "RPM", "ENGINE_FRICTION_TORQUE", "MAF"}));
        QVERIFY(DerivedParameters::inputs("RPM", modern, {}).isEmpty());
    }

    void arithmetic()
    {
        // 300 Nm at 5252 rpm is 221 hp (and 221 lb-ft)
        QVERIFY(qAbs(DerivedParameters::powerFromTorque(300.0, 5252.0) - 221.3) < 0.2);
        // A 4.0 L at 3000 rpm, sea level, 27 °C breathes about 99 g/s
        QVERIFY(qAbs(DerivedParameters::speedDensityAirflow(100.0, 26.85, 3000.0, 4.0) - 98.7) < 0.5);
        QCOMPARE(DerivedParameters::speedDensityAirflow(100.0, 20.0, 0.0, 4.0), 0.0);
        // The usual MAF formula: MPG = 7.107 * km/h / MAF
        QVERIFY(qAbs(DerivedParameters::mpg(60.0, DerivedParameters::fuelFromAirflow(20.0))
                     - 7.107 * 96.56 / 20.0) < 0.3);
        QCOMPARE(DerivedParameters::mpg(0.0, 0.5), 0.0);                                    // idling
        QCOMPARE(DerivedParameters::mpg(50.0, 0.0), DerivedParameters::kMaxMpg);            // fuel cut
    }

    void horsepowerFromTheEnginesOwnFigures()
    {
        DerivedParameters d;
        d.setSupported(set({"RPM", "ACTUAL_ENGINE_TORQUE", "REFERENCE_TORQUE", "MAF"}));
        d.connected(0);
        QVERIFY(d.ingest("REFERENCE_TORQUE", 400.0f, 1000).isEmpty());   // one input: no value yet
        QVERIFY(d.ingest("ACTUAL_ENGINE_TORQUE", 75.0f, 1100).size() == 1);   // torque alone
        const auto out = d.ingest("RPM", 5252.0f, 1200);
        // 75 % of 400 Nm = 300 Nm = 221 lb-ft, 221 hp at 5252 rpm
        QVERIFY(qAbs(valueOf(out, "ENGINE_POWER") - 221.3) < 0.3);
        QVERIFY(qAbs(valueOf(d.ingest("ACTUAL_ENGINE_TORQUE", 75.0f, 1300), "ENGINE_TORQUE") - 221.3) < 0.3);
        // The air flow way is not this car's for power: MAF moves the fuel
        // readings only
        const auto fromMaf = d.ingest("MAF", 50.0f, 1400);
        QVERIFY(qIsNaN(valueOf(fromMaf, "ENGINE_POWER")));
        QVERIFY(!qIsNaN(valueOf(fromMaf, "FUEL_FLOW")));

        // The reference torque is a constant, the rest goes stale
        const auto later = d.ingest("RPM", 3000.0f, 1300 + DerivedParameters::kMaxAgeMs + 1);
        QVERIFY(qIsNaN(valueOf(later, "ENGINE_POWER")));
        const auto again = d.ingest("ACTUAL_ENGINE_TORQUE", 50.0f, 1300 + DerivedParameters::kMaxAgeMs + 2);
        QVERIFY(qAbs(valueOf(again, "ENGINE_POWER") - DerivedParameters::powerFromTorque(200.0, 3000.0)) < 0.2);
    }

    void frictionComesOff()
    {
        DerivedParameters d;
        d.setSupported(set({"RPM", "ACTUAL_ENGINE_TORQUE", "REFERENCE_TORQUE", "ENGINE_FRICTION_TORQUE"}));
        d.connected(0);
        d.ingest("REFERENCE_TORQUE", 400.0f, 10);
        d.ingest("ENGINE_FRICTION_TORQUE", 10.0f, 20);
        const auto out = d.ingest("ACTUAL_ENGINE_TORQUE", 60.0f, 30);
        QVERIFY(qAbs(valueOf(out, "ENGINE_TORQUE") - 200.0 * 0.737562) < 0.1);   // (60 - 10) % of 400 Nm
    }

    void airFlowWays()
    {
        // A car with an air flow sensor and nothing better
        DerivedParameters maf;
        maf.setSupported(set({"RPM", "SPEED", "MAF"}));
        maf.connected(0);
        QVERIFY(qAbs(valueOf(maf.ingest("MAF", 100.0f, 10), "ENGINE_POWER")
                     - DerivedParameters::powerFromAirflow(100.0)) < 0.1);
        maf.ingest("SPEED", 60.0f, 20);
        QVERIFY(qAbs(valueOf(maf.ingest("MAF", 20.0f, 30), "FUEL_ECONOMY") - 34.5) < 0.2);

        // A speed-density car needs its engine's size
        DerivedParameters sd;
        sd.setSupported(set({"RPM", "SPEED", "INTAKE_PRESSURE", "INTAKE_TEMP"}));
        sd.connected(0);
        sd.ingest("INTAKE_TEMP", 26.85f, 10);
        sd.ingest("INTAKE_PRESSURE", 100.0f, 20);
        QVERIFY(qIsNaN(valueOf(sd.ingest("RPM", 3000.0f, 30), "ENGINE_POWER")));
        sd.setConfig(Config{4.0, 0.0});
        sd.ingest("INTAKE_TEMP", 26.85f, 32);
        sd.ingest("INTAKE_PRESSURE", 100.0f, 34);
        const double hp = valueOf(sd.ingest("RPM", 3000.0f, 40), "ENGINE_POWER");
        QVERIFY2(qAbs(hp - DerivedParameters::powerFromAirflow(98.7)) < 1.0, qPrintable(QString::number(hp)));
    }

    void boostAndVacuum()
    {
        DerivedParameters d;
        d.setSupported(set({"INTAKE_PRESSURE", "BAROMETRIC_PRESSURE"}));
        d.connected(0);
        d.ingest("BAROMETRIC_PRESSURE", 100.0f, 10);
        QVERIFY(qAbs(valueOf(d.ingest("INTAKE_PRESSURE", 200.0f, 20), "BOOST") - 14.5) < 0.01);
        QVERIFY(qAbs(valueOf(d.ingest("INTAKE_PRESSURE", 30.0f, 30), "BOOST") + 10.15) < 0.01);   // idle vacuum

        // No barometer: sea level
        DerivedParameters plain;
        plain.setSupported(set({"INTAKE_PRESSURE"}));
        plain.connected(0);
        QVERIFY(qAbs(valueOf(plain.ingest("INTAKE_PRESSURE", 101.325f, 10), "BOOST")) < 0.01);
    }

    void tripSums()
    {
        DerivedParameters d;
        d.setSupported(set({"SPEED", "FUEL_RATE", "FUEL_LEVEL"}));
        d.setConfig(Config{0.0, 20.0});
        d.connected(0);
        // An hour at 60 mph burning 2 gal/h (7.571 L/h), a reading a second
        qint64 t = 1000;
        QList<DerivedParameters::Sample> out;
        d.ingest("FUEL_LEVEL", 50.0f, t);
        for (int i = 0; i <= 3600; ++i, t += 1000) {
            d.ingest("FUEL_LEVEL", 50.0f, t);
            d.ingest("FUEL_RATE", 7.571f, t);
            out = d.ingest("SPEED", 60.0f, t);
        }
        QVERIFY(qAbs(valueOf(out, "TRIP_DISTANCE") - 60.0) < 0.05);
        QVERIFY(qAbs(valueOf(out, "TRIP_FUEL_ECONOMY") - 30.0) < 0.1);
        QVERIFY(qAbs(valueOf(out, "FUEL_ECONOMY") - 30.0) < 0.1);
        // Half of a 20 gallon tank at 30 MPG
        QVERIFY(qAbs(valueOf(out, "FUEL_RANGE") - 300.0) < 1.5);
        QVERIFY(qAbs(valueOf(d.ingest("FUEL_RATE", 7.571f, t), "TRIP_FUEL_USED") - 2.0) < 0.01);

        // A gap in the readings adds nothing (the adapter was away)
        t += 60000;
        out = d.ingest("SPEED", 60.0f, t);
        QVERIFY(qAbs(valueOf(out, "TRIP_DISTANCE") - 60.0) < 0.05);

        // A reconnect in the same drive keeps the sums, a new drive starts them
        d.connected(t + 1000);
        QVERIFY(qAbs(valueOf(d.ingest("SPEED", 60.0f, t + 2000), "TRIP_DISTANCE") - 60.0) < 0.05);
        const qint64 nextDay = t + 2000 + DerivedParameters::kSameTripMs + 1;
        d.connected(nextDay);
        out = d.ingest("SPEED", 30.0f, nextDay + 10);
        QCOMPARE(valueOf(out, "TRIP_DISTANCE"), 0.0);
        QVERIFY(qIsNaN(valueOf(out, "TRIP_FUEL_ECONOMY")));   // not until some of a drive is in
    }

    void zeroToSixty()
    {
        DerivedParameters d;
        d.setSupported(set({"SPEED"}));
        d.connected(0);
        // At rest, then 10 mph more every second: 60 after 6 s
        qint64 t = 1000;
        QVERIFY(qIsNaN(valueOf(d.ingest("SPEED", 0.0f, t), "ZERO_TO_SIXTY")));
        double measured = qQNaN();
        for (int mph = 5; mph <= 75; mph += 10) {   // 5, 15 ... 55, 65: crosses 60 between two readings
            t += 1000;
            const double v = valueOf(d.ingest("SPEED", float(mph), t), "ZERO_TO_SIXTY");
            if (!qIsNaN(v)) {
                QVERIFY(qIsNaN(measured));   // once per run
                measured = v;
            }
        }
        // Started half way through the first second, crossed half way through the last
        QVERIFY2(qAbs(measured - 6.0) < 0.05, qPrintable(QString::number(measured)));

        // Already moving: no run. A lift on the way: no time
        QVERIFY(qIsNaN(valueOf(d.ingest("SPEED", 80.0f, t + 1000), "ZERO_TO_SIXTY")));
        t += 10000;
        d.ingest("SPEED", 0.0f, t);
        d.ingest("SPEED", 30.0f, t + 1000);
        d.ingest("SPEED", 20.0f, t + 2000);
        QVERIFY(qIsNaN(valueOf(d.ingest("SPEED", 70.0f, t + 3000), "ZERO_TO_SIXTY")));
    }
};

QTEST_GUILESS_MAIN(TstDerivedParameters)
#include "tst_derivedparameters.moc"
