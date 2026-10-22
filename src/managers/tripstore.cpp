#include "tripstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

#include <algorithm>

Q_LOGGING_CATEGORY(lcTripStore, "octave.tripstore")

namespace {
const int kSchemaVersion = 1;
// Gaps longer than this between two speed samples are not integrated into
// the distance (a dropout, not a 10 s stretch at the last known speed)
const qint64 kMaxIntegrationGapMs = 10000;
}

TripStore::TripStore(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("octave_blackbox_%1").arg(reinterpret_cast<quintptr>(this)))
{
}

TripStore::~TripStore()
{
    close();
}

// ---------------------------------------------------------------------------
// Open / schema
// ---------------------------------------------------------------------------

bool TripStore::open(const QString &dir)
{
    close();
    m_dir = dir;
    if (!QDir().mkpath(tripsDir())) {
        emit error(QStringLiteral("Cannot create %1").arg(tripsDir()));
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(databasePath());
    if (!m_db.open()) {
        const QString msg = QStringLiteral("Cannot open %1: %2").arg(databasePath(), m_db.lastError().text());
        qCWarning(lcTripStore) << msg;
        emit error(msg);
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }
    if (!createSchema()) {
        close();
        return false;
    }
    qCInfo(lcTripStore) << "Black box open at" << databasePath();
    return true;
}

void TripStore::close()
{
    if (m_db.isValid()) {
        if (m_db.isOpen()) {
            exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
            m_db.close();
        }
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_paramIds.clear();
}

bool TripStore::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        qCWarning(lcTripStore) << "SQL failed:" << sql << "--" << q.lastError().text();
        emit error(q.lastError().text());
        return false;
    }
    return true;
}

bool TripStore::createSchema()
{
    // auto_vacuum only takes effect on a fresh file; set before any table
    exec(QStringLiteral("PRAGMA auto_vacuum=INCREMENTAL"));
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    const QStringList ddl = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS trips("
                       "id INTEGER PRIMARY KEY, start_ms INTEGER NOT NULL, end_ms INTEGER, "
                       "clean INTEGER NOT NULL DEFAULT 0, vin TEXT, make TEXT, model_year INTEGER, "
                       "sample_count INTEGER NOT NULL DEFAULT 0, summary TEXT, file_base TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS params(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS samples("
                       "trip_id INTEGER NOT NULL, ts_ms INTEGER NOT NULL, param_id INTEGER NOT NULL, value REAL NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS samples_trip_param_ts ON samples(trip_id, param_id, ts_ms)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS events("
                       "trip_id INTEGER NOT NULL, ts_ms INTEGER NOT NULL, kind TEXT NOT NULL, detail TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS events_trip ON events(trip_id, ts_ms)"),
    };
    for (const QString &sql : ddl) {
        if (!exec(sql))
            return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO meta(key, value) VALUES('schema', ?)"));
    q.addBindValue(QString::number(kSchemaVersion));
    q.exec();
    return true;
}

int TripStore::paramId(const QString &name)
{
    auto it = m_paramIds.constFind(name);
    if (it != m_paramIds.constEnd())
        return it.value();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO params(name) VALUES(?)"));
    q.addBindValue(name);
    q.exec();
    q.prepare(QStringLiteral("SELECT id FROM params WHERE name = ?"));
    q.addBindValue(name);
    int id = -1;
    if (q.exec() && q.next())
        id = q.value(0).toInt();
    if (id >= 0)
        m_paramIds.insert(name, id);
    return id;
}

QHash<int, QString> TripStore::paramNames()
{
    QHash<int, QString> names;
    QSqlQuery q(QStringLiteral("SELECT id, name FROM params"), m_db);
    while (q.next())
        names.insert(q.value(0).toInt(), q.value(1).toString());
    return names;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void TripStore::beginTrip(qint64 id, qint64 startMs, const QVariantMap &meta)
{
    if (!isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO trips(id, start_ms, vin, make, model_year) VALUES(?, ?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(startMs);
    q.addBindValue(meta.value(QStringLiteral("vin")).toString());
    q.addBindValue(meta.value(QStringLiteral("make")).toString());
    q.addBindValue(meta.value(QStringLiteral("modelYear")).toInt());
    if (!q.exec()) {
        qCWarning(lcTripStore) << "beginTrip failed:" << q.lastError().text();
        emit error(q.lastError().text());
        return;
    }
    qCInfo(lcTripStore) << "Trip" << id << "started";
    emit tripsChanged(listTrips());
}

void TripStore::updateTripMeta(qint64 id, const QVariantMap &meta)
{
    if (!isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE trips SET vin = ?, make = ?, model_year = ? WHERE id = ?"));
    q.addBindValue(meta.value(QStringLiteral("vin")).toString());
    q.addBindValue(meta.value(QStringLiteral("make")).toString());
    q.addBindValue(meta.value(QStringLiteral("modelYear")).toInt());
    q.addBindValue(id);
    q.exec();
}

void TripStore::append(const QVector<TripSample> &samples)
{
    if (!isOpen() || samples.isEmpty())
        return;
    if (!m_db.transaction()) {
        qCWarning(lcTripStore) << "transaction failed:" << m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO samples(trip_id, ts_ms, param_id, value) VALUES(?, ?, ?, ?)"));
    QHash<qint64, int> perTrip;
    for (const TripSample &s : samples) {
        const int pid = paramId(s.paramId);
        if (pid < 0)
            continue;
        q.addBindValue(s.tripId);
        q.addBindValue(s.tsMs);
        q.addBindValue(pid);
        q.addBindValue(static_cast<double>(s.value));
        if (q.exec())
            ++perTrip[s.tripId];
    }
    QSqlQuery c(m_db);
    c.prepare(QStringLiteral("UPDATE trips SET sample_count = sample_count + ? WHERE id = ?"));
    for (auto it = perTrip.constBegin(); it != perTrip.constEnd(); ++it) {
        c.addBindValue(it.value());
        c.addBindValue(it.key());
        c.exec();
    }
    if (!m_db.commit()) {
        qCWarning(lcTripStore) << "commit failed:" << m_db.lastError().text();
        m_db.rollback();
    }
}

void TripStore::appendEvent(qint64 tripId, qint64 tsMs, const QString &kind, const QString &detail)
{
    if (!isOpen())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO events(trip_id, ts_ms, kind, detail) VALUES(?, ?, ?, ?)"));
    q.addBindValue(tripId);
    q.addBindValue(tsMs);
    q.addBindValue(kind);
    q.addBindValue(detail);
    q.exec();
}

QVariantMap TripStore::tripRow(qint64 id)
{
    QVariantMap row;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, start_ms, end_ms, clean, vin, make, model_year, sample_count, summary, file_base "
                             "FROM trips WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return row;
    row[QStringLiteral("id")] = q.value(0).toLongLong();
    row[QStringLiteral("startMs")] = q.value(1).toLongLong();
    row[QStringLiteral("endMs")] = q.value(2).isNull() ? QVariant() : QVariant(q.value(2).toLongLong());
    row[QStringLiteral("clean")] = q.value(3).toInt() != 0;
    row[QStringLiteral("vin")] = q.value(4).toString();
    row[QStringLiteral("make")] = q.value(5).toString();
    row[QStringLiteral("modelYear")] = q.value(6).toInt();
    row[QStringLiteral("sampleCount")] = q.value(7).toLongLong();
    row[QStringLiteral("summary")] = q.value(8).toString();
    row[QStringLiteral("fileBase")] = q.value(9).toString();
    return row;
}

QVariantMap TripStore::endTrip(qint64 id, qint64 endMs, bool clean)
{
    if (!isOpen())
        return {};
    const QVariantMap row = tripRow(id);
    if (row.isEmpty()) {
        qCWarning(lcTripStore) << "endTrip: unknown trip" << id;
        return {};
    }
    const qint64 startMs = row.value(QStringLiteral("startMs")).toLongLong();
    if (endMs < startMs)
        endMs = startMs;
    QVariantMap meta;
    meta[QStringLiteral("vin")] = row.value(QStringLiteral("vin"));
    meta[QStringLiteral("make")] = row.value(QStringLiteral("make"));
    meta[QStringLiteral("modelYear")] = row.value(QStringLiteral("modelYear"));

    QVariantMap summary = summarise(id, startMs, endMs, clean, meta);
    QString base;
    if (writeTripFiles(id, startMs, summary, &base))
        summary[QStringLiteral("file")] = base + QStringLiteral(".csv");

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE trips SET end_ms = ?, clean = ?, summary = ?, file_base = ? WHERE id = ?"));
    q.addBindValue(endMs);
    q.addBindValue(clean ? 1 : 0);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(summary)).toJson(QJsonDocument::Compact)));
    q.addBindValue(base);
    q.addBindValue(id);
    if (!q.exec()) {
        qCWarning(lcTripStore) << "endTrip failed:" << q.lastError().text();
        emit error(q.lastError().text());
    }
    exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE)"));
    qCInfo(lcTripStore) << "Trip" << id << "ended:" << summary.value(QStringLiteral("sampleCount")).toLongLong()
                        << "samples," << summary.value(QStringLiteral("durationS")).toInt() << "s"
                        << (clean ? "" : "(recovered)");
    emit tripFinalized(id, summary);
    emit tripsChanged(listTrips());
    emitStorage();
    return summary;
}

int TripStore::recoverOpenTrips()
{
    if (!isOpen())
        return 0;
    QList<qint64> open;
    QSqlQuery q(QStringLiteral("SELECT id FROM trips WHERE end_ms IS NULL"), m_db);
    while (q.next())
        open.append(q.value(0).toLongLong());
    for (qint64 id : open) {
        QSqlQuery last(m_db);
        last.prepare(QStringLiteral("SELECT MAX(ts_ms), COUNT(*) FROM samples WHERE trip_id = ?"));
        last.addBindValue(id);
        qint64 endMs = 0;
        qint64 count = 0;
        if (last.exec() && last.next()) {
            endMs = last.value(0).toLongLong();
            count = last.value(1).toLongLong();
        }
        if (count == 0) {
            // Nothing was ever written: drop the empty shell instead of
            // producing a zero-length trip
            deleteTrip(id);
            continue;
        }
        qCInfo(lcTripStore) << "Recovering trip" << id << "left open (power cut?)";
        endTrip(id, endMs, false);
    }
    return open.size();
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

QVariantMap TripStore::summarise(qint64 id, qint64 startMs, qint64 endMs, bool clean, const QVariantMap &meta)
{
    QVariantMap s;
    s[QStringLiteral("id")] = id;
    s[QStringLiteral("startMs")] = startMs;
    s[QStringLiteral("endMs")] = endMs;
    s[QStringLiteral("startIso")] = QDateTime::fromMSecsSinceEpoch(startMs).toString(Qt::ISODate);
    s[QStringLiteral("endIso")] = QDateTime::fromMSecsSinceEpoch(endMs).toString(Qt::ISODate);
    s[QStringLiteral("durationS")] = static_cast<double>(endMs - startMs) / 1000.0;
    s[QStringLiteral("clean")] = clean;
    s[QStringLiteral("vin")] = meta.value(QStringLiteral("vin")).toString();
    s[QStringLiteral("make")] = meta.value(QStringLiteral("make")).toString();
    s[QStringLiteral("modelYear")] = meta.value(QStringLiteral("modelYear")).toInt();

    // Per-parameter statistics
    QVariantList params;
    QHash<QString, QVariantMap> byName;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT p.name, COUNT(*), MIN(s.value), MAX(s.value), AVG(s.value) "
                                 "FROM samples s JOIN params p ON p.id = s.param_id "
                                 "WHERE s.trip_id = ? GROUP BY p.name ORDER BY p.name"));
        q.addBindValue(id);
        if (q.exec()) {
            while (q.next()) {
                QVariantMap p;
                p[QStringLiteral("id")] = q.value(0).toString();
                p[QStringLiteral("count")] = q.value(1).toLongLong();
                p[QStringLiteral("min")] = q.value(2).toDouble();
                p[QStringLiteral("max")] = q.value(3).toDouble();
                p[QStringLiteral("avg")] = q.value(4).toDouble();
                byName.insert(p.value(QStringLiteral("id")).toString(), p);
            }
        }
    }
    qint64 sampleCount = 0;
    QStringList names = byName.keys();
    names.sort();
    for (const QString &name : names) {
        QVariantMap p = byName.value(name);
        sampleCount += p.value(QStringLiteral("count")).toLongLong();
        const int pid = paramId(name);
        for (const char *order : {"ASC", "DESC"}) {
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("SELECT value FROM samples WHERE trip_id = ? AND param_id = ? ORDER BY ts_ms %1 LIMIT 1")
                          .arg(QLatin1String(order)));
            q.addBindValue(id);
            q.addBindValue(pid);
            if (q.exec() && q.next())
                p[QLatin1String(order[0] == 'A' ? "first" : "last")] = q.value(0).toDouble();
        }
        params.append(p);
    }
    s[QStringLiteral("sampleCount")] = sampleCount;
    s[QStringLiteral("paramCount")] = params.size();
    s[QStringLiteral("params")] = params;

    auto stat = [&byName](const char *name, const char *key) -> QVariant {
        const QVariantMap p = byName.value(QLatin1String(name));
        return p.isEmpty() ? QVariant() : p.value(QLatin1String(key));
    };
    s[QStringLiteral("maxSpeed")] = stat("SPEED", "max");
    s[QStringLiteral("maxRpm")] = stat("RPM", "max");
    s[QStringLiteral("maxCoolant")] = stat("COOLANT_TEMP", "max");
    s[QStringLiteral("maxOilTemp")] = stat("OIL_TEMP", "max");
    s[QStringLiteral("minVoltage")] = stat("CONTROL_MODULE_VOLTAGE", "min");
    s[QStringLiteral("maxVoltage")] = stat("CONTROL_MODULE_VOLTAGE", "max");
    s[QStringLiteral("fuelStart")] = stat("FUEL_LEVEL", "first");
    s[QStringLiteral("fuelEnd")] = stat("FUEL_LEVEL", "last");
    if (s.value(QStringLiteral("fuelStart")).isValid() && s.value(QStringLiteral("fuelEnd")).isValid())
        s[QStringLiteral("fuelUsedPct")] = s.value(QStringLiteral("fuelStart")).toDouble()
                                          - s.value(QStringLiteral("fuelEnd")).toDouble();

    // Distance: trapezoid over the speed trace (mph x hours), dropouts skipped
    double miles = 0.0;
    if (byName.contains(QStringLiteral("SPEED"))) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT ts_ms, value FROM samples WHERE trip_id = ? AND param_id = ? ORDER BY ts_ms"));
        q.addBindValue(id);
        q.addBindValue(paramId(QStringLiteral("SPEED")));
        if (q.exec()) {
            bool have = false;
            qint64 lastTs = 0;
            double lastV = 0.0;
            while (q.next()) {
                const qint64 ts = q.value(0).toLongLong();
                const double v = std::max(0.0, q.value(1).toDouble());
                if (have) {
                    const qint64 dt = ts - lastTs;
                    if (dt > 0 && dt <= kMaxIntegrationGapMs)
                        miles += (lastV + v) * 0.5 * (static_cast<double>(dt) / 3600000.0);
                }
                have = true;
                lastTs = ts;
                lastV = v;
            }
        }
    }
    s[QStringLiteral("distanceMiles")] = miles;

    // Events
    QVariantList events;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT ts_ms, kind, detail FROM events WHERE trip_id = ? ORDER BY ts_ms"));
        q.addBindValue(id);
        if (q.exec()) {
            while (q.next()) {
                QVariantMap e;
                e[QStringLiteral("t")] = static_cast<double>(q.value(0).toLongLong() - startMs) / 1000.0;
                e[QStringLiteral("kind")] = q.value(1).toString();
                e[QStringLiteral("detail")] = q.value(2).toString();
                events.append(e);
            }
        }
    }
    s[QStringLiteral("events")] = events;
    s[QStringLiteral("eventCount")] = events.size();
    return s;
}

// ---------------------------------------------------------------------------
// Trip files
// ---------------------------------------------------------------------------

QString TripStore::stampFor(qint64 startMs)
{
    return QDateTime::fromMSecsSinceEpoch(startMs).toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

bool TripStore::writeTripFiles(qint64 id, qint64 startMs, const QVariantMap &summary, QString *baseOut)
{
    QString base = tripsDir() + QLatin1Char('/') + stampFor(startMs);
    if (QFile::exists(base + QStringLiteral(".csv")) || QFile::exists(base + QStringLiteral(".json")))
        base += QStringLiteral("-%1").arg(id);
    *baseOut = base;

    // CSV: every sample, long format, in time order
    {
        QSaveFile f(base + QStringLiteral(".csv"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qCWarning(lcTripStore) << "Cannot write" << f.fileName() << f.errorString();
            return false;
        }
        QTextStream out(&f);
        out << "timestamp_ms,time,elapsed_s,parameter,value\n";
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT s.ts_ms, p.name, s.value FROM samples s JOIN params p ON p.id = s.param_id "
                                 "WHERE s.trip_id = ? ORDER BY s.ts_ms, p.name"));
        q.addBindValue(id);
        if (q.exec()) {
            while (q.next()) {
                const qint64 ts = q.value(0).toLongLong();
                out << ts << ','
                    << QDateTime::fromMSecsSinceEpoch(ts).toString(Qt::ISODateWithMs) << ','
                    << QString::number(static_cast<double>(ts - startMs) / 1000.0, 'f', 3) << ','
                    << q.value(1).toString() << ','
                    << QString::number(q.value(2).toDouble(), 'g', 7) << '\n';
            }
        }
        out.flush();
        if (!f.commit()) {
            qCWarning(lcTripStore) << "Cannot commit" << f.fileName() << f.errorString();
            return false;
        }
    }
    // JSON: the summary with per-parameter stats and events
    {
        QSaveFile f(base + QStringLiteral(".json"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qCWarning(lcTripStore) << "Cannot write" << f.fileName() << f.errorString();
            return false;
        }
        QVariantMap doc = summary;
        doc[QStringLiteral("format")] = QStringLiteral("octave-trip");
        doc[QStringLiteral("schema")] = kSchemaVersion;
        doc[QStringLiteral("csv")] = QFileInfo(base + QStringLiteral(".csv")).fileName();
        f.write(QJsonDocument(QJsonObject::fromVariantMap(doc)).toJson(QJsonDocument::Indented));
        if (!f.commit()) {
            qCWarning(lcTripStore) << "Cannot commit" << f.fileName() << f.errorString();
            return false;
        }
    }
    return true;
}

void TripStore::removeTripFiles(const QString &base)
{
    if (base.isEmpty())
        return;
    QFile::remove(base + QStringLiteral(".csv"));
    QFile::remove(base + QStringLiteral(".json"));
}

QString TripStore::tripFileBase(qint64 id)
{
    return tripRow(id).value(QStringLiteral("fileBase")).toString();
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

QVariantMap TripStore::rowSummary(const QVariantMap &row)
{
    QVariantMap item;
    const QString json = row.value(QStringLiteral("summary")).toString();
    if (!json.isEmpty())
        item = QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
    item.remove(QStringLiteral("params"));
    item.remove(QStringLiteral("events"));
    item[QStringLiteral("id")] = row.value(QStringLiteral("id"));
    item[QStringLiteral("startMs")] = row.value(QStringLiteral("startMs"));
    const bool open = !row.value(QStringLiteral("endMs")).isValid();
    item[QStringLiteral("open")] = open;
    if (open) {
        item[QStringLiteral("endMs")] = QVariant();
        item[QStringLiteral("durationS")] = 0.0;
        item[QStringLiteral("startIso")] = QDateTime::fromMSecsSinceEpoch(row.value(QStringLiteral("startMs")).toLongLong())
                                               .toString(Qt::ISODate);
    }
    item[QStringLiteral("clean")] = row.value(QStringLiteral("clean"));
    item[QStringLiteral("vin")] = row.value(QStringLiteral("vin"));
    item[QStringLiteral("make")] = row.value(QStringLiteral("make"));
    item[QStringLiteral("modelYear")] = row.value(QStringLiteral("modelYear"));
    item[QStringLiteral("sampleCount")] = row.value(QStringLiteral("sampleCount"));
    const QString base = row.value(QStringLiteral("fileBase")).toString();
    item[QStringLiteral("file")] = base.isEmpty() ? QString() : base + QStringLiteral(".csv");
    item[QStringLiteral("summaryFile")] = base.isEmpty() ? QString() : base + QStringLiteral(".json");
    return item;
}

QVariantList TripStore::listTrips()
{
    QVariantList list;
    if (!isOpen())
        return list;
    QSqlQuery q(QStringLiteral("SELECT id FROM trips ORDER BY start_ms DESC"), m_db);
    QList<qint64> ids;
    while (q.next())
        ids.append(q.value(0).toLongLong());
    for (qint64 id : ids)
        list.append(rowSummary(tripRow(id)));
    emit tripsChanged(list);
    return list;
}

QVariantMap TripStore::tripDetail(qint64 id)
{
    QVariantMap detail;
    if (!isOpen())
        return detail;
    const QVariantMap row = tripRow(id);
    if (row.isEmpty())
        return detail;
    const bool open = !row.value(QStringLiteral("endMs")).isValid();
    if (open) {
        // Live summary of the trip being recorded (not persisted)
        QSqlQuery last(m_db);
        last.prepare(QStringLiteral("SELECT MAX(ts_ms) FROM samples WHERE trip_id = ?"));
        last.addBindValue(id);
        qint64 endMs = row.value(QStringLiteral("startMs")).toLongLong();
        if (last.exec() && last.next() && !last.value(0).isNull())
            endMs = last.value(0).toLongLong();
        QVariantMap meta;
        meta[QStringLiteral("vin")] = row.value(QStringLiteral("vin"));
        meta[QStringLiteral("make")] = row.value(QStringLiteral("make"));
        meta[QStringLiteral("modelYear")] = row.value(QStringLiteral("modelYear"));
        detail = summarise(id, row.value(QStringLiteral("startMs")).toLongLong(), endMs, true, meta);
        detail[QStringLiteral("open")] = true;
    } else {
        detail = QJsonDocument::fromJson(row.value(QStringLiteral("summary")).toString().toUtf8()).object().toVariantMap();
        detail[QStringLiteral("open")] = false;
        const QString base = row.value(QStringLiteral("fileBase")).toString();
        detail[QStringLiteral("file")] = base.isEmpty() ? QString() : base + QStringLiteral(".csv");
        detail[QStringLiteral("summaryFile")] = base.isEmpty() ? QString() : base + QStringLiteral(".json");
    }
    emit detailReady(id, detail);
    return detail;
}

QVariantList TripStore::series(qint64 id, const QString &param, int maxPoints)
{
    QVariantList points;
    if (!isOpen())
        return points;
    const QVariantMap row = tripRow(id);
    if (row.isEmpty())
        return points;
    const qint64 startMs = row.value(QStringLiteral("startMs")).toLongLong();
    qint64 endMs = row.value(QStringLiteral("endMs")).toLongLong();
    const int pid = m_paramIds.value(param, -1) >= 0 ? m_paramIds.value(param) : paramId(param);
    if (!row.value(QStringLiteral("endMs")).isValid()) {
        QSqlQuery last(m_db);
        last.prepare(QStringLiteral("SELECT MAX(ts_ms) FROM samples WHERE trip_id = ? AND param_id = ?"));
        last.addBindValue(id);
        last.addBindValue(pid);
        if (last.exec() && last.next())
            endMs = last.value(0).toLongLong();
    }
    maxPoints = std::max(2, maxPoints);
    const qint64 bucketMs = std::max<qint64>(1, (endMs - startMs) / maxPoints + 1);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ((ts_ms - ?) / ?) AS b, MIN(ts_ms), AVG(value), MIN(value), MAX(value) "
                             "FROM samples WHERE trip_id = ? AND param_id = ? GROUP BY b ORDER BY b"));
    q.addBindValue(startMs);
    q.addBindValue(bucketMs);
    q.addBindValue(id);
    q.addBindValue(pid);
    if (q.exec()) {
        while (q.next()) {
            QVariantMap p;
            p[QStringLiteral("t")] = static_cast<double>(q.value(1).toLongLong() - startMs) / 1000.0;
            p[QStringLiteral("v")] = q.value(2).toDouble();
            p[QStringLiteral("lo")] = q.value(3).toDouble();
            p[QStringLiteral("hi")] = q.value(4).toDouble();
            points.append(p);
        }
    }
    emit seriesReady(id, param, points);
    return points;
}

qint64 TripStore::sampleCount(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM samples WHERE trip_id = ?"));
    q.addBindValue(id);
    return (q.exec() && q.next()) ? q.value(0).toLongLong() : 0;
}

// ---------------------------------------------------------------------------
// Housekeeping
// ---------------------------------------------------------------------------

bool TripStore::deleteTrip(qint64 id)
{
    if (!isOpen())
        return false;
    const QString base = tripFileBase(id);
    m_db.transaction();
    for (const char *sql : {"DELETE FROM samples WHERE trip_id = ?", "DELETE FROM events WHERE trip_id = ?",
                            "DELETE FROM trips WHERE id = ?"}) {
        QSqlQuery q(m_db);
        q.prepare(QLatin1String(sql));
        q.addBindValue(id);
        q.exec();
    }
    const bool ok = m_db.commit();
    removeTripFiles(base);
    exec(QStringLiteral("PRAGMA incremental_vacuum"));
    qCInfo(lcTripStore) << "Trip" << id << "deleted";
    emit tripsChanged(listTrips());
    emitStorage();
    return ok;
}

void TripStore::clearAll()
{
    if (!isOpen())
        return;
    QList<QString> bases;
    QSqlQuery q(QStringLiteral("SELECT file_base FROM trips WHERE end_ms IS NOT NULL"), m_db);
    while (q.next())
        bases.append(q.value(0).toString());
    m_db.transaction();
    // Keep the trip being recorded (end_ms NULL) and its samples
    exec(QStringLiteral("DELETE FROM samples WHERE trip_id IN (SELECT id FROM trips WHERE end_ms IS NOT NULL)"));
    exec(QStringLiteral("DELETE FROM events WHERE trip_id IN (SELECT id FROM trips WHERE end_ms IS NOT NULL)"));
    exec(QStringLiteral("DELETE FROM trips WHERE end_ms IS NOT NULL"));
    m_db.commit();
    for (const QString &b : bases)
        removeTripFiles(b);
    exec(QStringLiteral("PRAGMA incremental_vacuum"));
    exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    qCInfo(lcTripStore) << "History cleared";
    emit tripsChanged(listTrips());
    emitStorage();
}

qint64 TripStore::storageBytes() const
{
    qint64 bytes = 0;
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")})
        bytes += QFileInfo(databasePath() + suffix).size();
    const QFileInfoList files = QDir(tripsDir()).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files)
        bytes += fi.size();
    return bytes;
}

void TripStore::emitStorage()
{
    emit storageChanged(storageBytes());
}

qint64 TripStore::prune(qint64 maxBytes)
{
    if (!isOpen())
        return 0;
    qint64 bytes = storageBytes();
    int deleted = 0;
    while (bytes > maxBytes) {
        QSqlQuery q(QStringLiteral("SELECT id FROM trips WHERE end_ms IS NOT NULL ORDER BY start_ms ASC LIMIT 1"), m_db);
        if (!q.next())
            break;
        const qint64 id = q.value(0).toLongLong();
        const QString base = tripFileBase(id);
        m_db.transaction();
        for (const char *sql : {"DELETE FROM samples WHERE trip_id = ?", "DELETE FROM events WHERE trip_id = ?",
                                "DELETE FROM trips WHERE id = ?"}) {
            QSqlQuery d(m_db);
            d.prepare(QLatin1String(sql));
            d.addBindValue(id);
            d.exec();
        }
        m_db.commit();
        removeTripFiles(base);
        ++deleted;
        exec(QStringLiteral("PRAGMA incremental_vacuum"));
        exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        bytes = storageBytes();
    }
    if (deleted > 0) {
        qCInfo(lcTripStore) << "Pruned" << deleted << "oldest trips to fit" << maxBytes << "bytes; using" << bytes;
        emit tripsChanged(listTrips());
    }
    emit storageChanged(bytes);
    return bytes;
}

QString TripStore::exportTrip(qint64 id, const QString &destDir)
{
    QString result;
    const QString base = tripFileBase(id);
    if (base.isEmpty() || !QFile::exists(base + QStringLiteral(".csv"))) {
        emit exportFinished(id, QString());
        return result;
    }
    if (!QDir().mkpath(destDir)) {
        emit exportFinished(id, QString());
        return result;
    }
    const QString name = QFileInfo(base).fileName();
    const QString csv = destDir + QStringLiteral("/OCTAVE-trip-") + name + QStringLiteral(".csv");
    const QString json = destDir + QStringLiteral("/OCTAVE-trip-") + name + QStringLiteral(".json");
    QFile::remove(csv);
    QFile::remove(json);
    if (QFile::copy(base + QStringLiteral(".csv"), csv)) {
        QFile::copy(base + QStringLiteral(".json"), json);
        result = csv;
        qCInfo(lcTripStore) << "Exported trip" << id << "to" << csv;
    }
    emit exportFinished(id, result);
    return result;
}
