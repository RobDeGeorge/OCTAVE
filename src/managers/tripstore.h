#ifndef TRIPSTORE_H
#define TRIPSTORE_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QHash>

// One OBD reading bound for the black box.
struct TripSample {
    qint64 tripId = 0;
    qint64 tsMs = 0;       // wall-clock ms (trip start + monotonic elapsed)
    QString paramId;       // OBDParameterModel id ("RPM", "COOLANT_TEMP", ...)
    float value = 0.0f;
};

// ===========================================================================
// TripStore -- the black box on disk.
//
// One SQLite database (<dir>/blackbox.sqlite, WAL) holds every sample of
// every trip, keyed by trip, parameter and time, plus the events around them
// (connection drops, MIL, trouble codes). When a trip ends it is summarised
// (peaks, distance, per-parameter stats) and written out as a pair of files
// a person can grab from the file system:
//
//   <dir>/trips/2026-10-21_08-12-05.csv    every sample, long format
//   <dir>/trips/2026-10-21_08-12-05.json   summary + events
//
// Everything here runs on the thread the object lives in; TripRecorder moves
// a store to a worker thread and talks to it with queued invokeMethod calls,
// the tests drive one directly. Results that QML needs come back as signals.
// ===========================================================================
class TripStore : public QObject
{
    Q_OBJECT

public:
    explicit TripStore(QObject *parent = nullptr);
    ~TripStore() override;

    // Open (creating) <dir>/blackbox.sqlite and <dir>/trips/. False (and
    // error()) when the database can't be opened; every other call is then
    // a no-op.
    bool open(const QString &dir);
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    QString dir() const { return m_dir; }
    QString tripsDir() const { return m_dir + QStringLiteral("/trips"); }
    QString databasePath() const { return m_dir + QStringLiteral("/blackbox.sqlite"); }

    // --- recording ---
    void beginTrip(qint64 id, qint64 startMs, const QVariantMap &meta);   // meta: vin, make, modelYear
    void updateTripMeta(qint64 id, const QVariantMap &meta);
    void append(const QVector<TripSample> &samples);                       // one transaction
    void appendEvent(qint64 tripId, qint64 tsMs, const QString &kind, const QString &detail);
    // Close a trip: summarise it, write its files, emit tripFinalized() and
    // tripsChanged(). `clean` is false for a trip recovered after a power cut.
    QVariantMap endTrip(qint64 id, qint64 endMs, bool clean);
    // Trips left open by a crash / power cut get their last sample time as
    // the end and are finalised (clean = false). Returns how many.
    int recoverOpenTrips();

    // --- reading ---
    // Newest first. Each item is the summary (without the per-parameter and
    // event lists) plus `open` for the trip being recorded. Emits tripsChanged().
    QVariantList listTrips();
    // Full summary including `params` ([{id, count, min, max, avg, first, last}])
    // and `events` ([{t, kind, detail}]); emits detailReady().
    QVariantMap tripDetail(qint64 id);
    // Downsampled series of one parameter: [{t (s since start), v (avg), lo, hi}],
    // at most `maxPoints` buckets. Emits seriesReady().
    QVariantList series(qint64 id, const QString &paramId, int maxPoints);

    // --- housekeeping ---
    bool deleteTrip(qint64 id);               // rows + files
    void clearAll();
    // Delete the oldest closed trips until the database and the trip files
    // fit in maxBytes (never the trip being recorded). Returns bytes in use.
    qint64 prune(qint64 maxBytes);
    qint64 storageBytes() const;              // database (+ WAL) + trip files
    // Copy a closed trip's CSV + JSON into destDir (created). Returns the
    // CSV path, "" on failure; emits exportFinished().
    QString exportTrip(qint64 id, const QString &destDir);
    QString tripFileBase(qint64 id);          // "<tripsDir>/<stamp>" or ""

    // Number of stored samples for a trip (tests, status)
    qint64 sampleCount(qint64 id);

signals:
    void tripsChanged(const QVariantList &trips);
    void tripFinalized(qint64 id, const QVariantMap &summary);
    void detailReady(qint64 id, const QVariantMap &detail);
    void seriesReady(qint64 id, const QString &paramId, const QVariantList &points);
    void exportFinished(qint64 id, const QString &path);
    void storageChanged(qint64 bytes);
    void error(const QString &message);

private:
    bool exec(const QString &sql);
    bool createSchema();
    int paramId(const QString &name);          // interned, created on first use
    QHash<int, QString> paramNames();
    QVariantMap summarise(qint64 id, qint64 startMs, qint64 endMs, bool clean, const QVariantMap &meta);
    QVariantMap rowSummary(const QVariantMap &row);   // trips row -> list item
    QVariantMap tripRow(qint64 id);
    bool writeTripFiles(qint64 id, qint64 startMs, const QVariantMap &summary, QString *base);
    static QString stampFor(qint64 startMs);
    void removeTripFiles(const QString &base);
    void emitStorage();

    QString m_dir;
    QString m_connectionName;
    QSqlDatabase m_db;
    QHash<QString, int> m_paramIds;
};

#endif // TRIPSTORE_H
