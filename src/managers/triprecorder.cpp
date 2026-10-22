#include "triprecorder.h"

#include "obdmanager.h"
#include "settingsmanager.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <algorithm>

Q_LOGGING_CATEGORY(lcTrip, "octave.triprecorder")

namespace {
const int kFlushIntervalMs = 2000;
const int kFlushBatchSize = 1000;
const int kTickMs = 1000;
const QString kRpmId = QStringLiteral("RPM");
}

TripRecorder::TripRecorder(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_historyDir(SettingsManager::getAppDataDir() + QStringLiteral("/history"))
{
    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &TripRecorder::flush);
    m_tickTimer.setInterval(kTickMs);
    connect(&m_tickTimer, &QTimer::timeout, this, &TripRecorder::tick);

    if (m_settings) {
        m_enabled = m_settings->blackBoxEnabled();
        m_sweepAll = m_settings->blackBoxSweepAll();
        m_maxBytes = static_cast<qint64>(m_settings->blackBoxMaxMB()) * 1024 * 1024;
        connect(m_settings, &SettingsManager::blackBoxEnabledChanged, this, &TripRecorder::setEnabled);
        connect(m_settings, &SettingsManager::blackBoxSweepAllChanged, this, [this](bool on) {
            m_sweepAll = on;
            applyBackgroundDemand();
        });
        connect(m_settings, &SettingsManager::blackBoxMaxMBChanged, this, [this](int mb) {
            m_maxBytes = static_cast<qint64>(mb) * 1024 * 1024;
            prune();
        });
    }
}

TripRecorder::~TripRecorder()
{
    shutdown();
}

void TripRecorder::setHistoryDir(const QString &dir)
{
    if (m_started) {
        qCWarning(lcTrip) << "setHistoryDir() after start() ignored";
        return;
    }
    m_historyDir = dir;
    emit historyDirChanged();
}

void TripRecorder::setFlushIntervalMs(int ms)
{
    m_flushTimer.setInterval(ms);
}

bool TripRecorder::canOpenFolder() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return false;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Store thread plumbing
// ---------------------------------------------------------------------------

template <typename F> void TripRecorder::onStore(F &&fn)
{
    if (!m_store)
        return;
    TripStore *store = m_store;
    QMetaObject::invokeMethod(store, [store, fn = std::forward<F>(fn)]() { fn(store); }, Qt::QueuedConnection);
}

template <typename F> void TripRecorder::onStoreBlocking(F &&fn)
{
    if (!m_store)
        return;
    TripStore *store = m_store;
    if (QThread::currentThread() == store->thread()) {
        fn(store);
        return;
    }
    QMetaObject::invokeMethod(store, [store, fn = std::forward<F>(fn)]() { fn(store); }, Qt::BlockingQueuedConnection);
}

void TripRecorder::start()
{
    if (m_started)
        return;
    m_started = true;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("trip-store"));
    m_store = new TripStore();
    m_store->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_store, &QObject::deleteLater);

    connect(m_store, &TripStore::storeOpened, this, [this](bool ok, const QString &err) {
        if (!ok) {
            m_lastError = err;
            emit lastErrorChanged();
            qCWarning(lcTrip) << "Black box unavailable, nothing will be recorded:" << err;
        }
        if (ok != m_storeReady) {
            m_storeReady = ok;
            emit storeReadyChanged();
        }
    });
    connect(m_store, &TripStore::tripsChanged, this, [this](const QVariantList &trips) {
        m_trips = trips;
        emit tripsChanged();
    });
    connect(m_store, &TripStore::tripFinalized, this, [this](qint64 id, const QVariantMap &summary) {
        emit tripEnded(id, summary);
    });
    connect(m_store, &TripStore::storageChanged, this, [this](qint64 bytes) {
        if (bytes == m_storageBytes)
            return;
        m_storageBytes = bytes;
        emit storageBytesChanged();
    });
    connect(m_store, &TripStore::detailReady, this, &TripRecorder::detailReady);
    connect(m_store, &TripStore::seriesReady, this, &TripRecorder::seriesReady);
    connect(m_store, &TripStore::exportFinished, this, [this](qint64 id, const QString &path) {
        if (!path.isEmpty()) {
            m_lastExportPath = path;
            emit lastExportPathChanged();
        }
        emit exportFinished(id, path);
    });
    connect(m_store, &TripStore::error, this, [this](const QString &message) {
        m_lastError = message;
        emit lastErrorChanged();
    });

    m_thread->start();
    const QString dir = m_historyDir;
    const qint64 maxBytes = m_maxBytes;
    onStore([dir, maxBytes](TripStore *store) {
        if (!store->open(dir))
            return;
        store->recoverOpenTrips();
        store->listTrips();
        store->prune(maxBytes);
    });
    m_tickTimer.start();
    qCInfo(lcTrip) << "Black box" << (m_enabled ? "enabled" : "disabled") << "- history in" << dir;
}

void TripRecorder::attach(OBDManager *obd)
{
    if (!obd || m_obd)
        return;
    m_obd = obd;
    connect(obd, &OBDManager::parameterSampled, this, &TripRecorder::recordSample);
    connect(obd, &OBDManager::connectionStatusChanged, this, [this, obd](const QString &) {
        setConnected(obd->isConnected());
    });
    connect(obd, &OBDManager::connectionLostUnexpectedly, this, [this](const QString &reason) {
        noteEvent(QStringLiteral("connection_lost"), reason);
    });
    connect(obd, &OBDManager::vehicleSupportedParametersChanged, this, [this, obd]() {
        setVehicleParameters(obd->vehicleSupportedParameters());
    });
    connect(obd, &OBDManager::vinChanged, this, [this, obd](const QString &vin) {
        setVehicleIdentity(vin, obd->vehicleMake(), obd->vehicleModelYear());
    });
    connect(obd, &OBDManager::milStatusChanged, this, [this](bool on) {
        noteEvent(QStringLiteral("mil"), on ? QStringLiteral("on") : QStringLiteral("off"));
    });
    connect(obd, &OBDManager::dtcCodesChanged, this, [this](const QVariantList &codes) {
        QStringList names;
        for (const QVariant &c : codes)
            names << c.toMap().value(QStringLiteral("code")).toString();
        noteEvent(QStringLiteral("dtc"), names.isEmpty() ? QStringLiteral("none") : names.join(QLatin1Char(' ')));
    });
    connect(obd, &OBDManager::dtcClearResult, this, [this](bool success, const QString &message) {
        noteEvent(QStringLiteral("dtc_clear"), success ? QStringLiteral("ok") : message);
    });
    setConnected(obd->isConnected());
    setVehicleParameters(obd->vehicleSupportedParameters());
    if (!obd->vin().isEmpty())
        setVehicleIdentity(obd->vin(), obd->vehicleMake(), obd->vehicleModelYear());
    applyBackgroundDemand();
}

void TripRecorder::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    m_tickTimer.stop();
    m_flushTimer.stop();
    if (m_tripOpen)
        closeTrip(QStringLiteral("shutdown"), true);
    else
        flush();
    if (m_thread) {
        onStoreBlocking([](TripStore *store) { store->close(); });
        m_thread->quit();
        if (!m_thread->wait(10000))
            qCWarning(lcTrip) << "store thread did not stop in time";
        m_store = nullptr;
        m_thread = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

qint64 TripRecorder::nowMs() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

qint64 TripRecorder::sampleTimeMs()
{
    // Within a trip, time is start + monotonic elapsed: an NTP step on the
    // head unit mid-drive must not fold or stretch the trace. Tests install a
    // clock and get it verbatim.
    if (m_clock)
        return m_clock();
    return m_tripStartMs + m_tripElapsed.elapsed();
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void TripRecorder::recordSample(const QString &paramId, float value)
{
    if (!m_enabled || !m_store || !m_storeReady)
        return;   // no database: never pretend to record
    const qint64 now = nowMs();
    m_lastSampleMs = now;

    if (paramId == kRpmId) {
        if (value > 0.0f)
            m_rpmZeroSinceMs = 0;
        else if (m_rpmZeroSinceMs == 0)
            m_rpmZeroSinceMs = now;
    }

    if (!m_tripOpen) {
        bool start = false;
        if (paramId == kRpmId)
            start = value > 0.0f;
        else if (!m_rpmExpected)
            start = true;
        if (!start)
            return;
        openTrip(now);
    }

    const qint64 ts = sampleTimeMs();
    m_lastSampleTs = ts;
    Last &last = m_lastStored[paramId];
    if (last.valid && last.value == value && ts - last.tsMs < m_heartbeatMs)
        return;   // unchanged: a heartbeat every few seconds is enough
    last.valid = true;
    last.value = value;
    last.tsMs = ts;
    m_buffer.append({m_tripId, ts, paramId, value});
    ++m_sampleCount;
    if (m_buffer.size() >= kFlushBatchSize)
        flush();
}

void TripRecorder::openTrip(qint64 now)
{
    m_tripOpen = true;
    m_tripId = now;
    m_tripStartMs = now;
    m_tripElapsed.start();
    m_sampleCount = 0;
    m_lastStored.clear();
    m_buffer.clear();
    m_rpmZeroSinceMs = 0;
    const qint64 id = m_tripId;
    const QVariantMap meta = m_identity;
    onStore([id, now, meta](TripStore *store) { store->beginTrip(id, now, meta); });
    m_flushTimer.start();
    qCInfo(lcTrip) << "Trip" << id << "started";
    m_lastLiveRefreshMs = now;
    emit recordingChanged();
    emit currentSampleCountChanged();
    updateElapsed();
    emit tripStarted(id);
}

void TripRecorder::closeTrip(const QString &reason, bool blocking)
{
    if (!m_tripOpen)
        return;
    const qint64 id = m_tripId;
    const qint64 endTs = m_lastSampleTs > 0 ? m_lastSampleTs : m_tripStartMs;
    QVector<TripSample> batch;
    batch.swap(m_buffer);
    m_flushTimer.stop();
    m_tripOpen = false;
    const int count = m_sampleCount;
    m_lastStored.clear();
    qCInfo(lcTrip) << "Trip" << id << "ending (" << reason << "):" << count << "samples";
    auto finish = [id, endTs, batch, reason](TripStore *store) {
        store->append(batch);
        store->appendEvent(id, endTs, QStringLiteral("trip_end"), reason);
        store->endTrip(id, endTs, true);
    };
    if (blocking)
        onStoreBlocking(finish);
    else
        onStore(finish);
    emit recordingChanged();
    emit currentSampleCountChanged();
    updateElapsed();
    prune();
}

void TripRecorder::flush()
{
    if (m_buffer.isEmpty()) {
        return;
    }
    QVector<TripSample> batch;
    batch.swap(m_buffer);
    onStore([batch](TripStore *store) { store->append(batch); });
    emit currentSampleCountChanged();
    if (m_tripOpen && nowMs() - m_lastLiveRefreshMs >= m_liveRefreshMs) {
        m_lastLiveRefreshMs = nowMs();
        emit liveTripChanged(m_tripId);
    }
}

void TripRecorder::updateElapsed()
{
    const int s = m_tripOpen ? int(std::max<qint64>(0, nowMs() - m_tripStartMs) / 1000) : 0;
    if (s == m_elapsedS)
        return;
    m_elapsedS = s;
    emit currentTripElapsedSChanged();
}

void TripRecorder::tick()
{
    if (!m_tripOpen)
        return;
    updateElapsed();
    const qint64 now = nowMs();
    if (!m_connected) {
        if (now - std::max(m_disconnectedAtMs, m_lastSampleMs) >= m_reconnectGraceMs)
            closeTrip(QStringLiteral("connection lost"));
        return;
    }
    if (m_rpmExpected && m_rpmZeroSinceMs > 0 && now - m_rpmZeroSinceMs >= m_engineOffTimeoutMs) {
        closeTrip(QStringLiteral("engine off"));
        return;
    }
    if (now - m_lastSampleMs >= m_reconnectGraceMs)
        closeTrip(QStringLiteral("no data"));
}

void TripRecorder::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    const qint64 now = nowMs();
    if (!connected) {
        m_disconnectedAtMs = now;
        if (m_tripOpen)
            noteEvent(QStringLiteral("disconnected"), QString());
    } else if (m_tripOpen) {
        noteEvent(QStringLiteral("reconnected"), QString());
    }
}

void TripRecorder::setVehicleParameters(const QStringList &ids)
{
    m_vehicleParams = ids;
    // Until the scan says otherwise, assume RPM exists (every car we have met
    // has it); a scan result without RPM lets any reading start the trip.
    m_rpmExpected = ids.isEmpty() || ids.contains(kRpmId);
    applyBackgroundDemand();
}

void TripRecorder::setVehicleIdentity(const QString &vin, const QString &make, int modelYear)
{
    m_identity[QStringLiteral("vin")] = vin;
    m_identity[QStringLiteral("make")] = make;
    m_identity[QStringLiteral("modelYear")] = modelYear;
    if (m_tripOpen) {
        const qint64 id = m_tripId;
        const QVariantMap meta = m_identity;
        onStore([id, meta](TripStore *store) { store->updateTripMeta(id, meta); });
    }
}

void TripRecorder::noteEvent(const QString &kind, const QString &detail)
{
    if (!m_tripOpen)
        return;
    const qint64 id = m_tripId;
    const qint64 ts = sampleTimeMs();
    onStore([id, ts, kind, detail](TripStore *store) { store->appendEvent(id, ts, kind, detail); });
}

void TripRecorder::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    if (!enabled && m_tripOpen)
        closeTrip(QStringLiteral("recording disabled"));
    applyBackgroundDemand();
    emit enabledChanged(enabled);
}

void TripRecorder::applyBackgroundDemand()
{
    if (!m_obd)
        return;
    const bool sweep = m_enabled && m_sweepAll && !m_vehicleParams.isEmpty();
    m_obd->setBackgroundDemand(QStringLiteral("blackbox"), sweep ? m_vehicleParams : QStringList());
}

void TripRecorder::prune()
{
    const qint64 maxBytes = m_maxBytes;
    onStore([maxBytes](TripStore *store) { store->prune(maxBytes); });
}

// ---------------------------------------------------------------------------
// QML API
// ---------------------------------------------------------------------------

void TripRecorder::endTripNow()
{
    closeTrip(QStringLiteral("ended by user"));
}

void TripRecorder::refresh()
{
    onStore([](TripStore *store) {
        store->listTrips();
        emit store->storageChanged(store->storageBytes());
    });
}

void TripRecorder::requestDetail(qint64 id)
{
    if (id == m_tripId && m_tripOpen)
        flush();
    onStore([id](TripStore *store) { store->tripDetail(id); });
}

void TripRecorder::requestSeries(qint64 id, const QString &paramId, int maxPoints, double fromS, double toS)
{
    if (id == m_tripId && m_tripOpen)
        flush();
    onStore([id, paramId, maxPoints, fromS, toS](TripStore *store) { store->series(id, paramId, maxPoints, fromS, toS); });
}

void TripRecorder::deleteTrip(qint64 id)
{
    if (m_tripOpen && id == m_tripId)
        closeTrip(QStringLiteral("deleted"));
    onStore([id](TripStore *store) { store->deleteTrip(id); });
}

void TripRecorder::clearHistory()
{
    onStore([](TripStore *store) { store->clearAll(); });
}

void TripRecorder::exportTrip(qint64 id)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString dest = base + QStringLiteral("/OCTAVE-trips");
    onStore([id, dest](TripStore *store) { store->exportTrip(id, dest); });
}

bool TripRecorder::openHistoryFolder()
{
    if (!canOpenFolder())
        return false;
    QDir().mkpath(tripsDir());
    return QDesktopServices::openUrl(QUrl::fromLocalFile(tripsDir()));
}
