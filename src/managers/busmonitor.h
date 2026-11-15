#ifndef OCTAVE_BUSMONITOR_H
#define OCTAVE_BUSMONITOR_H

// The bus monitor: everything the adapter hears on the vehicle bus while it
// only listens, as a table with one row per message id (how often it comes,
// its latest bytes, which of them just changed). For finding out which
// message carries what: press the brake, see which byte moves.
//
// Listen only. The adapter is put in its monitor mode ("ATMA", see
// OBDConnectionWorker::doStartBusMonitor()), in which it transmits nothing
// and on CAN does not even acknowledge frames. There is no way to send a
// message from here, and none should be added without the user asking for
// it: a frame on the wrong bus at the wrong moment can do real harm.
//
// While it listens the adapter can't answer requests, so live readings (the
// gauges, the black box) pause and resume when it stops. Any other OBD
// request (trouble codes, a vehicle scan) ends it.
//
// What the OBD port shows depends on the car: on many built since about
// 2010 a gateway keeps the internal traffic off the port and only replies to
// diagnostic requests appear, which is nothing while nobody asks. An ELM327
// at 38400 baud also carries about 200 messages a second where a busy
// 500 kbit/s bus has ten times that: its buffer fills ("overruns") and
// messages are lost, which a filter avoids.
//
// QML context property / service object "busMonitor". Not on the Android
// BLE path yet (available stays false there).

#include "obdmanager.h"

#include <QFile>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class BusMonitor : public QObject, public BusSink
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "vehicle")
    Q_CLASSINFO("octave.scope.files", "startRecording stopRecording")
    Q_CLASSINFO("octave.experimental", "*")

    // A vehicle is connected through an adapter OCTAVE can put in monitor mode
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // The filter of the session in progress (or the last one), "" for everything
    Q_PROPERTY(QString filter READ filter NOTIFY activeChanged)
    // Why start() refused or monitoring ended by itself; "" otherwise
    Q_PROPERTY(QString note READ note NOTIFY noteChanged)
    // One row per message id, sorted by id, refreshed four times a second:
    // {id: "0C9", count, periodMs, bytes: ["00", "1F", ...],
    //  changed: [false, true, ...]} with `changed` true for a byte whose
    // value changed within the last second. At most kMaxIds rows.
    Q_PROPERTY(QVariantList ids READ ids NOTIFY idsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY idsChanged)
    Q_PROPERTY(int framesPerSecond READ framesPerSecond NOTIFY idsChanged)
    // Times the adapter's buffer filled: messages were lost each time
    Q_PROPERTY(int overruns READ overruns NOTIFY idsChanged)
    // Recording to a file (not in the browser build)
    Q_PROPERTY(bool canRecord READ canRecord CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    // The file of the recording in progress, or of the last one
    Q_PROPERTY(QString logFile READ logFile NOTIFY recordingChanged)

public:
    static constexpr int kMaxIds = 1024;
    static constexpr int kChangedMs = 1000;

    // `obd` may be null (tests feed busFrames() directly); `logDir`
    // defaults to <config>/history/bus
    explicit BusMonitor(OBDManager *obd, const QString &logDir = QString(), QObject *parent = nullptr);
    ~BusMonitor() override;

    bool available() const;
    bool active() const { return m_active; }
    QString filter() const { return m_filter; }
    QString note() const { return m_note; }
    QVariantList ids() const { return m_rows; }
    int frameCount() const { return m_frameCount; }
    int framesPerSecond() const { return m_framesPerSecond; }
    int overruns() const { return m_overruns; }
    bool canRecord() const;
    bool recording() const { return m_log.isOpen(); }
    QString logFile() const { return m_logFile; }

    // Starts listening, clearing the table. `filter`: a message id to keep,
    // 3 hex digits (11-bit CAN) or 8 (29-bit), X for any digit ("7E8",
    // "0CX"); empty for everything. CAN only: the older buses are not
    // filtered. False (and `note`) when the filter is not one or nothing
    // is connected.
    Q_INVOKABLE bool start(const QString &filter = QString());
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();

    // Writes every message from now until monitoring ends to a new file in
    // the log folder, one line each in the can-utils log format
    // ("(1731600000.123456) can0 0C9#0000000000000000"), which candump,
    // SavvyCAN and Wireshark read. False when not listening or the file
    // can't be written.
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();

    // BusSink
    void busFrames(const QList<BusFrame> &frames, int overruns) override;
    void busMonitorChanged(bool active, const QString &note) override;

    // Rebuilds `ids` now instead of at the next quarter second (tests)
    void refresh();

signals:
    void availableChanged();
    void activeChanged();
    void noteChanged();
    void idsChanged();
    void recordingChanged();

private:
    struct Entry {
        quint8 idDigits = 3;
        int count = 0;
        qint64 lastMs = 0;
        double periodMs = 0;
        QByteArray data;
        QVector<qint64> changedAt;   // per byte, -1 = not since it was first seen
    };
    void setNote(const QString &note);

    OBDManager *m_obd = nullptr;
    QString m_logDir;
    bool m_active = false;
    bool m_wasAvailable = false;
    QString m_filter;
    QString m_note;
    // Sorted by id: the 11-bit ids, then the 29-bit ones
    QMap<quint64, Entry> m_entries;
    QVariantList m_rows;
    int m_frameCount = 0;
    int m_framesPerSecond = 0;
    int m_overruns = 0;
    qint64 m_latestMs = 0;
    int m_countAtRefresh = 0;
    qint64 m_refreshedAtMs = 0;
    bool m_dirty = false;
    QTimer m_refresh;
    QFile m_log;
    QString m_logFile;
    qint64 m_startEpochMs = 0;
};

#endif // OCTAVE_BUSMONITOR_H
