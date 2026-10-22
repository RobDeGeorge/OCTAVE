#include "tripstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

#include "sqlite3.h"

Q_LOGGING_CATEGORY(lcTripStore, "octave.tripstore")

namespace {
const int kSchemaVersion = 1;
// Gaps longer than this between two speed samples are not integrated into
// the distance (a dropout, not a 10 s stretch at the last known speed)
const qint64 kMaxIntegrationGapMs = 10000;

// A prepared statement with Qt-typed binds and reads; finalised on scope exit.
class Query
{
public:
    explicit Query(sqlite3 *db) : m_db(db) {}
    ~Query() { sqlite3_finalize(m_stmt); }
    Query(const Query &) = delete;
    Query &operator=(const Query &) = delete;

    bool prepare(const char *sql)
    {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &m_stmt, nullptr) != SQLITE_OK) {
            m_error = QString::fromUtf8(sqlite3_errmsg(m_db));
            qCWarning(lcTripStore) << "prepare failed:" << sql << "--" << m_error;
            return false;
        }
        return true;
    }
    bool prepare(const QString &sql) { return prepare(sql.toUtf8().constData()); }
    Query &bind(int idx, qint64 v) { if (m_stmt) sqlite3_bind_int64(m_stmt, idx, v); return *this; }
    Query &bind(int idx, int v) { if (m_stmt) sqlite3_bind_int(m_stmt, idx, v); return *this; }
    Query &bind(int idx, double v) { if (m_stmt) sqlite3_bind_double(m_stmt, idx, v); return *this; }
    Query &bind(int idx, const QString &v)
    {
        if (m_stmt) {
            const QByteArray utf8 = v.toUtf8();
            sqlite3_bind_text(m_stmt, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
        }
        return *this;
    }
    // One row available (SELECT); false when done or failed
    bool step()
    {
        if (!m_stmt)
            return false;
        const int rc = sqlite3_step(m_stmt);
        if (rc == SQLITE_ROW)
            return true;
        if (rc != SQLITE_DONE) {
            m_error = QString::fromUtf8(sqlite3_errmsg(m_db));
            qCWarning(lcTripStore) << "step failed:" << m_error;
        }
        return false;
    }
    // Run to completion (INSERT / UPDATE / DELETE); true on success
    bool exec()
    {
        if (!m_stmt)
            return false;
        const int rc = sqlite3_step(m_stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            m_error = QString::fromUtf8(sqlite3_errmsg(m_db));
            qCWarning(lcTripStore) << "exec failed:" << m_error;
            return false;
        }
        return true;
    }
    void reset() { if (m_stmt) { sqlite3_reset(m_stmt); sqlite3_clear_bindings(m_stmt); } }
    bool isNull(int col) const { return sqlite3_column_type(m_stmt, col) == SQLITE_NULL; }
    qint64 int64(int col) const { return sqlite3_column_int64(m_stmt, col); }
    int integer(int col) const { return sqlite3_column_int(m_stmt, col); }
    double dbl(int col) const { return sqlite3_column_double(m_stmt, col); }
    QString text(int col) const
    {
        const unsigned char *t = sqlite3_column_text(m_stmt, col);
        return t ? QString::fromUtf8(reinterpret_cast<const char *>(t), sqlite3_column_bytes(m_stmt, col)) : QString();
    }
    QString error() const { return m_error; }

private:
    sqlite3 *m_db;
    sqlite3_stmt *m_stmt = nullptr;
    QString m_error;
};
} // namespace

TripStore::TripStore(QObject *parent)
    : QObject(parent)
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
        const QString msg = QStringLiteral("Cannot create %1").arg(tripsDir());
        qCWarning(lcTripStore) << msg;
        emit error(msg);
        emit storeOpened(false, msg);
        return false;
    }
    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(databasePath().toUtf8().constData(), &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        const QString msg = QStringLiteral("Cannot open %1: %2")
                                .arg(databasePath(), db ? QString::fromUtf8(sqlite3_errmsg(db)) : QString::fromUtf8(sqlite3_errstr(rc)));
        qCWarning(lcTripStore) << msg;
        if (db)
            sqlite3_close(db);
        emit error(msg);
        emit storeOpened(false, msg);
        return false;
    }
    m_db = db;
    sqlite3_busy_timeout(m_db, 2000);
    if (!createSchema()) {
        const QString msg = QStringLiteral("Cannot initialise %1").arg(databasePath());
        close();
        emit storeOpened(false, msg);
        return false;
    }
    qCInfo(lcTripStore).noquote() << "Black box open at" << databasePath()
                                  << QStringLiteral("(SQLite %1)").arg(QLatin1String(SQLITE_VERSION));
    emit storeOpened(true, QString());
    return true;
}

void TripStore::close()
{
    if (m_db) {
        exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    m_paramIds.clear();
}

bool TripStore::exec(const QString &sql)
{
    if (!m_db)
        return false;
    char *errmsg = nullptr;
    const int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        const QString msg = errmsg ? QString::fromUtf8(errmsg) : QString::fromUtf8(sqlite3_errstr(rc));
        sqlite3_free(errmsg);
        qCWarning(lcTripStore) << "SQL failed:" << sql << "--" << msg;
        emit error(msg);
        return false;
    }
    return true;
}

bool TripStore::createSchema()
{
    // auto_vacuum only takes effect on a fresh file; set before any table
    exec(QStringLiteral("PRAGMA auto_vacuum=INCREMENTAL"));
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    // FULL: every commit (one per 2 s batch) is fsynced, so a hard power cut
    // at ignition-off loses at most that batch, not the page cache's backlog
    exec(QStringLiteral("PRAGMA synchronous=FULL"));
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
    Query q(m_db);
    if (q.prepare("INSERT OR IGNORE INTO meta(key, value) VALUES('schema', ?)"))
        q.bind(1, QString::number(kSchemaVersion)).exec();
    return true;
}

int TripStore::paramId(const QString &name)
{
    auto it = m_paramIds.constFind(name);
    if (it != m_paramIds.constEnd())
        return it.value();
    Query ins(m_db);
    if (ins.prepare("INSERT OR IGNORE INTO params(name) VALUES(?)"))
        ins.bind(1, name).exec();
    Query sel(m_db);
    int id = -1;
    if (sel.prepare("SELECT id FROM params WHERE name = ?")) {
        sel.bind(1, name);
        if (sel.step())
            id = sel.integer(0);
    }
    if (id >= 0)
        m_paramIds.insert(name, id);
    return id;
}

QHash<int, QString> TripStore::paramNames()
{
    QHash<int, QString> names;
    Query q(m_db);
    if (q.prepare("SELECT id, name FROM params")) {
        while (q.step())
            names.insert(q.integer(0), q.text(1));
    }
    return names;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void TripStore::beginTrip(qint64 id, qint64 startMs, const QVariantMap &meta)
{
    if (!isOpen())
        return;
    Query q(m_db);
    if (!q.prepare("INSERT OR REPLACE INTO trips(id, start_ms, vin, make, model_year) VALUES(?, ?, ?, ?, ?)"))
        return;
    q.bind(1, id).bind(2, startMs)
        .bind(3, meta.value(QStringLiteral("vin")).toString())
        .bind(4, meta.value(QStringLiteral("make")).toString())
        .bind(5, meta.value(QStringLiteral("modelYear")).toInt());
    if (!q.exec()) {
        emit error(q.error());
        return;
    }
    qCInfo(lcTripStore) << "Trip" << id << "started";
    emit tripsChanged(listTrips());
}

void TripStore::updateTripMeta(qint64 id, const QVariantMap &meta)
{
    if (!isOpen())
        return;
    Query q(m_db);
    if (!q.prepare("UPDATE trips SET vin = ?, make = ?, model_year = ? WHERE id = ?"))
        return;
    q.bind(1, meta.value(QStringLiteral("vin")).toString())
        .bind(2, meta.value(QStringLiteral("make")).toString())
        .bind(3, meta.value(QStringLiteral("modelYear")).toInt())
        .bind(4, id)
        .exec();
}

void TripStore::append(const QVector<TripSample> &samples)
{
    if (!isOpen() || samples.isEmpty())
        return;
    if (!exec(QStringLiteral("BEGIN")))
        return;
    Query q(m_db);
    if (!q.prepare("INSERT INTO samples(trip_id, ts_ms, param_id, value) VALUES(?, ?, ?, ?)")) {
        exec(QStringLiteral("ROLLBACK"));
        return;
    }
    QHash<qint64, int> perTrip;
    for (const TripSample &s : samples) {
        const int pid = paramId(s.paramId);
        if (pid < 0)
            continue;
        q.bind(1, s.tripId).bind(2, s.tsMs).bind(3, pid).bind(4, static_cast<double>(s.value));
        if (q.exec())
            ++perTrip[s.tripId];
        q.reset();
    }
    Query c(m_db);
    if (c.prepare("UPDATE trips SET sample_count = sample_count + ? WHERE id = ?")) {
        for (auto it = perTrip.constBegin(); it != perTrip.constEnd(); ++it) {
            c.bind(1, it.value()).bind(2, it.key()).exec();
            c.reset();
        }
    }
    if (!exec(QStringLiteral("COMMIT")))
        exec(QStringLiteral("ROLLBACK"));
}

void TripStore::appendEvent(qint64 tripId, qint64 tsMs, const QString &kind, const QString &detail)
{
    if (!isOpen())
        return;
    Query q(m_db);
    if (q.prepare("INSERT INTO events(trip_id, ts_ms, kind, detail) VALUES(?, ?, ?, ?)"))
        q.bind(1, tripId).bind(2, tsMs).bind(3, kind).bind(4, detail).exec();
}

QVariantMap TripStore::tripRow(qint64 id)
{
    QVariantMap row;
    Query q(m_db);
    if (!q.prepare("SELECT id, start_ms, end_ms, clean, vin, make, model_year, sample_count, summary, file_base "
                   "FROM trips WHERE id = ?"))
        return row;
    q.bind(1, id);
    if (!q.step())
        return row;
    row[QStringLiteral("id")] = q.int64(0);
    row[QStringLiteral("startMs")] = q.int64(1);
    row[QStringLiteral("endMs")] = q.isNull(2) ? QVariant() : QVariant(q.int64(2));
    row[QStringLiteral("clean")] = q.integer(3) != 0;
    row[QStringLiteral("vin")] = q.text(4);
    row[QStringLiteral("make")] = q.text(5);
    row[QStringLiteral("modelYear")] = q.integer(6);
    row[QStringLiteral("sampleCount")] = q.int64(7);
    row[QStringLiteral("summary")] = q.text(8);
    row[QStringLiteral("fileBase")] = q.text(9);
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

    Query q(m_db);
    if (q.prepare("UPDATE trips SET end_ms = ?, clean = ?, summary = ?, file_base = ? WHERE id = ?")) {
        q.bind(1, endMs).bind(2, clean ? 1 : 0)
            .bind(3, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(summary)).toJson(QJsonDocument::Compact)))
            .bind(4, base).bind(5, id);
        if (!q.exec())
            emit error(q.error());
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
    {
        Query q(m_db);
        if (q.prepare("SELECT id FROM trips WHERE end_ms IS NULL")) {
            while (q.step())
                open.append(q.int64(0));
        }
    }
    for (qint64 id : open) {
        qint64 endMs = 0;
        qint64 count = 0;
        Query last(m_db);
        if (last.prepare("SELECT MAX(ts_ms), COUNT(*) FROM samples WHERE trip_id = ?")) {
            last.bind(1, id);
            if (last.step()) {
                endMs = last.int64(0);
                count = last.int64(1);
            }
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
        Query q(m_db);
        if (q.prepare("SELECT p.name, COUNT(*), MIN(s.value), MAX(s.value), AVG(s.value) "
                      "FROM samples s JOIN params p ON p.id = s.param_id "
                      "WHERE s.trip_id = ? GROUP BY p.name ORDER BY p.name")) {
            q.bind(1, id);
            while (q.step()) {
                QVariantMap p;
                p[QStringLiteral("id")] = q.text(0);
                p[QStringLiteral("count")] = q.int64(1);
                p[QStringLiteral("min")] = q.dbl(2);
                p[QStringLiteral("max")] = q.dbl(3);
                p[QStringLiteral("avg")] = q.dbl(4);
                byName.insert(p.value(QStringLiteral("id")).toString(), p);
            }
        }
    }
    qint64 sampleCount = 0;
    QStringList names = byName.keys();
    names.sort();
    Query first(m_db), lastQ(m_db);
    const bool haveEnds = first.prepare("SELECT value FROM samples WHERE trip_id = ? AND param_id = ? ORDER BY ts_ms ASC LIMIT 1")
                          && lastQ.prepare("SELECT value FROM samples WHERE trip_id = ? AND param_id = ? ORDER BY ts_ms DESC LIMIT 1");
    for (const QString &name : names) {
        QVariantMap p = byName.value(name);
        sampleCount += p.value(QStringLiteral("count")).toLongLong();
        const int pid = paramId(name);
        if (haveEnds) {
            first.bind(1, id).bind(2, pid);
            if (first.step())
                p[QStringLiteral("first")] = first.dbl(0);
            first.reset();
            lastQ.bind(1, id).bind(2, pid);
            if (lastQ.step())
                p[QStringLiteral("last")] = lastQ.dbl(0);
            lastQ.reset();
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
        Query q(m_db);
        if (q.prepare("SELECT ts_ms, value FROM samples WHERE trip_id = ? AND param_id = ? ORDER BY ts_ms")) {
            q.bind(1, id).bind(2, paramId(QStringLiteral("SPEED")));
            bool have = false;
            qint64 lastTs = 0;
            double lastV = 0.0;
            while (q.step()) {
                const qint64 ts = q.int64(0);
                const double v = std::max(0.0, q.dbl(1));
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
        Query q(m_db);
        if (q.prepare("SELECT ts_ms, kind, detail FROM events WHERE trip_id = ? ORDER BY ts_ms")) {
            q.bind(1, id);
            while (q.step()) {
                QVariantMap e;
                e[QStringLiteral("t")] = static_cast<double>(q.int64(0) - startMs) / 1000.0;
                e[QStringLiteral("kind")] = q.text(1);
                e[QStringLiteral("detail")] = q.text(2);
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
        Query q(m_db);
        if (q.prepare("SELECT s.ts_ms, p.name, s.value FROM samples s JOIN params p ON p.id = s.param_id "
                      "WHERE s.trip_id = ? ORDER BY s.ts_ms, p.name")) {
            q.bind(1, id);
            while (q.step()) {
                const qint64 ts = q.int64(0);
                out << ts << ','
                    << QDateTime::fromMSecsSinceEpoch(ts).toString(Qt::ISODateWithMs) << ','
                    << QString::number(static_cast<double>(ts - startMs) / 1000.0, 'f', 3) << ','
                    << q.text(1) << ','
                    << QString::number(q.dbl(2), 'g', 7) << '\n';
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
    QList<qint64> ids;
    {
        Query q(m_db);
        if (q.prepare("SELECT id FROM trips ORDER BY start_ms DESC")) {
            while (q.step())
                ids.append(q.int64(0));
        }
    }
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
        qint64 endMs = row.value(QStringLiteral("startMs")).toLongLong();
        Query last(m_db);
        if (last.prepare("SELECT MAX(ts_ms) FROM samples WHERE trip_id = ?")) {
            last.bind(1, id);
            if (last.step() && !last.isNull(0))
                endMs = last.int64(0);
        }
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
    const int pid = paramId(param);
    if (!row.value(QStringLiteral("endMs")).isValid()) {
        Query last(m_db);
        if (last.prepare("SELECT MAX(ts_ms) FROM samples WHERE trip_id = ? AND param_id = ?")) {
            last.bind(1, id).bind(2, pid);
            if (last.step())
                endMs = last.int64(0);
        }
    }
    maxPoints = std::max(2, maxPoints);
    const qint64 bucketMs = std::max<qint64>(1, (endMs - startMs) / maxPoints + 1);
    Query q(m_db);
    if (q.prepare("SELECT ((ts_ms - ?) / ?) AS b, MIN(ts_ms), AVG(value), MIN(value), MAX(value) "
                  "FROM samples WHERE trip_id = ? AND param_id = ? GROUP BY b ORDER BY b")) {
        q.bind(1, startMs).bind(2, bucketMs).bind(3, id).bind(4, pid);
        while (q.step()) {
            QVariantMap p;
            p[QStringLiteral("t")] = static_cast<double>(q.int64(1) - startMs) / 1000.0;
            p[QStringLiteral("v")] = q.dbl(2);
            p[QStringLiteral("lo")] = q.dbl(3);
            p[QStringLiteral("hi")] = q.dbl(4);
            points.append(p);
        }
    }
    emit seriesReady(id, param, points);
    return points;
}

qint64 TripStore::sampleCount(qint64 id)
{
    if (!isOpen())
        return 0;
    Query q(m_db);
    if (!q.prepare("SELECT COUNT(*) FROM samples WHERE trip_id = ?"))
        return 0;
    q.bind(1, id);
    return q.step() ? q.int64(0) : 0;
}

// ---------------------------------------------------------------------------
// Housekeeping
// ---------------------------------------------------------------------------

bool TripStore::deleteTrip(qint64 id)
{
    if (!isOpen())
        return false;
    const QString base = tripFileBase(id);
    exec(QStringLiteral("BEGIN"));
    for (const char *sql : {"DELETE FROM samples WHERE trip_id = ?", "DELETE FROM events WHERE trip_id = ?",
                            "DELETE FROM trips WHERE id = ?"}) {
        Query q(m_db);
        if (q.prepare(sql))
            q.bind(1, id).exec();
    }
    const bool ok = exec(QStringLiteral("COMMIT"));
    if (!ok)
        exec(QStringLiteral("ROLLBACK"));
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
    {
        Query q(m_db);
        if (q.prepare("SELECT file_base FROM trips WHERE end_ms IS NOT NULL")) {
            while (q.step())
                bases.append(q.text(0));
        }
    }
    exec(QStringLiteral("BEGIN"));
    // Keep the trip being recorded (end_ms NULL) and its samples
    exec(QStringLiteral("DELETE FROM samples WHERE trip_id IN (SELECT id FROM trips WHERE end_ms IS NOT NULL)"));
    exec(QStringLiteral("DELETE FROM events WHERE trip_id IN (SELECT id FROM trips WHERE end_ms IS NOT NULL)"));
    exec(QStringLiteral("DELETE FROM trips WHERE end_ms IS NOT NULL"));
    if (!exec(QStringLiteral("COMMIT")))
        exec(QStringLiteral("ROLLBACK"));
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
        qint64 id = 0;
        bool found = false;
        {
            Query q(m_db);
            if (q.prepare("SELECT id FROM trips WHERE end_ms IS NOT NULL ORDER BY start_ms ASC LIMIT 1") && q.step()) {
                id = q.int64(0);
                found = true;
            }
        }
        if (!found)
            break;
        const QString base = tripFileBase(id);
        exec(QStringLiteral("BEGIN"));
        for (const char *sql : {"DELETE FROM samples WHERE trip_id = ?", "DELETE FROM events WHERE trip_id = ?",
                                "DELETE FROM trips WHERE id = ?"}) {
            Query d(m_db);
            if (d.prepare(sql))
                d.bind(1, id).exec();
        }
        if (!exec(QStringLiteral("COMMIT")))
            exec(QStringLiteral("ROLLBACK"));
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
