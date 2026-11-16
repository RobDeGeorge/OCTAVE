// Custom PIDs (src/managers/custompids.cpp): the formula language, what the
// file reader accepts and refuses, the read-by-identifier request and reply
// (ELM327Protocol), and the rows joining ParameterCatalog. The polling side
// runs against the simulated car in tst_obdsim.

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

#include "managers/custompids.h"
#include "managers/elm327protocol.h"
#include "managers/parametercatalog.h"

class TstCustomPids : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("HOME", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp.path() + "/appdata").toUtf8());
    }

    void formulas_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<double>("expected");

        QTest::newRow("one byte offset") << "A-40" << QByteArray::fromHex("7B") << 83.0;
        QTest::newRow("two bytes scaled") << "(A*256+B)/10" << QByteArray::fromHex("0123") << 29.1;
        QTest::newRow("precedence") << "A+B*2" << QByteArray::fromHex("0A05") << 20.0;
        QTest::newRow("unary minus") << "-A+1" << QByteArray::fromHex("03") << -2.0;
        QTest::newRow("lower case, spaces") << " ( a * 256 + b ) / 4 " << QByteArray::fromHex("1AF8") << 1726.0;
        QTest::newRow("signed byte") << "SIGNED(A)" << QByteArray::fromHex("F6") << -10.0;
        QTest::newRow("signed word") << "SIGNED(A)*256+B" << QByteArray::fromHex("FF38") << -200.0;
        QTest::newRow("mask") << "A&15" << QByteArray::fromHex("A7") << 7.0;
        QTest::newRow("shift and or") << "A<<8|B" << QByteArray::fromHex("1234") << double(0x1234);
        QTest::newRow("shift right") << "A>>4" << QByteArray::fromHex("A7") << 10.0;
        QTest::newRow("bit set") << "{A:7}" << QByteArray::fromHex("80") << 1.0;
        QTest::newRow("bit clear") << "{B:0}*100" << QByteArray::fromHex("FFFE") << 0.0;
        QTest::newRow("hex number") << "A-0x28" << QByteArray::fromHex("7B") << 83.0;
        QTest::newRow("decimal factor") << "A*0.5" << QByteArray::fromHex("07") << 3.5;
        QTest::newRow("27th byte") << "AA" << QByteArray(26, '\0') + QByteArray::fromHex("2A") << 42.0;
    }

    void formulas()
    {
        QFETCH(QString, text);
        QFETCH(QByteArray, bytes);
        QFETCH(double, expected);
        QString error;
        const PidFormula f = PidFormula::compile(text, &error);
        QVERIFY2(f.isValid(), qPrintable(error));
        const auto value = f.evaluate(QVector<uint8_t>(bytes.cbegin(), bytes.cend()));
        QVERIFY(value.has_value());
        QCOMPARE(*value, expected);
    }

    void badFormulasSayWhy_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << "";
        QTest::newRow("dangling operator") << "A+";
        QTest::newRow("unclosed paren") << "(A+1";
        QTest::newRow("unknown word") << "SQRT(A)";
        QTest::newRow("three letters") << "ABC";
        QTest::newRow("bit out of range") << "{A:8}";
        QTest::newRow("trailing junk") << "A 5";
        QTest::newRow("a command, not a formula") << "A; ATZ";
    }

    void badFormulasSayWhy()
    {
        QFETCH(QString, text);
        QString error;
        QVERIFY(!PidFormula::compile(text, &error).isValid());
        QVERIFY(!error.isEmpty());
    }

    void aShortReplyOrDivisionByZeroGivesNoValue()
    {
        const PidFormula two = PidFormula::compile("A*256+B");
        QCOMPARE(two.bytesNeeded(), 2);
        QVERIFY(!two.evaluate({0x12}).has_value());
        QVERIFY(two.evaluate({0x12, 0x34, 0x56}).has_value());   // extra bytes are fine
        QVERIFY(!PidFormula::compile("100/A").evaluate({0}).has_value());
        QVERIFY(!PidFormula().evaluate({1, 2}).has_value());
    }

    void requestAndReply()
    {
        QCOMPARE(ELM327Protocol::formatDataRequest(0x22, 0x1940), QByteArray("221940\r"));
        QCOMPARE(ELM327Protocol::formatDataRequest(0x22, 0x0C), QByteArray("22000C\r"));
        QCOMPARE(ELM327Protocol::formatDataRequest(0x21, 0x01), QByteArray("2101\r"));

        QCOMPARE(ELM327Protocol::parseDataResponse({"6219407B"}, 0x22, 0x1940), QVector<uint8_t>{0x7B});
        QCOMPARE(ELM327Protocol::parseDataResponse({"62 19 40 7B"}, 0x22, 0x1940), QVector<uint8_t>{0x7B});
        QCOMPARE(ELM327Protocol::parseDataResponse({"610112AB"}, 0x21, 0x01), (QVector<uint8_t>{0x12, 0xAB}));
        // CAN multi-frame: byte count, then numbered frames
        QCOMPARE(ELM327Protocol::parseDataResponse({"00A", "0:621940010203", "1:04050607000000"}, 0x22, 0x1940),
                 (QVector<uint8_t>{1, 2, 3, 4, 5, 6, 7}));
        // A refusal, silence, another id
        QVERIFY(!ELM327Protocol::parseDataResponse({"7F2231"}, 0x22, 0x1940).has_value());
        QVERIFY(!ELM327Protocol::parseDataResponse({"NO DATA"}, 0x22, 0x1940).has_value());
        QVERIFY(!ELM327Protocol::parseDataResponse({"6219417B"}, 0x22, 0x1940).has_value());
    }

    void defaultHeaders()
    {
        QCOMPARE(ELM327Protocol::defaultHeader("A6"), QByteArray("7DF"));
        QCOMPARE(ELM327Protocol::defaultHeader("8"), QByteArray("7DF"));
        QCOMPARE(ELM327Protocol::defaultHeader("7"), QByteArray("DB33F1"));
        QCOMPARE(ELM327Protocol::defaultHeader("A2"), QByteArray("686AF1"));
        QCOMPARE(ELM327Protocol::defaultHeader("3"), QByteArray("686AF1"));
        QCOMPARE(ELM327Protocol::defaultHeader("1"), QByteArray("616AF1"));
        // KWP2000, J1939 and nonsense: not known, so headers are refused there
        QVERIFY(ELM327Protocol::defaultHeader("5").isEmpty());
        QVERIFY(ELM327Protocol::defaultHeader("A").isEmpty());
        QVERIFY(ELM327Protocol::defaultHeader("OK").isEmpty());
    }

    void parsesAFile()
    {
        const auto parsed = CustomPids::parse(R"({ "schema": 1, "pids": [
            { "id": "TRANS_TEMP", "title": "Transmission Temp", "unit": "°C", "min": -40, "max": 150,
              "kind": "temperature", "decimals": 0, "mode": "22", "pid": "1940", "header": "7e1", "formula": "A-40" },
            { "id": "BATT_MV", "pid": "0x2000", "formula": "A*256+B", "max": 16000 },
            { "id": "LOCAL_ONE", "mode": 33, "pid": "01", "formula": "A" }
        ] })");
        QVERIFY2(parsed.errors.isEmpty(), qPrintable(parsed.errors.join("; ")));
        QCOMPARE(parsed.pids.size(), 3);
        const CustomPid &t = parsed.pids.at(0);
        QCOMPARE(t.meta.id, QStringLiteral("TRANS_TEMP"));
        QCOMPARE(t.meta.kind, QStringLiteral("temperature"));
        QCOMPARE(t.meta.min, -40.0);
        QCOMPARE(t.mode, 0x22);
        QCOMPARE(t.pid, 0x1940);
        QCOMPARE(t.header, QStringLiteral("7E1"));
        // Defaults: mode 22, kind numeric, title = id, no header
        const CustomPid &b = parsed.pids.at(1);
        QCOMPARE(b.mode, 0x22);
        QCOMPARE(b.pid, 0x2000);
        QCOMPARE(b.meta.kind, QStringLiteral("numeric"));
        QCOMPARE(b.meta.title, QStringLiteral("BATT_MV"));
        QVERIFY(b.header.isEmpty());
        QCOMPARE(parsed.pids.at(2).mode, 0x21);
    }

    void refusesWhatCouldWriteToTheCar_data()
    {
        QTest::addColumn<QByteArray>("entry");
        QTest::addColumn<QString>("mentions");
        // Anything but read-by-identifier: clear codes, actuator tests, writes
        QTest::newRow("mode 04") << QByteArray(R"({"id":"X1","mode":"04","pid":"00","formula":"A"})") << "mode";
        QTest::newRow("mode 2E write") << QByteArray(R"({"id":"X1","mode":"2E","pid":"1940","formula":"A"})") << "mode";
        QTest::newRow("mode 31 routine") << QByteArray(R"({"id":"X1","mode":"31","pid":"01","formula":"A"})") << "mode";
        // A header is hex digits only: never text for the adapter
        QTest::newRow("header with a command") << QByteArray(R"({"id":"X1","pid":"1940","header":"7E1\rATZ","formula":"A"})") << "header";
        QTest::newRow("header of odd length") << QByteArray(R"({"id":"X1","pid":"1940","header":"7E","formula":"A"})") << "header";
        QTest::newRow("pid too long") << QByteArray(R"({"id":"X1","pid":"194000","formula":"A"})") << "pid";
        QTest::newRow("pid not hex") << QByteArray(R"({"id":"X1","pid":"19 40 ATZ","formula":"A"})") << "pid";
        QTest::newRow("mode 21 with a two-byte id") << QByteArray(R"({"id":"X1","mode":"21","pid":"1940","formula":"A"})") << "pid";
        QTest::newRow("built-in id") << QByteArray(R"({"id":"RPM","pid":"1940","formula":"A"})") << "already";
        QTest::newRow("bad id") << QByteArray(R"({"id":"trans temp","pid":"1940","formula":"A"})") << "id";
        QTest::newRow("bad formula") << QByteArray(R"({"id":"X1","pid":"1940","formula":"A+"})") << "formula";
        QTest::newRow("no formula") << QByteArray(R"({"id":"X1","pid":"1940"})") << "formula";
        QTest::newRow("bad kind") << QByteArray(R"({"id":"X1","pid":"1940","formula":"A","kind":"speed"})") << "kind";
        QTest::newRow("range upside down") << QByteArray(R"({"id":"X1","pid":"1940","formula":"A","min":5,"max":5})") << "max";
    }

    void refusesWhatCouldWriteToTheCar()
    {
        QFETCH(QByteArray, entry);
        QFETCH(QString, mentions);
        const auto parsed = CustomPids::parse("{\"pids\":[" + entry + "]}");
        QVERIFY(parsed.pids.isEmpty());
        QCOMPARE(parsed.errors.size(), 1);
        QVERIFY2(parsed.errors.first().contains(mentions), qPrintable(parsed.errors.first()));
    }

    void aBadEntryLeavesTheOthers()
    {
        const auto parsed = CustomPids::parse(R"([
            {"id":"GOOD_ONE","pid":"1940","formula":"A"},
            {"id":"BAD_ONE","pid":"1940","formula":"A+"},
            {"id":"GOOD_ONE","pid":"1941","formula":"A"},
            {"id":"GOOD_TWO","pid":"1941","formula":"B"}
        ])");
        QCOMPARE(parsed.pids.size(), 2);
        QCOMPARE(parsed.errors.size(), 2);
        QVERIFY(parsed.errors.at(0).startsWith("Entry 2 (BAD_ONE)"));
        QVERIFY(parsed.errors.at(1).contains("used by an entry above"));
        QCOMPARE(CustomPids::parse("{ not json").errors.size(), 1);
        QVERIFY(CustomPids::parse("{}").errors.isEmpty());
    }

    // Last: these fix the catalog's table for the process
    void theExampleFileLoadsIntoTheCatalog()
    {
        const QString path = m_tmp.path() + "/custom-pids.json";
        {
            CustomPids none(path);
            QVERIFY(!none.fileExists());
            QVERIFY(none.pids().isEmpty());
            QVERIFY(none.errors().isEmpty());
            QVERIFY(none.createExample());
            QVERIFY(none.fileExists());
            QVERIFY(!none.createExample());   // never over a file that exists
        }
        CustomPids pids(path);
        QVERIFY2(pids.errors().isEmpty(), qPrintable(pids.errors().join("; ")));
        QCOMPARE(pids.pids().size(), 1);
        const QVariantMap row = pids.pids().first().toMap();
        QCOMPARE(row.value("id").toString(), QStringLiteral("TRANS_TEMP"));
        QCOMPARE(row.value("pid").toString(), QStringLiteral("1940"));
        QCOMPARE(row.value("header").toString(), QStringLiteral("7E1"));

        const QList<CustomPidRequest> requests = pids.requests();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().decoder({0x7B}), 83.0);
        QVERIFY(std::isnan(requests.first().decoder({})));

        const ParameterCatalog::Parameter *p = ParameterCatalog::find("TRANS_TEMP");
        QVERIFY(p);
        QVERIFY(p->custom);
        QCOMPARE(p->group, QStringLiteral("Custom"));
        QCOMPARE(p->kind, QStringLiteral("temperature"));
        QVERIFY(ParameterCatalog::groups().contains("Custom"));
        QVERIFY(ParameterCatalog::obdIds().contains("TRANS_TEMP"));
        QVERIFY(!ParameterCatalog::find("RPM")->custom);
        // The table is fixed now: a later file can't change it
        QVERIFY(!ParameterCatalog::addCustom({}));
    }

private:
    QTemporaryDir m_tmp;
};

QTEST_GUILESS_MAIN(TstCustomPids)
#include "tst_custompids.moc"
