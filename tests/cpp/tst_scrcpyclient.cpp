// The phone-mirror client (src/phone_mirror/scrcpyclient.cpp) without a phone:
// device-message parsing and the media command wire format over a socket
// pair; stopping mid-start leaves no adb forward or server shell behind; and a
// fake scrcpy 3.x server (handshake, config and key packets, a mid-stream
// resolution change, a corrupt packet, a garbage header, early exits).
// tests/cpp/fake-adb.sh stands in for adb; the H.264 clips are
// tests/fixtures/h264/.

#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <atomic>
#include <functional>
#include <future>
#include <thread>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "phone_mirror/scrcpyclient.h"

namespace {
const QString kRepo = QStringLiteral(OCTAVE_SOURCE_DIR);
const QString kFakeAdb = kRepo + QStringLiteral("/tests/cpp/fake-adb.sh");
constexpr int kW = 64, kH = 48;     // first stream
constexpr int kW2 = 96, kH2 = 64;   // after the "rotation" (new SPS)

// ── H.264 fixtures ──────────────────────────────────────────────────────

struct Stream {
    QByteArray config;                       // SPS + PPS
    QList<QPair<QByteArray, bool>> packets;  // access unit, is key frame
};

// Annex-B file -> config + one packet per access unit, like the phone's
// encoder sends them (SPS/PPS split out into the config packet)
Stream loadStream(const QString &name)
{
    QFile f(kRepo + "/tests/fixtures/h264/" + name);
    Stream s;
    if (!f.open(QIODevice::ReadOnly))
        return s;
    const QByteArray data = f.readAll();
    QList<QPair<int, int>> starts;   // (start incl. start code, body)
    for (qsizetype i = 0;;) {
        const qsizetype j = data.indexOf(QByteArray("\x00\x00\x01", 3), i);
        if (j < 0)
            break;
        starts.append({int(j > 0 && data[j - 1] == 0 ? j - 1 : j), int(j + 3)});
        i = j + 3;
    }
    QByteArray pending;
    bool key = false;
    for (int n = 0; n < starts.size(); ++n) {
        const int end = n + 1 < starts.size() ? starts[n + 1].first : int(data.size());
        const QByteArray nal = data.mid(starts[n].first, end - starts[n].first);
        const int type = quint8(data[starts[n].second]) & 0x1F;
        if (type == 7 || type == 8) {
            s.config += nal;
            continue;
        }
        pending += nal;
        if (type == 1 || type == 5) {   // a slice ends the access unit (one slice per frame)
            key = key || type == 5;
            s.packets.append({pending, key});
            pending.clear();
            key = false;
        }
    }
    return s;
}

// ── Fake phone server ───────────────────────────────────────────────────

struct Step {
    enum Kind { Config, Frame, Raw, Close } kind;
    QByteArray data;
    bool key = false;
};

QList<Step> script(const Stream &s)
{
    QList<Step> out{{Step::Config, s.config}};
    for (const auto &p : s.packets)
        out.append({Step::Frame, p.first, p.second});
    return out;
}

QByteArray packet(const QByteArray &data, quint64 ptsAndFlags)
{
    QByteArray h(12, 0);
    qToBigEndian<quint64>(ptsAndFlags, h.data());
    qToBigEndian<quint32>(quint32(data.size()), h.data() + 8);
    return h + data;
}

// Speaks the scrcpy 3.x tunnel_forward protocol on a local port
// (phone_server DesktopConnection / Streamer; docs/PHONE_MIRROR_NATIVE_PLAN.md):
// 1st connection = video (a dummy byte at once), 2nd = control, then on video
// a 64-byte device name, codec meta and the packets. Point the client at it
// with OCTAVE_SCRCPY_PORT.
class FakeServer
{
public:
    FakeServer(QList<Step> steps, int w, int h, bool handshakeClose = false)
    {
        std::promise<int> portPromise;
        auto portFuture = portPromise.get_future();
        m_thread = std::thread([this, steps, w, h, handshakeClose, p = std::move(portPromise)]() mutable {
            run(steps, w, h, handshakeClose, p);
        });
        m_port = portFuture.get();
    }
    ~FakeServer()
    {
        m_stop = true;
        m_thread.join();
    }
    int port() const { return m_port; }
    QByteArray controlBytes() const
    {
        QMutexLocker l(&m_lock);
        return m_control;
    }

private:
    void run(const QList<Step> &steps, int w, int h, bool handshakeClose, std::promise<int> &portPromise)
    {
        QTcpServer srv;
        srv.listen(QHostAddress::LocalHost, 0);
        portPromise.set_value(srv.serverPort());
        auto accept = [&]() -> QTcpSocket * {
            while (!m_stop) {
                if (srv.waitForNewConnection(50))
                    return srv.nextPendingConnection();
            }
            return nullptr;
        };
        std::unique_ptr<QTcpSocket> video(accept());
        if (!video)
            return;
        video->write(QByteArray(1, '\0'));   // dummy byte
        video->waitForBytesWritten(2000);
        if (handshakeClose)
            return;
        std::unique_ptr<QTcpSocket> control(accept());
        if (!control)
            return;
        QByteArray meta = QByteArray("Fake Phone").leftJustified(64, '\0', true);
        QByteArray codec(12, 0);
        qToBigEndian<quint32>(0x68323634, codec.data());   // "h264"
        qToBigEndian<quint32>(quint32(w), codec.data() + 4);
        qToBigEndian<quint32>(quint32(h), codec.data() + 8);
        video->write(meta + codec);
        quint64 pts = 0;
        for (const Step &step : steps) {
            if (m_stop)
                return;
            switch (step.kind) {
            case Step::Config: video->write(packet(step.data, quint64(1) << 63)); break;
            case Step::Frame:
                pts += 16666;
                video->write(packet(step.data, pts | (step.key ? quint64(1) << 62 : 0)));
                break;
            case Step::Raw: video->write(step.data); break;
            case Step::Close: return;   // both sockets close with the unique_ptrs
            }
            video->waitForBytesWritten(2000);
            pump(control.get(), 5);
        }
        while (!m_stop)
            pump(control.get(), 50);
    }

    void pump(QTcpSocket *control, int ms)
    {
        if (control->waitForReadyRead(ms)) {
            QMutexLocker l(&m_lock);
            m_control += control->readAll();
        }
    }

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    int m_port = 0;
    mutable QMutex m_lock;
    QByteArray m_control;
};

bool waitUntil(const std::function<bool()> &cond, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QThread::msleep(20);
    return cond();
}

QByteArray shortStr(const QString &s)
{
    const QByteArray raw = s.toUtf8();
    QByteArray out(2, 0);
    qToBigEndian<quint16>(quint16(raw.size()), out.data());
    return out + raw;
}
} // namespace

class TstScrcpyClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        qputenv("FAKE_ADB_LOG", m_tmp.filePath("adb.log").toUtf8());
    }

    void init()
    {
        QFile::remove(m_tmp.filePath("adb.log"));
        qunsetenv("FAKE_ADB_SLOW");
        qunsetenv("FAKE_ADB_SHELL_PID");
        qunsetenv("OCTAVE_SCRCPY_PORT");
    }

#ifdef Q_OS_UNIX
    // ── Device messages and media commands over a socket pair ───────────
    void parsesMediaMessages()
    {
        ScrcpyClient client(kFakeAdb, "/nonexistent.jar");
        QList<QVariantMap> states;
        QList<QByteArray> arts;
        connect(&client, &ScrcpyClient::mediaStateChanged, this, [&](const QVariantMap &s) { states << s; },
                Qt::DirectConnection);
        connect(&client, &ScrcpyClient::mediaArtChanged, this, [&](const QByteArray &a) { arts << a; },
                Qt::DirectConnection);
        int fds[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        QByteArray msg;
        msg += char(101);                     // media state
        msg += char(1 | 2 | 4);               // has session, playing, can seek
        QByteArray times(16, 0);
        qToBigEndian<qint64>(1000, times.data());
        qToBigEndian<qint64>(200000, times.data() + 8);
        msg += times + shortStr("com.spotify.music") + shortStr(QStringLiteral("Dummy ✓")) + shortStr("Artist")
               + shortStr("Album");
        msg += char(102) + QByteArray("\x00\x00\x00\x03\xff\xd8\xff", 7);   // art, 3 bytes
        msg += char(102) + QByteArray(4, '\0');                              // no art
        QCOMPARE(::write(fds[0], msg.constData(), msg.size()), ssize_t(msg.size()));
        ::close(fds[0]);
        client.deviceMessageLoop(fds[1]);   // returns at end of stream
        ::close(fds[1]);

        QCOMPARE(states.size(), 1);
        const QVariantMap expected{{"has_session", true}, {"playing", true}, {"can_seek", true},
                                   {"position_ms", qint64(1000)}, {"duration_ms", qint64(200000)},
                                   {"package", "com.spotify.music"}, {"title", QStringLiteral("Dummy ✓")},
                                   {"artist", "Artist"}, {"album", "Album"}};
        QCOMPARE(states.first(), expected);
        QCOMPARE(arts, (QList<QByteArray>{QByteArray("\xff\xd8\xff"), QByteArray()}));
    }

    void mediaCommandWireFormat()
    {
        ScrcpyClient client(kFakeAdb, "/nonexistent.jar");
        int fds[2];
        QVERIFY(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        client.m_controlFd = fds[0];
        client.mediaCommand(ScrcpyClient::MediaSeek, 61000);
        client.m_controlFd = -1;
        char buf[32];
        const ssize_t n = ::recv(fds[1], buf, sizeof buf, MSG_DONTWAIT);
        ::close(fds[0]);
        ::close(fds[1]);
        QByteArray expected(10, 0);
        expected[0] = char(103);
        expected[1] = char(ScrcpyClient::MediaSeek);
        qToBigEndian<qint64>(61000, expected.data() + 2);
        QCOMPARE(QByteArray(buf, int(n)), expected);
    }

    // ── Stop while starting ─────────────────────────────────────────────
    void stopDuringPushLeavesNoForward()
    {
        requireDecoder();
        qputenv("FAKE_ADB_SLOW", "push");   // stop() lands before the forward exists
        ScrcpyClient client(kFakeAdb, "/nonexistent.jar");
        QVERIFY(client.start("SERIAL"));
        QThread::msleep(100);
        client.stop();
        QVERIFY(forwardsBalanced());
        QCOMPARE(client.m_port, 0);
    }

    void stopDuringServerStartEndsTheAdbShell()
    {
        requireDecoder();
        qputenv("FAKE_ADB_SLOW", "shell");
        qputenv("FAKE_ADB_SHELL_PID", m_tmp.filePath("shell.pid").toUtf8());
        QFile::remove(m_tmp.filePath("shell.pid"));
        ScrcpyClient client(kFakeAdb, "/nonexistent.jar");
        QVERIFY(client.start("SERIAL"));
        QVERIFY(waitUntil([this] { return adbCalls().filter("app_process").size() == 1; }));
        client.stop();
        QThread::msleep(800);   // past the shell's start-up delay
        QFile pidFile(m_tmp.filePath("shell.pid"));
        if (pidFile.open(QIODevice::ReadOnly)) {
            const int pid = pidFile.readAll().trimmed().toInt();
            QVERIFY2(pid <= 0 || ::kill(pid, 0) != 0, "adb shell left running after stop()");
        }
        QVERIFY(forwardsBalanced());
    }

    // ── Against the fake server ─────────────────────────────────────────
    void handshakeAndFrames()
    {
        requireDecoder();
        const Stream first = loadStream("grey_64x48.h264");
        QVERIFY(!first.packets.isEmpty());
        FakeServer srv(script(first), kW, kH);
        Session c(srv.port());
        QVERIFY2(waitUntil([&] { return c.client.frameCount() >= quint64(first.packets.size() - 1); }),
                 qPrintable(QString::number(c.client.frameCount())));
        QVERIFY(c.client.isRunning());
        QCOMPARE(c.client.frameWidth(), kW);
        QCOMPARE(c.client.frameHeight(), kH);
        QVERIFY(c.reasons().isEmpty());
        // The control socket is live: a touch reaches the server as 32 bytes
        c.client.injectTouch(0, ScrcpyClient::ActionDown, 10, 20);
        QVERIFY(waitUntil([&] { return srv.controlBytes().size() >= 32; }));
        QCOMPARE(int(quint8(srv.controlBytes().at(0))), 2);   // inject touch event
    }

    void attachFollowsTheRunningServer()
    {
        // A persisted server (link dropped, virtual display kept): the client
        // reconnects to its socket and follows its logcat, with no push and no
        // second app_process
        requireDecoder();
        const Stream first = loadStream("grey_64x48.h264");
        FakeServer srv(script(first), kW, kH);
        Session c(srv.port(), "2cac176c");
        QVERIFY(c.started);
        QVERIFY2(waitUntil([&] { return c.client.frameCount() >= quint64(first.packets.size() - 1); }),
                 qPrintable(QString::number(c.client.frameCount())));
        QCOMPARE(c.client.scid(), QStringLiteral("2cac176c"));
        QVERIFY(c.reasons().isEmpty());
        const QString calls = adbCalls().join('\n');
        QVERIFY2(calls.contains("logcat"), qPrintable(calls));
        QVERIFY2(!calls.contains("push") && !calls.contains("app_process"), qPrintable(calls));
    }

    void attachToAGoneServerFailsWithinTheDeadline()
    {
        // The persisted server died with the link: give up after the 4 s attach
        // deadline (the manager then starts a fresh session), not the 20 s one
        requireDecoder();
        int port = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            port = probe.serverPort();
        }   // closed: nothing listens there now
        QElapsedTimer t;
        t.start();
        Session c(port, "2cac176c");
        QVERIFY2(waitUntil([&] { return !c.reasons().isEmpty(); }, 10000), "no failure reported");
        QVERIFY2(t.elapsed() < 8000, qPrintable(QString::number(t.elapsed())));
        QVERIFY2(c.reasons().first().contains("could not attach"), qPrintable(c.reasons().first()));
    }

    void midStreamSpsChangeResizes()
    {
        requireDecoder();
        const Stream first = loadStream("grey_64x48.h264"), second = loadStream("grey_96x64.h264");
        FakeServer srv(script(first) + script(second), kW, kH);
        Session c(srv.port());
        QVERIFY(waitUntil([&] { return c.client.frameWidth() == kW2 && c.client.frameHeight() == kH2; }));
        QVERIFY(c.sizes().contains(QSize(kW2, kH2)));
        QVERIFY(waitUntil([&] {
            return c.client.frameCount() >= quint64(first.packets.size() + second.packets.size() - 2);
        }));
        QVERIFY(c.client.isRunning());
        QVERIFY(c.reasons().isEmpty());
    }

    void corruptPacketIsSkipped()
    {
        requireDecoder();
        const Stream first = loadStream("grey_64x48.h264"), second = loadStream("grey_96x64.h264");
        QByteArray garbage("\x00\x00\x00\x01\x65", 5);   // an IDR slice header followed by noise
        for (int i = 0; i < 4; ++i)
            for (int b = 0; b < 256; ++b)
                garbage += char(b);
        FakeServer srv(script(first) + QList<Step>{{Step::Frame, garbage, true}} + script(second), kW, kH);
        Session c(srv.port());
        QVERIFY2(waitUntil([&] { return c.client.frameWidth() == kW2; }), qPrintable(c.reasons().join("; ")));
        QVERIFY(c.client.isRunning());
        QVERIFY(c.reasons().isEmpty());
    }

    void outOfSyncHeaderEndsSession()
    {
        requireDecoder();
        QByteArray bad(12, 0);
        qToBigEndian<quint32>(32u * 1024 * 1024 + 1, bad.data() + 8);   // over the packet size limit
        FakeServer srv(script(loadStream("grey_64x48.h264")) + QList<Step>{{Step::Raw, bad}}, kW, kH);
        Session c(srv.port());
        QVERIFY2(waitUntil([&] { return !c.reasons().isEmpty(); }), "no disconnect");
        QVERIFY2(c.reasons().first().contains("out of sync"), qPrintable(c.reasons().first()));
    }

    void serverExitMidStreamReportsDisconnect()
    {
        requireDecoder();
        FakeServer srv(script(loadStream("grey_64x48.h264")) + QList<Step>{{Step::Close, {}}}, kW, kH);
        Session c(srv.port());
        QVERIFY2(waitUntil([&] { return !c.reasons().isEmpty(); }), "no disconnect");
        QCOMPARE(c.reasons(), QStringList{ScrcpyClient::disconnectedMessage("SERIAL")});
        QVERIFY(!c.client.isRunning());
    }

    void serverExitDuringHandshake()
    {
        requireDecoder();
        FakeServer srv({}, kW, kH, true);
        Session c(srv.port());
        QVERIFY2(waitUntil([&] { return !c.reasons().isEmpty(); }, 25000), "no disconnect");
        QVERIFY(!c.client.isRunning());
        QCOMPARE(c.client.frameCount(), quint64(0));
    }
#endif

private:
    // A client started against the fake server, recording what its session
    // thread emits (direct connections: no event loop runs here)
    struct Session {
        explicit Session(int port, const QString &attachScid = QString())
            : client(kFakeAdb, "/nonexistent.jar")
        {
            qputenv("OCTAVE_SCRCPY_PORT", QByteArray::number(port));
            QObject::connect(&client, &ScrcpyClient::disconnected, &client, [this](const QString &r) {
                QMutexLocker l(&lock);
                m_reasons << r;
            }, Qt::DirectConnection);
            QObject::connect(&client, &ScrcpyClient::frameSizeChanged, &client, [this](int w, int h) {
                QMutexLocker l(&lock);
                m_sizes << QSize(w, h);
            }, Qt::DirectConnection);
            started = client.start("SERIAL", attachScid.isEmpty() ? QString() : QStringLiteral("1280x720/160"),
                                   60, 8000000, false, true, attachScid);
        }
        ~Session() { client.stop(); }
        QStringList reasons() const { QMutexLocker l(&lock); return m_reasons; }
        QList<QSize> sizes() const { QMutexLocker l(&lock); return m_sizes; }

        ScrcpyClient client;
        bool started = false;
        mutable QMutex lock;
        QStringList m_reasons;
        QList<QSize> m_sizes;
    };

    void requireDecoder()
    {
#ifndef OCTAVE_HAVE_FFMPEG
        QSKIP("built without libavcodec: the client never starts a session");
#endif
    }

    QStringList adbCalls() const
    {
        QFile f(m_tmp.filePath("adb.log"));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }

    // Every "forward tcp:N ..." the session made was removed again
    bool forwardsBalanced() const
    {
        QSet<QString> created, removed;
        for (const QString &line : adbCalls()) {
            const QStringList a = line.split(' ', Qt::SkipEmptyParts);
            const int i = a.indexOf("forward");
            if (i < 0 || i + 1 >= a.size())
                continue;
            if (a[i + 1] == "--remove" && i + 2 < a.size())
                removed.insert(a[i + 2]);
            else if (a[i + 1].startsWith("tcp:"))
                created.insert(a[i + 1]);
        }
        const bool ok = (created - removed).isEmpty();
        if (!ok)
            qWarning() << "stranded forwards:" << (created - removed);
        return ok;
    }

    QTemporaryDir m_tmp;
};

QTEST_GUILESS_MAIN(TstScrcpyClient)
#include "tst_scrcpyclient.moc"
