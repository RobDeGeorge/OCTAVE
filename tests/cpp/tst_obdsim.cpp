// The real OBDManager against the simulated ELM327 (src/sim/simelm327.cpp):
// connect, VIN decode, vehicle scan, demand-driven polling of single and
// multi-value PIDs, trouble codes, and a lost link that reconnects. The
// whole shipped OBD stack runs; only the adapter is simulated (src/sim/,
// built in every configuration).
//
// The slots share one manager and run in declaration order.

#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <limits>
#include <memory>

#include "sim/carsim.h"
#include "sim/simelm327.h"
#include "managers/busmonitor.h"
#include "managers/obdmanager.h"
#include "managers/settingsmanager.h"

using namespace OctaveSim;

class TstObdSim : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("HOME", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp.path() + "/appdata").toUtf8());

        m_settings = std::make_unique<SettingsManager>();
        m_sim = std::make_unique<CarSim>();
        m_sim->setRealisticMode(false);   // fast, no NO DATA drops: deterministic
        const std::shared_ptr<CarState> state = m_sim->state();
        OBDConnectionWorker::setCustomTransport(
            [](const QString &port) { return port == QLatin1String(kSimPort); },
            [state](const QString &, QObject *parent) -> QIODevice * { return new SimElm327Device(state, parent); });
        m_obd = std::make_unique<OBDManager>(m_settings.get());
        // Custom PIDs (custompids.h builds these from the user's file): the
        // simulated transmission ECU's fluid temperature, the engine ECU's
        // battery millivolts, and an id nobody answers
        const double nan = std::numeric_limits<double>::quiet_NaN();
        m_obd->setCustomPids({
            {"SIM_TRANS_TEMP", 0x22, 0x1940, "7E1", [nan](const QVector<uint8_t> &b) { return b.size() >= 1 ? b[0] - 40.0 : nan; }},
            {"SIM_BATT_MV", 0x22, 0x2000, "", [nan](const QVector<uint8_t> &b) { return b.size() >= 2 ? b[0] * 256.0 + b[1] : nan; }},
            {"SIM_MISSING", 0x22, 0x9999, "7E1", [nan](const QVector<uint8_t> &b) { return b.size() >= 1 ? double(b[0]) : nan; }},
        });
        m_obd->setPortOverride(QString::fromLatin1(kSimPort));
    }

    void cleanupTestCase()
    {
        m_obd.reset();
        OBDConnectionWorker::setCustomTransport({}, {});
    }

    void connectsAndReadsVin()
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 15000);
        QCOMPARE(m_obd->getConnectionStatus(), QStringLiteral("Connected"));
        QTRY_COMPARE_WITH_TIMEOUT(m_obd->vin(), QStringLiteral("1J4FA49S03P123456"), 5000);
        QCOMPARE(m_obd->vehicleModelYear(), 2003);
        QCOMPARE(m_obd->vehicleMake(), QStringLiteral("Jeep"));
    }

    void vehicleScanFindsEverySimulatedParameter()
    {
        // The manager scans 2 s after connecting
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->vehicleScanComplete(), 15000);
        const QStringList supported = m_obd->vehicleSupportedParameters();
        for (const char *id : {"RPM", "SPEED", "COOLANT_TEMP", "OIL_TEMP", "ELM_VOLTAGE",
                               "BOOST_PRESSURE_A", "ODOMETER", "ACTUAL_ENGINE_TORQUE"})
            QVERIFY2(supported.contains(QLatin1String(id)), id);
    }

    void polledValuesFollowTheSimulation()
    {
        QSignalSpy coolant(m_obd.get(), &OBDManager::coolantTempChanged);
        QSignalSpy speed(m_obd.get(), &OBDManager::speedMPHChanged);
        QSignalSpy extended(m_obd.get(), &OBDManager::obdParameterChanged);
        m_sim->simulateOverheating();   // coolant held around 115 C
        m_sim->setThrottle(60);
        m_obd->setParameterDemand("test", {"COOLANT_TEMP", "SPEED", "BOOST_PRESSURE_A"});

        QTRY_VERIFY_WITH_TIMEOUT(!coolant.isEmpty(), 10000);
        QVERIFY(qAbs(coolant.last().at(0).toFloat() - 115.0f) <= 1.0f);
        QTRY_VERIFY_WITH_TIMEOUT(!speed.isEmpty() && speed.last().at(0).toFloat() > 5.0f, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(extended.cbegin(), extended.cend(), [](const QList<QVariant> &a) {
            return a.at(0).toString() == QLatin1String("BOOST_PRESSURE_A") && a.at(1).toFloat() > 100.0f;
        }), 10000);

        m_sim->setThrottle(0);
        m_sim->resetToNormal();
    }

    void customPidsAreReadFromTheirEcuAndTheHeaderPutBack()
    {
        // The scan can't see them; the file says the car has them
        QVERIFY(m_obd->vehicleSupportedParameters().contains(QStringLiteral("SIM_TRANS_TEMP")));

        QSignalSpy values(m_obd.get(), &OBDManager::obdParameterChanged);
        QSignalSpy sampled(m_obd.get(), &OBDManager::parameterSampled);
        QSignalSpy coolant(m_obd.get(), &OBDManager::coolantTempChanged);
        auto readings = [&values](const char *id) {
            QList<float> out;
            for (const QList<QVariant> &a : values) {
                if (a.at(0).toString() == QLatin1String(id))
                    out.append(a.at(1).toFloat());
            }
            return out;
        };
        m_obd->setParameterDemand("test", {"SIM_TRANS_TEMP", "SIM_BATT_MV", "SIM_MISSING", "COOLANT_TEMP"});

        QTRY_VERIFY_WITH_TIMEOUT(!readings("SIM_TRANS_TEMP").isEmpty() && !readings("SIM_BATT_MV").isEmpty(), 10000);
        const CarValues car = m_sim->state()->values();
        QVERIFY(qAbs(readings("SIM_TRANS_TEMP").last() - (car.oil - 8.0)) <= 3.0);
        QVERIFY(qAbs(readings("SIM_BATT_MV").last() - car.voltage * 1000.0) <= 500.0);
        // They reach the parameter store and the black box like any reading
        QVERIFY(std::any_of(sampled.cbegin(), sampled.cend(), [](const QList<QVariant> &a) {
            return a.at(0).toString() == QLatin1String("SIM_TRANS_TEMP");
        }));

        // The simulated engine ECU is deaf while the adapter addresses the
        // transmission's: coolant only keeps arriving between transmission
        // readings if the default header went back after each one
        coolant.clear();
        values.clear();
        QTRY_VERIFY_WITH_TIMEOUT(coolant.size() >= 3 && readings("SIM_TRANS_TEMP").size() >= 3, 10000);
        QVERIFY(readings("SIM_MISSING").isEmpty());
        QVERIFY(m_obd->isConnected());

        m_obd->setParameterDemand("test", {"COOLANT_TEMP", "SPEED", "BOOST_PRESSURE_A"});
    }

    void busMonitorListensAndPutsTheAdapterBack()
    {
        QTemporaryDir logs;
        BusMonitor bus(m_obd.get(), logs.path());
        QSignalSpy coolant(m_obd.get(), &OBDManager::coolantTempChanged);
        auto row = [&bus](const char *id) {
            bus.refresh();
            for (const QVariant &v : bus.ids()) {
                if (v.toMap().value("id").toString() == QLatin1String(id))
                    return v.toMap();
            }
            return QVariantMap();
        };

        QVERIFY(bus.available());
        QVERIFY(!bus.start("7E"));          // not a message id
        QVERIFY(!bus.note().isEmpty());
        QVERIFY(!bus.active());

        // Everything: the simulated bus's six broadcast messages
        QVERIFY(bus.start());
        QTRY_VERIFY_WITH_TIMEOUT(bus.active(), 5000);
        QVERIFY(bus.note().isEmpty());
        QVERIFY(bus.startRecording());
        QTRY_VERIFY_WITH_TIMEOUT(row("0C9").value("count").toInt() >= 20 && !row("120").isEmpty(), 8000);
        const QVariantMap engine = row("0C9");
        QCOMPARE(engine.value("bytes").toStringList().size(), 8);
        QVERIFY(qAbs(engine.value("periodMs").toInt() - 20) <= 10);
        // Its rolling counter (the last byte) changes with every message
        QCOMPARE(engine.value("changed").toList().last().toBool(), true);
        // Engine speed is in bytes 1-2, as the simulated car has it
        const QStringList bytes = engine.value("bytes").toStringList();
        const int rpm = (bytes.at(1) + bytes.at(2)).toInt(nullptr, 16) / 4;
        QVERIFY(qAbs(rpm - m_sim->state()->values().rpm) <= 1500);
        // The adapter is listening, not answering: no readings meanwhile
        coolant.clear();
        QTest::qWait(600);
        QCOMPARE(coolant.size(), 0);
        // Its buffer fills on an unfiltered bus; listening carries on
        QTRY_VERIFY_WITH_TIMEOUT(bus.overruns() >= 1, 15000);
        const int heard = bus.frameCount();
        QTRY_VERIFY_WITH_TIMEOUT(bus.frameCount() > heard + 50, 5000);
        QVERIFY(bus.active());

        const QString file = bus.logFile();
        bus.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!bus.active(), 5000);
        QVERIFY(!bus.recording());
        QFile log(file);
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QByteArray first = log.readLine().trimmed();
        QVERIFY2(QRegularExpression(QStringLiteral("^\\(\\d+\\.\\d{6}\\) can0 [0-9A-F]{3}#[0-9A-F]{16}$"))
                     .match(QString::fromLatin1(first)).hasMatch(), first.constData());

        // Polling resumes: the simulated adapter puts an id in front of
        // every reply until message ids are turned off again
        coolant.clear();
        QTRY_VERIFY_WITH_TIMEOUT(coolant.size() >= 3, 10000);
        QVERIFY(m_obd->isConnected());

        // One id: nothing else comes through, and the buffer never fills
        QVERIFY(bus.start("4c1"));
        QTRY_VERIFY_WITH_TIMEOUT(bus.active(), 5000);
        QCOMPARE(bus.filter(), QStringLiteral("4C1"));
        QTRY_VERIFY_WITH_TIMEOUT(row("4C1").value("count").toInt() >= 10, 8000);
        bus.refresh();
        QCOMPARE(bus.ids().size(), 1);
        QCOMPARE(bus.overruns(), 0);

        // Any other request ends it: here, reading the trouble codes
        QSignalSpy codes(m_obd.get(), &OBDManager::dtcCodesChanged);
        m_obd->read_dtc();
        QTRY_VERIFY_WITH_TIMEOUT(!bus.active(), 5000);
        QVERIFY(!bus.note().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!codes.isEmpty(), 5000);
        // The filter is gone too: replies reach us again
        coolant.clear();
        QTRY_VERIFY_WITH_TIMEOUT(coolant.size() >= 3, 10000);
        QVERIFY(m_obd->isConnected());
    }

    void troubleCodesReadAndClear()
    {
        QSignalSpy codes(m_obd.get(), &OBDManager::dtcCodesChanged);
        QSignalSpy mil(m_obd.get(), &OBDManager::milStatusChanged);
        QSignalSpy cleared(m_obd.get(), &OBDManager::dtcClearResult);

        m_obd->read_status();
        QTRY_VERIFY_WITH_TIMEOUT(!mil.isEmpty(), 10000);
        QCOMPARE(mil.last().at(0).toBool(), true);
        QCOMPARE(m_obd->get_dtc_count(), 2);

        m_obd->read_dtc();
        QTRY_VERIFY_WITH_TIMEOUT(!codes.isEmpty(), 10000);
        QStringList got;
        for (const QVariant &c : codes.last().at(0).toList())
            got << c.toMap().value("code").toString();
        QCOMPARE(got, (QStringList{"P0300", "P0420"}));

        m_obd->clear_dtc();
        QTRY_VERIFY_WITH_TIMEOUT(!cleared.isEmpty(), 10000);
        QCOMPARE(cleared.last().at(0).toBool(), true);
        codes.clear();
        m_obd->read_dtc();
        QTRY_VERIFY_WITH_TIMEOUT(!codes.isEmpty(), 10000);
        QVERIFY(codes.last().at(0).toList().isEmpty());
    }

    // The ECU refuses (7F 04 22, usual with the engine running): that is
    // not "cleared", and the codes on screen stay
    void aRefusedClearKeepsTheCodes()
    {
        m_sim->state()->updateValues([](CarValues &c) { c.storedDtcs = {QStringLiteral("P0171")}; });
        m_sim->state()->updateLink([](LinkSettings &l) { l.refuseClear = true; });
        QSignalSpy codes(m_obd.get(), &OBDManager::dtcCodesChanged);
        QSignalSpy cleared(m_obd.get(), &OBDManager::dtcClearResult);
        m_obd->read_dtc();
        QTRY_VERIFY_WITH_TIMEOUT(!codes.isEmpty(), 10000);
        QCOMPARE(codes.last().at(0).toList().size(), 1);
        codes.clear();

        m_obd->clear_dtc();
        QTRY_VERIFY_WITH_TIMEOUT(!cleared.isEmpty(), 10000);
        QCOMPARE(cleared.last().at(0).toBool(), false);
        QVERIFY2(cleared.last().at(1).toString().contains("refused"), qPrintable(cleared.last().at(1).toString()));
        QVERIFY(codes.isEmpty());   // the list was not wiped

        m_sim->state()->updateLink([](LinkSettings &l) { l.refuseClear = false; });
        m_obd->clear_dtc();
        QTRY_COMPARE_WITH_TIMEOUT(cleared.size(), 2, 10000);
        QCOMPARE(cleared.last().at(0).toBool(), true);
    }

    void freezeFrameCarriesItsValues()
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 25000);
        m_sim->state()->updateValues([](CarValues &c) { c.freezeFrameDtc = QStringLiteral("P0420"); });
        QSignalSpy frame(m_obd.get(), &OBDManager::freezeFrameChanged);
        m_obd->read_freeze_frame();
        QTRY_VERIFY_WITH_TIMEOUT(!frame.isEmpty(), 15000);
        const QVariantList list = frame.last().at(0).toList();
        QCOMPARE(list.size(), 1);
        const QVariantMap item = list.first().toMap();
        QCOMPARE(item.value("code").toString(), QStringLiteral("P0420"));
        QCOMPARE(item.value("description").toString(), QStringLiteral("Catalyst system efficiency below threshold (bank 1)"));
        // The frame's values by paramStore id, native units
        const QVariantMap values = item.value("values").toMap();
        QVERIFY2(values.contains("RPM") && values.contains("COOLANT_TEMP") && values.contains("SPEED"),
                 qPrintable(QStringList(values.keys()).join(", ")));
        QVERIFY(values.value("RPM").toDouble() > 300);
        QCOMPARE(m_obd->get_freeze_frame(), list);
    }

    void lostLinkReconnects()
    {
        m_sim->setLinkUp(false);   // adapter unplugged
        QTRY_VERIFY_WITH_TIMEOUT(!m_obd->isConnected(), 10000);
        QCOMPARE(m_obd->connectionState(), OBDManager::ConnectionState::Disconnected);
        QVERIFY(!m_obd->vehicleScanComplete());   // the next car may support a different set

        // Plugged back in: the passive scan (every 10 s) reconnects
        m_sim->setLinkUp(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 25000);
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->vehicleScanComplete(), 15000);
    }

    void closeExitsSimulationAndExplicitSelectionReconnects()
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 25000);
        QCOMPARE(m_obd->connectionState(), OBDManager::ConnectionState::Connected);
        QSignalSpy status(m_obd.get(), &OBDManager::connectionStatusChanged);
        m_obd->close();
        // connected, connectionState and connectionStatus all notify on
        // connectionStatusChanged: a subscriber must hear the change
        QVERIFY(!m_obd->isConnected());
        QCOMPARE(status.size(), 1);
        QCOMPARE(status.first().at(0).toString(), QStringLiteral("Disconnected"));
        QCOMPARE(m_obd->getConnectionStatus(), QStringLiteral("Disconnected"));
        QCOMPARE(m_obd->connectionState(), OBDManager::ConnectionState::Disconnected);
        // Closed means closed: no reconnect behind the user's back
        QTest::qWait(3000);
        QCOMPARE(m_obd->connectionState(), OBDManager::ConnectionState::Disconnected);
        m_obd->close();   // again: nothing new to say
        QCOMPARE(status.size(), 1);

        QVERIFY(!m_obd->isSimulated());
        // Close exits demo mode; explicitly select it again before reconnecting.
        m_obd->setPortOverride(QString::fromLatin1(kSimPort));
        m_obd->reset_connection();
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 15000);
        QCOMPARE(m_obd->connectionState(), OBDManager::ConnectionState::Connected);
    }

    // A gear chosen by hand stays chosen (the model used to put its own
    // back within one 16 ms step)
    void aGearChosenByHandStays()
    {
        m_sim->setGear(5);
        QTest::qWait(300);
        QCOMPARE(m_sim->state()->values().gear, 5);
        m_sim->downshift();
        QTest::qWait(100);
        QCOMPARE(m_sim->state()->values().gear, 4);
    }

    // A connect that arrives while connected (a retry timer that outlived
    // its reason, a second tap) must not rebuild the working link
    void aLateConnectLeavesALiveLinkAlone()
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 15000);
        QSignalSpy status(m_obd.get(), &OBDManager::connectionStatusChanged);
        m_obd->connect_obd();
        QTest::qWait(1500);
        QVERIFY(m_obd->isConnected());
        QCOMPARE(status.size(), 0);
    }

    // A reconnect in the middle of the vehicle scan: the next scan still runs
    void aScanCutShortDoesNotBlockTheNext()
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->vehicleScanComplete(), 15000);
        m_obd->scan_vehicle();
        QVERIFY(m_obd->is_scanning());
        m_obd->reconnect();
        QVERIFY(!m_obd->is_scanning());
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->vehicleScanComplete(), 15000);
    }

    // Ignition off: the adapter answers and no ECU does. That is "No
    // Vehicle", and the car is found once it is switched on.
    void ignitionOffIsNoVehicleUntilTheCarWakes()
    {
        m_sim->state()->updateLink([](LinkSettings &l) { l.ignitionOff = true; });
        m_obd->reconnect();
        QTRY_COMPARE_WITH_TIMEOUT(m_obd->getConnectionStatus(), QStringLiteral("No Vehicle"), 30000);
        QVERIFY(!m_obd->isConnected());
        m_sim->state()->updateLink([](LinkSettings &l) { l.ignitionOff = false; });
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 30000);
    }

    // Something on the port that never answers is a failed adapter, not a
    // silent car
    void aSilentPortIsAFailedAdapter()
    {
        m_sim->state()->updateLink([](LinkSettings &l) { l.silent = true; });
        m_obd->reconnect();
        QTRY_COMPARE_WITH_TIMEOUT(m_obd->getConnectionStatus(), QStringLiteral("Connection Failed"), 45000);
        m_sim->state()->updateLink([](LinkSettings &l) { l.silent = false; });
        m_obd->reconnect();
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 20000);
    }

    // A car with no engine speed PID (some EVs) is still a car
    void aCarWithoutRpmConnects()
    {
        m_sim->state()->updateLink([](LinkSettings &l) { l.noRpm = true; });
        m_obd->reconnect();
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->vehicleScanComplete(), 15000);
        QVERIFY(!m_obd->vehicleSupportedParameters().contains(QStringLiteral("RPM")));
        QVERIFY(m_obd->vehicleSupportedParameters().contains(QStringLiteral("SPEED")));
        m_sim->state()->updateLink([](LinkSettings &l) { l.noRpm = false; });
    }

    // Replies that trickle in a few bytes at a time (a slow serial link)
    // are put back together
    void repliesInPiecesStillDecode()
    {
        m_sim->state()->updateLink([](LinkSettings &l) { l.chunkBytes = 3; });
        m_obd->reconnect();
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 30000);
        QSignalSpy coolant(m_obd.get(), &OBDManager::coolantTempChanged);
        m_obd->setParameterDemand("pieces", {"COOLANT_TEMP"});
        QTRY_VERIFY_WITH_TIMEOUT(!coolant.isEmpty(), 10000);
        const float c = coolant.last().at(0).toFloat();
        QVERIFY2(c > 40.0f && c < 130.0f, qPrintable(QString::number(c)));
        m_obd->setParameterDemand("pieces", {});
        m_sim->state()->updateLink([](LinkSettings &l) { l.chunkBytes = 0; });
    }

    // Headers on (ATH1): a reply of several frames comes as the frames on
    // the bus, each behind the ECU's id, not as the headers-off "014 / 0: /
    // 1:" form. The worker reads its diagnostics with headers off, so this
    // talks to the simulated adapter itself, as a terminal would
    void headersOnMultiFrameRepliesCarryTheEcuId()
    {
        CarSim sim;
        sim.setRealisticMode(false);
        SimElm327Device elm(sim.state());
        QVERIFY(elm.open(QIODevice::ReadWrite));
        const auto ask = [&elm](const QByteArray &command) {
            elm.write(command + "\r");
            QByteArray in;
            QElapsedTimer t;
            t.start();
            while (!in.contains('>') && t.elapsed() < 3000) {
                if (elm.waitForReadyRead(50))
                    in += elm.readAll();
            }
            QStringList lines;
            for (const QByteArray &line : in.split('\r'))
                if (!line.trimmed().isEmpty() && line.trimmed() != ">")
                    lines.append(QString::fromLatin1(line.trimmed()));
            return lines;
        };
        ask("ATZ");
        ask("ATE0");
        QCOMPARE(ask("ATH1"), QStringList{"OK"});

        // The VIN: 20 bytes, a first frame and two consecutive ones
        const QStringList vin = ask("0902");
        QCOMPARE(vin.size(), 3);
        QCOMPARE(vin.at(0).left(18), QStringLiteral("7E8 10 14 49 02 01"));
        QVERIFY2(vin.at(1).startsWith("7E8 21 "), qPrintable(vin.at(1)));
        QVERIFY2(vin.at(2).startsWith("7E8 22 "), qPrintable(vin.at(2)));
        QByteArray payload;
        for (int i = 0; i < vin.size(); ++i) {
            const QByteArray frame = QByteArray::fromHex(vin.at(i).mid(4).toLatin1());
            QCOMPARE(frame.size(), 8);   // a whole CAN frame each, the last one padded
            payload += frame.mid(i == 0 ? 2 : 1);
        }
        QCOMPARE(payload.left(20).mid(3), QByteArray("1J4FA49S03P123456"));

        // One frame: id, length, data. An addressed ECU answers under its own id
        const QStringList rpm = ask("010C");
        QCOMPARE(rpm.size(), 1);
        QVERIFY2(rpm.at(0).startsWith("7E8 04 41 0C "), qPrintable(rpm.at(0)));
        ask("ATSH7E1");
        const QStringList trans = ask("221940");
        QCOMPARE(trans.size(), 1);
        QVERIFY2(trans.at(0).startsWith("7E9 04 62 19 40 "), qPrintable(trans.at(0)));
        ask("ATSH7DF");

        // Spaces off, and headers off again: the byte-count form is back
        ask("ATS0");
        QCOMPARE(ask("0902").at(0).left(15), QStringLiteral("7E8101449020131"));
        QCOMPARE(ask("ATH0"), QStringList{"OK"});
        const QStringList plain = ask("0902");
        QCOMPARE(plain.at(0), QStringLiteral("014"));
        QVERIFY(plain.at(1).startsWith("0:490201"));
    }

private:
    QTemporaryDir m_tmp;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<CarSim> m_sim;
    std::unique_ptr<OBDManager> m_obd;
};

QTEST_MAIN(TstObdSim)
#include "tst_obdsim.moc"
