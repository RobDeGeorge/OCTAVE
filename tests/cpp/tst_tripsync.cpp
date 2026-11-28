// The black box's drives from one OCTAVE to another (TripSync): what
// GET /trips serves, a pull from a real `octave --headless` paired with
// its code, and the push over adb (a fake adb that writes into a folder).

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/settingsmanager.h"
#include "managers/triprecorder.h"
#include "managers/tripstore.h"
#include "managers/tripsync.h"

namespace {
const QString kApp = QStringLiteral(OCTAVE_APP_BINARY);
}

class TstTripSync : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<SettingsManager> m_settings;
    qint64 m_now = 1'700'000'000'000LL;

    std::unique_ptr<TripRecorder> makeRecorder(const QString &historyDir)
    {
        auto r = std::make_unique<TripRecorder>(m_settings.get());
        r->setHistoryDir(historyDir);
        r->setClock([this]() { return m_now; });
        r->setFlushIntervalMs(50);
        r->start();
        if (!QTest::qWaitFor([&r]() { return r->storeReady(); }, 5000))
            qWarning("the trip store did not report ready within 5 s");
        r->setConnected(true);
        r->setVehicleParameters({QStringLiteral("RPM"), QStringLiteral("SPEED")});
        return r;
    }

    // `drives` closed drives of 30 s in `historyDir`; their ids, oldest first
    QList<qint64> record(const QString &historyDir, int drives)
    {
        QList<qint64> ids;
        auto r = makeRecorder(historyDir);
        for (int d = 0; d < drives; ++d) {
            m_now += 3'600'000;
            for (int i = 0; i < 30; ++i) {
                m_now += 1000;
                r->recordSample(QStringLiteral("RPM"), 2000.0f + i);
                r->recordSample(QStringLiteral("SPEED"), 30.0f + d);
            }
            ids.append(r->currentTripId());
            r->endTripNow();
        }
        r->shutdown();
        return ids;
    }

    static quint16 freePort()
    {
        QTcpServer s;
        s.listen(QHostAddress::LocalHost, 0);
        return s.serverPort();
    }

    QProcessEnvironment env() const
    {
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert("QT_QPA_PLATFORM", "offscreen");
        e.insert("XDG_CONFIG_HOME", m_tmp->path() + "/xdg");
        e.insert("HOME", m_tmp->path() + "/xdg");
        e.insert("OCTAVE_USB_SYSFS", m_tmp->path() + "/no-usb");
        e.insert("ANDROID_ADB_SERVER_PORT", "1");
        return e;
    }

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/xdg").toUtf8());
        SettingsManager::setAppDataDirOverride(m_tmp->path() + "/xdg/OCTAVE");
        m_settings = std::make_unique<SettingsManager>();
    }

    void cleanup()
    {
        m_settings.reset();
        m_tmp.reset();
    }

    // GET /trips: the list holds closed real drives only, and a file is
    // served for an id of that list and nothing else
    void whatIsServed()
    {
        const QString dir = m_tmp->path() + "/car/history";
        const QList<qint64> ids = record(dir, 2);
        TripStore store;
        QVERIFY(store.open(dir));
        QVariantList trips = store.listTrips();
        QCOMPARE(trips.size(), 2);
        QVariantMap sim = trips.first().toMap();
        sim["id"] = 42;
        sim["simulated"] = true;
        trips.append(sim);
        QVariantMap open = trips.first().toMap();
        open["id"] = 43;
        open["open"] = true;
        trips.append(open);

        const auto list = TripSync::serve(trips, "/trips");
        QCOMPARE(list.status, 200);
        const QJsonArray rows = QJsonDocument::fromJson(list.body).array();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toObject().value("id").toInteger(), ids.at(1));   // newest first

        const QByteArray id = QByteArray::number(ids.first());
        const auto json = TripSync::serve(trips, "/trips?id=" + id + "&format=json");
        QCOMPARE(json.status, 200);
        QCOMPARE(QJsonDocument::fromJson(json.body).object().value("id").toInteger(), ids.first());
        const auto csv = TripSync::serve(trips, "/trips?id=" + id + "&format=csv");
        QCOMPARE(csv.status, 200);
        QVERIFY(csv.body.startsWith("timestamp_ms,"));
        const auto packed = TripSync::serve(trips, "/trips?id=" + id + "&format=csv&z=1");
        QCOMPARE(qUncompress(packed.body), csv.body);
        QVERIFY(packed.body.size() < csv.body.size());

        QCOMPARE(TripSync::serve(trips, "/trips?id=42&format=csv").status, 404);   // simulated
        QCOMPARE(TripSync::serve(trips, "/trips?id=43&format=csv").status, 404);   // being recorded
        QCOMPARE(TripSync::serve(trips, "/trips?id=7&format=json").status, 404);
        QCOMPARE(TripSync::serve(trips, "/trips?id=../../etc/passwd").status, 404);
    }

    // The whole pull: pair with a running OCTAVE by its code, fetch its
    // drives, see them here; nothing comes twice, and a drive deleted here
    // stays deleted
    void drivesArePulledFromAPairedOctave()
    {
#if defined(Q_OS_ANDROID) || !QT_CONFIG(process)
        QSKIP("desktop only");
#else
        const QString carConfig = m_tmp->path() + "/car";
        const QList<qint64> ids = record(carConfig + "/history", 3);
        const quint16 port = freePort();
        QProcess car;
        car.setProcessEnvironment(env());
        car.setProcessChannelMode(QProcess::MergedChannels);
        car.start(kApp, {"--headless", "--config", carConfig, "--service", QString::number(port)});
        QVERIFY(car.waitForStarted(10000));
        struct Stop { QProcess &p; ~Stop() { p.terminate(); if (!p.waitForFinished(20000)) p.kill(); p.waitForFinished(5000); } } stop{car};

        // The code, as `octave --pair-code` gives it once the car is up
        QString code;
        for (int i = 0; i < 40 && code.isEmpty(); ++i) {
            QProcess p;
            p.setProcessEnvironment(env());
            p.start(kApp, {"--pair-code", "--config", carConfig});
            if (p.waitForFinished(30000) && p.exitCode() == 0) {
                static const QRegularExpression re(QStringLiteral("\"code\": \"(\\d{6})\""));
                code = re.match(QString::fromUtf8(p.readAllStandardOutput())).captured(1);
            }
            if (code.isEmpty())
                QTest::qWait(500);
        }
        QVERIFY(!code.isEmpty());

        auto phone = makeRecorder(m_tmp->path() + "/phone/history");
        TripSync sync(phone.get());
        sync.setConfigDir(m_tmp->path() + "/phone");
        sync.start();
        QSignalSpy paired(&sync, &TripSync::pairFinished);
        QSignalSpy synced(&sync, &TripSync::syncFinished);

        sync.addPeer("127.0.0.1:" + QString::number(port), 0, "000000" == code ? "000001" : "000000");
        QTRY_COMPARE_WITH_TIMEOUT(paired.count(), 1, 15000);
        QCOMPARE(paired.first().at(0).toBool(), false);   // a wrong code
        QVERIFY(sync.peers().isEmpty());
        QTest::qWait(1100);   // one attempt a second per address

        sync.addPeer("127.0.0.1", port, code);
        QTRY_COMPARE_WITH_TIMEOUT(paired.count(), 2, 15000);
        QVERIFY2(paired.last().at(0).toBool(), qPrintable(paired.last().at(1).toString()));
        QCOMPARE(sync.peers().size(), 1);
        QVERIFY(!QJsonDocument(QJsonArray::fromVariantList(sync.peers())).toJson().contains("token"));

        // Pairing starts a pull
        QTRY_COMPARE_WITH_TIMEOUT(synced.count(), 1, 30000);
        QCOMPARE(synced.first().at(0).toInt(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(phone->trips().size(), 3, 10000);
        QCOMPARE(phone->trips().last().toMap().value("id").toLongLong(), ids.first());
        {
            TripStore carStore;   // a second reader of the car's database
            QVERIFY(carStore.open(carConfig + "/history"));
            QCOMPARE(phone->trips().last().toMap().value("sampleCount").toLongLong(), carStore.sampleCount(ids.first()));
            QVERIFY(carStore.sampleCount(ids.first()) > 30);
        }
        QVERIFY(QDir(m_tmp->path() + "/phone/history/inbox").entryList(QDir::Files).isEmpty());

        // The file with the token is the owner's alone
        const QFileInfo state(m_tmp->path() + "/phone/trip-sync.json");
        QVERIFY(state.exists());
        QVERIFY(!(state.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));

        sync.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(synced.count(), 2, 30000);
        QCOMPARE(synced.last().at(0).toInt(), 0);
        QCOMPARE(sync.status(), QStringLiteral("Up to date"));

        phone->deleteTrip(ids.first());
        QTRY_COMPARE_WITH_TIMEOUT(phone->trips().size(), 2, 10000);
        sync.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(synced.count(), 3, 30000);
        QCOMPARE(synced.last().at(0).toInt(), 0);
        QCOMPARE(phone->trips().size(), 2);

        // The car goes away: said, not an error that stops anything
        stop.p.terminate();
        QVERIFY(stop.p.waitForFinished(30000));
        sync.syncNow();
        QTRY_COMPARE_WITH_TIMEOUT(synced.count(), 4, 40000);
        QCOMPARE(synced.last().at(1).toInt(), 1);
        QVERIFY(sync.status().contains("Not in reach"));
        phone->shutdown();
#endif
    }

    // The push over adb: the pairs land in the app's folder on the phone
    // under their final names, with the car's address beside them, and the
    // OCTAVE there takes them in
    void drivesArePushedOverAdb()
    {
#if !defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        QSKIP("needs a shell script as adb");
#else
        const QString carDir = m_tmp->path() + "/car/history";
        const QList<qint64> ids = record(carDir, 2);
        const QString phoneRoot = m_tmp->path() + "/phone-storage";
        const QString adb = m_tmp->path() + "/adb";
        QFile script(adb);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(QByteArray("#!/bin/sh\n"
                                "ROOT='") + phoneRoot.toUtf8() + "'\n"
                     "echo \"$@\" >> \"$ROOT.log\"\n"
                     "[ \"$1\" = -s ] && [ \"$2\" = PHONE1 ] || exit 1\n"
                     "shift 2\n"
                     "case \"$1 $2\" in\n"
                     "  'shell pm') [ -e \"$ROOT.noapp\" ] || echo package:/data/app/org.octave.app/base.apk ;;\n"
                     "  'shell mkdir') mkdir -p \"$ROOT$4\" ;;\n"
                     "  'shell mv') mv \"$ROOT$3\" \"$ROOT$4\" ;;\n"
                     "  push\\ *) cp \"$2\" \"$ROOT$3\" ;;\n"
                     "  *) exit 2 ;;\n"
                     "esac\n");
        script.close();
        QVERIFY(script.setPermissions(script.permissions() | QFileDevice::ExeOwner));

        auto car = makeRecorder(carDir);
        QTRY_COMPARE_WITH_TIMEOUT(car->trips().size(), 2, 5000);
        TripSync carSync(car.get());
        carSync.setConfigDir(m_tmp->path() + "/car");
        carSync.start();
        QSignalSpy pushed(&carSync, &TripSync::pushFinished);
        carSync.setServiceUrls({"http://10.42.0.1:9876", "http://127.0.0.1:9876"}, true);

        // No app on the phone: nothing is written, and it is tried again later
        QVERIFY(QFile(phoneRoot + ".noapp").open(QIODevice::WriteOnly));
        carSync.setPhone(adb, "PHONE1");
        carSync.pushNow();
        QTRY_COMPARE_WITH_TIMEOUT(pushed.count(), 1, 20000);
        QCOMPARE(pushed.first().at(0).toInt(), 0);
        QVERIFY(!QDir(phoneRoot).exists());
        QVERIFY(QFile::remove(phoneRoot + ".noapp"));

        carSync.pushNow();
        QTRY_COMPARE_WITH_TIMEOUT(pushed.count(), 2, 20000);
        QCOMPARE(pushed.last().at(0).toInt(), 2);
        const QString phoneHistory = phoneRoot + QString::fromLatin1(TripSync::kPhoneHistoryDir);
        QCOMPARE(QDir(phoneHistory + "/inbox").entryList(QDir::Files, QDir::Name),
                 QStringList({QString::number(ids.at(0)) + ".csv", QString::number(ids.at(0)) + ".json",
                              QString::number(ids.at(1)) + ".csv", QString::number(ids.at(1)) + ".json"}));

        // Nothing new: only the note goes again, no drive
        carSync.pushNow();
        QTRY_COMPARE_WITH_TIMEOUT(pushed.count(), 3, 20000);
        QCOMPARE(pushed.last().at(0).toInt(), 0);
        car->shutdown();

        // The OCTAVE on the phone
        auto phone = makeRecorder(m_tmp->path() + "/phone/history");
        TripSync phoneSync(phone.get());
        phoneSync.setConfigDir(m_tmp->path() + "/phone");
        phoneSync.start(phoneHistory);
        QTRY_COMPARE_WITH_TIMEOUT(phone->trips().size(), 2, 10000);
        QCOMPARE(phone->trips().first().toMap().value("id").toLongLong(), ids.at(1));
        QVERIFY(QDir(phoneHistory + "/inbox").entryList(QDir::Files).isEmpty());
        QCOMPARE(phoneSync.carHint().value("host").toString(), QStringLiteral("10.42.0.1"));
        QCOMPARE(phoneSync.carHint().value("port").toInt(), 9876);
        phone->shutdown();
#endif
    }

    // The shutdown button: the drive being recorded is ended and is on the
    // phone, whole, before the power goes; with no phone nothing is waited for
    void theShutdownButtonSendsTheDriveFirst()
    {
#if !defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        QSKIP("needs a shell script as adb");
#else
        const QString phoneRoot = m_tmp->path() + "/phone-storage";
        const QString adb = m_tmp->path() + "/adb";
        QFile script(adb);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(QByteArray("#!/bin/sh\nROOT='") + phoneRoot.toUtf8() + "'\nshift 2\n"
                     "case \"$1 $2\" in\n"
                     "  'shell pm') echo package:/data/app/org.octave.app/base.apk ;;\n"
                     "  'shell mkdir') mkdir -p \"$ROOT$4\" ;;\n"
                     "  'shell mv') mv \"$ROOT$3\" \"$ROOT$4\" ;;\n"
                     "  push\\ *) cp \"$2\" \"$ROOT$3\" ;;\n"
                     "esac\n");
        script.close();
        QVERIFY(script.setPermissions(script.permissions() | QFileDevice::ExeOwner));
        const QString inbox = phoneRoot + QString::fromLatin1(TripSync::kPhoneHistoryDir) + "/inbox";

        auto car = makeRecorder(m_tmp->path() + "/car/history");
        TripSync carSync(car.get());
        carSync.setConfigDir(m_tmp->path() + "/car");
        carSync.start();
        auto driveFor = [&](int seconds) {
            for (int i = 0; i < seconds; ++i) {
                m_now += 1000;
                car->recordSample(QStringLiteral("RPM"), 2000.0f + i);
                car->recordSample(QStringLiteral("SPEED"), 30.0f + i % 20);
            }
        };

        // No phone: the drive is ended and the power may go at once
        driveFor(20);
        QVERIFY(car->recording());
        bool done = false;
        carSync.finishBeforePowerOff([&done] { done = true; });
        QVERIFY(done);
        QVERIFY(!car->recording());
        driveFor(5);   // the engine still runs: no new drive
        QVERIFY(!car->recording());
        car->shutdown();

        // With the phone mirrored
        car = makeRecorder(m_tmp->path() + "/car/history");
        TripSync sync(car.get());
        sync.setConfigDir(m_tmp->path() + "/car");
        sync.start();
        sync.setPhone(adb, "PHONE1");
        m_now += 600'000;
        driveFor(40);
        const qint64 id = car->currentTripId();
        done = false;
        sync.finishBeforePowerOff([&done] { done = true; });
        QVERIFY(!done);   // the drive has to be written and sent first
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(!car->recording());
        // Both drives are there whole: the earlier one and the one just ended
        const QStringList files = QDir(inbox).entryList(QDir::Files, QDir::Name);
        QCOMPARE(files.size(), 4);
        QVERIFY(files.contains(QString::number(id) + ".json"));
        QFile f(inbox + "/" + QString::number(id) + ".json");
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject summary = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(summary.value("partial").toBool(), false);
        QCOMPARE(qRound(summary.value("durationS").toDouble()), 39);
        car->shutdown();
#endif
    }

    // A car computer that is cut off with no shutdown (snapshotMinutes): the drive being recorded
    // goes to the phone in parts while it lasts; each part adds to the ones
    // before, and the whole drive, pushed at the car's next start, takes
    // their place
    void theDriveBeingRecordedReachesThePhoneInParts()
    {
#if !defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        QSKIP("needs a shell script as adb");
#else
        const QString phoneRoot = m_tmp->path() + "/phone-storage";
        const QString adb = m_tmp->path() + "/adb";
        QFile script(adb);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(QByteArray("#!/bin/sh\nROOT='") + phoneRoot.toUtf8() + "'\nshift 2\n"
                     "case \"$1 $2\" in\n"
                     "  'shell pm') echo package:/data/app/org.octave.app/base.apk ;;\n"
                     "  'shell mkdir') mkdir -p \"$ROOT$4\" ;;\n"
                     "  'shell mv') mv \"$ROOT$3\" \"$ROOT$4\" ;;\n"
                     "  push\\ *) cp \"$2\" \"$ROOT$3\" ;;\n"
                     "esac\n");
        script.close();
        QVERIFY(script.setPermissions(script.permissions() | QFileDevice::ExeOwner));
        const QString phoneHistory = phoneRoot + QString::fromLatin1(TripSync::kPhoneHistoryDir);
        const QString carDir = m_tmp->path() + "/car/history";

        auto phone = makeRecorder(m_tmp->path() + "/phone/history");
        TripSync phoneSync(phone.get());
        phoneSync.setConfigDir(m_tmp->path() + "/phone");
        phoneSync.start(phoneHistory);
        QSignalSpy taken(phone.get(), &TripRecorder::importFinished);
        auto phoneTrip = [&] { return phone->trips().value(0).toMap(); };
        auto importNow = [&] {
            const int before = taken.count();
            phone->importInbox();
            return QTest::qWaitFor([&] { return taken.count() > before; }, 10000);
        };

        qint64 id = 0;
        {
            auto car = makeRecorder(carDir);
            TripSync carSync(car.get());
            carSync.setConfigDir(m_tmp->path() + "/car");
            carSync.start();
            QSignalSpy pushed(&carSync, &TripSync::pushFinished);
            carSync.setPhone(adb, "PHONE1");
            auto driveFor = [&](int seconds) {
                for (int i = 0; i < seconds; ++i) {
                    m_now += 1000;
                    car->recordSample(QStringLiteral("RPM"), 2000.0f + (m_now / 1000) % 500);
                    car->recordSample(QStringLiteral("SPEED"), 20.0f + (m_now / 1000) % 40);
                }
            };
            carSync.snapshotNow();   // no drive: nothing goes
            QTest::qWait(300);
            QCOMPARE(pushed.count(), 0);

            driveFor(60);
            id = car->currentTripId();
            carSync.snapshotNow();
            QTRY_COMPARE_WITH_TIMEOUT(pushed.count(), 1, 20000);
            QCOMPARE(pushed.last().at(0).toInt(), 1);
            QVERIFY(importNow());
            QTRY_COMPARE_WITH_TIMEOUT(phone->trips().size(), 1, 5000);
            QCOMPARE(phoneTrip().value("id").toLongLong(), id);
            QCOMPARE(phoneTrip().value("partial").toBool(), true);
            QCOMPARE(phoneTrip().value("sampleCount").toLongLong(), 120LL);
            QCOMPARE(qRound(phoneTrip().value("durationS").toDouble()), 59);

            // Nothing new since: nothing goes
            carSync.snapshotNow();
            QTest::qWait(500);
            QCOMPARE(pushed.count(), 1);

            driveFor(60);
            carSync.snapshotNow();
            QTRY_COMPARE_WITH_TIMEOUT(pushed.count(), 2, 20000);
            QVERIFY(importNow());
            QTRY_COMPARE_WITH_TIMEOUT(phoneTrip().value("sampleCount").toLongLong(), 240LL, 5000);
            QCOMPARE(phone->trips().size(), 1);
            QCOMPARE(phoneTrip().value("partial").toBool(), true);
            QCOMPARE(qRound(phoneTrip().value("durationS").toDouble()), 119);
            // The phone's own copy of a part is not a drive it hands on
            QCOMPARE(QJsonDocument::fromJson(TripSync::serve(phone->trips(), "/trips").body).array().size(), 0);

            // The last 30 s never leave the car before the key is turned
            driveFor(30);
            QVERIFY(QDir(carDir + "/outbox").entryList(QDir::Files).isEmpty());
            car->shutdown();
        }
        {
            // The car's next start: the drive is closed, and goes whole
            auto car = makeRecorder(carDir);
            QTRY_COMPARE_WITH_TIMEOUT(car->trips().size(), 1, 5000);
            TripSync carSync(car.get());
            carSync.setConfigDir(m_tmp->path() + "/car");
            carSync.start();
            QSignalSpy pushed(&carSync, &TripSync::pushFinished);
            carSync.setPhone(adb, "PHONE1");
            carSync.pushNow();
            QTRY_COMPARE_WITH_TIMEOUT(pushed.count(), 1, 20000);
            QCOMPARE(pushed.last().at(0).toInt(), 1);
            car->shutdown();
        }
        QVERIFY(importNow());
        QTRY_COMPARE_WITH_TIMEOUT(phoneTrip().value("sampleCount").toLongLong(), 300LL, 5000);
        QCOMPARE(phone->trips().size(), 1);
        QCOMPARE(phoneTrip().value("partial").toBool(), false);
        QCOMPARE(qRound(phoneTrip().value("durationS").toDouble()), 149);
        QCOMPARE(QDir(m_tmp->path() + "/phone/history/trips").entryList(QDir::Files).size(), 2);   // one pair, no leftovers
        QVERIFY(QDir(phoneHistory + "/inbox").entryList(QDir::Files).isEmpty());
        phone->shutdown();
#endif
    }
};

QTEST_GUILESS_MAIN(TstTripSync)
#include "tst_tripsync.moc"
