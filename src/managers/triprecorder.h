#ifndef TRIPRECORDER_H
#define TRIPRECORDER_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

#include "tripstore.h"

class OBDManager;
class QThread;
class SettingsManager;

// ===========================================================================
// TripRecorder -- the vehicle's black box.
//
// Taps every decoded OBD value (OBDManager::parameterSampled) and records it
// with its time into the TripStore database, one trip per drive. On top of
// what the pages on screen poll, it asks OBDManager to sweep every PID the
// vehicle supports in the background (setBackgroundDemand), so the record is
// complete even on the home screen. When the drive ends the trip is
// summarised and written out as CSV + JSON files under <config>/history/trips.
//
// A trip starts on the first RPM > 0 reading (any reading, when the car has
// no RPM PID) and ends when the engine has been off for engineOffTimeoutMs,
// the adapter has been gone for reconnectGraceMs (a short dropout does not
// split a trip), or the app shuts down. A trip left open by a power cut is
// closed on the next start with its last sample time.
//
// Samples are coalesced in memory and written in one transaction every
// flushIntervalMs on the store's thread; a value that has not changed is
// stored again only every heartbeatMs, so a 3 MB/hour drive stays small
// without losing the fact that the value held.
//
// Lives on the GUI thread; the store lives on its own thread.
// ===========================================================================
class TripRecorder : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    // The database opened; while false nothing is recorded and lastError says why
    Q_PROPERTY(bool storeReady READ storeReady NOTIFY storeReadyChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(qint64 currentTripId READ currentTripId NOTIFY recordingChanged)
    Q_PROPERTY(qint64 currentTripStartMs READ currentTripStartMs NOTIFY recordingChanged)
    Q_PROPERTY(int currentSampleCount READ currentSampleCount NOTIFY currentSampleCountChanged)
    // Seconds since the trip being recorded started (0 when none); moves once a second
    Q_PROPERTY(int currentTripElapsedS READ currentTripElapsedS NOTIFY currentTripElapsedSChanged)
    // Trip summaries, newest first (see TripStore::listTrips)
    Q_PROPERTY(QVariantList trips READ trips NOTIFY tripsChanged)
    Q_PROPERTY(QString historyDir READ historyDir NOTIFY historyDirChanged)
    Q_PROPERTY(QString tripsDir READ tripsDir NOTIFY historyDirChanged)
    Q_PROPERTY(qint64 storageBytes READ storageBytes NOTIFY storageBytesChanged)
    Q_PROPERTY(bool canOpenFolder READ canOpenFolder CONSTANT)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY lastExportPathChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit TripRecorder(SettingsManager *settings = nullptr, QObject *parent = nullptr);
    ~TripRecorder() override;

    // Where the database and trip files go; default <app data>/history.
    // Call before start().
    void setHistoryDir(const QString &dir);
    // Open the store on its thread, recover an unfinished trip, list the
    // history and apply the size cap.
    void start();
    // Wire the OBD manager's samples, connection state, vehicle identity and
    // diagnostic events into the recorder, and register the background sweep.
    void attach(OBDManager *obd);
    // End the trip being recorded, write it out and stop the store thread.
    // Blocks (bounded) so the files exist before the process exits.
    void shutdown();

    bool enabled() const { return m_enabled; }
    bool storeReady() const { return m_storeReady; }
    bool recording() const { return m_tripOpen; }
    qint64 currentTripId() const { return m_tripOpen ? m_tripId : 0; }
    qint64 currentTripStartMs() const { return m_tripOpen ? m_tripStartMs : 0; }
    int currentSampleCount() const { return m_sampleCount; }
    int currentTripElapsedS() const { return m_elapsedS; }
    QVariantList trips() const { return m_trips; }
    QString historyDir() const { return m_historyDir; }
    QString tripsDir() const { return m_historyDir + QStringLiteral("/trips"); }
    qint64 storageBytes() const { return m_storageBytes; }
    bool canOpenFolder() const;
    QString lastExportPath() const { return m_lastExportPath; }
    QString lastError() const { return m_lastError; }

    // --- tunables (tests shorten them) ---
    void setEngineOffTimeoutMs(int ms) { m_engineOffTimeoutMs = ms; }
    void setReconnectGraceMs(int ms) { m_reconnectGraceMs = ms; }
    void setFlushIntervalMs(int ms);
    void setHeartbeatMs(int ms) { m_heartbeatMs = ms; }
    // Replace the wall clock (ms since epoch). With a clock installed sample
    // times come straight from it instead of start + monotonic elapsed.
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }
    // Least time between two liveTripChanged signals (default 5 s)
    void setLiveRefreshMs(int ms) { m_liveRefreshMs = ms; }
    // Run the trip-boundary check now (the 1 s timer does this normally)
    void tick();
    // Push buffered samples to the store now
    void flush();

public slots:
    // One decoded reading. Starts a trip when the rules say so, otherwise
    // dropped until one is open.
    void recordSample(const QString &paramId, float value);
    void setConnected(bool connected);
    // The vehicle scan result: decides whether RPM gates the trip start and
    // is the background sweep set when blackBoxSweepAll is on.
    void setVehicleParameters(const QStringList &ids);
    void setVehicleIdentity(const QString &vin, const QString &make, int modelYear);
    // Something worth a line in the trip's event log (MIL, DTCs, link drops)
    void noteEvent(const QString &kind, const QString &detail);
    void setEnabled(bool enabled);

    // --- QML API ---
    Q_INVOKABLE void endTripNow();                 // split here; a new trip starts on the next RPM > 0
    Q_INVOKABLE void refresh();                    // re-list trips and storage
    Q_INVOKABLE void requestDetail(qint64 id);     // -> detailReady
    // -> seriesReady; fromS / toS (seconds since trip start, negative = whole trip) window the query
    Q_INVOKABLE void requestSeries(qint64 id, const QString &paramId, int maxPoints = 400,
                                   double fromS = -1, double toS = -1);
    Q_INVOKABLE void deleteTrip(qint64 id);
    Q_INVOKABLE void clearHistory();               // everything but the trip being recorded
    Q_INVOKABLE void exportTrip(qint64 id);        // copy CSV + JSON to Downloads -> exportFinished
    Q_INVOKABLE bool openHistoryFolder();          // desktop only

signals:
    void enabledChanged(bool enabled);
    void storeReadyChanged();
    void recordingChanged();
    void currentSampleCountChanged();
    void currentTripElapsedSChanged();
    // New samples of the trip being recorded have reached the store, at most
    // once per setLiveRefreshMs: a view showing that trip re-requests its
    // detail and series (the requests queue behind the write)
    void liveTripChanged(qint64 id);
    void tripsChanged();
    void historyDirChanged();
    void storageBytesChanged();
    void lastExportPathChanged();
    void lastErrorChanged();
    void tripStarted(qint64 id);
    void tripEnded(qint64 id, const QVariantMap &summary);
    void detailReady(qint64 id, const QVariantMap &detail);
    void seriesReady(qint64 id, const QString &paramId, const QVariantList &points);
    void exportFinished(qint64 id, const QString &path);

private:
    qint64 nowMs() const;
    qint64 sampleTimeMs();
    void openTrip(qint64 nowMs);
    void closeTrip(const QString &reason, bool blocking = false);
    void applyBackgroundDemand();
    void prune();
    template <typename F> void onStore(F &&fn);
    template <typename F> void onStoreBlocking(F &&fn);

    SettingsManager *m_settings = nullptr;
    OBDManager *m_obd = nullptr;
    QThread *m_thread = nullptr;
    TripStore *m_store = nullptr;
    QString m_historyDir;
    bool m_started = false;
    bool m_shutDown = false;
    bool m_storeReady = false;

    // Settings mirror
    bool m_enabled = true;
    bool m_sweepAll = true;
    qint64 m_maxBytes = 500LL * 1024 * 1024;

    // Trip state
    bool m_tripOpen = false;
    qint64 m_tripId = 0;
    qint64 m_tripStartMs = 0;
    QElapsedTimer m_tripElapsed;
    int m_sampleCount = 0;
    bool m_connected = false;
    qint64 m_disconnectedAtMs = 0;
    qint64 m_lastSampleMs = 0;      // wall clock of the last reading
    qint64 m_lastSampleTs = 0;      // its stored timestamp
    qint64 m_rpmZeroSinceMs = 0;    // 0 while the engine runs
    bool m_rpmExpected = true;      // the car has an RPM PID (or we don't know yet)
    QStringList m_vehicleParams;
    QVariantMap m_identity;

    // Coalescing
    struct Last { bool valid = false; float value = 0.0f; qint64 tsMs = 0; };
    QHash<QString, Last> m_lastStored;
    QVector<TripSample> m_buffer;
    QTimer m_flushTimer;
    QTimer m_tickTimer;

    // Tunables
    int m_engineOffTimeoutMs = 120000;
    int m_reconnectGraceMs = 120000;
    int m_heartbeatMs = 5000;
    int m_liveRefreshMs = 5000;
    qint64 m_lastLiveRefreshMs = 0;
    int m_elapsedS = 0;
    void updateElapsed();
    std::function<qint64()> m_clock;

    // Mirrors of store results for QML
    QVariantList m_trips;
    qint64 m_storageBytes = 0;
    QString m_lastExportPath;
    QString m_lastError;
};

#endif // TRIPRECORDER_H
