#include "obdconnectionworker.h"
#include "elm327protocol.h"
#include "rfcommsocket.h"
#include "../util/perfscope.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QSerialPortInfo>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

OBDConnectionWorker::OBDConnectionWorker(QObject *parent)
    : QObject(parent)
    , m_pollTimer(this)  // a child, so moveToThread() takes it to the worker thread
    , m_monitorTimer(this)
{
    m_pollTimer.setInterval(0);  // Poll as fast as possible (like delay_cmds=0)
    QObject::connect(&m_pollTimer, &QTimer::timeout, this, &OBDConnectionWorker::onPollTimer);
    qRegisterMetaType<QList<BusFrame>>("QList<BusFrame>");
    m_monitorTimer.setInterval(0);
    QObject::connect(&m_monitorTimer, &QTimer::timeout, this, &OBDConnectionWorker::onMonitorTimer);
}

OBDConnectionWorker::~OBDConnectionWorker()
{
    doDisconnect();
}

void OBDConnectionWorker::setPort(const QString &portName) { m_portName = portName; }
void OBDConnectionWorker::setFastMode(bool fast) { m_fastMode = fast; }
void OBDConnectionWorker::setTimeout(int seconds) { m_timeout = seconds; }
void OBDConnectionWorker::setCustomPids(const QList<CustomPidRequest> &pids)
{
    m_customPids = pids;
    m_customState = QVector<CustomState>(pids.size());
}

void OBDConnectionWorker::setPidsToWatch(const QList<PidKey> &pids, const QList<PidKey> &background)
{
    m_pidsToWatch = pids;
    m_backgroundPids = background;
    m_currentPidIndex = 0;
    m_backgroundIndex = 0;
    m_backgroundNext = false;
    m_deferred.clear();
}

PidKey OBDConnectionWorker::nextPollPid()
{
    if (!m_backgroundPids.isEmpty() && (m_pidsToWatch.isEmpty() || m_backgroundNext)) {
        m_backgroundNext = false;
        if (m_backgroundIndex >= m_backgroundPids.size())
            m_backgroundIndex = 0;
        return m_backgroundPids[m_backgroundIndex++];
    }
    if (m_currentPidIndex >= m_pidsToWatch.size())
        m_currentPidIndex = 0;
    const PidKey pid = m_pidsToWatch[m_currentPidIndex];
    m_currentPidIndex = (m_currentPidIndex + 1) % m_pidsToWatch.size();
    if (m_currentPidIndex == 0)
        m_backgroundNext = true;   // one background PID after each full pass
    return pid;
}

namespace {
struct CustomTransport {
    std::function<bool(const QString &)> claims;
    OBDConnectionWorker::TransportCreate create;
};
QList<CustomTransport> &customTransports()
{
    static QList<CustomTransport> transports;
    return transports;
}
const CustomTransport *transportFor(const QString &port)
{
    for (const CustomTransport &t : customTransports()) {
        if (t.claims && t.create && t.claims(port))
            return &t;
    }
    return nullptr;
}
} // namespace

void OBDConnectionWorker::setCustomTransport(std::function<bool(const QString &)> claims, TransportCreate create)
{
    customTransports().clear();
    addCustomTransport(std::move(claims), std::move(create));
}

void OBDConnectionWorker::addCustomTransport(std::function<bool(const QString &)> claims, TransportCreate create)
{
    if (claims && create)
        customTransports().append({std::move(claims), std::move(create)});
}

bool OBDConnectionWorker::transportClaims(const QString &port)
{
    return transportFor(port) != nullptr;
}

void OBDConnectionWorker::updatePidsToWatch(const QList<PidKey> &pids, const QList<PidKey> &background)
{
    setPidsToWatch(pids, background);
    if (!m_initialized)
        return;  // doConnect() still running; startPolling() picks the list up
    if (m_pidsToWatch.isEmpty() && m_backgroundPids.isEmpty()) {
        if (m_polling) {
            stopPolling();
            qDebug() << "[OBD Worker] Poll list emptied -- polling paused";
        }
    } else if (!m_polling) {
        startPolling();
    } else {
        qDebug() << "[OBD Worker] Poll list updated," << m_pidsToWatch.size() << "PIDs +"
                 << m_backgroundPids.size() << "background";
    }
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void OBDConnectionWorker::doConnect()
{
    qDebug() << "[OBD Worker] Connecting to" << m_portName;

    if (transportClaims(m_portName)) {
        m_custom = transportFor(m_portName)->create(m_portName, this);
        if (!m_custom || !m_custom->open(QIODevice::ReadWrite)) {
            const QString err = QStringLiteral("Failed to open %1: %2")
                                    .arg(m_portName, m_custom ? m_custom->errorString() : QStringLiteral("no device"));
            releasePort();
            emit initComplete(false, err);
            return;
        }
        // A transport may describe its link in objectName (Web Bluetooth: the
        // GATT characteristics it picked)
        emit logLine(m_custom->objectName().isEmpty()
                         ? QStringLiteral("Opened %1").arg(m_portName)
                         : QStringLiteral("Opened %1 (%2)").arg(m_portName, m_custom->objectName()));
        m_io = m_custom;
    } else if (RfcommSocket::isUrl(m_portName)) {
        QString mac;
        int channel = 1;
        if (!RfcommSocket::parseUrl(m_portName, &mac, &channel)) {
            emit initComplete(false, QStringLiteral("Bad Bluetooth address: %1").arg(m_portName));
            return;
        }
        emit logLine(QStringLiteral("Bluetooth: connecting to %1, channel %2").arg(mac).arg(channel));
        QElapsedTimer t;
        t.start();
        m_rfcomm = new RfcommSocket(this);
        if (!m_rfcomm->connectTo(mac, channel, qMax(10, m_timeout) * 1000)) {
            const QString err = m_rfcomm->errorString();
            qCWarning(lcElm327) << "could not connect" << m_portName << ":" << err;
            emit logLine(err);
            releasePort();
            emit initComplete(false, err);
            return;
        }
        qCInfo(lcElm327) << "connected" << m_portName << "in" << t.elapsed() << "ms";
        emit logLine(QStringLiteral("Bluetooth: connected in %1 ms, initialising ELM327").arg(t.elapsed()));
        m_io = m_rfcomm;
    } else if (!openSerialPort()) {
        return;
    }

    // Run ELM327 init sequence
    const bool initOk = sendInitSequence();
    if (QThread::currentThread()->isInterruptionRequested()) {
        // Superseded (reconnect, port change, quit): every command above
        // returned at once without a word, so say why the link went quiet
        qCInfo(lcElm327) << "connect to" << m_portName << "abandoned (superseded by a newer connect)";
        releasePort();
        emit initComplete(false, QStringLiteral("Superseded"));
        return;
    }
    if (!initOk) {
        releasePort();
        emit initComplete(false, QStringLiteral("ELM327 init failed -- adapter not responding"));
        return;
    }


    // Try to read a PID to verify vehicle connection: RPM. After ATSP0 this
    // first request is the adapter's protocol search ("SEARCHING..."),
    // which takes 10 s and more on a J1850 / ISO 9141 / KWP car; giving up
    // sooner meant the next attempt's ATZ restarted the search, forever.
    QString response = sendCommand(ELM327Protocol::formatPidRequest(1, 0x0C), qMax(m_timeout, 20) * 1000);
    // STOPPED: a byte reached the adapter mid-search and cut it short. The
    // search starts over on the next request; give it one.
    if (response.contains(QLatin1String("STOPPED"), Qt::CaseInsensitive))
        response = sendCommand(ELM327Protocol::formatPidRequest(1, 0x0C), qMax(m_timeout, 20) * 1000);
    // A timeout returns nothing; show what did arrive (e.g. "SEARCHING...")
    const QString shown = response.isEmpty()
                              ? QString::fromLatin1(m_responseBuffer.pending()).simplified()
                              : response.simplified();
    emit logLine(QStringLiteral("Vehicle check (010C): %1%2")
                     .arg(shown.isEmpty() ? QStringLiteral("no answer") : shown.left(60),
                          response.isEmpty() && !shown.isEmpty() ? QStringLiteral(" (no prompt, timed out)")
                                                                 : QString()));
    // Only an RPM reply ("410C...") is a vehicle answering: after a
    // reconnect with the ignition off, "STOPPED" and a search's leftovers
    // passed for one and left a connection that never had data
    bool answered = !response.isEmpty()
                    && ELM327Protocol::parseResponseLines(m_responseBuffer.lastLines(), 1, 0x0C).has_value();
    // A car with no engine speed (some EVs) says NO DATA to 010C and is
    // still a car: every OBD-II vehicle answers the supported-PID bitmap.
    // Only asked when 010C got a reply at all, so a silent bus is not
    // searched twice.
    if (!answered && response.contains(QLatin1String("NO DATA"), Qt::CaseInsensitive)) {
        response = sendCommand(ELM327Protocol::formatPidRequest(1, 0x00), qMax(m_timeout, 20) * 1000);
        answered = !response.isEmpty()
                   && !ELM327Protocol::supportedPidsFromLines(m_responseBuffer.lastLines(), 0x00).isEmpty();
        emit logLine(QStringLiteral("Vehicle check (0100, no RPM on this car): %1")
                         .arg(answered ? QStringLiteral("answered") : QStringLiteral("no answer")));
    }
    if (!answered) {
        // Adapter is connected but no vehicle. Release the port: an open
        // QSerialPort keeps a read notifier in this thread's event loop, and
        // an rfcomm node with no remote reports readable-with-zero-bytes
        // forever, which spins that loop at 100 % of a core between
        // commands (gdb on the Pi: the whole stack was QSerialPort inside
        // QThread::exec, no OCTAVE frame). Nothing may hold the port open
        // while we are not actively talking to a live adapter.
        releasePort();
        emit initComplete(false, QStringLiteral("Connected to adapter, no vehicle response"));
        return;
    }

    // The protocol the search settled on: CAN takes several PIDs per request
    QString dpn = sendCommand(QByteArrayLiteral("ATDPN\r"), 500).simplified().section(QLatin1Char(' '), -1);
    if (dpn.size() > 2)   // not a protocol number: a late reply to something else
        dpn = sendCommand(QByteArrayLiteral("ATDPN\r"), 500).simplified().section(QLatin1Char(' '), -1);
    m_isCan = ELM327Protocol::isCanProtocol(dpn);
    m_protocol = dpn;
    m_customState = QVector<CustomState>(m_customPids.size());
    m_customClock.start();

    m_initialized = true;
    qCInfo(lcElm327) << "ELM327 initialised, vehicle answered on" << m_portName << "protocol" << dpn
                     << (m_isCan ? "(CAN: several PIDs per request)" : "");
    emit logLine(QStringLiteral("Connected: the vehicle is answering"));
    emit initComplete(true, QStringLiteral("Connected"));
    // Queued startPolling() from the manager runs after this returns
    doReadVin();
}

bool OBDConnectionWorker::openSerialPort()
{
    // Create serial port on this thread
    m_serial = new QSerialPort(this);
    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(QSerialPort::Baud38400);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    QObject::connect(m_serial, &QSerialPort::errorOccurred,
                     this, &OBDConnectionWorker::onSerialError);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString err = m_serial->errorString();
        qCWarning(lcElm327) << "could not open" << m_portName << ":" << err;
        releasePort();
        emit initComplete(false, QStringLiteral("Failed to open port: %1").arg(err));
        return false;
    }

    qCInfo(lcElm327) << "opened" << m_portName;
    m_serial->setReadBufferSize(4096);
    m_io = m_serial;
    return true;
}

void OBDConnectionWorker::doReadVin()
{
    if (!m_initialized)
        return;
    const bool wasPolling = m_polling;
    if (wasPolling) stopPolling();
    // Multi-frame on CAN, five messages on pre-CAN; many pre-2005 vehicles
    // answer NO DATA, which parses to ""
    sendCommand(QByteArrayLiteral("0902\r"), 3000);
    emit vinRead(ELM327Protocol::parseVin(m_responseBuffer.lastLines()));
    if (wasPolling) startPolling();
}

void OBDConnectionWorker::releasePort()
{
    if (m_serial) {
        if (m_serial->isOpen())
            m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_rfcomm) {
        m_rfcomm->close();
        m_rfcomm->deleteLater();
        m_rfcomm = nullptr;
    }
    if (m_custom) {
        m_custom->close();
        m_custom->deleteLater();
        m_custom = nullptr;
    }
    m_io = nullptr;
    m_responseBuffer.clear();
}

void OBDConnectionWorker::doDisconnect()
{
    if (m_monitoring) {
        // The port is about to close: nothing to put back in the adapter
        m_monitoring = false;
        m_monitorTimer.stop();
        m_monitorBatch.clear();
        emit busMonitorChanged(false, QString());
    }
    stopPolling();
    releasePort();
    m_initialized = false;
}

bool OBDConnectionWorker::sendInitSequence()
{
    const auto cmds = ELM327Protocol::initCommands();
    int answered = 0;
    QString resetAnswer;
    for (const InitCommand &cmd : cmds) {
        // A bound /dev/rfcommN opens instantly but the kernel only brings the
        // Bluetooth link up on the first write, so ATZ's answer includes the
        // link-up as well as the reset: ~2.2 s on a NEXAS in the Jeep, past
        // the 1.5 s the protocol table allows. Returns as soon as it answers.
        const bool reset = cmd.command.startsWith("ATZ");
        QString response = sendCommand(cmd.command, reset ? qMax(cmd.timeoutMs, 5000) : cmd.timeoutMs);
        if (reset) {
            // Some adapters go on talking after the reset banner; let them
            // finish so it isn't taken for the next command's answer
            QThread::msleep(200);
        } else if (response.trimmed().isEmpty()) {
            // Lost over Bluetooth, or the adapter was still busy: once more,
            // so echo / spaces / headers really are off
            response = sendCommand(cmd.command, qMax(cmd.timeoutMs, 1000));
        }
        qDebug() << "[OBD Worker] Init:" << cmd.command.trimmed() << "->" << response;
        if (!response.trimmed().isEmpty())
            ++answered;
        // ATZ response usually contains "ELM327" -- verify
        if (cmd.command.startsWith("ATZ") && !response.toUpper().contains(QStringLiteral("ELM"))) {
            // Not necessarily fatal -- some clones don't report ELM in reset response
            qDebug() << "[OBD Worker] Warning: ATZ response doesn't contain 'ELM'";
        }
        if (cmd.command.startsWith("ATZ"))
            resetAnswer = response.simplified().left(40);
    }
    // In the connection log: "No Vehicle" alone can't tell a silent adapter
    // from a silent car
    emit logLine(QStringLiteral("ELM327: %1 of %2 setup commands answered (ATZ: %3)")
                     .arg(answered)
                     .arg(cmds.size())
                     .arg(resetAnswer.isEmpty() ? QStringLiteral("no answer") : resetAnswer));
    // Nothing answered a single setup command: whatever is on this port is
    // not an ELM327 (or is switched off), which is not "no vehicle"
    return answered > 0;
}

QString OBDConnectionWorker::sendCommand(const QByteArray &cmd, int timeoutMs)
{
    if (!m_io || !m_io->isOpen())
        return QString();
    // The manager abandoned this worker (reconnect, port change, quit):
    // fail fast so a blocking doConnect() unwinds and the thread can finish
    if (QThread::currentThread()->isInterruptionRequested())
        return QString();
    // The adapter is in monitor mode: this request ends it
    if (m_monitoring) {
        endMonitor(QStringLiteral("Stopped for another request"));
        if (!m_io || !m_io->isOpen())
            return QString();
    }

    // QSerialPort errors are sticky until cleared: without this, one
    // transient error (e.g. during rfcomm link-up) failed every later
    // command of the init sequence instantly
    if (m_serial)
        m_serial->clearError();
    m_responseBuffer.clear();
    // Bytes nobody asked for (the tail of a reply that timed out, an
    // adapter's banner) would be read as this command's answer, and every
    // answer after it would be one behind
    if (m_io->bytesAvailable() > 0) {
        const QByteArray stale = m_io->readAll();
        qCDebug(lcElm327) << "dropped" << stale.size() << "stale bytes before" << cmd.trimmed() << ":"
                          << stale.simplified().left(80);
    }
    qCDebug(lcElm327) << "TX" << cmd.trimmed();
    m_io->write(cmd);
    if (m_serial)
        m_serial->flush();

    // Wait for response with timeout (blocking within worker thread is OK)
    QElapsedTimer timer;
    timer.start();

    // A protocol search ("SEARCHING...", after ATSP0 or an adapter reset)
    // ends in an answer or UNABLE TO CONNECT within ~15 s. Any byte sent
    // before then aborts it and the next request searches again, so a poll
    // with a 500 ms timeout never let it finish: let a search run out.
    constexpr int kSearchMs = 15000;
    const auto limit = [&] {
        return m_responseBuffer.pending().contains("SEARCHING") ? qMax(timeoutMs, kSearchMs) : timeoutMs;
    };
    int emptyReads = 0;
    while (timer.elapsed() < limit()) {
        if (QThread::currentThread()->isInterruptionRequested())
            return QString();
        QElapsedTimer waitTimer;
        waitTimer.start();
        if (m_io->waitForReadyRead(100)) {
            const QByteArray chunk = m_io->readAll();
            if (!chunk.isEmpty()) {
                emptyReads = 0;
                m_responseBuffer.feed(chunk);
                auto resp = m_responseBuffer.getResponse();
                if (resp.has_value()) {
                    qCDebug(lcElm327) << "RX" << resp->trimmed().left(200) << "(" << timer.elapsed() << "ms )";
                    return resp.value();
                }
                continue;  // productive read: the prompt is probably next, do not pace
            } else if (++emptyReads >= 50) {
                // Readable-but-empty on every poll: an rfcomm node whose
                // remote went away never clears this on its own. Treat it
                // as a lost connection and release the port so its read
                // notifier stops spinning the event loop.
                qWarning() << "[OBD Worker] Port reports readable but delivers no data; treating as disconnected";
                stopPolling();
                releasePort();
                m_initialized = false;
                emit connectionLost(QStringLiteral("Adapter stopped responding"));
                return QString();
            }
        } else if (m_rfcomm) {
            if (m_rfcomm->isBroken()) {
                const QString err = m_rfcomm->errorString();
                qCWarning(lcElm327) << err << "on" << m_portName;
                emit logLine(err);
                stopPolling();
                releasePort();
                m_initialized = false;
                emit connectionLost(err);
                return QString();
            }
        } else if (m_custom && !m_custom->isOpen()) {
            // A custom transport closes itself when its link goes away
            const QString err = m_custom->errorString();
            qCWarning(lcElm327) << err << "on" << m_portName;
            emit logLine(err);
            stopPolling();
            releasePort();
            m_initialized = false;
            emit connectionLost(err);
            return QString();
        } else if (m_serial) {
            const auto err = m_serial->error();
            if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError ||
                err == QSerialPort::PermissionError || err == QSerialPort::NotOpenError)
                break;  // port is gone; onSerialError reports it
            if (err != QSerialPort::NoError)
                m_serial->clearError();  // transient (timeout, read hiccup): keep waiting
        }
        // When the wait returned without data, never iterate faster than
        // ~10 Hz: an rfcomm node with no remote returns from the wait
        // immediately (readable, zero bytes), which turned this loop into a
        // 100 % CPU spin for the whole command timeout, repeated by the
        // passive scanner forever. Reads that delivered bytes skip this (the
        // ELM327 often sends data and the '>' prompt as separate chunks, and
        // pacing between them cost 100 ms per PID).
        const qint64 spent = waitTimer.elapsed();
        if (spent < 100)
            QThread::msleep(static_cast<unsigned long>(100 - spent));
    }
    qCWarning(lcElm327) << "no response to" << cmd.trimmed() << "within" << timeoutMs << "ms"
                        << "(partial:" << m_responseBuffer.pending().simplified().left(80) << ")";

    // Timeout -- return whatever we have
    auto resp = m_responseBuffer.getResponse();
    return resp.value_or(QString());
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

void OBDConnectionWorker::startPolling()
{
    if (m_monitoring) {
        m_monitorResume = true;   // when monitoring ends
        return;
    }
    if (m_pidsToWatch.isEmpty() && m_backgroundPids.isEmpty()) {
        qDebug() << "[OBD Worker] No PIDs to watch";
        return;
    }
    m_currentPidIndex = 0;
    m_backgroundIndex = 0;
    m_backgroundNext = false;
    m_polling = true;

    if (m_fastMode) {
        m_pollTimer.setInterval(0);  // Maximum speed
    } else {
        m_pollTimer.setInterval(50);
    }
    m_pollTimer.start();
    qDebug() << "[OBD Worker] Polling started," << m_pidsToWatch.size() << "PIDs +"
             << m_backgroundPids.size() << "background";
}

void OBDConnectionWorker::stopPolling()
{
    m_monitorResume = false;
    m_polling = false;
    m_pollTimer.stop();
}

void OBDConnectionWorker::onPollTimer()
{
    if (!m_polling || !m_io || !m_io->isOpen())
        return;

    if (m_pidsToWatch.isEmpty() && m_backgroundPids.isEmpty())
        return;

    // Get next PID to poll
    const PidKey pid = m_deferred.isEmpty() ? nextPollPid() : m_deferred.takeFirst();

    if (m_fastMode && m_isCan && m_multiPid && ELM327Protocol::isBatchable(pid)) {
        // Fill the request from the next PIDs in turn. One that can't join
        // (ATRV, a multi-value PID) waits for the next ticks; a repeat means
        // the list has gone round.
        QList<int> batch{pid.second};
        const int lookahead = m_pidsToWatch.size() + 1;
        for (int i = 0; i < lookahead && batch.size() < ELM327Protocol::kMaxPidsPerRequest; ++i) {
            const PidKey next = nextPollPid();
            if (batch.contains(next.second) && next.first == 1) {
                m_deferred.append(next);
                break;
            }
            if (ELM327Protocol::isBatchable(next))
                batch.append(next.second);
            else
                m_deferred.append(next);
        }
        if (batch.size() > 1) {
            pollBatch(batch);
            return;
        }
    }

    if (pid.first == kCustomPidMode) {
        pollCustom(pid.second);
        return;
    }

    if (pid == kElmVoltageKey) {
        // ELM_VOLTAGE is the adapter's own "ATRV" -- a text reply, not a PID.
        QString response = sendCommand(ELM327Protocol::elmVoltageRequest(), 500);
        if (response.isEmpty())
            return;
        if (auto volts = ELM327Protocol::parseElmVoltage(response))
            emit dataReceived(QStringLiteral("elmVoltageChanged"), static_cast<float>(*volts));
        return;
    }

    // Multi-value PIDs can be several CAN frames, each one a "reply" to
    // the count: those keep the adapter's own timeout
    const bool extended = ELM327Protocol::extendedPidTable().contains(pid);
    const bool single = m_fastMode && m_singleReply && !extended;
    QByteArray request = ELM327Protocol::formatPidRequest(pid.first, pid.second, single ? 1 : 0);
    QElapsedTimer roundTrip;
    roundTrip.start();
    QString response = sendCommand(request, 500);
    notePoll(roundTrip.elapsed(), response, 1);

    if (response.isEmpty())
        return;
    if (single && response.trimmed() == QLatin1String("?")) {
        m_singleReply = false;
        qCInfo(lcElm327) << "adapter refused the response count (" << request.trimmed()
                         << "-> ?); polling without it";
        emit logLine(QStringLiteral("Adapter has no response count; polling at its own timeout"));
        return;
    }

    // Multi-value PIDs: several values per reply, often CAN multi-frame
    if (extended) {
        const auto ext = ELM327Protocol::parseResponseLines(m_responseBuffer.lastLines(),
                                                            pid.first, pid.second);
        if (!ext.has_value())
            return;
        const auto values = ELM327Protocol::decodeExtendedPid(ext->mode, ext->pid, ext->dataBytes);
        for (const auto &v : values)
            emit extendedDataReceived(v.first, static_cast<float>(v.second));
        return;
    }

    // Parse response: this PID's line, wherever it sits among the ECUs'
    // (the last line can be another module's refusal, "7F0112")
    auto parsed = ELM327Protocol::parseResponseLines(m_responseBuffer.lastLines(), pid.first, pid.second);
    if (!parsed.has_value())
        return;

    // Decode PID
    auto decoded = ELM327Protocol::decodePid(parsed->mode, parsed->pid, parsed->dataBytes);
    if (!decoded.has_value())
        return;

    // Emit the data
    emit dataReceived(decoded->signalName, static_cast<float>(decoded->value));
}

// ---------------------------------------------------------------------------
// Bus monitor: listen only
// ---------------------------------------------------------------------------
//
// "ATMA" makes the adapter print every message on the bus and transmit
// nothing; on CAN it does not even acknowledge frames ("ATCSM1", the
// adapter's default, sent again here). Headers go on so each line starts
// with the message's id, and CAN formatting off so the data bytes are shown
// as they are. All three are put back when monitoring ends.

void OBDConnectionWorker::doStartBusMonitor(const QString &filter)
{
    if (!m_initialized || !m_io || !m_io->isOpen()) {
        emit busMonitorChanged(false, QStringLiteral("Not connected"));
        return;
    }
    if (m_monitoring)
        endMonitor(QString());
    if (!m_io)
        return;

    const bool resume = m_polling || m_monitorResume;
    stopPolling();
    m_monitorResume = resume;
    m_monitorFilter = m_isCan ? ELM327Protocol::busFilter(filter) : QByteArray();

    auto ok = [this](const QByteArray &cmd) {
        return sendCommand(cmd, 500).contains(QLatin1String("OK"), Qt::CaseInsensitive);
    };
    QString refused;
    if (!ok("ATH1\r") && !ok("ATH1\r"))
        refused = QStringLiteral("The adapter did not turn message ids on");
    if (refused.isEmpty() && m_isCan) {
        ok("ATCSM1\r");   // silent: older chips have no such command and are silent anyway
        if (!ok("ATCAF0\r"))
            refused = QStringLiteral("The adapter can't show raw CAN data");
        else if (!m_monitorFilter.isEmpty() && !ok("ATCRA" + m_monitorFilter + "\r"))
            refused = QStringLiteral("The adapter did not take the filter %1").arg(QString::fromLatin1(m_monitorFilter));
    }
    if (!m_io || QThread::currentThread()->isInterruptionRequested())
        return;

    // From here endMonitor() undoes the settings above
    m_monitoring = true;
    m_monitorLine.clear();
    m_monitorNote.clear();
    m_monitorBatch.clear();
    m_monitorOverruns = 0;
    m_monitorRestarts = 0;
    m_monitorEmptyReads = 0;
    m_monitorClock.start();
    m_monitorEmitAt = 0;
    if (!refused.isEmpty()) {
        endMonitor(refused);
        return;
    }
    qCInfo(lcElm327) << "bus monitor on" << (m_monitorFilter.isEmpty() ? QByteArray("(everything)") : m_monitorFilter);
    emit logLine(QStringLiteral("Listening to the bus; live readings pause meanwhile"));
    emit busMonitorChanged(true, QString());
    writeMonitorCommand();
    m_monitorTimer.start();
}

void OBDConnectionWorker::doStopBusMonitor()
{
    endMonitor(QString());
}

void OBDConnectionWorker::writeMonitorCommand()
{
    if (!m_io)
        return;
    if (m_serial)
        m_serial->clearError();
    qCDebug(lcElm327) << "TX ATMA";
    m_io->write(QByteArrayLiteral("ATMA\r"));
    if (m_serial)
        m_serial->flush();
}

void OBDConnectionWorker::flushMonitorBatch()
{
    if (m_monitorBatch.isEmpty() && m_monitorOverruns == 0)
        return;
    emit busFrames(m_monitorBatch, m_monitorOverruns);
    m_monitorBatch.clear();
    m_monitorOverruns = 0;
    m_monitorEmitAt = m_monitorClock.elapsed() + 50;
}

void OBDConnectionWorker::monitorLinkLost(const QString &reason)
{
    m_monitoring = false;
    m_monitorTimer.stop();
    flushMonitorBatch();
    emit busMonitorChanged(false, reason);
    qCWarning(lcElm327) << reason << "on" << m_portName << "(bus monitor)";
    emit logLine(reason);
    stopPolling();
    releasePort();
    m_initialized = false;
    emit connectionLost(reason);
}

void OBDConnectionWorker::onMonitorTimer()
{
    if (!m_monitoring)
        return;
    if (!m_io || !m_io->isOpen()) {
        monitorLinkLost(m_custom ? m_custom->errorString() : QStringLiteral("Adapter disconnected"));
        return;
    }

    QElapsedTimer wait;
    wait.start();
    QByteArray chunk;
    if (m_io->waitForReadyRead(50)) {
        chunk = m_io->readAll();
        // Readable with nothing to read, over and over: a link whose other
        // end went away (see sendCommand())
        if (!chunk.isEmpty()) {
            m_monitorEmptyReads = 0;
        } else if (++m_monitorEmptyReads >= 50) {
            monitorLinkLost(QStringLiteral("Adapter stopped responding"));
            return;
        }
    } else if (m_rfcomm && m_rfcomm->isBroken()) {
        monitorLinkLost(m_rfcomm->errorString());
        return;
    } else if (m_custom && !m_custom->isOpen()) {
        monitorLinkLost(m_custom->errorString());
        return;
    } else if (m_serial) {
        const auto err = m_serial->error();
        if (err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError ||
            err == QSerialPort::PermissionError || err == QSerialPort::NotOpenError) {
            // onSerialError reports the port; monitoring is over either way
            m_monitoring = false;
            m_monitorTimer.stop();
            flushMonitorBatch();
            emit busMonitorChanged(false, QStringLiteral("Adapter disconnected"));
            return;
        }
        if (err != QSerialPort::NoError)
            m_serial->clearError();
    }
    if (chunk.isEmpty()) {
        // Never spin: a wait that returns at once with nothing is paced
        if (wait.elapsed() < 20)
            QThread::msleep(static_cast<unsigned long>(20 - wait.elapsed()));
        if (m_monitorClock.elapsed() >= m_monitorEmitAt)
            flushMonitorBatch();
        return;
    }

    bool prompt = false;
    auto endLine = [this] {
        const QByteArray line = m_monitorLine.trimmed();
        m_monitorLine.clear();
        if (line.isEmpty())
            return;
        if (auto frame = ELM327Protocol::parseMonitorLine(line, m_isCan)) {
            frame->ms = m_monitorClock.elapsed();
            m_monitorBatch.append(*frame);
            m_monitorRestarts = 0;
            return;
        }
        if (line.contains("BUFFER FULL"))
            ++m_monitorOverruns;
        if (line != "STOPPED" && !line.startsWith("ATMA"))
            m_monitorNote = QString::fromLatin1(line.left(40));
    };
    for (const char c : std::as_const(chunk)) {
        if (c == '\r' || c == '\n') {
            endLine();
        } else if (c == '>') {
            endLine();
            prompt = true;
        } else if (m_monitorLine.size() < 256) {
            m_monitorLine.append(c);
        }
    }

    if (prompt) {
        // The adapter left monitor mode by itself: its buffer filled (the
        // bus is faster than the link to us) or the bus had an error. Carry
        // on, unless it keeps coming back with nothing heard.
        if (m_monitorNote == QLatin1String("?")) {
            endMonitor(QStringLiteral("The adapter has no monitor mode"));
            return;
        }
        if (++m_monitorRestarts > 3) {
            endMonitor(m_monitorNote.isEmpty() ? QStringLiteral("The adapter stopped listening")
                                               : QStringLiteral("The adapter stopped listening: %1").arg(m_monitorNote));
            return;
        }
        writeMonitorCommand();
    }
    if (m_monitorBatch.size() >= 400 || m_monitorClock.elapsed() >= m_monitorEmitAt)
        flushMonitorBatch();
}

void OBDConnectionWorker::endMonitor(const QString &note)
{
    if (!m_monitoring)
        return;
    m_monitoring = false;
    m_monitorTimer.stop();
    flushMonitorBatch();
    const bool resume = m_monitorResume;
    m_monitorResume = false;

    bool restored = false;
    if (m_io && m_io->isOpen()) {
        // Any byte ends monitor mode; the adapter answers STOPPED and its
        // prompt. A space, because a bare carriage return at the prompt
        // (had it stopped already) would repeat the last command: ATMA.
        m_io->write(QByteArrayLiteral(" "));
        if (m_serial)
            m_serial->flush();
        QElapsedTimer t;
        t.start();
        bool prompt = false;
        while (!prompt && t.elapsed() < 600 && m_io && m_io->isOpen()) {
            if (m_io->waitForReadyRead(50))
                prompt = m_io->readAll().contains('>');
            else
                QThread::msleep(10);
        }
        auto ok = [this](const QByteArray &cmd) {
            for (int attempt = 0; attempt < 2 && m_io; ++attempt) {
                if (sendCommand(cmd, 500).contains(QLatin1String("OK"), Qt::CaseInsensitive))
                    return true;
            }
            return false;
        };
        restored = ok("ATH0\r");
        if (restored && m_isCan) {
            restored = ok("ATCAF1\r");
            // "ATAR": receive what answers our requests again (undoes ATCRA)
            if (restored && !m_monitorFilter.isEmpty())
                restored = ok("ATAR\r");
        }
    }
    emit busMonitorChanged(false, note);
    if (!m_io || QThread::currentThread()->isInterruptionRequested())
        return;
    if (!restored) {
        // Polling on with message ids in every reply would read nothing:
        // start over from ATZ, which resets all of it
        qCWarning(lcElm327) << "could not restore the adapter after the bus monitor; reconnecting";
        emit logLine(QStringLiteral("The adapter did not leave monitor mode cleanly; reconnecting"));
        stopPolling();
        releasePort();
        m_initialized = false;
        emit connectionLost(QStringLiteral("Adapter stayed in monitor mode"));
        return;
    }
    qCInfo(lcElm327) << "bus monitor off" << note;
    emit logLine(QStringLiteral("Stopped listening to the bus"));
    if (resume)
        startPolling();
}

bool OBDConnectionWorker::setHeader(const QByteArray &header)
{
    return sendCommand("ATSH" + header + "\r", 500).contains(QLatin1String("OK"), Qt::CaseInsensitive);
}

void OBDConnectionWorker::pollCustom(int index)
{
    if (index < 0 || index >= m_customPids.size() || index >= m_customState.size())
        return;
    const CustomPidRequest &pid = m_customPids.at(index);
    CustomState &state = m_customState[index];
    if (state.unusable || (state.retryAt && m_customClock.elapsed() < state.retryAt))
        return;

    // A header addresses one ECU, and stays set in the adapter: every other
    // request (the standard PIDs, trouble codes, the scan) needs the
    // protocol's own back, so it is restored before this returns. Where
    // OCTAVE doesn't know that header, the PID is not polled at all.
    const QByteArray restore = ELM327Protocol::defaultHeader(m_protocol);
    if (!pid.header.isEmpty() && restore.isEmpty()) {
        state.unusable = true;
        qCInfo(lcElm327) << "custom PID" << pid.paramId << "needs header" << pid.header
                         << "which can't be restored on protocol" << m_protocol << "; not polled";
        emit logLine(QStringLiteral("%1 needs an ECU header, which this vehicle's protocol doesn't allow yet")
                         .arg(pid.paramId));
        return;
    }

    std::optional<QVector<uint8_t>> data;
    QElapsedTimer roundTrip;
    roundTrip.start();
    const bool addressed = pid.header.isEmpty() || setHeader(pid.header.toLatin1());
    if (addressed) {
        // No response count: the reply is often several CAN frames
        const QString response = sendCommand(ELM327Protocol::formatDataRequest(pid.mode, pid.pid), 1000);
        notePoll(roundTrip.elapsed(), response, 1);
        if (!response.isEmpty())
            data = ELM327Protocol::parseDataResponse(m_responseBuffer.lastLines(), pid.mode, pid.pid);
    }
    if (!pid.header.isEmpty()) {
        bool restored = false;
        for (int attempt = 0; attempt < 2 && m_io && !restored; ++attempt)
            restored = setHeader(restore);
        // The link went (sendCommand() reported it) or this connection is
        // being replaced: nothing left to restore
        if (!m_io || QThread::currentThread()->isInterruptionRequested())
            return;
        if (!restored) {
            // Polling on with another ECU's header would turn every reading
            // into NO DATA: start over from ATZ, which resets it
            qCWarning(lcElm327) << "could not restore the default header after" << pid.paramId << "; reconnecting";
            emit logLine(QStringLiteral("The adapter kept a custom PID's header; reconnecting"));
            stopPolling();
            releasePort();
            m_initialized = false;
            emit connectionLost(QStringLiteral("Adapter kept a custom header"));
            return;
        }
    }
    if (!m_io)
        return;

    const double value = data && pid.decoder ? pid.decoder(*data) : std::numeric_limits<double>::quiet_NaN();
    if (std::isnan(value)) {
        if (++state.misses == kCustomMissLimit) {
            qCInfo(lcElm327) << "custom PID" << pid.paramId << "unanswered" << kCustomMissLimit
                             << "times; trying every" << kCustomRetryMs / 1000 << "s";
            emit logLine(QStringLiteral("%1 isn't answering; trying it now and then").arg(pid.paramId));
        }
        if (state.misses >= kCustomMissLimit)
            state.retryAt = m_customClock.elapsed() + kCustomRetryMs;
        return;
    }
    state.misses = 0;
    state.retryAt = 0;
    emit extendedDataReceived(pid.paramId, static_cast<float>(value));
}

void OBDConnectionWorker::pollBatch(const QList<int> &pids)
{
    const QByteArray request = ELM327Protocol::formatMultiPidRequest(1, pids);
    QElapsedTimer roundTrip;
    roundTrip.start();
    // Longer than a single poll: the reply can be several CAN frames
    const QString response = sendCommand(request, 1000);
    const QList<ParsedResponse> values =
        response.isEmpty() ? QList<ParsedResponse>()
                           : ELM327Protocol::parseMultiPidResponse(m_responseBuffer.lastLines(), 1, pids);
    notePoll(roundTrip.elapsed(), response, values.size());
    for (const ParsedResponse &v : values) {
        if (const auto decoded = ELM327Protocol::decodePid(v.mode, v.pid, v.dataBytes))
            emit dataReceived(decoded->signalName, static_cast<float>(decoded->value));
    }
    if (response.isEmpty() || response.contains(QLatin1String("NO DATA"), Qt::CaseInsensitive))
        return;
    // Some ECUs answer only the first PID of a request (or "?"): poll one
    // at a time rather than starve the rest
    if (values.size() >= 2) {
        m_batchMisses = 0;
    } else if (++m_batchMisses >= 3) {
        m_multiPid = false;
        qCInfo(lcElm327) << "ECU answers one PID per request (" << request.trimmed() << "->"
                         << response.simplified().left(60) << "); polling one at a time";
        emit logLine(QStringLiteral("Vehicle takes one PID per request"));
    }
}

void OBDConnectionWorker::notePoll(qint64 ms, const QString &response, int values)
{
    if (!m_statsWindow.isValid())
        m_statsWindow.start();
    if (response.isEmpty())
        ++m_statSilent;
    else if (response.contains(QLatin1String("NO DATA"), Qt::CaseInsensitive))
        ++m_statNoData;
    else {
        ++m_statReplies;
        m_statValues += values;
    }
    m_statMs += ms;
    m_statMaxMs = qMax(m_statMaxMs, ms);
    const qint64 window = m_statsWindow.elapsed();
    // The first after 10 s, so a short session's log has one too
    if (window < (m_statsLogged ? 30000 : 10000))
        return;
    m_statsLogged = true;
    const int polls = m_statReplies + m_statNoData + m_statSilent;
    qCInfo(lcElm327).nospace().noquote() << "OBD polling: " << polls << " requests in " << window / 1000 << " s ("
                               << QString::number(polls * 1000.0 / window, 'f', 1) << "/s), " << m_statValues
                               << " values (" << QString::number(m_statValues * 1000.0 / window, 'f', 1)
                               << "/s), round trip mean "
                               << m_statMs / qMax(1, polls) << " ms, max " << m_statMaxMs << " ms; "
                               << m_statNoData << " NO DATA, " << m_statSilent << " unanswered; "
                               << m_pidsToWatch.size() << " PIDs shown + " << m_backgroundPids.size()
                               << " background" << (m_fastMode && m_singleReply ? ", single reply" : "")
                               << (m_fastMode && m_isCan && m_multiPid ? ", batched" : "");
    m_statsWindow.restart();
    m_statValues = m_statReplies = m_statNoData = m_statSilent = 0;
    m_statMs = m_statMaxMs = 0;
}

void OBDConnectionWorker::onSerialReadyRead()
{
    // Not used in polling mode (we use blocking reads in sendCommand)
    // Kept as placeholder for potential async mode
}

void OBDConnectionWorker::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    // TimeoutError is every empty 100 ms wait in sendCommand(), not news
    if (error != QSerialPort::TimeoutError)
        qCWarning(lcElm327) << "serial error on" << m_portName << ":" << error << m_serial->errorString();

    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError) {
        stopPolling();
        emit connectionLost(QStringLiteral("Serial port error: %1").arg(m_serial->errorString()));
    }
}

// ---------------------------------------------------------------------------
// Diagnostic commands (run on worker thread, blocking)
// ---------------------------------------------------------------------------

void OBDConnectionWorker::doReadDtc()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 03 request (stored DTCs)
    QString response = sendCommand(QByteArrayLiteral("03\r"), 5000);
    QStringList codes;
    if (!response.isEmpty()) {
        codes = ELM327Protocol::parseDtcResponse(m_responseBuffer.lastLines(), 0x03);
    }
    emit dtcResult(codes);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doReadCurrentDtc()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 07 request (current/pending DTCs)
    QString response = sendCommand(QByteArrayLiteral("07\r"), 5000);
    QStringList codes;
    if (!response.isEmpty()) {
        codes = ELM327Protocol::parseDtcResponse(m_responseBuffer.lastLines(), 0x07);
    }
    emit dtcResult(codes);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doClearDtc()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 04 request (clear DTCs)
    // Only the positive response (44) means the ECU cleared anything.
    // "NO DATA", "?" and a refusal (7F 04 xx, usual with the engine
    // running) used to pass as success and wiped the list on screen while
    // the car kept its codes.
    sendCommand(QByteArrayLiteral("04\r"), 5000);
    bool success = false;
    QString refusal;
    for (const QString &raw : m_responseBuffer.lastLines()) {
        QString line = raw.toUpper();
        line.remove(QLatin1Char(' '));
        if (line.startsWith(QLatin1String("44")))
            success = true;
        else if (line.startsWith(QLatin1String("7F04")))
            refusal = line.mid(4, 2);
    }
    QString message = QStringLiteral("DTCs cleared successfully");
    if (!success) {
        if (refusal == QLatin1String("22"))
            message = QStringLiteral("The vehicle refused to clear codes (conditions not correct: try ignition on, engine off)");
        else if (!refusal.isEmpty())
            message = QStringLiteral("The vehicle refused to clear codes (code %1)").arg(refusal);
        else
            message = QStringLiteral("Clear command failed: no confirmation from the vehicle");
    }
    emit dtcCleared(success, message);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doReadStatus()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 01 PID 01 (Monitor status since DTCs cleared)
    QString response = sendCommand(ELM327Protocol::formatPidRequest(1, 0x01), 3000);
    bool mil = false;
    int dtcCount = 0;

    // The engine ECU's line among every module's (Byte A of each ECU
    // carries its own MIL / count; the first that answers is the ECM)
    const auto parsed = response.isEmpty()
                            ? std::nullopt
                            : ELM327Protocol::parseResponseLines(m_responseBuffer.lastLines(), 1, 0x01);
    if (parsed.has_value() && parsed->dataBytes.size() >= 4) {
        // Byte A, bit 7 = MIL, bits 0-6 = DTC count
        uint8_t byteA = parsed->dataBytes[0];
        mil = (byteA & 0x80) != 0;
        dtcCount = byteA & 0x7F;
    }

    emit milStatus(mil, dtcCount);

    if (wasPolling) startPolling();
}

void OBDConnectionWorker::doReadFreezeFrame()
{
    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    // Send Mode 02 PID 02 frame 00 (the DTC that stored the freeze frame)
    QString response = sendCommand(QByteArrayLiteral("020200\r"), 5000);
    QStringList codes;
    if (!response.isEmpty()) {
        codes = ELM327Protocol::parseFreezeFrameDtc(m_responseBuffer.lastLines());
    }
    // Then what the frame recorded: its bitmap (PIDs 01-20, the range
    // freeze frames use), and each PID OCTAVE can decode
    QVariantMap values;
    if (!codes.isEmpty() && !sendCommand(QByteArrayLiteral("020000\r"), 3000).isEmpty()) {
        if (const auto bitmap = ELM327Protocol::parseFreezeFrameData(m_responseBuffer.lastLines(), 0x00)) {
            QList<int> pids = ELM327Protocol::parseSupportedPids(*bitmap).values();
            std::sort(pids.begin(), pids.end());
            for (int pid : pids) {
                if (pid <= 0x03 || !ELM327Protocol::pidTable().contains({0x01, pid}))
                    continue;   // status, the DTC and fuel system status: not values
                const QByteArray cmd = QStringLiteral("02%100\r").arg(pid, 2, 16, QLatin1Char('0')).toUpper().toLatin1();
                if (sendCommand(cmd, 3000).isEmpty())
                    continue;
                const auto data = ELM327Protocol::parseFreezeFrameData(m_responseBuffer.lastLines(), pid);
                if (!data)
                    continue;
                if (const auto decoded = ELM327Protocol::decodePid(0x01, pid, *data))
                    values.insert(decoded->signalName, decoded->value);
            }
        }
    }
    emit freezeFrame(codes, values);

    if (wasPolling) startPolling();
}

// ---------------------------------------------------------------------------
// Vehicle scan
// ---------------------------------------------------------------------------

QSet<int> OBDConnectionWorker::querySupportedPids()
{
    QSet<int> allSupported;

    // Query supported PIDs in ranges: 00, 20, 40, 60, 80, A0, C0, E0
    const int ranges[] = {0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0};
    for (int base : ranges) {
        bool more = false;
        QSet<int> supported;
        // Every vehicle answers 0100; without a bitmap (STOPPED, a reply
        // lost over Bluetooth) ask again before calling the car empty
        for (int attempt = 0; attempt < (base == 0 ? 3 : 1) && supported.isEmpty(); ++attempt) {
            const QString response = sendCommand(ELM327Protocol::formatPidRequest(1, base), 5000);
            if (!response.isEmpty())
                supported = ELM327Protocol::supportedPidsFromLines(m_responseBuffer.lastLines(), base, &more);
        }
        if (supported.isEmpty() && base == 0)
            qCWarning(lcElm327) << "no supported-PID bitmap in the reply to 0100:"
                                << m_responseBuffer.lastLines().join(QLatin1Char('|')).left(200);
        allSupported.unite(supported);
        // Bit 32 (base + 0x20) set by no ECU: no next range to ask
        if (!more)
            break;
    }
    qCInfo(lcElm327) << "vehicle reports" << allSupported.size() << "supported PIDs";

    return allSupported;
}

void OBDConnectionWorker::doScanVehicle()
{
    emit scanOutput(QStringLiteral("[SCAN] Starting vehicle PID scan..."));
    emit scanOutput(QStringLiteral("[INFO] Querying supported PIDs from vehicle..."));

    bool wasPolling = m_polling;
    if (wasPolling) stopPolling();

    QSet<int> supportedPids = querySupportedPids();

    if (supportedPids.isEmpty()) {
        emit scanOutput(QStringLiteral("[WARN] No supported PIDs returned from vehicle"));
        emit scanProgress(100, QStringLiteral("No supported PIDs found"));
        emit scanComplete(QStringList());
        if (wasPolling) startPolling();
        return;
    }

    emit scanOutput(QStringLiteral("[INFO] Vehicle reports %1 supported PIDs")
                        .arg(supportedPids.size()));

    // Check which PIDs from our table are supported
    const auto &table = ELM327Protocol::pidTable();
    QStringList supportedNames;

    int total = table.size();
    int i = 0;
    for (auto it = table.constBegin(); it != table.constEnd(); ++it, ++i) {
        const QString &cmdName = it.value().commandName;
        int progress = static_cast<int>((static_cast<float>(i) / total) * 100.0f);
        emit scanProgress(progress, QStringLiteral("Checking %1...").arg(cmdName));

        // Check if this PID is in the vehicle's supported set (mode 1, pid
        // number). ELM_VOLTAGE is the adapter's own ATRV: always available,
        // as in python-obd's base_commands().
        const PidKey &key = it.key();
        if (key == kElmVoltageKey || (key.first == 1 && supportedPids.contains(key.second))) {
            supportedNames.append(cmdName);
            emit scanOutput(QStringLiteral("[OK] %1").arg(cmdName));
        }
    }
    // Report in the shared vocabulary (base command names + multi-value
    // signal ids for PIDs 0x61+), same as the Android path.
    const int unsupported = total - supportedNames.size();
    supportedNames = ELM327Protocol::supportedCommandNames(supportedPids);

    // Summary
    emit scanOutput(QString());
    emit scanOutput(QStringLiteral("========================================"));
    emit scanOutput(QStringLiteral("[DONE] Scan complete!"));
    emit scanOutput(QStringLiteral("[INFO] Supported: %1 parameters").arg(supportedNames.size()));
    emit scanOutput(QStringLiteral("[INFO] Unsupported: %1 parameters").arg(unsupported));
    emit scanOutput(QStringLiteral("========================================"));

    emit scanComplete(supportedNames);

    if (wasPolling) startPolling();
}
