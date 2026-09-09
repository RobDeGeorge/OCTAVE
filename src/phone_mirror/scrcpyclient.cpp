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
#include <QVideoFrameFormat>
#include <QVideoSink>
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
#endif

#ifdef OCTAVE_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}
#endif

#include <cstring>

Q_LOGGING_CATEGORY(lcScrcpyClient, "octave.phonemirror.client")

namespace {

// Control message types (app/src/control_msg.h)
constexpr quint8 kMsgInjectKeycode = 0;
constexpr quint8 kMsgInjectTouchEvent = 2;
constexpr quint8 kMsgSetDisplayPower = 10;

// Frame header flags (app/src/demuxer.c)
constexpr quint64 kFlagConfig = quint64(1) << 63;

constexpr const char *kDeviceJarPath = "/data/local/tmp/scrcpy-server.jar";
constexpr const char *kJarResource = ":/scrcpy/scrcpy-server-v3.3.4";

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
    const QString out = dir + QStringLiteral("/octave-scrcpy-server-v") + QLatin1String(kServerVersion);
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
                         int maxFps, int bitRate, bool audio, bool stayAwake)
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
    m_thread = std::thread([this, displaySize, maxFps, bitRate, audio, stayAwake]() {
        session(displaySize, maxFps, bitRate, audio, stayAwake);
    });
    return true;
}

void ScrcpyClient::stop()
{
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

QTcpSocket *ScrcpyClient::connectUntilReady(qint64 deadlineMs)
{
    // The adb tunnel accepts locally before the server listens, then EOFs;
    // retry until the dummy byte actually arrives on this first socket.
    while (nowMs() < deadlineMs && !m_stopping.load()) {
        if (m_proc && m_proc->state() == QProcess::NotRunning) {
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

void ScrcpyClient::session(QString displaySize, int maxFps, int bitRate, bool audio, bool stayAwake)
{
    const qint64 t0 = nowMs();
    QString out;

    // 1. push the server (cleanup=true deletes it on exit, so every time)
    if (!adbRun({QStringLiteral("push"), m_jar, QString::fromLatin1(kDeviceJarPath)}, &out, 30000)) {
        fail(QStringLiteral("could not push scrcpy server: ") + out.right(200));
        return;
    }
    // 2. tunnel (scid must fit a signed 32-bit int on the server side).
    //    Reap forwards left by a hard-killed OCTAVE first: they live in the
    //    adb server and accumulate one listening port per session.
    m_scid = QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(0x7fffffff), 8, 16, QLatin1Char('0'));
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
    // 3. start the server
    QStringList args;
    if (!m_serial.isEmpty())
        args << QStringLiteral("-s") << m_serial;
    args << QStringLiteral("shell")
         << QStringLiteral("CLASSPATH=") + QLatin1String(kDeviceJarPath)
         << QStringLiteral("app_process") << QStringLiteral("/")
         << QStringLiteral("com.genymobile.scrcpy.Server") << QLatin1String(kServerVersion)
         << QStringLiteral("scid=") + m_scid << QStringLiteral("tunnel_forward=true")
         << QStringLiteral("video=true")
         << QStringLiteral("audio=%1").arg(audio ? QStringLiteral("true") : QStringLiteral("false"))
         << QStringLiteral("control=true") << QStringLiteral("video_codec=h264")
         << QStringLiteral("max_size=0") << QStringLiteral("video_bit_rate=%1").arg(bitRate)
         << QStringLiteral("max_fps=%1").arg(maxFps)
         << QStringLiteral("stay_awake=%1").arg(stayAwake ? QStringLiteral("true") : QStringLiteral("false"))
         << QStringLiteral("cleanup=true") << QStringLiteral("send_device_meta=true")
         << QStringLiteral("send_frame_meta=true") << QStringLiteral("send_codec_meta=true")
         << QStringLiteral("send_dummy_byte=true") << QStringLiteral("log_level=info");
    if (!displaySize.isEmpty())
        args << QStringLiteral("new_display=") + displaySize;
    qCInfo(lcScrcpyClient) << "starting server:" << args.mid(args.indexOf(QStringLiteral("app_process")));

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
        const qint64 deadline = t0 + 20000;
        while (!video && nowMs() < deadline && !m_stopping.load()) {
            pumpServerLog();
            video = connectUntilReady(qMin(deadline, nowMs() + 3000));
            if (m_stopping.load())
                break;
        }
    }
    if (!video) {
        if (!m_stopping.load())
            fail(QStringLiteral("scrcpy server did not answer within 20 s"));
        pumpServerLog();
        if (m_proc) { m_proc->kill(); m_proc->waitForFinished(1000); delete m_proc; m_proc = nullptr; }
        adbRun({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:%1").arg(m_port)}, nullptr, 5000);
        return;
    }
    m_video = video;
    if (audio) {
        m_audio = new QTcpSocket;
        m_audio->connectToHost(QHostAddress::LocalHost, quint16(m_port));
        m_audio->waitForConnected(5000);
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
            emit frameSizeChanged(w, h);
            emit connected(w, h);
            // 5. demux + decode until stop/disconnect
            videoLoop(video, t0);
        }
    }

    // teardown (worker owns everything)
    m_controlFd = -1;
    for (QTcpSocket **s : {&m_video, &m_audio, &m_control}) {
        if (*s) { (*s)->abort(); delete *s; *s = nullptr; }
    }
    pumpServerLog();
    if (m_proc) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(2000)) { m_proc->kill(); m_proc->waitForFinished(1000); }
        delete m_proc; m_proc = nullptr;
    }
    adbRun({QStringLiteral("forward"), QStringLiteral("--remove"), QStringLiteral("tcp:%1").arg(m_port)}, nullptr, 5000);
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
