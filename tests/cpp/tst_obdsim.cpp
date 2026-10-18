// The real OBDManager against the simulated ELM327 (src/dev/simelm327.cpp):
// connect, VIN decode, vehicle scan, demand-driven polling of single and
// multi-value PIDs, trouble codes, and a lost link that reconnects. The
// whole shipped OBD stack runs; only the adapter is simulated. Needs
// OCTAVE_DEV_TOOLS (the simulator lives in src/dev/).
//
// The slots share one manager and run in declaration order.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

#include "dev/carsim.h"
#include "dev/simelm327.h"
#include "managers/obdmanager.h"
#include "managers/settingsmanager.h"

using namespace OctaveDev;

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

    void lostLinkReconnects()
    {
        m_sim->setLinkUp(false);   // adapter unplugged
        QTRY_VERIFY_WITH_TIMEOUT(!m_obd->isConnected(), 10000);
        QVERIFY(!m_obd->vehicleScanComplete());   // the next car may support a different set

        // Plugged back in: the passive scan (every 10 s) reconnects
        m_sim->setLinkUp(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->isConnected(), 25000);
        QTRY_VERIFY_WITH_TIMEOUT(m_obd->vehicleScanComplete(), 15000);
    }

private:
    QTemporaryDir m_tmp;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<CarSim> m_sim;
    std::unique_ptr<OBDManager> m_obd;
};

QTEST_MAIN(TstObdSim)
#include "tst_obdsim.moc"
