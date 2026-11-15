#ifndef OBDCONNECTIONWORKER_H
#define OBDCONNECTIONWORKER_H

#include <QElapsedTimer>
#include <QIODevice>
#include <QList>
#include <QObject>
#include <QSerialPort>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <functional>

#include "elm327protocol.h"

class RfcommSocket;

// ===========================================================================
// OBDConnectionWorker -- runs on a dedicated QThread, owns the QSerialPort.
//
// Handles ELM327 initialization, PID polling, and diagnostic commands.
// Communicates results back to OBDManager via signals (thread-safe).
// ===========================================================================
class OBDConnectionWorker : public QObject
{
    Q_OBJECT

public:
    explicit OBDConnectionWorker(QObject *parent = nullptr);
    ~OBDConnectionWorker() override;

    void setPort(const QString &portName);
    void setFastMode(bool fast);
    void setTimeout(int seconds);
    void setPidsToWatch(const QList<PidKey> &pids, const QList<PidKey> &background = {});
    // What the keys {kCustomPidMode, index} stand for. Before the thread starts.
    void setCustomPids(const QList<CustomPidRequest> &pids);

    // Extra transport for ports the serial / RFCOMM paths don't handle:
    // `claims` says whether it serves a port, `create` returns an unopened
    // QIODevice for it. Dev tooling registers the simulated ELM327
    // ("sim://elm327") this way; nothing is registered in a normal run. Set
    // before the first connection; `create` runs on the worker thread.
    // setCustomTransport() replaces every registered transport (empty
    // functions clear them), addCustomTransport() adds one beside them (the
    // browser demo serves the simulated car and Web Bluetooth adapters).
    using TransportCreate = std::function<QIODevice *(const QString &port, QObject *parent)>;
    static void setCustomTransport(std::function<bool(const QString &port)> claims, TransportCreate create);
    static void addCustomTransport(std::function<bool(const QString &port)> claims, TransportCreate create);
    static bool transportClaims(const QString &port);

signals:
    // Connection
    void initComplete(bool success, const QString &message);
    void connectionLost(const QString &reason);

    // Live data
    void dataReceived(const QString &signalName, float value);
    void extendedDataReceived(const QString &paramId, float value);
    void vinRead(const QString &vin);

    // Diagnostics
    void dtcResult(const QStringList &codes);
    void dtcCleared(bool success, const QString &message);
    void milStatus(bool mil, int dtcCount);
    // The code that stored freeze frame 0, and the frame's values by
    // signal name (native units)
    void freezeFrame(const QStringList &codes, const QVariantMap &values);

    // Scan
    void scanProgress(int progress, const QString &message);
    void scanComplete(const QStringList &supported);
    void scanOutput(const QString &line);

    // One line for the settings page's connection log
    void logLine(const QString &line);

    // Bus monitor: what was heard since the last emission, and how often
    // the adapter's buffer filled meanwhile
    void busFrames(const QList<BusFrame> &frames, int overruns);
    void busMonitorChanged(bool active, const QString &note);

public slots:
    // Called from OBDManager (via queued connection)
    void doConnect();
    void doDisconnect();
    void releasePort();   // close + delete the QSerialPort; nothing may hold it open while idle
    void startPolling();
    void stopPolling();
    // Swap the poll list without reconnecting (settings toggles).
    void updatePidsToWatch(const QList<PidKey> &pids, const QList<PidKey> &background);
    void doReadDtc();
    void doReadCurrentDtc();
    void doClearDtc();
    void doReadStatus();
    void doReadFreezeFrame();
    void doReadVin();
    void doScanVehicle();
    // Listen only: "ATMA" with headers on, until doStopBusMonitor() or any
    // other request. `filter`: ELM327Protocol::busFilter() text or empty.
    void doStartBusMonitor(const QString &filter);
    void doStopBusMonitor();

private slots:
    void onPollTimer();
    void onMonitorTimer();
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    // ELM327 init sequence
    bool sendInitSequence();
    bool openSerialPort();   // QSerialPort path of doConnect(); emits initComplete(false) itself

    // Send a command and wait for response (blocking, with timeout)
    QString sendCommand(const QByteArray &cmd, int timeoutMs = 2000);

    // Query supported PIDs from vehicle
    QSet<int> querySupportedPids();

    // The link to the adapter, created on the worker thread: a QSerialPort
    // (USB, /dev/rfcommN, COM) or, for "rfcomm://<MAC>", a direct Bluetooth
    // socket. m_io is whichever one is open; everything talks through it.
    QSerialPort *m_serial = nullptr;
    RfcommSocket *m_rfcomm = nullptr;
    QIODevice *m_custom = nullptr;   // from setCustomTransport()
    QIODevice *m_io = nullptr;
    ResponseBuffer m_responseBuffer;

    // Configuration
    QString m_portName;
    bool m_fastMode = true;
    int m_timeout = 5;

    // PIDs to poll: the foreground list round-robin, with one background
    // PID slipped in after every full pass (or plain round-robin over the
    // background list when the foreground is empty)
    QList<PidKey> m_pidsToWatch;
    int m_currentPidIndex = 0;
    QList<PidKey> m_backgroundPids;
    int m_backgroundIndex = 0;
    bool m_backgroundNext = false;
    // The PID for this poll slot, advancing the cursors
    PidKey nextPollPid();

    // Poll timer
    QTimer m_pollTimer;
    bool m_polling = false;

    // Polls ask for one reply ("010C1"): the adapter answers at the first
    // ECU instead of waiting out ATST for more. Fast mode only; dropped for
    // the connection when the adapter answers "?" (a clone without it).
    bool m_singleReply = true;
    // On CAN, consecutive batchable PIDs share one request ("010C0D05",
    // up to 6): one round trip for a whole page. Fast mode only; dropped for
    // the connection when the ECU keeps answering one PID per request.
    bool m_isCan = false;
    bool m_multiPid = true;
    int m_batchMisses = 0;
    QList<PidKey> m_deferred;   // drawn by a batch they couldn't join; polled next, in order
    void pollBatch(const QList<int> &pids);

    // Custom PIDs (CustomPidRequest). One that keeps going unanswered is
    // left alone for kCustomRetryMs between tries, so a file of readings
    // this car doesn't have costs the live gauges next to nothing.
    static constexpr int kCustomMissLimit = 3;
    static constexpr qint64 kCustomRetryMs = 30000;
    struct CustomState {
        int misses = 0;
        qint64 retryAt = 0;      // m_customClock time, 0 = poll freely
        bool unusable = false;   // needs a header this protocol can't restore
    };
    QList<CustomPidRequest> m_customPids;
    QVector<CustomState> m_customState;
    QElapsedTimer m_customClock;
    QString m_protocol;          // ATDPN's answer
    void pollCustom(int index);
    // "ATSH": true when the adapter said OK
    bool setHeader(const QByteArray &header);

    // Bus monitor. While m_monitoring the adapter prints messages unasked
    // and nothing may be written to it: a byte ends its monitor mode.
    // sendCommand() therefore ends monitoring first (endMonitor()).
    QTimer m_monitorTimer;
    bool m_monitoring = false;
    bool m_monitorResume = false;    // polling was on, or was asked for meanwhile
    QByteArray m_monitorFilter;
    QByteArray m_monitorLine;        // bytes of a line not yet ended
    QString m_monitorNote;           // the last line that wasn't a message
    QList<BusFrame> m_monitorBatch;
    int m_monitorOverruns = 0;
    int m_monitorRestarts = 0;       // "ATMA" sent again with nothing heard since
    int m_monitorEmptyReads = 0;
    QElapsedTimer m_monitorClock;
    qint64 m_monitorEmitAt = 0;
    void writeMonitorCommand();
    void flushMonitorBatch();
    void endMonitor(const QString &note);
    void monitorLinkLost(const QString &reason);

    // Poll round trips, logged at info level ("OBD polling: ...") 10 s
    // into the connection and every 30 s after
    QElapsedTimer m_statsWindow;
    bool m_statsLogged = false;
    int m_statValues = 0;
    int m_statReplies = 0;
    int m_statNoData = 0;
    int m_statSilent = 0;
    qint64 m_statMs = 0;
    qint64 m_statMaxMs = 0;
    void notePoll(qint64 ms, const QString &response, int values);

    // State
    bool m_initialized = false;
};

#endif // OBDCONNECTIONWORKER_H
