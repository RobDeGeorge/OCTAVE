// The command server's WebSocket side (src/service/websocket.{h,cpp}): the
// handshake (RFC 6455's own example key), frames both ways, fragmentation,
// limits, the Origin rule that keeps websites off it, and a real browser-
// shaped client against a running CommandServer, plus the page at GET /.

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QTest>

#include <memory>

#include "service/commandserver.h"
#include "service/staticfiles.h"
#include "service/websocket.h"

using namespace OctaveService;
namespace Ws = OctaveService::WebSocket;

namespace {
// A client frame: masked, as browsers send them
QByteArray clientFrame(Ws::Opcode op, const QByteArray &payload, bool fin = true)
{
    const QByteArray mask = QByteArray::fromHex("37fa213d");
    QByteArray f;
    f.append(char((fin ? 0x80 : 0) | op));
    if (payload.size() < 126) {
        f.append(char(0x80 | payload.size()));
    } else {
        f.append(char(0x80 | 126));
        f.append(char(payload.size() >> 8));
        f.append(char(payload.size() & 0xFF));
    }
    f += mask;
    for (int i = 0; i < payload.size(); ++i)
        f.append(char(payload.at(i) ^ mask.at(i % 4)));
    return f;
}

Ws::HttpRequest upgrade(const QByteArray &host, const QByteArray &origin = {})
{
    QByteArray head = "GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
    if (!origin.isEmpty())
        head += "Origin: " + origin + "\r\n";
    return Ws::parseRequest(head + "\r\n", nullptr);
}
} // namespace

class TstWebSocket : public QObject
{
    Q_OBJECT

private slots:
    void handshake()
    {
        const Ws::HttpRequest r = upgrade("127.0.0.1:9876");
        QVERIFY(r.valid);
        QVERIFY(Ws::isUpgrade(r));
        // RFC 6455 section 1.3
        QVERIFY(Ws::acceptResponse(r).contains("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"));
        QVERIFY(!Ws::parseRequest("GET / HTTP/1.1\r\nHost: x\r\n", nullptr).valid);   // incomplete
    }

    void originRule_data()
    {
        QTest::addColumn<QByteArray>("host");
        QTest::addColumn<QByteArray>("origin");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("no origin: a script") << QByteArray("127.0.0.1:9876") << QByteArray() << true;
        QTest::newRow("own page") << QByteArray("127.0.0.1:9876") << QByteArray("http://127.0.0.1:9876") << true;
        QTest::newRow("localhost") << QByteArray("localhost:9876") << QByteArray("http://localhost:9876") << true;
        QTest::newRow("ipv6") << QByteArray("[::1]:9876") << QByteArray("http://[::1]:9876") << true;
        QTest::newRow("lan address") << QByteArray("192.168.1.20:9876") << QByteArray("http://192.168.1.20:9876") << true;
        QTest::newRow("another site") << QByteArray("127.0.0.1:9876") << QByteArray("https://example.com") << false;
        QTest::newRow("another port") << QByteArray("127.0.0.1:9876") << QByteArray("http://127.0.0.1:8080") << false;
        QTest::newRow("file page / sandbox") << QByteArray("127.0.0.1:9876") << QByteArray("null") << false;
        QTest::newRow("dns rebinding") << QByteArray("evil.example:9876") << QByteArray("http://evil.example:9876") << false;
    }

    void originRule()
    {
        QFETCH(QByteArray, host);
        QFETCH(QByteArray, origin);
        QFETCH(bool, allowed);
        const QString why = Ws::refusal(upgrade(host, origin));
        QVERIFY2(why.isEmpty() == allowed, qPrintable(why));
    }

    void decodesFrames()
    {
        Ws::Decoder d;
        Ws::Decoder::Message m;
        const QByteArray frame = clientFrame(Ws::Text, R"({"op":"hello"})");
        d.feed(frame.left(3));   // split anywhere
        QVERIFY(!d.next(m));
        d.feed(frame.mid(3));
        QVERIFY(d.next(m));
        QCOMPARE(m.opcode, Ws::Text);
        QCOMPARE(m.payload, QByteArray(R"({"op":"hello"})"));

        // Fragmented, with a ping between the pieces
        d.feed(clientFrame(Ws::Text, "ab", false) + clientFrame(Ws::Ping, "p") + clientFrame(Ws::Continuation, "cd"));
        QVERIFY(d.next(m));
        QCOMPARE(m.opcode, Ws::Ping);
        QVERIFY(d.next(m));
        QCOMPARE(m.payload, QByteArray("abcd"));

        // 126-length form
        const QByteArray big(300, 'x');
        d.feed(clientFrame(Ws::Text, big));
        QVERIFY(d.next(m));
        QCOMPARE(m.payload, big);
        QVERIFY(d.error().isEmpty());
    }

    void refusesBadFrames()
    {
        Ws::Decoder unmasked;
        Ws::Decoder::Message m;
        unmasked.feed(Ws::encodeFrame(Ws::Text, "x"));   // a server frame has no mask
        QVERIFY(!unmasked.next(m));
        QVERIFY(!unmasked.error().isEmpty());

        Ws::Decoder huge;
        QByteArray head;
        head.append(char(0x81));
        head.append(char(0x80 | 127));
        for (int i = 0; i < 8; ++i)
            head.append(char(i == 4 ? 0x10 : 0));   // 2^32 bytes
        huge.feed(head + QByteArray(4, 0));
        QVERIFY(!huge.next(m));
        QVERIFY(huge.error().contains("over"));
    }

    void encodesServerFrames()
    {
        QCOMPARE(Ws::encodeFrame(Ws::Text, "hi"), QByteArray::fromHex("8102") + "hi");
        const QByteArray f = Ws::encodeFrame(Ws::Text, QByteArray(70000, 'y'));
        QCOMPARE(quint8(f.at(1)), quint8(127));
        QCOMPARE(f.size(), 10 + 70000);
    }

    // ── against a running server ───────────────────────────────────────

    void browserClientRoundTrip()
    {
        CommandServer server(0);
        server.setPage("<html>client</html>");
        QVERIFY(server.start());
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());

        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(s.waitForConnected(3000));
        s.write("GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                "Origin: http://" + host + "\r\n\r\n");
        QByteArray in = readUntil(s, "\r\n\r\n");
        QVERIFY2(in.startsWith("HTTP/1.1 101"), in.constData());
        in = in.mid(in.indexOf("\r\n\r\n") + 4);

        s.write(clientFrame(Ws::Text, R"({"action": "ping"})"));
        s.write(clientFrame(Ws::Ping, "still there"));
        QByteArray frames = in + readFor(s, 2);
        // Text reply: no trailing newline inside the frame
        QCOMPARE(quint8(frames.at(0)), quint8(0x81));
        const int len = quint8(frames.at(1));
        QCOMPARE(QJsonDocument::fromJson(frames.mid(2, len)).object().value("result").toString(), QStringLiteral("pong"));
        frames = frames.mid(2 + len);
        QCOMPARE(quint8(frames.at(0)), quint8(0x8A));   // pong
        QCOMPARE(frames.mid(2, quint8(frames.at(1))), QByteArray("still there"));

        s.write(clientFrame(Ws::Close, QByteArray::fromHex("03e8")));
        const QByteArray closing = readFor(s, 1);
        QCOMPARE(quint8(closing.at(0)), quint8(0x88));
        server.stop();
    }

    void servesThePageAndRefusesOtherSites()
    {
        CommandServer server(0);
        server.setPage("<html>client</html>");
        server.addPage("/dashboards", "<html>editor</html>");
        QVERIFY(server.start());
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());

        QByteArray reply = once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.endsWith("<html>client</html>"), reply.constData());
        QVERIFY(reply.contains("connect-src 'self' ws: wss:"));
        QVERIFY(!reply.contains("img-src"));
        reply = once(server.port(), "GET /secret HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY(reply.startsWith("HTTP/1.1 404"));
        // Another page (the dashboard editor), with the query ignored; data:
        // images allowed there (thumbnails)
        reply = once(server.port(), "GET /dashboards?x=1 HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.endsWith("<html>editor</html>"), reply.constData());
        QVERIFY(reply.contains("img-src 'self' data:"));
        reply = once(server.port(), "GET /dashboards/x HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY(reply.startsWith("HTTP/1.1 404"));
        reply = once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: https://example.com\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 403"), reply.constData());
        // The JSON-lines protocol is unchanged on the same port
        reply = once(server.port(), "{\"action\": \"ping\"}\n");
        QCOMPARE(reply.trimmed(), QByteArray(R"({"ok":true,"result":"pong"})"));
        server.stop();
    }

    void servesAWebRootSameOrigin()
    {
        QTemporaryDir tmp;
        QDir(tmp.path()).mkpath("assets");
        auto write = [&tmp](const char *rel, const QByteArray &data) {
            QFile f(QDir(tmp.path()).filePath(QString::fromLatin1(rel)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write("index.html", "<html>frontend</html>");
        write("assets/app.js", "go()");
        CommandServer server(0);
        server.setPage("<html>client</html>");
        server.setWebRoot(tmp.path());   // wins over the page
        QVERIFY(server.start());
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());

        QByteArray reply = once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.endsWith("<html>frontend</html>"), reply.constData());
        QVERIFY(reply.contains("X-Content-Type-Options: nosniff"));
        QVERIFY(reply.contains("Cache-Control: no-store"));
        // Its own scripts, no inline script, sockets to this service only
        QVERIFY2(reply.contains("script-src 'self' 'wasm-unsafe-eval';"), reply.constData());
        QVERIFY2(reply.contains("connect-src 'self' ws://" + host + " wss://" + host + ";"), reply.constData());
        QVERIFY(!reply.contains("ws: wss:"));
        // A module script fetch carries the page's Origin: same origin passes
        reply = once(server.port(), "GET /assets/app.js HTTP/1.1\r\nHost: " + host + "\r\nOrigin: http://" + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.contains("Content-Type: text/javascript"), reply.constData());
        reply = once(server.port(), "GET /assets/app.js HTTP/1.1\r\nHost: " + host + "\r\nOrigin: https://example.com\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 403"), reply.constData());
        reply = once(server.port(), "GET /../../etc/passwd HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 403"), reply.constData());
        // Only GET is HTTP on this port: anything else is a JSON line, never a file
        reply = once(server.port(), "POST / HTTP/1.1\r\nHost: " + host + "\r\nContent-Length: 0\r\n\r\n");
        QVERIFY2(!reply.contains("frontend"), reply.constData());
        // A Host header that is a name (or tries to add a directive) is
        // refused before anything is served
        reply = once(server.port(), "GET / HTTP/1.1\r\nHost: evil.example; script-src *\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 403"), reply.constData());
        // The socket still upgrades on the same port
        reply = once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://" + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 101"), reply.constData());
        server.stop();
    }

    void routesAndCrossSiteLoads()
    {
        CommandServer server(0);
        server.setPage("<html>client</html>");
        int calls = 0;
        QByteArray seen;
        server.addRoute("/art", [&calls, &seen](const QByteArray &target) {
            ++calls;
            seen = target;
            return StaticFiles::Reply{200, "OK", "image/png", "\x89PNG"};
        });
        server.addRoute("/export/x", [](const QByteArray &) {
            StaticFiles::Reply r{200, "OK", "text/csv", "a,b\n"};
            r.headers = "Content-Disposition: attachment; filename=\"x.csv\"\r\n";
            return r;
        });
        QVERIFY(server.start());
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());
        QByteArray reply = once(server.port(), "GET /art?rev=7 HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.contains("Content-Type: image/png") && reply.endsWith("\x89PNG"),
                 reply.constData());
        QVERIFY(reply.contains("X-Content-Type-Options: nosniff"));
        QCOMPARE(calls, 1);
        QCOMPARE(seen, QByteArray("/art?rev=7"));   // the handler reads its own query
        reply = once(server.port(), "GET /export/x HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.contains("\r\nContent-Disposition: attachment; filename=\"x.csv\"\r\nConnection: close\r\n\r\na,b\n"),
                 reply.constData());
        // Exact paths only
        QVERIFY(once(server.port(), "GET /art/x HTTP/1.1\r\nHost: " + host + "\r\n\r\n").startsWith("HTTP/1.1 404"));
        // An <img> on another site sends no Origin; its Sec-Fetch-Site gives it away
        reply = once(server.port(), "GET /art HTTP/1.1\r\nHost: " + host + "\r\nSec-Fetch-Site: cross-site\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 403"), reply.constData());
        reply = once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\nSec-Fetch-Site: same-site\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 403"), reply.constData());
        QCOMPARE(calls, 1);
        // The page's own loads and the address bar pass
        QVERIFY(once(server.port(), "GET /art HTTP/1.1\r\nHost: " + host + "\r\nSec-Fetch-Site: same-origin\r\n\r\n").startsWith("HTTP/1.1 200"));
        QVERIFY(once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\nSec-Fetch-Site: none\r\n\r\n").startsWith("HTTP/1.1 200"));
        server.stop();
    }

    void everyRequestNeedsAnAddressInHost()
    {
        // M6: DNS rebinding sends no Origin on a plain GET; the Host says it
        CommandServer server(0);
        server.setPage("<html>client</html>");
        server.addRoute("/art", [](const QByteArray &) { return StaticFiles::Reply{200, "OK", "image/png", "img"}; });
        QVERIFY(server.start());
        const QByteArray port = QByteArray::number(server.port());
        for (const QByteArray &host : {QByteArray("rebind.example.com:") + port, QByteArray("octave.local"), QByteArray()}) {
            const QByteArray head = host.isEmpty() ? QByteArray() : "Host: " + host + "\r\n";
            for (const char *path : {"/", "/art"}) {
                const QByteArray r = once(server.port(), "GET " + QByteArray(path) + " HTTP/1.1\r\n" + head + "\r\n");
                QVERIFY2(r.startsWith("HTTP/1.1 403"), r.constData());
            }
        }
        for (const QByteArray &host : {"127.0.0.1:" + port, "localhost:" + port, "[::1]:" + port, QByteArray("192.168.1.20")})
            QVERIFY(once(server.port(), "GET /art HTTP/1.1\r\nHost: " + host + "\r\n\r\n").startsWith("HTTP/1.1 200"));
        server.stop();
    }

    void oneClientCannotExhaustTheServer()
    {
        CommandServer server(0);
        CommandServer::Limits limits;
        limits.maxConnections = 4;
        limits.firstByteMs = 300;
        limits.maxLineBytes = 4096;
        server.setLimits(limits);
        // Everyone untrusted, for the idle rule
        server.setTrust([](const Connection &) { return false; }, {}, {QStringLiteral("hello")});
        limits.untrustedIdleMs = 400;
        server.setLimits(limits);
        QVERIFY(server.start());

        // M3: a line that never ends is cut off at the cap
        {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, server.port());
            QVERIFY(s.waitForConnected(3000));
            s.write("{\"op\": \"" + QByteArray(8192, 'a'));
            QByteArray in;
            QElapsedTimer t;
            t.start();
            while (s.state() == QAbstractSocket::ConnectedState && t.elapsed() < 3000)
                if (s.waitForReadyRead(50))
                    in += s.readAll();
            QVERIFY2(in.contains("request too long"), in.constData());
            QVERIFY(s.state() != QAbstractSocket::ConnectedState || !s.waitForReadyRead(500));
        }
        // M4: a connection that says nothing is closed
        {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, server.port());
            QVERIFY(s.waitForConnected(3000));
            QTRY_VERIFY_WITH_TIMEOUT((s.waitForReadyRead(50), s.state() == QAbstractSocket::UnconnectedState), 3000);
        }
        // M4: an untrusted client that goes quiet is closed
        {
            QTcpSocket s;
            s.connectToHost(QHostAddress::LocalHost, server.port());
            QVERIFY(s.waitForConnected(3000));
            s.write("{\"op\": \"hello\"}\n");
            QTRY_VERIFY_WITH_TIMEOUT((s.waitForReadyRead(50), s.state() == QAbstractSocket::UnconnectedState), 3000);
        }
        // M4: past maxConnections, refused at the door
        {
            QList<QTcpSocket *> held;
            for (int i = 0; i < 4; ++i) {
                auto *s = new QTcpSocket;
                s->connectToHost(QHostAddress::LocalHost, server.port());
                QVERIFY(s->waitForConnected(3000));
                s->write("{\"op\": \"hello\"}\n");   // keep each busy past the first-byte rule
                held.append(s);
            }
            QTest::qWait(100);
            QTcpSocket extra;
            extra.connectToHost(QHostAddress::LocalHost, server.port());
            QVERIFY(extra.waitForConnected(3000));
            extra.write("{\"op\": \"hello\"}\n");
            QTRY_VERIFY_WITH_TIMEOUT((extra.waitForReadyRead(50), extra.state() == QAbstractSocket::UnconnectedState), 2000);
            QVERIFY(!extra.readAll().contains("\"ok\""));
            qDeleteAll(held);
        }
        server.stop();
    }

    // Step 7's limits hold for WebSocket clients too (the line protocol's
    // tests are in tst_servicebridge)
    void rateLimitedOverFrames()
    {
        CommandServer server(0);
        server.registerOp(QStringLiteral("echo"), [](const QVariantMap &) { return QVariant(1); });
        CommandServer::Limits limits;
        limits.opsPerSecond = 20;
        limits.opsBurst = 10;
        server.setLimits(limits);
        QVERIFY(server.start());
        QTcpSocket s;
        QByteArray in;
        QVERIFY(wsConnect(s, server.port(), in));
        QByteArray burst;
        for (int i = 1; i <= 50; ++i)
            burst += clientFrame(Ws::Text, R"({"op": "echo", "id": )" + QByteArray::number(i) + "}");
        s.write(burst);
        QList<QByteArray> texts;
        QElapsedTimer t;
        t.start();
        while (texts.size() < 50 && t.elapsed() < 5000) {
            if (s.bytesAvailable() > 0 || s.waitForReadyRead(20))
                in += s.readAll();
            texts += takeTextFrames(in);
        }
        QCOMPARE(texts.size(), 50);
        int ok = 0;
        for (const QByteArray &text : std::as_const(texts)) {
            const QJsonObject r = QJsonDocument::fromJson(text).object();
            if (r.value("ok").toBool()) {
                ++ok;
                continue;
            }
            QCOMPARE(r.value("code").toString(), QStringLiteral("rateLimited"));
            QVERIFY(r.value("retryAfterMs").toInt() > 0);
            QVERIFY(r.value("id").toInt() > 0);
        }
        QVERIFY2(ok >= 10 && ok < 30, qPrintable(QString::number(ok)));
        server.stop();
    }

    void aWebSocketConnectLoopIsRefused()
    {
        // Upgrades count against the same per-address bucket as everything
        CommandServer server(0);
        CommandServer::Limits limits;
        limits.loopbackConnectsPerSecond = 3;
        limits.loopbackConnectsBurst = 3;
        server.setLimits(limits);
        QVERIFY(server.start());
        int upgraded = 0, refused = 0;
        for (int i = 0; i < 15; ++i) {
            QTcpSocket s;
            QByteArray in;
            wsConnect(s, server.port(), in) ? ++upgraded : ++refused;
        }
        QVERIFY2(upgraded >= 3 && upgraded < 8, qPrintable(QStringLiteral("%1 upgraded").arg(upgraded)));
        QVERIFY(refused > 0);
        QTest::qWait(700);   // the bucket refills
        QTcpSocket late;
        QByteArray in;
        QVERIFY(wsConnect(late, server.port(), in));
        server.stop();
    }

    void aWebSocketReaderThatStaysBehindIsClosed()
    {
        // At maxUnsentBytes for maxBehindMs the connection goes; another
        // WebSocket client keeps getting its replies meanwhile
        CommandServer server(0);
        std::shared_ptr<Connection> held;
        QMutex heldLock;
        server.registerOp(QStringLiteral("hold"), [&](const QVariantMap &) {
            QMutexLocker l(&heldLock);
            held = CommandServer::currentConnection();
            return QVariant(true);
        });
        server.registerOp(QStringLiteral("echo"), [](const QVariantMap &) { return QVariant(1); });
        CommandServer::Limits limits;
        limits.maxUnsentBytes = 64 * 1024;
        limits.maxBehindMs = 500;
        server.setLimits(limits);
        QVERIFY(server.start());
        QTcpSocket slow, other;
        QByteArray slowIn, otherIn;
        QVERIFY(wsConnect(slow, server.port(), slowIn));
        QVERIFY(wsConnect(other, server.port(), otherIn));
        slow.write(clientFrame(Ws::Text, R"({"op": "hold", "id": 1})"));
        QVERIFY(slow.waitForBytesWritten(3000));
        QTRY_VERIFY_WITH_TIMEOUT((QMutexLocker(&heldLock), held != nullptr), 3000);
        slow.setReadBufferSize(1);   // and from now on reads nothing
        std::shared_ptr<Connection> c;
        {
            QMutexLocker l(&heldLock);
            c = held;
        }
        auto otherEchoes = [&other, &otherIn] {
            other.write(clientFrame(Ws::Text, R"({"op": "echo", "id": 2})"));
            QList<QByteArray> texts;
            QElapsedTimer w;
            w.start();
            while (texts.isEmpty() && w.elapsed() < 3000) {
                if (other.bytesAvailable() > 0 || other.waitForReadyRead(20))
                    otherIn += other.readAll();
                texts += takeTextFrames(otherIn);
            }
            return texts.size() == 1 && QJsonDocument::fromJson(texts.first()).object().value("ok").toBool();
        };
        const QByteArray event = "{\"event\":\"signal\",\"pad\":\"" + QByteArray(32 * 1024, 'x') + "\"}\n";
        QElapsedTimer t;
        t.start();
        int i = 0;
        while (!c->isClosed() && t.elapsed() < 15000) {
            c->push(event);
            QTest::qWait(2);
            if (++i % 100 == 0)   // the other client, meanwhile
                QVERIFY(otherEchoes());
        }
        QVERIFY2(c->isClosed(), "the WebSocket reader that never reads was never closed");
        QVERIFY(i >= 100);   // the other client was asked at least once while it was behind
        QVERIFY(otherEchoes());
        server.stop();
    }

private:
    // A WebSocket upgrade on `s`; false if the server refused or closed it.
    // Leftover bytes after the 101 head stay in `in`
    static bool wsConnect(QTcpSocket &s, quint16 port, QByteArray &in)
    {
        s.connectToHost(QHostAddress::LocalHost, port);
        if (!s.waitForConnected(3000))
            return false;
        const QByteArray host = "127.0.0.1:" + QByteArray::number(port);
        s.write("GET / HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
        QElapsedTimer t;
        t.start();
        while (!in.contains("\r\n\r\n") && s.state() == QAbstractSocket::ConnectedState && t.elapsed() < 3000)
            if (s.bytesAvailable() > 0 || s.waitForReadyRead(20))
                in += s.readAll();
        in += s.readAll();
        if (!in.startsWith("HTTP/1.1 101") || !in.contains("\r\n\r\n"))
            return false;
        in = in.mid(in.indexOf("\r\n\r\n") + 4);
        return true;
    }

    // The complete text frames at the front of `in` (server frames: unmasked)
    static QList<QByteArray> takeTextFrames(QByteArray &in)
    {
        QList<QByteArray> out;
        while (in.size() >= 2) {
            qint64 n = quint8(in.at(1)) & 0x7F;
            int off = 2;
            if (n == 126) {
                if (in.size() < 4)
                    break;
                n = (quint8(in.at(2)) << 8) | quint8(in.at(3));
                off = 4;
            } else if (n == 127) {
                if (in.size() < 10)
                    break;
                n = qFromBigEndian<quint64>(in.constData() + 2);
                off = 10;
            }
            if (in.size() < off + n)
                break;
            if ((quint8(in.at(0)) & 0x0F) == Ws::Text)
                out.append(in.mid(off, int(n)));
            in.remove(0, off + int(n));
        }
        return out;
    }

    static QByteArray readUntil(QTcpSocket &s, const QByteArray &marker)
    {
        QByteArray in;
        QElapsedTimer t;
        t.start();
        while (!in.contains(marker) && t.elapsed() < 3000)
            if (s.waitForReadyRead(100))
                in += s.readAll();
        return in;
    }

    // Everything that arrives until `frames` frames' worth stopped coming
    static QByteArray readFor(QTcpSocket &s, int frames)
    {
        QByteArray in;
        QElapsedTimer t;
        t.start();
        int seen = 0;
        while (t.elapsed() < 3000) {
            if (s.waitForReadyRead(100))
                in += s.readAll();
            seen = 0;
            for (int i = 0; i + 1 < in.size(); i += 2 + quint8(in.at(i + 1)))
                ++seen;
            if (seen >= frames)
                break;
        }
        return in;
    }

    static QByteArray once(quint16 port, const QByteArray &request)
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        if (!s.waitForConnected(3000))
            return {};
        s.write(request);
        QByteArray in;
        while (s.waitForReadyRead(500))
            in += s.readAll();
        return in;
    }
};

QTEST_GUILESS_MAIN(TstWebSocket)
#include "tst_websocket.moc"
