// One set of display rules (src/util/units): track and library times, the
// compass point for a heading, and the unit conversions. Pages used to carry
// four time formatters (two without hours, one in seconds) and two compass
// tables.

#include <QTest>

#include <cmath>
#include <limits>

#include "util/units.h"

class TstUnits : public QObject
{
    Q_OBJECT

private slots:
    void durations_data()
    {
        QTest::addColumn<double>("ms");
        QTest::addColumn<QString>("text");
        QTest::newRow("zero") << 0.0 << "0:00";
        QTest::newRow("under a second") << 999.0 << "0:00";
        QTest::newRow("59.999 s") << 59999.0 << "0:59";
        QTest::newRow("a minute") << 60000.0 << "1:00";
        QTest::newRow("just under an hour") << 3599999.0 << "59:59";
        QTest::newRow("an hour") << 3600000.0 << "1:00:00";
        QTest::newRow("70 min podcast") << 4212000.0 << "1:10:12";
        QTest::newRow("library total") << 13311000.0 << "3:41:51";
        QTest::newRow("negative") << -5000.0 << "0:00";
        QTest::newRow("NaN") << std::numeric_limits<double>::quiet_NaN() << "0:00";
        QTest::newRow("infinite") << std::numeric_limits<double>::infinity() << "0:00";
    }

    void durations()
    {
        QFETCH(double, ms);
        QFETCH(QString, text);
        QCOMPARE(UnitsFn::formatDuration(ms), text);
    }

    void secondsForDownloadHits()
    {
        Units u;
        QCOMPARE(u.formatSeconds(200), QStringLiteral("3:20"));
    }

    void cardinals_data()
    {
        QTest::addColumn<double>("deg");
        QTest::addColumn<QString>("point");
        QTest::newRow("0") << 0.0 << "N";
        QTest::newRow("22.4") << 22.4 << "N";
        QTest::newRow("22.5") << 22.5 << "NE";
        QTest::newRow("90") << 90.0 << "E";
        QTest::newRow("180") << 180.0 << "S";
        QTest::newRow("270") << 270.0 << "W";
        QTest::newRow("315") << 315.0 << "NW";
        QTest::newRow("337.5") << 337.5 << "N";
        QTest::newRow("359") << 359.0 << "N";
        QTest::newRow("360") << 360.0 << "N";
        QTest::newRow("NaN") << std::numeric_limits<double>::quiet_NaN() << "N";
    }

    void cardinals()
    {
        QFETCH(double, deg);
        QFETCH(QString, point);
        QCOMPARE(UnitsFn::cardinal(deg), point);
    }

    void conversions()
    {
        QCOMPARE(UnitsFn::cToF(0), 32.0);
        QCOMPARE(UnitsFn::cToF(100), 212.0);
        QCOMPARE(UnitsFn::fToC(212), 100.0);
        QVERIFY(std::abs(UnitsFn::mToFt(1000) - 3280.84) < 1e-6);
        QVERIFY(std::abs(UnitsFn::mphToKph(60) - 96.56064) < 1e-9);
        QVERIFY(std::abs(UnitsFn::kphToMph(UnitsFn::mphToKph(42)) - 42) < 1e-9);
        QVERIFY(std::abs(UnitsFn::linearToDb(2.0) - 6.0206) < 1e-4);
        QVERIFY(std::abs(UnitsFn::dbToLinear(-20) - 0.1) < 1e-12);
        QCOMPARE(UnitsFn::linearToDb(0), -120.0);
    }
};

QTEST_GUILESS_MAIN(TstUnits)
#include "tst_units.moc"
