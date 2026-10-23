// The command server's WebSocket side (src/service/websocket.{h,cpp}): the
// handshake (RFC 6455's own example key), frames both ways, fragmentation,
// limits, the Origin rule that keeps websites off it, and a real browser-
// shaped client against a running CommandServer, plus the page at GET /.

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "service/commandserver.h"
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
        QVERIFY(server.start());
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());

        QByteArray reply = once(server.port(), "GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.endsWith("<html>client</html>"), reply.constData());
        reply = once(server.port(), "GET /secret HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
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
        // A Host header that is a name, or tries to add a directive, is not echoed
        reply = once(server.port(), "GET / HTTP/1.1\r\nHost: evil.example; script-src *\r\n\r\n");
        QVERIFY2(reply.contains("connect-src 'self';") && !reply.contains("evil"), reply.constData());
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
        QVERIFY(server.start());
        const QByteArray host = "127.0.0.1:" + QByteArray::number(server.port());
        QByteArray reply = once(server.port(), "GET /art?rev=7 HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        QVERIFY2(reply.startsWith("HTTP/1.1 200") && reply.contains("Content-Type: image/png") && reply.endsWith("\x89PNG"),
                 reply.constData());
        QVERIFY(reply.contains("X-Content-Type-Options: nosniff"));
        QCOMPARE(calls, 1);
        QCOMPARE(seen, QByteArray("/art?rev=7"));   // the handler reads its own query
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

private:
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
