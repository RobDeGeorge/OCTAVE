#ifndef Q_OS_MOBILE

#include "scrcpyclient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <signal.h>
#include <pthread.h>
#endif

#ifdef OCTAVE_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}
#endif

#include <algorithm>
#include <cstring>
#include <cmath>

Q_LOGGING_CATEGORY(lcScrcpyClient, "octave.phonemirror.client")

namespace {

// Control message types (app/src/control_msg.h)
constexpr quint8 kMsgInjectKeycode = 0;
constexpr quint8 kMsgInjectTouchEvent = 2;
constexpr quint8 kMsgSetDisplayPower = 10;
// OCTAVE extensions (phone_server MirrorKeeper)
constexpr quint8 kMsgOctaveSetKeeper = 100;
constexpr quint8 kMsgOctaveTakeBack = 101;
// Device messages (server -> client on the control socket)
constexpr quint8 kDevMsgClipboard = 0;
constexpr quint8 kDevMsgAckClipboard = 1;
constexpr quint8 kDevMsgUhidOutput = 2;
constexpr quint8 kDevMsgOctavePhoneState = 100;
// A dozing phone streams pure black frames until it is woken. Rather than
// flash the dash black, a run of all-black frames is held back (the last good
// frame stays on screen) for this long, or for as long as the manager says
// the phone is asleep; a black run that outlives it is real content.
constexpr qint64 kBlackHoldMs = 3000;
constexpr int kBlackLumaMax = 20;
// While the phone is asleep or a stream is being reattached, a frame that is
// black apart from a strip of status icons is not content either: hold it if
// fewer than this fraction of samples are bright.
constexpr double kBlackLenientFraction = 0.01;

static bool isBlackFrame(const AVFrame *f, bool lenient)
{
    const int w = f->width, h = f->height, stride = f->linesize[0];
    // Max luma over every other pixel in both directions, early exit on the
    // first bright one. Point samples on a grid miss thin bright UI (a 2 px
    // icon stroke, a subtitle edge) and would hold a dark app's frame as if
    // the phone were dozing. ~256k byte reads on 1280x800: well under 1 ms.
    // Lenient: a black frame with only a strip of status icons still counts.
    if (w < 24 || h < 24)
        return false;
    const qint64 samples = qint64((h + 1) / 2) * ((w + 1) / 2);
    const qint64 allowed = lenient ? qint64(samples * kBlackLenientFraction) : 0;
    qint64 bright = 0;
    for (int r = 0; r < h; r += 2) {
        const uint8_t *row = f->data[0] + qint64(r) * stride;
        for (int c = 0; c < w; c += 2)
            if (row[c] > kBlackLumaMax && ++bright > allowed)
                return false;
    }
    return true;
}

// Frame header flags (app/src/demuxer.c)
constexpr quint64 kFlagConfig = quint64(1) << 63;

// Audio stream (phone_server/.../audio/AudioConfig.java, device/Streamer.java)
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioBytesPerFrame = kAudioChannels * 2;   // s16le
constexpr quint32 kAudioCodecRaw = 0x00726177;            // "raw"
constexpr quint32 kAudioStreamDisabled = 0;               // device could not capture; video continues
constexpr quint32 kAudioStreamError = 1;                  // configuration error
constexpr qsizetype kAudioQueueMaxBytes = kAudioSampleRate * kAudioBytesPerFrame / 4;  // ~250 ms backlog
// Sound detection (for ducking local media): a chunk counts as sound when its
// peak, before gain, exceeds about -46 dBFS; a silent remote submix is exact
// zeros, so this only has to reject dither-level noise.
constexpr int kAudioSignalThreshold = 164;
// How long the phone must stay silent before audioPlaying drops. Long enough
// to bridge the gaps between navigation sentences, short enough that music
// comes back promptly after a prompt.
constexpr int kAudioHoldMs = 1500;

constexpr const char *kDeviceJarPath = "/data/local/tmp/octave-phone-server.jar";
constexpr const char *kJarResource = ":/phone-server/octave-phone-server";

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// Read exactly n bytes with the blocking API; false on EOF/error/stop.
bool recvExact(QTcpSocket *s, char *dst, qint64 n, const std::atomic<bool> &stopping)
{
    qint64 got = 0;
    while (got < n) {
        if (stopping.load())
            return false;
        if (s->bytesAvailable() == 0 && !s->waitForReadyRead(1000)) {
            if (s->state() != QAbstractSocket::ConnectedState)
                return false;
            continue;
        }
        const qint64 k = s->read(dst + got, n - got);
        if (k < 0)
            return false;
        got += k;
    }
    return true;
}

} // namespace

// ─── static helpers ───────────────────────────────────────────────────

bool ScrcpyClient::available()
{
#ifdef OCTAVE_HAVE_FFMPEG
    return QFile::exists(QString::fromLatin1(kJarResource));
#else
    return false;
#endif
}

QString ScrcpyClient::bundledServerJar()
{
    if (!QFile::exists(QString::fromLatin1(kJarResource)))
        return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString out = dir + QStringLiteral("/octave-phone-server-") + QLatin1String(kServerVersion);
    QFile res(QString::fromLatin1(kJarResource));
    QFile dst(out);
    if (dst.exists() && dst.size() == res.size())
        return out;
    if (!res.open(QIODevice::ReadOnly) || !dst.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    dst.write(res.readAll());
    dst.close();
    return out;
}

int ScrcpyClient::freePort()
{
    // Test hook: pin the local tunnel port (used by the fake-server tests)
    const QByteArray forced = qgetenv("OCTAVE_SCRCPY_PORT");
    if (!forced.isEmpty())
        return forced.toInt();
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 27183;
    return probe.serverPort();
}

// ─── lifecycle ────────────────────────────────────────────────────────

ScrcpyClient::ScrcpyClient(const QString &adbPath, const QString &serverJar, QObject *parent)
    : QObject(parent), m_adb(adbPath), m_jar(serverJar)
{
    connect(this, &ScrcpyClient::frameReady, this, &ScrcpyClient::deliverFrame, Qt::QueuedConnection);
    connect(this, &ScrcpyClient::audioReady, this, &ScrcpyClient::deliverAudio, Qt::QueuedConnection);
    m_audioHoldTimer = new QTimer(this);
    m_audioHoldTimer->setSingleShot(true);
    m_audioHoldTimer->setInterval(kAudioHoldMs);
    connect(m_audioHoldTimer, &QTimer::timeout, this, [this] { setAudioPlaying(false); });
}

void ScrcpyClient::setAudioPlaying(bool playing)
{
    // GUI thread
    if (playing == m_audioPlaying)
        return;
    m_audioPlaying = playing;
    emit audioPlayingChanged(playing);
}

void ScrcpyClient::setVolume(float linear)
{
    m_volume = qBound(0.0f, linear, 1.0f);
    if (m_audioSink)
        m_audioSink->setVolume(m_volume.load());
}

ScrcpyClient::~ScrcpyClient()
{
    stop();
}

void ScrcpyClient::setVideoSink(QObject *sink)
{
    m_sink = qobject_cast<QVideoSink *>(sink);
}

bool ScrcpyClient::start(const QString &serial, const QString &displaySize,
                         int maxFps, int bitRate, bool audio, bool stayAwake, const QString &attachScid)
{
    if (m_running.load() || m_thread.joinable())
        return false;
#ifndef OCTAVE_HAVE_FFMPEG
    emit disconnected(QStringLiteral("This build has no H.264 decoder (libavcodec not linked)"));
    return false;
#endif
    m_serial = serial;
    m_stopping = false;
    m_frameCount = 0;
    m_thread = std::thread([this, displaySize, maxFps, bitRate, audio, stayAwake, attachScid]() {
#ifdef Q_OS_LINUX
        pthread_setname_np(pthread_self(), "scrcpy-client");
#endif
        session(displaySize, maxFps, bitRate, audio, stayAwake, attachScid);
    });
    return true;
}

void ScrcpyClient::stop()
{
    const bool wasActive = m_thread.joinable();
    if (wasActive)
        qCInfo(lcScrcpyClient) << "stop requested";
    m_stopping = true;
    m_running = false;
    // Unblock the worker: shut the control descriptor down from here; the
    // worker owns the QTcpSocket/QProcess objects and tears them down itself.
    const qintptr fd = m_controlFd.exchange(-1);
    if (fd >= 0) {
#ifdef Q_OS_WIN
        ::shutdown(static_cast<SOCKET>(fd), SD_BOTH);
#else
        ::shutdown(static_cast<int>(fd), SHUT_RDWR);
#endif
    }
    if (m_thread.joinable()) {
        if (std::this_thread::get_id() == m_thread.get_id())
            m_thread.detach();
        else
            m_thread.join();
    }
    if (m_devMsgThread.joinable()) {
        if (std::this_thread::get_id() == m_devMsgThread.get_id())
            m_devMsgThread.detach();
        else
            m_devMsgThread.join();
    }
    if (m_audioThread.joinable()) {
        if (std::this_thread::get_id() == m_audioThread.get_id())
            m_audioThread.detach();
        else
            m_audioThread.join();
    }
    stopAudioSink();
    // The worker removes its forward on the way out; if it could not (or was
    // never reached), do it here so a clean exit never strands a port.
    if (m_port) {
        adbRun({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:%1").arg(m_port)}, nullptr, 5000);
        qCInfo(lcScrcpyClient) << "removed forward tcp:" << m_port << "(from stop)";
        m_port = 0;
    }
    if (wasActive)
        qCInfo(lcScrcpyClient) << "stopped";
}

void ScrcpyClient::fail(const QString &reason)
{
    if (m_stopping.exchange(true))
        return;
    qCWarning(lcScrcpyClient) << reason;
    m_running = false;
    emit disconnected(reason);
}

// ─── control ──────────────────────────────────────────────────────────

void ScrcpyClient::sendControl(const QByteArray &msg)
{
    const qintptr fd = m_controlFd.load();
    if (fd < 0)
        return;
    std::lock_guard<std::mutex> lock(m_sendMutex);
#ifdef Q_OS_WIN
    ::send(static_cast<SOCKET>(fd), msg.constData(), msg.size(), 0);
#else
    ::send(static_cast<int>(fd), msg.constData(), msg.size(), MSG_NOSIGNAL);
#endif
}

void ScrcpyClient::injectTouch(qint64 pointerId, int action, int x, int y, float pressure)
{
    const int w = m_width.load(), h = m_height.load();
    if (!m_running.load() || w <= 0)
        return;
    x = qBound(0, x, w - 1);
    y = qBound(0, y, h - 1);
    const quint16 p = action == ActionUp ? 0 : quint16(qBound(0, int(pressure * 0xFFFF), 0xFFFF));
    QByteArray msg(32, 0);
    char *d = msg.data();
    d[0] = char(kMsgInjectTouchEvent);
    d[1] = char(action);
    qToBigEndian<qint64>(pointerId, d + 2);
    qToBigEndian<qint32>(x, d + 10);
    qToBigEndian<qint32>(y, d + 14);
    qToBigEndian<quint16>(quint16(w), d + 18);
    qToBigEndian<quint16>(quint16(h), d + 20);
    qToBigEndian<quint16>(p, d + 22);
    qToBigEndian<qint32>(0, d + 24);   // action_button
    qToBigEndian<qint32>(0, d + 28);   // buttons
    sendControl(msg);
}

void ScrcpyClient::injectKey(int keycode, int action, int meta)
{
    QByteArray msg(14, 0);
    char *d = msg.data();
    d[0] = char(kMsgInjectKeycode);
    d[1] = char(action);
    qToBigEndian<qint32>(keycode, d + 2);
    qToBigEndian<qint32>(0, d + 6);       // repeat
    qToBigEndian<qint32>(meta, d + 10);
    sendControl(msg);
}

void ScrcpyClient::pressKey(int keycode)
{
    injectKey(keycode, ActionDown);
    injectKey(keycode, ActionUp);
}

void ScrcpyClient::setKeeper(bool enabled, bool panelDark, int graceMs)
{
    QByteArray msg(7, 0);
    msg[0] = char(kMsgOctaveSetKeeper);
    msg[1] = enabled ? 1 : 0;
    msg[2] = panelDark ? 1 : 0;
    qToBigEndian<qint32>(graceMs, msg.data() + 3);
    sendControl(msg);
}

void ScrcpyClient::takeBack()
{
    sendControl(QByteArray(1, char(kMsgOctaveTakeBack)));
}

void ScrcpyClient::setDisplayPower(bool on)
{
    QByteArray msg(2, 0);
    msg[0] = char(kMsgSetDisplayPower);
    msg[1] = on ? 1 : 0;
    sendControl(msg);
}

// ─── worker thread ────────────────────────────────────────────────────

bool ScrcpyClient::adbRun(const QStringList &args, QString *output, int timeoutMs)
{
    QProcess p;
    QStringList full;
    if (!m_serial.isEmpty())
        full << QStringLiteral("-s") << m_serial;
    full << args;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(m_adb, full);
    const bool ok = p.waitForFinished(timeoutMs) && p.exitCode() == 0;
    if (output)
        *output = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    return ok;
}

QTcpSocket *ScrcpyClient::connectUntilReady(qint64 deadlineMs, bool startup)
{
    // The adb tunnel accepts locally before the server listens, then EOFs;
    // retry until the dummy byte actually arrives on this first socket.
    while (nowMs() < deadlineMs && !m_stopping.load()) {
        if (startup && m_proc && m_proc->state() == QProcess::NotRunning) {
            fail(QStringLiteral("scrcpy server exited during startup (see log)"));
            return nullptr;
        }
        auto *s = new QTcpSocket;
        s->connectToHost(QHostAddress::LocalHost, quint16(m_port));
        qCDebug(lcScrcpyClient) << "connect attempt port" << m_port;
        // waitForReadyRead() only reports *new* data, so check what may have
        // arrived already; otherwise a good connection gets aborted and the
        // server hands the next one out as the wrong socket.
        if (s->waitForConnected(2000) && (s->bytesAvailable() > 0 || s->waitForReadyRead(2000))) {
            char dummy = 0;
            if (s->read(&dummy, 1) == 1) {
                s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
                return s;
            }
        }
        qCDebug(lcScrcpyClient) << "not ready:" << s->state() << s->errorString() << "avail" << s->bytesAvailable();
        s->abort();
        delete s;
        QThread::msleep(100);
    }
    return nullptr;  // caller decides whether this is fatal
}

void ScrcpyClient::session(QString displaySize, int maxFps, int bitRate, bool audio, bool stayAwake, QString attachScid)
{
    const qint64 t0 = nowMs();
    QString out;
    // attach: reconnect to a server still running on the phone (kPersistMs
    // window after a link drop) instead of pushing and starting a new one.
    const bool attach = !attachScid.isEmpty();
    m_attached = attach;

    // 1. push the server (cleanup=true deletes it on exit, so every time)
    if (!attach && !adbRun({QStringLiteral("push"), m_jar, QString::fromLatin1(kDeviceJarPath)}, &out, 30000)) {
        fail(QStringLiteral("could not push scrcpy server: ") + out.right(200));
        return;
    }
    // 2. tunnel (scid must fit a signed 32-bit int on the server side).
    //    Reap forwards left by a hard-killed OCTAVE first: they live in the
    //    adb server and accumulate one listening port per session.
    m_scid = attach ? attachScid
                    : QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(0x7fffffff), 8, 16, QLatin1Char('0'));
    if (adbRun({QStringLiteral("forward"), QStringLiteral("--list")}, &out, 5000)) {
        const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() >= 3 && parts[2].startsWith(QLatin1String("localabstract:scrcpy_"))
                && (m_serial.isEmpty() || parts[0] == m_serial)) {
                qCInfo(lcScrcpyClient) << "removing stale forward" << parts[1] << "->" << parts[2];
                adbRun({QStringLiteral("forward"), QStringLiteral("--remove"), parts[1]}, nullptr, 5000);
            }
        }
    }
    m_port = freePort();
    if (!adbRun({QStringLiteral("forward"), QStringLiteral("tcp:%1").arg(m_port),
                 QStringLiteral("localabstract:scrcpy_") + m_scid}, &out)) {
        fail(QStringLiteral("adb forward failed: ") + out.right(200));
        return;
    }
    // 3. start the server (or, when attaching, follow the running server's
    //    logcat: its stdout went with the old adb shell)
    QStringList args;
    if (!m_serial.isEmpty())
        args << QStringLiteral("-s") << m_serial;
    QStringList opts;
    opts << QStringLiteral("scid=") + m_scid << QStringLiteral("tunnel_forward=true")
         << QStringLiteral("video=true")
         << QStringLiteral("audio=%1").arg(audio ? QStringLiteral("true") : QStringLiteral("false"))
         << QStringLiteral("control=true") << QStringLiteral("clipboard_autosync=false") << QStringLiteral("video_codec=h264")
         << QStringLiteral("audio_codec=raw")   // PCM s16le 48 kHz stereo: no decoder needed on our side
         << QStringLiteral("max_size=0") << QStringLiteral("video_bit_rate=%1").arg(bitRate)
         << QStringLiteral("max_fps=%1").arg(maxFps)
         << QStringLiteral("stay_awake=%1").arg(stayAwake ? QStringLiteral("true") : QStringLiteral("false"))
         << QStringLiteral("cleanup=true") << QStringLiteral("send_device_meta=true")
         << QStringLiteral("send_frame_meta=true") << QStringLiteral("send_codec_meta=true")
         << QStringLiteral("send_dummy_byte=true") << QStringLiteral("log_level=info")
         << QStringLiteral("octave_persist_ms=%1").arg(displaySize.isEmpty() ? 0 : kPersistMs);
    if (!displaySize.isEmpty())
        opts << QStringLiteral("new_display=") + displaySize;
    if (attach) {
        qCInfo(lcScrcpyClient) << "attaching to the running server (scid" << m_scid << ")";
        args << QStringLiteral("logcat") << QStringLiteral("-v") << QStringLiteral("raw")
             << QStringLiteral("-T") << QStringLiteral("1") << QStringLiteral("-s") << QStringLiteral("scrcpy:I");
    } else {
        // nohup + background + wait: the shell still relays stdout and lives as
        // long as the server, but the server survives the shell being killed by
        // adbd when the USB link drops (it is then reparented to init).
        args << QStringLiteral("shell")
             << QStringLiteral("CLASSPATH=%1 nohup app_process / %2 %3 %4 2>&1 & wait")   // assignment before nohup
                    .arg(QLatin1String(kDeviceJarPath), QLatin1String(kServerClass),
                         QLatin1String(kServerVersion), opts.join(QLatin1Char(' ')));
        qCInfo(lcScrcpyClient) << "starting server: app_process ..." << opts;
    }

    m_proc = new QProcess;
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_LINUX
    m_proc->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif
    m_proc->start(m_adb, args);
    if (!m_proc->waitForStarted(5000)) {
        fail(QStringLiteral("could not start adb"));
        delete m_proc; m_proc = nullptr;
        return;
    }
    auto pumpServerLog = [this]() {
        if (!m_proc)
            return;
        m_proc->waitForReadyRead(0);
        while (m_proc->canReadLine()) {
            const QString line = QString::fromUtf8(m_proc->readLine()).trimmed();
            if (line.isEmpty())
                continue;
            if (line.contains(QLatin1String("ERROR")) || line.contains(QLatin1String("WARN")))
                qCWarning(lcScrcpyClient) << "server:" << line;
            else
                qCInfo(lcScrcpyClient) << "server:" << line;
            emit serverLog(line);
        }
    };

    // 4. connect. The server sends device/codec meta only once EVERY socket
    //    (video, [audio], control) is connected, so open them all first.
    QTcpSocket *video = nullptr;
    {
        const qint64 deadline = t0 + (attach ? 4000 : 20000);
        while (!video && nowMs() < deadline && !m_stopping.load()) {
            pumpServerLog();
            video = connectUntilReady(qMin(deadline, nowMs() + 3000), !attach);
            if (m_stopping.load())
                break;
        }
    }
    if (!video) {
        if (!m_stopping.load())
            fail(attach ? QStringLiteral("could not attach to the running server")
                        : QStringLiteral("scrcpy server did not answer within 20 s"));
        pumpServerLog();
        if (m_proc) { m_proc->kill(); m_proc->waitForFinished(1000); delete m_proc; m_proc = nullptr; }
        adbRun({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:%1").arg(m_port)}, nullptr, 5000);
        return;
    }
    m_video = video;
    if (audio) {
        // The audio socket must be connected before the server sends any
        // meta; it is read on its own thread so video never waits on it.
        auto *audioSock = new QTcpSocket;
        audioSock->connectToHost(QHostAddress::LocalHost, quint16(m_port));
        if (audioSock->waitForConnected(5000)) {
            m_audio = audioSock;
            audioSock->moveToThread(nullptr);
            m_audioThread = std::thread([this, audioSock]() {
#ifdef Q_OS_LINUX
                pthread_setname_np(pthread_self(), "scrcpy-audio");
#endif
                audioLoop(audioSock);
            });
        } else {
            qCWarning(lcScrcpyClient) << "audio socket did not connect; mirroring video only";
            delete audioSock;
        }
    }
    m_control = new QTcpSocket;
    m_control->connectToHost(QHostAddress::LocalHost, quint16(m_port));
    if (!m_control->waitForConnected(5000)) {
        fail(QStringLiteral("could not open control socket"));
    } else {
        m_control->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_controlFd = m_control->socketDescriptor();

        char name[64] = {0};
        char meta[12] = {0};
        if (!recvExact(video, name, 64, m_stopping) || !recvExact(video, meta, 12, m_stopping)) {
            fail(QStringLiteral("handshake failed"));
        } else {
            const quint32 codecId = qFromBigEndian<quint32>(meta);
            const int w = int(qFromBigEndian<quint32>(meta + 4));
            const int h = int(qFromBigEndian<quint32>(meta + 8));
            Q_UNUSED(codecId)
            qCInfo(lcScrcpyClient) << "device" << QString::fromUtf8(name) << "codec"
                                   << QString::fromLatin1(QByteArray(meta, 4)) << w << "x" << h
                                   << "handshake at +" << (nowMs() - t0) / 1000.0 << "s";
            m_width = w;
            m_height = h;
            m_running = true;
            const qintptr cfd = m_controlFd.load();
            m_devMsgThread = std::thread([this, cfd]() {
#ifdef Q_OS_LINUX
                pthread_setname_np(pthread_self(), "scrcpy-devmsg");
#endif
                deviceMessageLoop(cfd);
            });
            emit frameSizeChanged(w, h);
            emit connected(w, h);
            // 5. demux + decode until stop/disconnect
            m_tFirst = nowMs();
            videoLoop(video, t0);
        }
    }

    // teardown (worker owns everything)
    qCInfo(lcScrcpyClient) << "session ending (stopping =" << m_stopping.load() << ")";
    m_controlFd = -1;
    for (QTcpSocket **s : {&m_video, &m_control}) {
        if (*s) { (*s)->abort(); delete *s; *s = nullptr; }
    }
    if (m_audio) {
        // Owned by the audio thread: just unblock it; it closes the socket itself
        const qintptr afd = m_audio->socketDescriptor();
        if (afd >= 0) {
#ifdef Q_OS_WIN
            ::shutdown(static_cast<SOCKET>(afd), SD_BOTH);
#else
            ::shutdown(static_cast<int>(afd), SHUT_RDWR);
#endif
        }
    }
    pumpServerLog();
    if (m_proc) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(2000)) { m_proc->kill(); m_proc->waitForFinished(1000); }
        delete m_proc; m_proc = nullptr;
    }
    adbRun({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:%1").arg(m_port)}, nullptr, 5000);
    qCInfo(lcScrcpyClient) << "removed forward tcp:" << m_port;
    m_port = 0;
    if (m_running.exchange(false)) {
        // Ended without a fail(): the peer closed the stream
        if (!m_stopping.exchange(true))
            emit disconnected(QStringLiteral("Phone disconnected. Reconnect the USB cable."));
    }
}

void ScrcpyClient::videoLoop(QTcpSocket *video, qint64 t0)
{
#ifdef OCTAVE_HAVE_FFMPEG
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx || avcodec_open2(ctx, codec, nullptr) < 0) {
        fail(QStringLiteral("could not open H.264 decoder"));
        if (ctx) avcodec_free_context(&ctx);
        return;
    }
    ctx->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QByteArray pendingConfig;
    QByteArray data;
    bool first = true;

    while (!m_stopping.load()) {
        char header[12];
        if (!recvExact(video, header, 12, m_stopping))
            break;
        const quint64 ptsFlags = qFromBigEndian<quint64>(header);
        const quint32 size = qFromBigEndian<quint32>(header + 8);
        data.resize(int(size));
        if (!recvExact(video, data.data(), size, m_stopping))
            break;
        if (ptsFlags & kFlagConfig) {
            pendingConfig = data;          // SPS/PPS: prepend to the next packet
            continue;
        }
        if (!pendingConfig.isEmpty()) {
            data.prepend(pendingConfig);
            pendingConfig.clear();
        }
        // libavcodec needs AV_INPUT_BUFFER_PADDING_SIZE zero bytes after the data
        data.append(AV_INPUT_BUFFER_PADDING_SIZE, '\0');
        pkt->data = reinterpret_cast<uint8_t *>(data.data());
        pkt->size = data.size() - AV_INPUT_BUFFER_PADDING_SIZE;
        if (avcodec_send_packet(ctx, pkt) < 0)
            continue;
        while (avcodec_receive_frame(ctx, frame) == 0) {
            if (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P) {
                const int w = frame->width, h = frame->height;
                if (w != m_width.load() || h != m_height.load()) {
                    m_width = w; m_height = h;
                    emit frameSizeChanged(w, h);
                }
                // Also on the first frames of a reattached stream: the display's
                // first composite after setSurface can be black + status bar, and
                // the sink still holds the last good frame of the previous session.
                // Lenient while asleep or in the first seconds of a reattached
                // stream: the display's first composite after setSurface is
                // black plus the status/task bar, and neither is content.
                const bool lenient = m_holdBlack.load() || (m_attached.load() && nowMs() - m_tFirst < kBlackHoldMs);
                if ((m_frameCount.load() > 0 || m_attached.load()) && isBlackFrame(frame, lenient)) {
                    const qint64 now = nowMs();
                    if (m_blackSince < 0)
                        m_blackSince = now;
                    if (m_holdBlack.load() || now - m_blackSince < kBlackHoldMs) {
                        av_frame_unref(frame);
                        continue;
                    }
                } else {
                    m_blackSince = -1;
                }
                QVideoFrameFormat fmt(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
                QVideoFrame vf(fmt);
                if (vf.map(QVideoFrame::WriteOnly)) {
                    for (int i = 0; i < 3; ++i) {
                        const int rows = i == 0 ? h : (h + 1) / 2;
                        const int rowBytes = i == 0 ? w : (w + 1) / 2;
                        const int srcStride = frame->linesize[i];
                        const int dstStride = vf.bytesPerLine(i);
                        uchar *dst = vf.bits(i);
                        const uint8_t *src = frame->data[i];
                        if (srcStride == dstStride) {
                            std::memcpy(dst, src, size_t(srcStride) * rows);
                        } else {
                            for (int r = 0; r < rows; ++r)
                                std::memcpy(dst + r * dstStride, src + r * srcStride, size_t(rowBytes));
                        }
                    }
                    vf.unmap();
                    pushFrame(vf);
                    if (first) {
                        first = false;
                        qCInfo(lcScrcpyClient) << "first frame at +" << (nowMs() - t0) / 1000.0 << "s";
                    }
                }
            }
            av_frame_unref(frame);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
#else
    Q_UNUSED(video) Q_UNUSED(t0)
#endif
}

// ─── audio ────────────────────────────────────────────────────────────

// Blocking read of exactly n bytes from a raw descriptor (the control socket
// belongs to the worker thread's QTcpSocket; this thread only reads the fd).
static bool recvExactFd(qintptr fd, char *buf, int n, const std::atomic<bool> &stopping)
{
    int got = 0;
    while (got < n && !stopping.load()) {
#ifdef Q_OS_WIN
        const int r = ::recv(static_cast<SOCKET>(fd), buf + got, n - got, 0);
#else
        const auto r = ::recv(static_cast<int>(fd), buf + got, size_t(n - got), 0);
#endif
        if (r <= 0)
            return false;
        got += int(r);
    }
    return got == n;
}

void ScrcpyClient::deviceMessageLoop(qintptr fd)
{
    // Device messages from the server (phone_server DeviceMessageWriter)
    char hdr[8];
    while (!m_stopping.load()) {
        if (!recvExactFd(fd, hdr, 1, m_stopping))
            return;
        const quint8 type = quint8(hdr[0]);
        if (type == kDevMsgClipboard) {
            if (!recvExactFd(fd, hdr, 4, m_stopping)) return;
            const quint32 n = qFromBigEndian<quint32>(hdr);
            QByteArray skip(int(qMin<quint32>(n, 1u << 18)), 0);
            if (!recvExactFd(fd, skip.data(), skip.size(), m_stopping)) return;
        } else if (type == kDevMsgAckClipboard) {
            if (!recvExactFd(fd, hdr, 8, m_stopping)) return;
        } else if (type == kDevMsgUhidOutput) {
            if (!recvExactFd(fd, hdr, 4, m_stopping)) return;
            const quint16 n = qFromBigEndian<quint16>(hdr + 2);
            QByteArray skip(n, 0);
            if (!recvExactFd(fd, skip.data(), skip.size(), m_stopping)) return;
        } else if (type == kDevMsgOctavePhoneState) {
            if (!recvExactFd(fd, hdr, 3, m_stopping)) return;
            emit phoneStateChanged(hdr[0] != 0, hdr[1] != 0, hdr[2] != 0);
        } else {
            qCWarning(lcScrcpyClient) << "unknown device message type" << type << "; stopping the reader";
            return;
        }
    }
}

void ScrcpyClient::audioLoop(QTcpSocket *audio)
{
    // 4-byte codec id, then [u64 pts][u32 size][PCM] packets.
    char meta[4] = {0};
    bool announced = false;
    if (recvExact(audio, meta, 4, m_stopping)) {
        const quint32 codecId = qFromBigEndian<quint32>(meta);
        if (codecId == kAudioStreamDisabled) {
            qCWarning(lcScrcpyClient) << "phone cannot capture audio (Android 11+ required or capture refused); video only";
        } else if (codecId == kAudioStreamError) {
            qCWarning(lcScrcpyClient) << "server reported an audio configuration error; video only";
        } else if (codecId != kAudioCodecRaw) {
            qCWarning(lcScrcpyClient) << "unexpected audio codec" << Qt::hex << codecId << "; video only";
        } else {
            qCInfo(lcScrcpyClient) << "audio stream is PCM 48 kHz stereo, playing through OCTAVE";
            m_audioActive = true;
            announced = true;
            emit audioStateChanged(true);
            QByteArray data;
            while (!m_stopping.load()) {
                char header[12];
                if (!recvExact(audio, header, 12, m_stopping))
                    break;
                const quint32 size = qFromBigEndian<quint32>(header + 8);
                data.resize(int(size));
                if (!recvExact(audio, data.data(), size, m_stopping))
                    break;
                auto *samples = reinterpret_cast<qint16 *>(data.data());
                const int n = data.size() / 2;
                // Peak before gain: is the phone actually making sound?
                int peak = 0;
                for (int i = 0; i < n; ++i)
                    peak = std::max(peak, std::abs(int(samples[i])));
                if (peak >= kAudioSignalThreshold)
                    m_audioSignalSeen = true;
                // Gain with a soft limiter (tanh) so a hot source cannot clip harshly
                const float gain = m_audioGain.load();
                if (std::abs(gain - 1.0f) > 1e-3f) {
                    for (int i = 0; i < n; ++i) {
                        const float x = float(samples[i]) * (gain / 32768.0f);
                        samples[i] = qint16(std::tanh(x) * 32767.0f);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(m_audioMutex);
                    m_audioQueue.append(data);
                    m_audioQueueBytes += data.size();
                    // Bound latency: if the GUI thread fell behind, drop the oldest
                    while (m_audioQueueBytes > kAudioQueueMaxBytes && m_audioQueue.size() > 1)
                        m_audioQueueBytes -= m_audioQueue.takeFirst().size();
                }
                emit audioReady();
            }
        }
    }
    audio->abort();
    delete audio;
    m_audio = nullptr;
    if (announced) {
        m_audioActive = false;
        emit audioStateChanged(false);
    }
}

bool ScrcpyClient::ensureAudioSink()
{
    if (m_audioSink)
        return m_audioIo != nullptr;
    QAudioFormat fmt;
    fmt.setSampleRate(kAudioSampleRate);
    fmt.setChannelCount(kAudioChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        qCWarning(lcScrcpyClient) << "no audio output device; phone audio will not play";
        return false;
    }
    if (!device.isFormatSupported(fmt)) {
        qCWarning(lcScrcpyClient) << "default audio output does not accept 48 kHz stereo s16; phone audio will not play";
        return false;
    }
    m_audioSink = new QAudioSink(device, fmt, this);
    m_audioSink->setBufferSize(kAudioSampleRate * kAudioBytesPerFrame / 5);  // ~200 ms device buffer
    m_audioSink->setVolume(m_volume.load());
    m_audioIo = m_audioSink->start();
    if (!m_audioIo) {
        qCWarning(lcScrcpyClient) << "could not start audio output:" << m_audioSink->error();
        return false;
    }
    return true;
}

void ScrcpyClient::stopAudioSink()
{
    m_audioSignalSeen = false;
    if (QThread::currentThread() == thread()) {
        m_audioHoldTimer->stop();
        setAudioPlaying(false);
    } else {
        QMetaObject::invokeMethod(this, [this] { m_audioHoldTimer->stop(); setAudioPlaying(false); },
                                  Qt::QueuedConnection);
    }
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
        m_audioIo = nullptr;
    }
    std::lock_guard<std::mutex> lock(m_audioMutex);
    m_audioQueue.clear();
    m_audioQueueBytes = 0;
}

void ScrcpyClient::deliverAudio()
{
    // GUI thread. Write whatever is queued; never block on a full device
    // buffer — dropping keeps the audio close to the picture.
    if (m_audioSignalSeen.exchange(false) && !m_stopping.load()) {
        setAudioPlaying(true);
        m_audioHoldTimer->start();   // (re)arm the release
    }
    if (m_stopping.load() || !ensureAudioSink()) {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioQueue.clear();
        m_audioQueueBytes = 0;
        return;
    }
    for (;;) {
        QByteArray chunk;
        {
            std::lock_guard<std::mutex> lock(m_audioMutex);
            if (m_audioQueue.isEmpty())
                return;
            chunk = m_audioQueue.first();
        }
        if (m_audioSink->bytesFree() < chunk.size())
            return;  // try again on the next audioReady
        m_audioIo->write(chunk);
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioQueue.removeFirst();
        m_audioQueueBytes -= chunk.size();
    }
}

// ─── frame delivery ──────────────────────────────────────────────────

void ScrcpyClient::pushFrame(const QVideoFrame &frame)
{
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_latestFrame = frame;
    }
    ++m_frameCount;
    emit frameReady();
}

void ScrcpyClient::deliverFrame()
{
    // GUI thread; coalesces if it falls behind
    QVideoFrame frame;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        frame = m_latestFrame;
        m_latestFrame = QVideoFrame();
    }
    if (frame.isValid() && m_sink)
        m_sink->setVideoFrame(frame);
}

#endif // Q_OS_MOBILE
