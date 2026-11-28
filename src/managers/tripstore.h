#ifndef TRIPSTORE_H
#define TRIPSTORE_H

#include <QObject>
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
// One SQLite database (<dir>/blackbox.sqlite, WAL; SQLite is compiled in from
// third_party/sqlite, so no Qt Sql driver plugin is needed) holds every sample of
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
    bool isOpen() const { return m_db != nullptr; }
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
    // `partial`: a drive still being recorded on another OCTAVE, of which only
    // the first part has arrived (importTrip); its summary says so.
    QVariantMap endTrip(qint64 id, qint64 endMs, bool clean, bool partial = false);
    // Trips left open by a crash / power cut get their last sample time as
    // the end and are finalised (clean = false, or true when a trip_end event
    // was logged: the drive ended, only the summary was lost). Returns how many.
    int recoverOpenTrips();

    // --- reading ---
    // Newest first. Each item is the summary (without the per-parameter and
    // event lists) plus `open` for the trip being recorded. Emits tripsChanged().
    QVariantList listTrips();
    // Full summary including `params` ([{id, count, min, max, avg, first, last}])
    // and `events` ([{t, kind, detail}]); emits detailReady().
    QVariantMap tripDetail(qint64 id);
    // Downsampled series of one parameter: [{t (s since start), v (avg), lo, hi}],
    // at most `maxPoints` buckets over the whole trip, or over the window
    // [fromS, toS] (seconds since start; negative = unbounded) so a zoomed
    // chart gets full detail. Emits seriesReady().
    QVariantList series(qint64 id, const QString &paramId, int maxPoints, double fromS = -1, double toS = -1);

    // --- health across drives ---
    // Every closed trip's summary carries `health`: a few numbers worked out
    // once at endTrip() from its samples (warm-up time, running coolant, idle
    // and charging voltage, warm idle RPM, long-term fuel trims; definitions
    // in kHealthMetrics, tripstore.cpp). Trips summarised before a metric
    // existed are filled in on the first trend query that reaches them.
    //
    // healthTrends: the closed trips that started in the last `days` days
    // (<= 0: all of them) before `nowMs`, oldest first, as
    //   { days, fromMs, toMs, tripCount,
    //     metrics: [{ id, title, unit, decimals, bandLo, bandHi (absent: no
    //                 normal band), description, rows: [{ id, startMs, v }],
    //                 latest, recent, baseline, status ("ok" | "watch" |
    //                 "none"), note }],
    //     params:  [{ id, title, unit, trips }] (every parameter recorded in
    //               the window, most trips first) }
    // Emits healthTrendsReady().
    QVariantMap healthTrends(int days, qint64 nowMs);
    // One parameter across the same window: [{ id, startMs, v (avg), lo
    // (min), hi (max) }] per trip that recorded it, oldest first, from the
    // stored summaries (no sample scan). Emits paramTrendReady().
    QVariantList paramTrend(const QString &paramId, int days, qint64 nowMs);
    // Per-trip health numbers of trip `id` from its samples (tests, summarise)
    QVariantMap computeHealth(qint64 id, qint64 startMs);

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

    // --- drives from another OCTAVE ---
    // A drive's CSV + JSON pair, as endTrip() writes it, is the whole drive:
    // importTrip() reads one pair (the JSON's path; its CSV beside it) into
    // this database and finishes it like a drive recorded here, so it gets
    // its own summary and its own pair under trips/. A drive already here
    // (same id) is left alone, unless what is here is only a part of it
    // (writeSnapshot); one marked simulated is refused.
    enum class ImportResult { Imported, AlreadyHere, Invalid };
    ImportResult importTrip(const QString &jsonPath);
    // The drive being recorded, for an OCTAVE that should have it before it
    // ends (the car loses power with the ignition): the samples newer than
    // `afterTs` as <base>.csv and the summary so far, marked `partial`, as
    // <base>.json. Returns the time of the last sample written, 0 when
    // there was nothing new (and no files). The receiver's importTrip()
    // adds each part to the ones before; the whole drive replaces them.
    qint64 writeSnapshot(qint64 id, qint64 afterTs, const QString &base);
    // Where pairs wait to be taken in: <dir>/inbox
    QString inboxDir() const { return m_dir + QStringLiteral("/inbox"); }
    // One more folder importInbox() empties (the phone's adb-reachable one)
    void addInboxDir(const QString &dir);
    // Import every pair in the inbox. A pair that was taken in, or was
    // already here, is deleted from it; one that could not be read stays,
    // renamed *.rejected, so it is not tried again. Emits importFinished().
    void importInbox();

    // Number of stored samples for a trip (tests, status)
    qint64 sampleCount(qint64 id);

signals:
    // Result of open(): ok, and the error text when not
    void storeOpened(bool ok, const QString &error);
    void tripsChanged(const QVariantList &trips);
    void tripFinalized(qint64 id, const QVariantMap &summary);
    void detailReady(qint64 id, const QVariantMap &detail);
    void seriesReady(qint64 id, const QString &paramId, const QVariantList &points);
    void exportFinished(qint64 id, const QString &path);
    void importFinished(int imported, int alreadyHere, int rejected);
    void healthTrendsReady(int days, const QVariantMap &result);
    void paramTrendReady(const QString &paramId, int days, const QVariantList &rows);
    void storageChanged(qint64 bytes);
    void error(const QString &message);

private:
    bool exec(const QString &sql);
    bool createSchema();
    bool setDamagedDatabaseAside();
    bool checkSchemaVersion();
    int paramId(const QString &name);          // interned, created on first use
    QHash<int, QString> paramNames();
    QVariantMap summarise(qint64 id, qint64 startMs, qint64 endMs, bool clean, const QVariantMap &meta);
    QVariantMap rowSummary(const QVariantMap &row);   // trips row -> list item
    QVariantMap tripRow(qint64 id);
    // Summaries of the closed trips in the window, oldest first, health filled in
    QList<QVariantMap> closedSummaries(int days, qint64 nowMs);
    bool writeTripFiles(qint64 id, qint64 startMs, const QVariantMap &summary, QString *base);
    bool writeCsv(const QString &path, qint64 id, qint64 startMs, qint64 afterTs, qint64 *lastTs);
    static QString stampFor(qint64 startMs);
    void removeTripFiles(const QString &base);
    void emitStorage();

    QString m_dir;
    struct sqlite3 *m_db = nullptr;
    QHash<QString, int> m_paramIds;
    QStringList m_extraInboxes;
};

#endif // TRIPSTORE_H
