// ELM327Protocol: DTC, freeze-frame, multi-value PID and VIN parsing.
// Replies are ELM327 output with headers and spaces off; expected values
// follow SAE J1979 scaling. Ported from tests/test_elm327_dtc.py and
// tests/test_obd_extended_pids.py.

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

#include "managers/elm327protocol.h"

using StringMap = QMap<QString, double>;
Q_DECLARE_METATYPE(StringMap)

namespace {
QVector<uint8_t> hexBytes(const QString &hex)
{
    QVector<uint8_t> out;
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    for (char c : raw)
        out.append(static_cast<uint8_t>(c));
    return out;
}

StringMap decodeExtended(int pid, const QVector<uint8_t> &data)
{
    StringMap out;
    for (const auto &v : ELM327Protocol::decodeExtendedPid(1, pid, data))
        out.insert(v.first, v.second);
    return out;
}

QString readSource(const char *relative)
{
    QFile f(QStringLiteral(OCTAVE_SOURCE_DIR "/") + QLatin1String(relative));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}
} // namespace

class TstElm327Protocol : public QObject
{
    Q_OBJECT

private slots:
    void parseDtcResponse_data();
    void parseDtcResponse();
    void parseFreezeFrameDtc_data();
    void parseFreezeFrameDtc();
    void evapVaporPressureAltIsOffsetNotScaled();
    void decodeExtendedPid_data();
    void decodeExtendedPid();
    void multiFrameReplyIsJoined();
    void parseVin_data();
    void parseVin();
    void parseVinNoData();
    void decodeVin_data();
    void decodeVin();
    void everyExtendedParameterIsWired();
};

void TstElm327Protocol::parseDtcResponse_data()
{
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<int>("mode");
    QTest::addColumn<QStringList>("expected");

    // Pre-CAN: always 6 data bytes, zero padded, no count byte
    QTest::newRow("pre-CAN empty") << QStringList{"43000000000000"} << 0x03 << QStringList{};
    QTest::newRow("pre-CAN two") << QStringList{"43013304200000"} << 0x03 << QStringList{"P0133", "P0420"};
    QTest::newRow("pre-CAN one") << QStringList{"43030100000000"} << 0x03 << QStringList{"P0301"};
    // CAN: count byte after the mode byte
    QTest::newRow("CAN one") << QStringList{"43010133"} << 0x03 << QStringList{"P0133"};
    QTest::newRow("CAN none") << QStringList{"4300"} << 0x03 << QStringList{};
    QTest::newRow("CAN pending none") << QStringList{"4700"} << 0x07 << QStringList{};   // was a bogus C0700
    QTest::newRow("CAN pending one") << QStringList{"47010171"} << 0x07 << QStringList{"P0171"};
    // CAN multi-frame: byte count, then numbered frames
    QTest::newRow("multi-frame") << QStringList{"00A", "0:430401330171", "1:03000420000000"} << 0x03
                                 << QStringList{"P0133", "P0171", "P0300", "P0420"};
    // Two ECUs answering, duplicate code reported once
    QTest::newRow("two ECUs") << QStringList{"43010133", "430201330442"} << 0x03 << QStringList{"P0133", "P0442"};
    QTest::newRow("no data") << QStringList{"SEARCHING...", "NO DATA"} << 0x03 << QStringList{};
    QTest::newRow("C/B/U codes") << QStringList{"4303412381A2C0FF"} << 0x03 << QStringList{"C0123", "B01A2", "U00FF"};
}

void TstElm327Protocol::parseDtcResponse()
{
    QFETCH(QStringList, lines);
    QFETCH(int, mode);
    QFETCH(QStringList, expected);
    QCOMPARE(ELM327Protocol::parseDtcResponse(lines, mode), expected);
}

void TstElm327Protocol::parseFreezeFrameDtc_data()
{
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("code") << QStringList{"4202000133"} << QStringList{"P0133"};
    QTest::newRow("zero") << QStringList{"4202000000"} << QStringList{};
    QTest::newRow("no data") << QStringList{"NO DATA"} << QStringList{};
}

void TstElm327Protocol::parseFreezeFrameDtc()
{
    QFETCH(QStringList, lines);
    QFETCH(QStringList, expected);
    QCOMPARE(ELM327Protocol::parseFreezeFrameDtc(lines), expected);
}

void TstElm327Protocol::evapVaporPressureAltIsOffsetNotScaled()
{
    // PID 0x54: (256A + B) - 32767 Pa
    QCOMPARE(ELM327Protocol::decodePid(1, 0x54, {0x80, 0x00})->value, 1.0);
    QCOMPARE(ELM327Protocol::decodePid(1, 0x54, {0x00, 0x00})->value, -32767.0);
}

void TstElm327Protocol::decodeExtendedPid_data()
{
    QTest::addColumn<int>("pid");
    QTest::addColumn<QString>("data");
    QTest::addColumn<StringMap>("expected");

    QTest::newRow("boost") << 0x70 << "030C800FA0000000000000"
                           << StringMap{{"BOOST_PRESSURE_A_COMMANDED", 100.0}, {"BOOST_PRESSURE_A", 125.0}};
    QTest::newRow("MAF, B unsupported") << 0x66 << "0103200000" << StringMap{{"MAF_SENSOR_A", 25.0}};
    QTest::newRow("odometer") << 0xA6 << "0001E240" << StringMap{{"ODOMETER", 12345.6}};
    QTest::newRow("run time") << 0x7F << "0300000E100000012C00000000"
                              << StringMap{{"ENGINE_RUN_TIME_TOTAL", 3600.0}, {"ENGINE_IDLE_TIME_TOTAL", 300.0}};
    QTest::newRow("hybrid") << 0x9A << "06005A00FF38"
                            << StringMap{{"HYBRID_BATTERY_VOLTAGE", 360.0}, {"HYBRID_BATTERY_CURRENT", -20.0}};
    QTest::newRow("demand torque") << 0x61 << "AF" << StringMap{{"DEMAND_ENGINE_TORQUE", 50.0}};
    QTest::newRow("coolant sensor") << 0x67 << "015A00" << StringMap{{"COOLANT_TEMP_SENSOR_1", 50.0}};
    QTest::newRow("gear ratio") << 0xA4 << "02000DAC" << StringMap{{"TRANSMISSION_GEAR_RATIO", 3.5}};
    QTest::newRow("EGR") << 0x69 << "0780C064"
                         << StringMap{{"EGR_A_COMMANDED", 50.2}, {"EGR_A_ACTUAL", 75.29}, {"EGR_A_ERROR", -21.88}};
    QTest::newRow("short reply") << 0x70 << "03" << StringMap{};
}

void TstElm327Protocol::decodeExtendedPid()
{
    QFETCH(int, pid);
    QFETCH(QString, data);
    QFETCH(StringMap, expected);
    const StringMap got = decodeExtended(pid, hexBytes(data));
    QCOMPARE(got.keys(), expected.keys());
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it)
        QVERIFY2(qAbs(got.value(it.key()) - it.value()) < 0.01,
                 qPrintable(QStringLiteral("%1 = %2, want %3").arg(it.key()).arg(got.value(it.key())).arg(it.value())));
}

void TstElm327Protocol::multiFrameReplyIsJoined()
{
    const auto parsed = ELM327Protocol::parseResponseLines({"00C", "0:41700301F4", "1:0FA00000000000"}, 1, 0x70);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->mode, 1);
    QCOMPARE(parsed->pid, 0x70);
    const StringMap got = decodeExtended(0x70, parsed->dataBytes);
    QVERIFY(qAbs(got.value("BOOST_PRESSURE_A_COMMANDED") - 15.63) < 0.01);
    QVERIFY(qAbs(got.value("BOOST_PRESSURE_A") - 125.0) < 0.01);
}

void TstElm327Protocol::parseVin_data()
{
    QTest::addColumn<QStringList>("lines");
    QTest::newRow("CAN") << QStringList{"014", "0:490201314A34", "1:46413439533033", "2:50313233343536"};
    QTest::newRow("pre-CAN") << QStringList{"49020100000031", "4902024A344641", "49020334395330",
                                            "49020433503132", "49020533343536"};
}

void TstElm327Protocol::parseVin()
{
    QFETCH(QStringList, lines);
    QCOMPARE(ELM327Protocol::parseVin(lines), QStringLiteral("1J4FA49S03P123456"));
}

void TstElm327Protocol::parseVinNoData()
{
    QCOMPARE(ELM327Protocol::parseVin({"NO DATA"}), QString());
}

void TstElm327Protocol::decodeVin_data()
{
    QTest::addColumn<QString>("vin");
    QTest::addColumn<int>("year");
    QTest::addColumn<QString>("make");
    QTest::newRow("Jeep") << "1J4FA49S03P123456" << 2003 << "Jeep";
    QTest::newRow("Tesla") << "5YJ3E1EA7KF317000" << 2019 << "Tesla";
    QTest::newRow("Honda") << "1HGCM82633A004352" << 2003 << "Honda";
    QTest::newRow("bad") << "BADVIN" << 0 << "";
}

void TstElm327Protocol::decodeVin()
{
    QFETCH(QString, vin);
    QFETCH(int, year);
    QFETCH(QString, make);
    const VinInfo info = ELM327Protocol::decodeVin(vin);
    QCOMPARE(info.modelYear, year);
    QCOMPARE(info.make, make);
}

void TstElm327Protocol::everyExtendedParameterIsWired()
{
    // Each multi-value id needs a settings default and QML metadata
    QSet<QString> ids;
    for (const auto &sigs : ELM327Protocol::extendedPidTable())
        for (const ExtendedSignal &s : sigs)
            ids.insert(s.paramId);
    QVERIFY(!ids.isEmpty());

    const QString settings = readSource("src/managers/settingsmanager.cpp");
    const QString qml = readSource("frontend/OBDParameterModel.qml");
    QVERIFY(!settings.isEmpty());
    QVERIFY(!qml.isEmpty());
    for (const QString &id : std::as_const(ids)) {
        QVERIFY2(settings.contains(QLatin1Char('"') + id + QLatin1Char('"')), qPrintable(id + " has no settings default"));
        QVERIFY2(qml.contains(QStringLiteral("id: \"%1\"").arg(id)), qPrintable(id + " has no OBDParameterModel entry"));
    }
}

QTEST_GUILESS_MAIN(TstElm327Protocol)
#include "tst_elm327protocol.moc"
