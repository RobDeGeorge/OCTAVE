// The black box: TripStore (SQLite + per-trip files) driven directly, and
// TripRecorder (trip detection, coalescing, store thread) driven with a fake
// clock and shortened timeouts. Everything runs in a temp XDG_CONFIG_HOME.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/obdmanager.h"
#include "managers/settingsmanager.h"
#include "managers/triprecorder.h"
#include "managers/tripstore.h"
#include "sqlite3.h"

class TstTripRecorder : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::unique_ptr<SettingsManager> m_settings;
    qint64 m_now = 1'700'000'000'000LL;   // fake wall clock, ms

    std::unique_ptr<TripRecorder> makeRecorder(const QString &sub = QStringLiteral("history"))
    {
        auto r = std::make_unique<TripRecorder>(m_settings.get());
        r->setHistoryDir(m_tmp->path() + QLatin1Char('/') + sub);
        r->setClock([this]() { return m_now; });
        r->setEngineOffTimeoutMs(5000);
        r->setReconnectGraceMs(8000);
        r->setFlushIntervalMs(50);
        r->setHeartbeatMs(2000);
        r->start();
        // The store opens on its own thread; nothing is recorded until it reports ready
        QTest::qWaitFor([&r]() { return r->storeReady(); }, 5000);
        r->setConnected(true);
        r->setVehicleParameters({QStringLiteral("RPM"), QStringLiteral("SPEED"), QStringLiteral("COOLANT_TEMP")});
        return r;
    }

    // Feed `seconds` of driving at `mph`, one reading set per second
    void drive(TripRecorder &r, int seconds, float mph, float rpm = 2000.0f)
    {
        for (int i = 0; i < seconds; ++i) {
            m_now += 1000;
            r.recordSample(QStringLiteral("RPM"), rpm);
            r.recordSample(QStringLiteral("SPEED"), mph);
            r.recordSample(QStringLiteral("COOLANT_TEMP"), 80.0f + i);
        }
    }

    static QVariantMap tripById(const QVariantList &trips, qint64 id)
    {
        for (const QVariant &t : trips) {
            if (t.toMap().value(QStringLiteral("id")).toLongLong() == id)
                return t.toMap();
        }
        return {};
    }

    // A 20-minute drive, one reading set per second: a cold start (20 °C)
    // with the coolant climbing 1 °C every `warmStepS` seconds to `settledC`,
    // one minute of cold idle, 14 minutes at 40 mph, five minutes of warm idle
    static void addHealthDrive(TripStore &store, qint64 id, int warmStepS = 5, float settledC = 90.0f,
                               float idleVolt = 13.8f, float ltft = 3.0f)
    {
        store.beginTrip(id, id, {});
        QVector<TripSample> b;
        for (int t = 0; t < 20 * 60; ++t) {
            const qint64 ts = id + t * 1000LL;
            const bool idle = t < 60 || t >= 15 * 60;
            const float coolant = std::min(settledC, 20.0f + float(t / warmStepS));
            b.append({id, ts, QStringLiteral("RPM"), idle ? (t < 60 ? 1000.0f : 750.0f) : 2000.0f});
            b.append({id, ts, QStringLiteral("SPEED"), idle ? 0.0f : 40.0f});
            b.append({id, ts, QStringLiteral("COOLANT_TEMP"), coolant});
            b.append({id, ts, QStringLiteral("CONTROL_MODULE_VOLTAGE"), idle ? idleVolt : 14.3f});
            // Cold-engine trims are open loop and must not count
            b.append({id, ts, QStringLiteral("LONG_FUEL_TRIM_1"), coolant >= 70.0f ? ltft : 20.0f});
        }
        store.append(b);
        store.endTrip(id, id + 20 * 60 * 1000LL, true);
    }

    static QVariantMap metricById(const QVariantMap &trends, const QString &id)
    {
        for (const QVariant &m : trends.value(QStringLiteral("metrics")).toList()) {
            if (m.toMap().value(QStringLiteral("id")).toString() == id)
                return m.toMap();
        }
        return {};
    }

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("XDG_DOWNLOAD_DIR", (m_tmp->path() + "/dl").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
        m_settings = std::make_unique<SettingsManager>();
    }

    void cleanup()
    {
        m_settings.reset();
        m_tmp.reset();
    }

    // ---- TripStore ---------------------------------------------------------

    void storeRecordsSummarisesAndWritesFiles()
    {
        TripStore store;
        QVERIFY(store.open(m_tmp->path() + "/store"));
        QVERIFY(QFile::exists(store.databasePath()));

        const qint64 id = 1'700'000'000'000LL;
        store.beginTrip(id, id, {{QStringLiteral("vin"), QStringLiteral("1J4FA49S03P123456")},
                                 {QStringLiteral("make"), QStringLiteral("Jeep")},
                                 {QStringLiteral("modelYear"), 2003}});
        QVector<TripSample> batch;
        // 60 s at a constant 60 mph = 1 mile; coolant climbs 80 -> 139
        for (int i = 0; i <= 60; ++i) {
            batch.append({id, id + i * 1000, QStringLiteral("SPEED"), 60.0f});
            batch.append({id, id + i * 1000, QStringLiteral("RPM"), 2500.0f});
            batch.append({id, id + i * 1000, QStringLiteral("COOLANT_TEMP"), 80.0f + i});
        }
        batch.append({id, id + 500, QStringLiteral("FUEL_LEVEL"), 50.0f});
        batch.append({id, id + 59500, QStringLiteral("FUEL_LEVEL"), 47.5f});
        store.append(batch);
        store.appendEvent(id, id + 30000, QStringLiteral("mil"), QStringLiteral("on"));
        QCOMPARE(store.sampleCount(id), 185);

        QSignalSpy finalized(&store, &TripStore::tripFinalized);
        const QVariantMap s = store.endTrip(id, id + 60000, true);
        QCOMPARE(finalized.count(), 1);
        QCOMPARE(s.value("sampleCount").toLongLong(), 185);
        QCOMPARE(s.value("fuelStart").toDouble(), 50.0);
        QCOMPARE(s.value("fuelEnd").toDouble(), 47.5);
        QCOMPARE(s.value("fuelUsedPct").toDouble(), 2.5);
        QCOMPARE(s.value("durationS").toDouble(), 60.0);
        QVERIFY(qAbs(s.value("distanceMiles").toDouble() - 1.0) < 0.001);
        QCOMPARE(s.value("maxSpeed").toDouble(), 60.0);
        QCOMPARE(s.value("maxRpm").toDouble(), 2500.0);
        QCOMPARE(s.value("maxCoolant").toDouble(), 140.0);
        QCOMPARE(s.value("make").toString(), QStringLiteral("Jeep"));
        QCOMPARE(s.value("eventCount").toInt(), 1);
        QCOMPARE(s.value("params").toList().size(), 4);
        QVariantMap coolant;
        for (const QVariant &p : s.value("params").toList())
            if (p.toMap().value("id") == QStringLiteral("COOLANT_TEMP")) coolant = p.toMap();
        QCOMPARE(coolant.value("first").toDouble(), 80.0);
        QCOMPARE(coolant.value("last").toDouble(), 140.0);
        QCOMPARE(coolant.value("count").toLongLong(), 61);
        // Units and titles from ParameterCatalog, so the files explain themselves
        QCOMPARE(coolant.value("unit").toString(), QString::fromUtf8("°C"));
        QCOMPARE(coolant.value("title").toString(), QStringLiteral("Coolant Temp"));

        // Files: CSV with a header and one line per sample, JSON with the summary
        const QString csv = s.value("file").toString();
        QVERIFY2(QFile::exists(csv), qPrintable(csv));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.first(), QStringLiteral("timestamp_ms,time,elapsed_s,parameter,value,unit"));
        QCOMPARE(lines.size(), 1 + 185);
        QVERIFY(lines.at(1).startsWith(QString::number(id) + ","));
        bool sawCoolant = false;
        for (const QString &line : lines) {
            const QStringList cols = line.split(',');
            if (cols.value(3) == QLatin1String("COOLANT_TEMP")) {
                QCOMPARE(cols.size(), 6);
                QCOMPARE(cols.at(5), QString::fromUtf8("°C"));
                sawCoolant = true;
            }
        }
        QVERIFY(sawCoolant);
        const QString json = csv.chopped(4) + ".json";
        QFile j(json);
        QVERIFY(j.open(QIODevice::ReadOnly));
        const QJsonObject doc = QJsonDocument::fromJson(j.readAll()).object();
        QCOMPARE(doc.value("format").toString(), QStringLiteral("octave-trip"));
        QCOMPARE(doc.value("sampleCount").toInt(), 185);
        QCOMPARE(doc.value("events").toArray().size(), 1);

        // List, detail and a downsampled series
        const QVariantList trips = store.listTrips();
        QCOMPARE(trips.size(), 1);
        QCOMPARE(trips.first().toMap().value("open").toBool(), false);
        QVERIFY(!trips.first().toMap().contains("params"));   // list items stay light
        const QVariantMap detail = store.tripDetail(id);
        QCOMPARE(detail.value("params").toList().size(), 4);
        const QVariantList series = store.series(id, QStringLiteral("COOLANT_TEMP"), 10);
        QVERIFY(series.size() >= 5 && series.size() <= 11);
        QCOMPARE(series.first().toMap().value("t").toDouble(), 0.0);
        QVERIFY(series.last().toMap().value("hi").toDouble() == 140.0);
        QVERIFY(series.first().toMap().value("lo").toDouble() == 80.0);
        // A zoomed window (20 s .. 30 s) gets its own buckets, all inside it
        const QVariantList window = store.series(id, QStringLiteral("COOLANT_TEMP"), 50, 20.0, 30.0);
        QVERIFY(window.size() >= 10);
        for (const QVariant &p : window) {
            const double t = p.toMap().value("t").toDouble();
            QVERIFY2(t >= 20.0 && t <= 30.0, qPrintable(QString::number(t)));
        }
        QCOMPARE(window.first().toMap().value("lo").toDouble(), 100.0);
        QCOMPARE(window.last().toMap().value("hi").toDouble(), 110.0);

        // Export copies both files
        const QString exported = store.exportTrip(id, m_tmp->path() + "/export");
        QVERIFY(QFile::exists(exported));
        QVERIFY(QFile::exists(exported.chopped(4) + ".json"));

        // Delete removes rows and files
        QVERIFY(store.deleteTrip(id));
        QCOMPARE(store.listTrips().size(), 0);
        QVERIFY(!QFile::exists(csv));
        QVERIFY(!QFile::exists(json));
    }

    void storeRecoversTripLeftOpen()
    {
        const QString dir = m_tmp->path() + "/store";
        const qint64 id = 1'700'000'000'000LL;
        {
            TripStore store;
            QVERIFY(store.open(dir));
            store.beginTrip(id, id, {});
            store.append({{id, id + 1000, QStringLiteral("RPM"), 900.0f},
                          {id, id + 9000, QStringLiteral("RPM"), 950.0f}});
            // An empty shell (power cut right after the start) must vanish
            store.beginTrip(id + 20000, id + 20000, {});
            // A trip whose end was logged but never summarised (tools/trips/make_mock_trip.py
            // relies on this) recovers clean
            store.beginTrip(id + 40000, id + 40000, {});
            store.append({{id + 40000, id + 41000, QStringLiteral("RPM"), 900.0f}});
            store.appendEvent(id + 40000, id + 41000, QStringLiteral("trip_end"), QStringLiteral("mock"));
            // no endTrip: "power cut"
        }
        TripStore store;
        QVERIFY(store.open(dir));
        QCOMPARE(store.recoverOpenTrips(), 3);
        const QVariantList trips = store.listTrips();
        QCOMPARE(trips.size(), 2);
        const QVariantMap ended = trips.first().toMap();   // newest first
        QCOMPARE(ended.value("id").toLongLong(), id + 40000);
        QCOMPARE(ended.value("clean").toBool(), true);
        const QVariantMap t = trips.last().toMap();
        QCOMPARE(t.value("id").toLongLong(), id);
        QCOMPARE(t.value("clean").toBool(), false);
        QCOMPARE(t.value("durationS").toDouble(), 9.0);   // last sample time
        QVERIFY(QFile::exists(t.value("file").toString()));
    }

    void storePrunesOldestFirst()
    {
        TripStore store;
        QVERIFY(store.open(m_tmp->path() + "/store"));
        const qint64 base = 1'700'000'000'000LL;
        for (int n = 0; n < 4; ++n) {
            const qint64 id = base + n * 3'600'000LL;
            store.beginTrip(id, id, {});
            QVector<TripSample> batch;
            for (int i = 0; i < 2000; ++i)
                batch.append({id, id + i * 100, QStringLiteral("RPM"), 800.0f + (i % 50)});
            store.append(batch);
            store.endTrip(id, id + 200000, true);
        }
        store.beginTrip(base + 10 * 3'600'000LL, base + 10 * 3'600'000LL, {});   // the live trip: never pruned
        const qint64 before = store.storageBytes();
        QVERIFY(before > 0);
        const qint64 after = store.prune(before / 2);
        QVERIFY(after < before);
        const QVariantList trips = store.listTrips();
        QVERIFY(trips.size() < 5 && trips.size() >= 1);
        // Newest first; the open trip survives, the oldest went first
        QCOMPARE(trips.first().toMap().value("open").toBool(), true);
        for (const QVariant &t : trips)
            QVERIFY(t.toMap().value("id").toLongLong() != base);

        store.clearAll();
        QCOMPARE(store.listTrips().size(), 1);   // only the open trip
        QCOMPARE(QDir(store.tripsDir()).entryList(QDir::Files).size(), 0);
    }

    // ---- Health across drives ---------------------------------------------

    void storeWorksOutHealthPerTrip()
    {
        TripStore store;
        QVERIFY(store.open(m_tmp->path() + "/store"));
        const qint64 id = 1'700'000'000'000LL;
        addHealthDrive(store, id);
        const QVariantMap detail = store.tripDetail(id);
        QCOMPARE(detail.value("healthVersion").toInt(), 1);
        const QVariantMap h = detail.value("health").toMap();
        // 20 -> 70 °C at 1 °C per 5 s: 250 s
        QVERIFY2(qAbs(h.value("warmupMin").toDouble() - 250.0 / 60.0) < 0.01, qPrintable(h.value("warmupMin").toString()));
        QCOMPARE(h.value("settledCoolant").toDouble(), 90.0);
        // Both idle stretches (cold at 1000 RPM, warm at 750) are idle; the drive is not
        QVERIFY(qAbs(h.value("idleVoltage").toDouble() - 13.8) < 0.001);
        QVERIFY(qAbs(h.value("chargingVoltage").toDouble() - 14.3) < 0.001);
        // Warm idle only: the cold fast idle is left out
        QCOMPARE(h.value("idleRpm").toDouble(), 750.0);
        QVERIFY(qAbs(h.value("ltft1").toDouble() - 3.0) < 0.001);
        QVERIFY(!h.contains("ltft2"));

        // A warm start has no warm-up time; a short drive no running coolant
        const qint64 id2 = id + 86'400'000LL;
        store.beginTrip(id2, id2, {});
        QVector<TripSample> b;
        for (int t = 0; t < 300; ++t) {
            b.append({id2, id2 + t * 1000LL, QStringLiteral("RPM"), 2000.0f});
            b.append({id2, id2 + t * 1000LL, QStringLiteral("COOLANT_TEMP"), 85.0f});
        }
        store.append(b);
        const QVariantMap h2 = store.endTrip(id2, id2 + 300000, true).value("health").toMap();
        QVERIFY(!h2.contains("warmupMin"));
        QVERIFY(!h2.contains("settledCoolant"));
        QVERIFY(!h2.contains("idleVoltage"));
    }

    void storeTrendsAcrossDrivesFlagADrift()
    {
        TripStore store;
        QVERIFY(store.open(m_tmp->path() + "/store"));
        const qint64 day = 86'400'000LL;
        const qint64 base = 1'700'000'000'000LL;
        // Eight daily drives; the last three with a thermostat stuck open:
        // slower to warm (1 °C every 9 s) and settling at 76 °C
        for (int n = 0; n < 8; ++n) {
            const bool failing = n >= 5;
            addHealthDrive(store, base + n * day, failing ? 9 : 5, failing ? 76.0f : 90.0f);
        }
        // A trip being recorded is not part of any trend
        store.beginTrip(base + 8 * day, base + 8 * day, {});
        store.append({{base + 8 * day, base + 8 * day + 1000, QStringLiteral("RPM"), 800.0f}});

        const qint64 now = base + 8 * day + 3'600'000LL;
        QSignalSpy ready(&store, &TripStore::healthTrendsReady);
        const QVariantMap all = store.healthTrends(0, now);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(all.value("tripCount").toInt(), 8);

        const QVariantMap warmup = metricById(all, "warmupMin");
        const QVariantList rows = warmup.value("rows").toList();
        QCOMPARE(rows.size(), 8);
        for (int i = 1; i < rows.size(); ++i)   // oldest first
            QVERIFY(rows.at(i).toMap().value("startMs").toLongLong() > rows.at(i - 1).toMap().value("startMs").toLongLong());
        QVERIFY(qAbs(warmup.value("baseline").toDouble() - 250.0 / 60.0) < 0.01);
        QVERIFY(qAbs(warmup.value("recent").toDouble() - 450.0 / 60.0) < 0.01);
        QCOMPARE(warmup.value("status").toString(), QStringLiteral("watch"));
        QVERIFY(!warmup.contains("bandLo"));

        const QVariantMap coolant = metricById(all, "settledCoolant");
        QCOMPARE(coolant.value("status").toString(), QStringLiteral("watch"));
        QCOMPARE(coolant.value("latest").toDouble(), 76.0);
        QCOMPARE(coolant.value("bandLo").toDouble(), 80.0);
        QVERIFY(coolant.value("note").toString().contains("under the usual"));

        const QVariantMap volt = metricById(all, "idleVoltage");
        QCOMPARE(volt.value("status").toString(), QStringLiteral("ok"));
        QCOMPARE(metricById(all, "ltft2").value("status").toString(), QStringLiteral("none"));
        QVERIFY(metricById(all, "ltft2").value("rows").toList().isEmpty());

        // Every recorded parameter, most trips first
        const QVariantList params = all.value("params").toList();
        QCOMPARE(params.size(), 5);
        QCOMPARE(params.first().toMap().value("trips").toInt(), 8);

        // The window: the last four days hold three drives (days 5-7), too few for a baseline
        const QVariantMap recent = store.healthTrends(4, now);
        QCOMPARE(recent.value("tripCount").toInt(), 3);
        QVERIFY(!metricById(recent, "warmupMin").contains("baseline"));
        QCOMPARE(metricById(recent, "warmupMin").value("status").toString(), QStringLiteral("ok"));

        // One parameter, per trip, from the summaries
        QSignalSpy paramReady(&store, &TripStore::paramTrendReady);
        const QVariantList coolantRows = store.paramTrend("COOLANT_TEMP", 0, now);
        QCOMPARE(paramReady.count(), 1);
        QCOMPARE(coolantRows.size(), 8);
        QCOMPARE(coolantRows.first().toMap().value("lo").toDouble(), 20.0);
        QCOMPARE(coolantRows.first().toMap().value("hi").toDouble(), 90.0);
        QCOMPARE(coolantRows.last().toMap().value("hi").toDouble(), 76.0);
        QVERIFY(store.paramTrend("NOT_RECORDED", 0, now).isEmpty());
    }

    void storeFillsInHealthForOlderTrips()
    {
        const QString dir = m_tmp->path() + "/store";
        const qint64 id = 1'700'000'000'000LL;
        {
            TripStore store;
            QVERIFY(store.open(dir));
            addHealthDrive(store, id);
        }
        // A summary written before the health numbers existed
        {
            sqlite3 *db = nullptr;
            QCOMPARE(sqlite3_open((dir + "/blackbox.sqlite").toUtf8().constData(), &db), SQLITE_OK);
            QCOMPARE(sqlite3_exec(db, "UPDATE trips SET summary = json_remove(summary, '$.health', '$.healthVersion')",
                                  nullptr, nullptr, nullptr), SQLITE_OK);
            sqlite3_close(db);
        }
        TripStore store;
        QVERIFY(store.open(dir));
        QVERIFY(!store.tripDetail(id).contains("health"));
        const QVariantMap trends = store.healthTrends(0, id + 86'400'000LL);
        QCOMPARE(metricById(trends, "idleRpm").value("rows").toList().size(), 1);
        // ... and kept, so the next query reads it from the summary
        const QVariantMap detail = store.tripDetail(id);
        QCOMPARE(detail.value("healthVersion").toInt(), 1);
        QCOMPARE(detail.value("health").toMap().value("idleRpm").toDouble(), 750.0);
    }

    void recorderServesTrendsFromItsThread()
    {
        auto r = makeRecorder();
        drive(*r, 30, 30.0f);
        r->endTripNow();
        QTRY_COMPARE(r->trips().size(), 1);
        QSignalSpy ready(r.get(), &TripRecorder::healthTrendsReady);
        r->requestHealthTrends(7);
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().at(0).toInt(), 7);
        QCOMPARE(ready.first().at(1).toMap().value("tripCount").toInt(), 1);
        QSignalSpy param(r.get(), &TripRecorder::paramTrendReady);
        r->requestParamTrend("SPEED", 7);
        QTRY_COMPARE(param.count(), 1);
        QCOMPARE(param.first().at(0).toString(), QStringLiteral("SPEED"));
        QCOMPARE(param.first().at(2).toList().size(), 1);
        QCOMPARE(param.first().at(2).toList().first().toMap().value("hi").toDouble(), 30.0);
        r->shutdown();
    }

    // ---- TripRecorder ------------------------------------------------------

    void recorderStartsOnRpmAndEndsWhenEngineStops()
    {
        auto r = makeRecorder();
        QSignalSpy started(r.get(), &TripRecorder::tripStarted);
        QSignalSpy ended(r.get(), &TripRecorder::tripEnded);

        // Engine off: nothing recorded, no trip
        r->recordSample(QStringLiteral("RPM"), 0.0f);
        r->recordSample(QStringLiteral("COOLANT_TEMP"), 20.0f);
        QVERIFY(!r->recording());
        QCOMPARE(started.count(), 0);

        m_now += 1000;
        r->recordSample(QStringLiteral("RPM"), 800.0f);
        QVERIFY(r->recording());
        QCOMPARE(started.count(), 1);
        const qint64 id = r->currentTripId();
        QCOMPARE(id, m_now);

        drive(*r, 30, 30.0f);
        QVERIFY(r->currentSampleCount() > 60);

        // Engine stops; the trip ends after the engine-off timeout, not before
        for (int i = 0; i < 4; ++i) {
            m_now += 1000;
            r->recordSample(QStringLiteral("RPM"), 0.0f);
            r->tick();
        }
        QVERIFY(r->recording());
        m_now += 2000;
        r->recordSample(QStringLiteral("RPM"), 0.0f);
        r->tick();
        QVERIFY(!r->recording());
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        const QVariantMap s = ended.first().at(1).toMap();
        QCOMPARE(s.value("id").toLongLong(), id);
        // 30 s at 30 mph is 0.25 mi; the stored trace starts a second in and
        // holds an unchanged speed only every heartbeat, so a little under
        const double miles = s.value("distanceMiles").toDouble();
        QVERIFY2(miles > 0.2 && miles <= 0.25, qPrintable(QString::number(miles)));
        QCOMPARE(s.value("maxSpeed").toDouble(), 30.0);
        QVERIFY(QFile::exists(s.value("file").toString()));
        QVERIFY(s.value("file").toString().startsWith(r->tripsDir()));
        // The end event carries the reason
        bool sawEnd = false;
        for (const QVariant &e : s.value("events").toList())
            if (e.toMap().value("kind") == QStringLiteral("trip_end")) sawEnd = e.toMap().value("detail") == QStringLiteral("engine off");
        QVERIFY(sawEnd);

        QTRY_VERIFY_WITH_TIMEOUT(!r->trips().isEmpty(), 5000);
        QCOMPARE(r->trips().first().toMap().value("id").toLongLong(), id);
        QVERIFY(r->storageBytes() > 0);
        r->shutdown();
    }

    void liveTripReportsElapsedAndThrottledGrowth()
    {
        // Trip History follows these instead of a 1 s clock and a 5 s poll
        auto r = makeRecorder();
        r->setLiveRefreshMs(5000);
        QSignalSpy elapsed(r.get(), &TripRecorder::currentTripElapsedSChanged);
        QSignalSpy grew(r.get(), &TripRecorder::liveTripChanged);
        QCOMPARE(r->currentTripElapsedS(), 0);

        drive(*r, 1, 10.0f);
        QVERIFY(r->recording());
        const qint64 id = r->currentTripId();
        m_now += 3000;
        r->tick();
        QCOMPARE(r->currentTripElapsedS(), 3);
        QVERIFY(elapsed.count() >= 1);

        // Flushes every 50 ms here, but growth is reported once per 5 s
        drive(*r, 1, 20.0f);
        r->flush();
        QCOMPARE(grew.count(), 0);       // 4 s in
        drive(*r, 2, 30.0f);
        r->flush();
        QCOMPARE(grew.count(), 1);       // 6 s in
        QCOMPARE(grew.first().first().toLongLong(), id);
        drive(*r, 1, 30.0f);
        r->flush();
        QCOMPARE(grew.count(), 1);

        r->endTripNow();
        QCOMPARE(r->currentTripElapsedS(), 0);
        r->shutdown();
    }

    void unchangedValuesAreCoalesced()
    {
        auto r = makeRecorder();
        m_now += 1000;
        r->recordSample(QStringLiteral("RPM"), 800.0f);
        // Fuel level that never changes: 20 readings a second apart, heartbeat 2 s
        for (int i = 0; i < 20; ++i) {
            m_now += 1000;
            r->recordSample(QStringLiteral("FUEL_LEVEL"), 50.0f);
        }
        // changed values always land
        for (int i = 0; i < 5; ++i) {
            m_now += 100;
            r->recordSample(QStringLiteral("SPEED"), 10.0f + i);
        }
        // RPM once (start) + fuel every 2 s (10) + 5 speeds
        QCOMPARE(r->currentSampleCount(), 1 + 10 + 5);
        r->shutdown();
    }

    void shortDropoutDoesNotSplitTheTrip()
    {
        auto r = makeRecorder();
        QSignalSpy started(r.get(), &TripRecorder::tripStarted);
        QSignalSpy ended(r.get(), &TripRecorder::tripEnded);
        m_now += 1000;
        r->recordSample(QStringLiteral("RPM"), 1500.0f);
        drive(*r, 5, 40.0f);
        // Adapter drops for 4 s (grace is 8 s), then comes back
        r->setConnected(false);
        for (int i = 0; i < 4; ++i) {
            m_now += 1000;
            r->tick();
        }
        QVERIFY(r->recording());
        r->setConnected(true);
        drive(*r, 5, 40.0f);
        QCOMPARE(started.count(), 1);
        QCOMPARE(ended.count(), 0);
        // A long drop ends it with "connection lost"
        r->setConnected(false);
        m_now += 9000;
        r->tick();
        QVERIFY(!r->recording());
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
        const QVariantList events = ended.first().at(1).toMap().value("events").toList();
        QStringList kinds;
        for (const QVariant &e : events)
            kinds << e.toMap().value("kind").toString();
        QVERIFY(kinds.contains(QStringLiteral("disconnected")));
        QVERIFY(kinds.contains(QStringLiteral("reconnected")));
        QCOMPARE(events.last().toMap().value("detail").toString(), QStringLiteral("connection lost"));
        r->shutdown();
    }

    void shutdownWritesTheOpenTripAndRestartKeepsHistory()
    {
        qint64 id = 0;
        {
            auto r = makeRecorder();
            m_now += 1000;
            r->recordSample(QStringLiteral("RPM"), 1200.0f);
            drive(*r, 10, 25.0f);
            id = r->currentTripId();
            r->shutdown();   // app quit mid-drive: trip closed cleanly, files written
        }
        auto r = makeRecorder();
        QTRY_VERIFY_WITH_TIMEOUT(!r->trips().isEmpty(), 5000);
        const QVariantMap t = tripById(r->trips(), id);
        QVERIFY(!t.isEmpty());
        QCOMPARE(t.value("clean").toBool(), true);
        QCOMPARE(t.value("open").toBool(), false);
        QVERIFY(QFile::exists(t.value("file").toString()));

        // detail and series come back through the recorder's signals
        QSignalSpy detail(r.get(), &TripRecorder::detailReady);
        QSignalSpy series(r.get(), &TripRecorder::seriesReady);
        r->requestDetail(id);
        r->requestSeries(id, QStringLiteral("SPEED"), 50);
        QTRY_COMPARE_WITH_TIMEOUT(detail.count(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(series.count(), 1, 5000);
        QCOMPARE(detail.first().at(1).toMap().value("maxSpeed").toDouble(), 25.0);
        QVERIFY(!series.first().at(2).toList().isEmpty());

        // Export lands under Downloads
        QSignalSpy exported(r.get(), &TripRecorder::exportFinished);
        r->exportTrip(id);
        QTRY_COMPARE_WITH_TIMEOUT(exported.count(), 1, 5000);
        const QString path = exported.first().at(1).toString();
        QVERIFY(QFile::exists(path));
        QVERIFY(path.contains(QStringLiteral("OCTAVE-trips")));

        r->deleteTrip(id);
        QTRY_VERIFY_WITH_TIMEOUT(tripById(r->trips(), id).isEmpty(), 5000);
        r->shutdown();
    }

    void disabledRecorderIgnoresSamples()
    {
        m_settings->save_black_box_enabled(false);
        auto r = makeRecorder();
        QVERIFY(!r->enabled());
        m_now += 1000;
        r->recordSample(QStringLiteral("RPM"), 1000.0f);
        QVERIFY(!r->recording());
        m_settings->save_black_box_enabled(true);
        QVERIFY(r->enabled());
        m_now += 1000;
        r->recordSample(QStringLiteral("RPM"), 1000.0f);
        QVERIFY(r->recording());
        // Turning it off mid-trip ends the trip
        m_settings->save_black_box_enabled(false);
        QVERIFY(!r->recording());
        r->shutdown();
    }

    void unopenableStoreNeverPretendsToRecord()
    {
        // A history dir under a regular file cannot be created
        QFile blocker(m_tmp->path() + "/blocker");
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        auto r = std::make_unique<TripRecorder>(m_settings.get());
        r->setHistoryDir(m_tmp->path() + "/blocker/history");
        r->setClock([this]() { return m_now; });
        QSignalSpy ready(r.get(), &TripRecorder::storeReadyChanged);
        QSignalSpy errors(r.get(), &TripRecorder::lastErrorChanged);
        r->start();
        r->setConnected(true);
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, 5000);
        QVERIFY(!r->storeReady());
        QVERIFY(!r->lastError().isEmpty());
        QCOMPARE(ready.count(), 0);
        m_now += 1000;
        r->recordSample(QStringLiteral("RPM"), 1200.0f);
        QVERIFY(!r->recording());
        QCOMPARE(r->currentSampleCount(), 0);
        r->shutdown();
    }

    void backgroundSweepFollowsTheScanAndTheSetting()
    {
        // OBDManager without a connection: only the demand bookkeeping runs
        OBDManager obd(m_settings.get());
        auto r = makeRecorder();
        r->attach(&obd);
        // The static rules: background PIDs exclude the heartbeat and the foreground
        QHash<QString, QSet<QString>> fg;
        fg.insert("page", {"SPEED", "RPM"});
        QHash<QString, QSet<QString>> bg;
        bg.insert("blackbox", {"SPEED", "RPM", "COOLANT_TEMP", "OIL_TEMP"});
        const QStringList supported = {"SPEED", "RPM", "COOLANT_TEMP", "OIL_TEMP"};
        const QList<PidKey> front = OBDManager::pidsToWatch(fg, supported);
        const QList<PidKey> back = OBDManager::backgroundPidsToWatch(bg, front, supported);
        QCOMPARE(front.size(), 2);
        QCOMPARE(back.size(), 2);   // coolant + oil only
        for (const PidKey &k : back)
            QVERIFY(!front.contains(k));
        QVERIFY(OBDManager::backgroundPidsToWatch({}, front, supported).isEmpty());
        r->shutdown();
    }
};

QTEST_MAIN(TstTripRecorder)
#include "tst_triprecorder.moc"
