// OBDAdapterFinder logic that needs no Bluetooth hardware: which devices count
// as OBD adapters, list order, the hand-off to OBDManager after pairing
// (connect_direct + a named saved-adapter chip, then the outcome read from
// connectionStatusChanged) and staying inert without BlueZ. Ported from
// tests/test_obd_adapter_finder.py. The hand-off runs against the real
// OBDManager talking to the simulated ELM327 (needs OCTAVE_DEV_TOOLS).

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "dev/carsim.h"
#include "dev/simelm327.h"
#include "managers/obdadapterfinder.h"
#include "managers/obdmanager.h"
#include "managers/settingsmanager.h"

namespace {
const QString kMac = QStringLiteral("88:1B:99:66:DD:5F");
}

class TstObdAdapterFinder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("HOME", m_tmp.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp.path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp.path() + "/appdata").toUtf8());

        // connect_direct() goes to the simulated adapter, never a radio
        m_sim = std::make_unique<OctaveDev::CarSim>();
        m_sim->setRealisticMode(false);
        const std::shared_ptr<OctaveDev::CarState> state = m_sim->state();
        OBDConnectionWorker::setCustomTransport(
            [](const QString &port) { return port == QLatin1String(OctaveDev::kSimPort); },
            [state](const QString &, QObject *parent) -> QIODevice * {
                return new OctaveDev::SimElm327Device(state, parent);
            });
    }

    void cleanupTestCase() { OBDConnectionWorker::setCustomTransport({}, {}); }

    void commonAdapterNamesAreRecognised()
    {
        for (const char *name : {"OBDII", "OBD2", "V-LINK", "Android-Vlink", "OBDLink MX+", "OBDLink CX",
                                 "Vgate iCar Pro", "VEEPEAK", "BAFX Products", "KONNWEI", "NEXAS23082749",
                                 "LELink", "ELM327 v1.5", "Viecar", "KW902"})
            QVERIFY2(OBDAdapterFinder::looksLikeObd(name, {}), name);
        for (const char *name : {"Washer", "Xbox Wireless Controller", "MX Master 2S", "VIZIO V21", "Pixel 8"})
            QVERIFY2(!OBDAdapterFinder::looksLikeObd(name, {}), name);
    }

    void bleServiceUuidMarksAnUnrecognisedName()
    {
        QVERIFY(OBDAdapterFinder::looksLikeObd("Y1csie", {"{0000FFF0-0000-1000-8000-00805F9B34FB}"}));
        QVERIFY(OBDAdapterFinder::looksLikeObd("HM10", {"49535343-fe7d-4ae5-8fa9-9fafd205e455"}));
        QVERIFY(!OBDAdapterFinder::looksLikeObd("Y1csie", {"0000180A-0000-1000-8000-00805F9B34FB"}));
    }

    void listSortsAdaptersFirstThenBySignal()
    {
        OBDAdapterFinder f(nullptr, nullptr);
        auto add = [&f](const QString &addr, const QString &name, int rssi, bool obd) {
            f.m_devices.insert(addr, QVariantMap{{"address", addr}, {"name", name}, {"rssi", rssi}, {"likelyObd", obd}});
        };
        add("01", "Dryer", -50, false);
        add("02", "OBDII", 0, true);   // cached, not heard this scan
        add("03", "NEXAS", -88, true);
        add("04", "Washer", -70, false);
        f.rebuildList();
        QStringList order;
        for (const QVariant &d : f.devices())
            order << d.toMap().value("name").toString();
        QCOMPARE(order, (QStringList{"NEXAS", "OBDII", "Dryer", "Washer"}));
    }

    void handOffConnectsAndNamesTheSavedChip()
    {
        SettingsManager settings;
        OBDManager obd(&settings);
        obd.setPortOverride(QString::fromLatin1(OctaveDev::kSimPort));
        OBDAdapterFinder f(&obd, &settings);
        f.m_busyName = QStringLiteral("NEXAS23082749");
        f.setBusy(kMac);

        f.handOff(2);
        QCOMPARE(f.busyAddress(), kMac);
        QCOMPARE(f.status(), QStringLiteral("Connecting to the vehicle…"));
        QVERIFY2(settings.obdSavedAdapters().contains("NEXAS23082749"), qPrintable(settings.obdSavedAdapters()));
        QVERIFY2(settings.obdSavedAdapters().contains("rfcomm://" + kMac + "/2"), qPrintable(settings.obdSavedAdapters()));
        QCOMPARE(settings.obdBluetoothPort(), "rfcomm://" + kMac + "/2");

        // The real connection (to the simulator) decides the outcome
        QTRY_COMPARE_WITH_TIMEOUT(f.status(), QStringLiteral("Connected to NEXAS23082749"), 15000);
        QCOMPARE(f.busyAddress(), QString());
        QVERIFY(!f.statusIsError());
    }

    void outcomeWaitsForTheAttemptToStart()
    {
        OBDManager obd(nullptr);
        obd.close();   // no connection attempts of its own
        OBDAdapterFinder f(&obd, nullptr);
        handedOff(f, "NEXAS");

        // force_connect() tears the old link down first; that isn't the result
        emit obd.connectionStatusChanged("Disconnected");
        QCOMPARE(f.busyAddress(), kMac);

        emit obd.connectionStatusChanged("Connecting");
        emit obd.connectionStatusChanged("Connected");
        QCOMPARE(f.busyAddress(), QString());
        QCOMPARE(f.status(), QStringLiteral("Connected to NEXAS"));
        QVERIFY(!f.statusIsError());
    }

    void failedConnectionIsReportedAsError()
    {
        OBDManager obd(nullptr);
        obd.close();
        OBDAdapterFinder f(&obd, nullptr);
        handedOff(f, "NEXAS");
        emit obd.connectionStatusChanged("Connecting");
        emit obd.connectionStatusChanged("Connection Failed");
        QCOMPARE(f.busyAddress(), QString());
        QVERIFY(f.statusIsError());
        QVERIFY(f.status().contains("Connection Failed"));
    }

    void pairingErrorIsTheMessageWhenTheTrustedConnectFails()
    {
        OBDManager obd(nullptr);
        obd.close();
        OBDAdapterFinder f(&obd, nullptr);
        handedOff(f, "NEXAS");
        f.m_pairError = QStringLiteral("Pairing timed out.");
        emit obd.connectionStatusChanged("Connecting");
        emit obd.connectionStatusChanged("Connection Failed");
        QCOMPARE(f.status(), QStringLiteral("Pairing timed out."));
        QVERIFY(f.statusIsError());
    }

    void unreachableAdapterStopsAtPairing()
    {
        OBDManager obd(nullptr);
        obd.close();
        QSignalSpy connecting(&obd, &OBDManager::connectionStatusChanged);
        OBDAdapterFinder f(&obd, nullptr);
        f.m_busyName = QStringLiteral("NEXAS");
        f.setBusy(kMac);
        f.onPaired(false, false, QStringLiteral("The adapter didn't answer."));
        QCOMPARE(f.busyAddress(), QString());
        QVERIFY(f.statusIsError());
        QVERIFY(!f.m_handedOff);
        QVERIFY(connecting.isEmpty());   // no hand-off
    }

    void noBluezMeansNoBluetoothObjects()
    {
        if (!OBDAdapterFinder(nullptr, nullptr).supported())
            QSKIP("the finder is Linux (BlueZ) only");
        if (OBDAdapterFinder::bluezRunning())
            QSKIP("bluetoothd is running here");
        // bluetoothd stopped / no adapter: building QBluetoothLocalDevice
        // would D-Bus-activate org.bluez and block startup
        OBDAdapterFinder f(nullptr, nullptr);
        QVERIFY(f.m_local == nullptr);
        QVERIFY(!f.bluetoothOn());
        f.powerOn();
        f.startScan();
        QVERIFY(f.m_local == nullptr);
        QVERIFY(!f.scanning());
        QCOMPARE(f.status(), QStringLiteral("Bluetooth is not available: the Bluetooth service is not running."));
        QVERIFY(f.statusIsError());
        f.connectAdapter(kMac);   // nothing listed, nothing to pair
        QCOMPARE(f.busyAddress(), QString());
    }

private:
    // The finder after pairing handed off to OBDManager, before any status
    static void handedOff(OBDAdapterFinder &f, const QString &name)
    {
        f.m_busyName = name;
        f.setBusy(kMac);
        f.m_handedOff = true;
        f.m_sawConnecting = false;
    }

    QTemporaryDir m_tmp;
    std::unique_ptr<OctaveDev::CarSim> m_sim;
};

QTEST_MAIN(TstObdAdapterFinder)
#include "tst_obdadapterfinder.moc"
