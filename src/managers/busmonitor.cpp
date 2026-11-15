#include "busmonitor.h"

#include "settingsmanager.h"

#include <QDateTime>
#include <QDir>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcBus, "octave.busmonitor")

BusMonitor::BusMonitor(OBDManager *obd, const QString &logDir, QObject *parent)
    : QObject(parent)
    , m_obd(obd)
    , m_logDir(logDir.isEmpty() ? SettingsManager::getAppDataDir() + QStringLiteral("/history/bus") : logDir)
{
    m_refresh.setInterval(250);
    connect(&m_refresh, &QTimer::timeout, this, &BusMonitor::refresh);
    if (m_obd) {
        m_obd->setBusSink(this);
        m_wasAvailable = available();
        connect(m_obd, &OBDManager::connectionStatusChanged, this, [this] {
            const bool now = available();
            if (now != m_wasAvailable) {
                m_wasAvailable = now;
                emit availableChanged();
            }
        });
    }
}

BusMonitor::~BusMonitor()
{
    if (m_obd)
        m_obd->setBusSink(nullptr);
}

bool BusMonitor::available() const
{
    return m_obd && m_obd->canMonitorBus();
}

bool BusMonitor::canRecord() const
{
#ifdef OCTAVE_WASM
    return false;   // the page's files are gone on reload
#else
    return true;
#endif
}

void BusMonitor::setNote(const QString &note)
{
    if (note == m_note)
        return;
    m_note = note;
    emit noteChanged();
}

bool BusMonitor::start(const QString &filter)
{
    const QString wanted = filter.trimmed();
    const QByteArray checked = ELM327Protocol::busFilter(wanted);
    if (!wanted.isEmpty() && checked.isEmpty()) {
        setNote(QStringLiteral("A filter is a message id: 3 or 8 hex digits, X for any digit (7E8, 0CX)"));
        return false;
    }
    if (!available()) {
        setNote(QStringLiteral("Connect to a vehicle first"));
        return false;
    }
    setNote(QString());
    stopRecording();
    clear();
    m_latestMs = 0;       // the worker's clock starts again
    m_refreshedAtMs = 0;
    m_filter = QString::fromLatin1(checked);
    m_startEpochMs = QDateTime::currentMSecsSinceEpoch();
    return m_obd->startBusMonitor(m_filter);
}

void BusMonitor::stop()
{
    if (m_obd)
        m_obd->stopBusMonitor();
}

void BusMonitor::clear()
{
    m_entries.clear();
    m_rows.clear();
    m_frameCount = 0;
    m_framesPerSecond = 0;
    m_overruns = 0;
    m_countAtRefresh = 0;
    m_refreshedAtMs = m_latestMs;
    m_dirty = false;
    emit idsChanged();
}

bool BusMonitor::startRecording()
{
    if (!canRecord() || !m_active)
        return false;
    if (m_log.isOpen())
        return true;
    if (!QDir().mkpath(m_logDir))
        return false;
    const QString path = QDir(m_logDir).filePath(
        QStringLiteral("bus-%1.log").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    m_log.setFileName(path);
    if (!m_log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCWarning(lcBus) << "can't write" << path << ":" << m_log.errorString();
        return false;
    }
    m_logFile = path;
    qCInfo(lcBus) << "recording to" << path;
    emit recordingChanged();
    return true;
}

void BusMonitor::stopRecording()
{
    if (!m_log.isOpen())
        return;
    m_log.close();
    emit recordingChanged();
}

void BusMonitor::busFrames(const QList<BusFrame> &frames, int overruns)
{
    QByteArray lines;
    for (const BusFrame &f : frames) {
        ++m_frameCount;
        m_latestMs = qMax(m_latestMs, f.ms);
        // 29-bit ids sort after the 11-bit ones, whatever their value
        const quint64 key = (quint64(f.idDigits) << 32) | f.id;
        auto it = m_entries.find(key);
        if (it == m_entries.end()) {
            if (m_entries.size() >= kMaxIds)
                continue;
            it = m_entries.insert(key, Entry());
            it->idDigits = f.idDigits;
            it->changedAt = QVector<qint64>(f.data.size(), -1);
        } else {
            const double gap = static_cast<double>(f.ms - it->lastMs);
            it->periodMs = it->count == 1 ? gap : it->periodMs * 0.8 + gap * 0.2;
            if (it->changedAt.size() < f.data.size())
                it->changedAt.resize(f.data.size(), -1);
            for (int i = 0; i < f.data.size(); ++i) {
                if (i >= it->data.size() || it->data.at(i) != f.data.at(i))
                    it->changedAt[i] = f.ms;
            }
        }
        ++it->count;
        it->lastMs = f.ms;
        it->data = f.data;

        if (m_log.isOpen()) {
            const qint64 t = m_startEpochMs + f.ms;
            // can-utils wants 3 hex digits for an 11-bit id and 8 for a
            // 29-bit one; a J1850 / ISO header (6) is written as 8
            lines += '(' + QByteArray::number(t / 1000) + '.'
                     + QByteArray::number(t % 1000).rightJustified(3, '0') + "000) can0 "
                     + QByteArray::number(f.id, 16).toUpper().rightJustified(f.idDigits == 3 ? 3 : 8, '0') + '#'
                     + f.data.toHex().toUpper() + '\n';
        }
    }
    if (!lines.isEmpty()) {
        m_log.write(lines);
        m_log.flush();
    }
    m_overruns += overruns;
    m_dirty = true;
}

void BusMonitor::busMonitorChanged(bool active, const QString &note)
{
    if (!note.isEmpty() || active)
        setNote(note);
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        m_refresh.start();
    } else {
        m_refresh.stop();
        refresh();
        stopRecording();
    }
    emit activeChanged();
}

void BusMonitor::refresh()
{
    const qint64 span = m_latestMs - m_refreshedAtMs;
    if (span >= 200) {
        m_framesPerSecond = qRound((m_frameCount - m_countAtRefresh) * 1000.0 / static_cast<double>(span));
        m_countAtRefresh = m_frameCount;
        m_refreshedAtMs = m_latestMs;
    }
    if (!m_dirty)
        return;
    m_dirty = false;

    QVariantList rows;
    rows.reserve(m_entries.size());
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        QStringList bytes;
        QVariantList changed;
        for (int i = 0; i < it->data.size(); ++i) {
            bytes.append(QString::fromLatin1(it->data.mid(i, 1).toHex().toUpper()));
            const qint64 at = i < it->changedAt.size() ? it->changedAt.at(i) : -1;
            changed.append(at >= 0 && m_latestMs - at <= kChangedMs);
        }
        rows.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("%1").arg(quint32(it.key() & 0xFFFFFFFFu), it->idDigits, 16, QLatin1Char('0')).toUpper()},
            {QStringLiteral("count"), it->count},
            {QStringLiteral("periodMs"), qRound(it->periodMs)},
            {QStringLiteral("bytes"), bytes},
            {QStringLiteral("changed"), changed},
        });
    }
    m_rows = rows;
    emit idsChanged();
}
