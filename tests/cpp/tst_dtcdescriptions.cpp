// DtcDescriptions (src/managers/dtcdescriptions.{h,cpp}): the meaning of a
// common generic code, and for any other code only what its structure
// says: system, generic or manufacturer-specific, the powertrain area.

#include <QTest>

#include "managers/dtcdescriptions.h"

class TstDtcDescriptions : public QObject
{
    Q_OBJECT

private slots:
    void describes_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("catalyst") << "P0420" << "Catalyst system efficiency below threshold (bank 1)";
        QTest::newRow("lowercase") << " p0171 " << "System too lean (bank 1)";
        QTest::newRow("misfire 4") << "P0304" << "Cylinder 4 misfire detected";
        QTest::newRow("injector 2") << "P0202" << "Injector circuit, cylinder 2";
        QTest::newRow("network") << "U0100" << "Lost communication with ECM / PCM A";
        // Not in the table: the category only
        QTest::newRow("generic ignition") << "P0399" << "Generic powertrain code: ignition system or misfire";
        QTest::newRow("generic P2") << "P2096" << "Generic powertrain code: fuel and air metering";
        QTest::newRow("manufacturer P1") << "P1443" << "Manufacturer-specific powertrain code: auxiliary emission controls";
        QTest::newRow("P30 manufacturer") << "P3011" << "Manufacturer-specific powertrain code: fuel and air metering";
        QTest::newRow("P34 generic") << "P3400" << "Generic powertrain code: auxiliary emission controls";
        QTest::newRow("transmission") << "P0731" << "Generic powertrain code: transmission";
        QTest::newRow("hybrid") << "P0A80" << "Generic powertrain code: hybrid propulsion";
        QTest::newRow("chassis") << "C0035" << "Generic chassis code";
        QTest::newRow("body B1") << "B1234" << "Manufacturer-specific body code";
        QTest::newRow("network U2") << "U2100" << "Manufacturer-specific network communication code";
        QTest::newRow("network U3") << "U3000" << "Generic network communication code";
        QTest::newRow("not a code") << "hello" << "";
        QTest::newRow("bad digit") << "P4000" << "";
        QTest::newRow("empty") << "" << "";
    }
    void describes()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        QCOMPARE(DtcDescriptions::describe(code), text);
    }

    void knowsWhichAreSpecific()
    {
        QVERIFY(DtcDescriptions::isKnown("P0420"));
        QVERIFY(DtcDescriptions::isKnown("p0308"));
        QVERIFY(!DtcDescriptions::isKnown("P0309"));   // V8 at most
        QVERIFY(!DtcDescriptions::isKnown("P1443"));
    }
};

QTEST_GUILESS_MAIN(TstDtcDescriptions)
#include "tst_dtcdescriptions.moc"
