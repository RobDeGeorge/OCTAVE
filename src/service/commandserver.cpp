#include "commandserver.h"
#include "memberscope.h"
#include "staticfiles.h"
#include "websocket.h"

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMutex>
#include <QPointF>
#include <QRectF>
#include <QSemaphore>
#include <QSizeF>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QVarLengthArray>
#include <QtEndian>

#include <atomic>
#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcCommand, "octave.service")

namespace OctaveService {

namespace {
// The connection a request on this (connection) thread came in on
thread_local std::shared_ptr<Connection> t_connection;
}

// ---------------------------------------------------------------------------
// Listener (own thread) + one thread per client connection
// ---------------------------------------------------------------------------

class CommandTcpServer : public QTcpServer
{
public:
    explicit CommandTcpServer(CommandServer *owner) : m_owner(owner) {}
    ~CommandTcpServer() override { shutdown(); }

    void shutdown()
    {
        m_stopping = true;
        QList<QThread *> threads;
        {
            QMutexLocker l(&m_lock);
            threads.swap(m_clients);
        }
        for (QThread *t : threads) {
            t->wait(2000);
            if (t->isFinished())
                delete t;   // else: still inside a GUI call; leak rather than crash on exit
        }
    }

protected:
    void incomingConnection(qintptr fd) override
    {
        QThread *t = QThread::create([this, fd] { serve(fd); });
        t->setObjectName(QStringLiteral("cmd-client"));
        {
            QMutexLocker l(&m_lock);
            // Reap finished client threads as new ones arrive
            for (auto it = m_clients.begin(); it != m_clients.end();) {
                if ((*it)->isFinished()) {
                    delete *it;
                    it = m_clients.erase(it);
                } else {
                    ++it;
                }
            }
            m_clients.append(t);
        }
        t->start();
    }

private:
    void serve(qintptr fd)
    {
        QTcpSocket sock;
        if (!sock.setSocketDescriptor(fd))
            return;
        qCInfo(lcCommand) << "command client connected:" << sock.peerAddress().toString() << sock.peerPort();
        auto connection = std::make_shared<Connection>();
        connection->setPeer(sock.peerAddress().isLoopback(), sock.peerAddress().toString());
        t_connection = connection;
        m_owner->trackConnection(connection);

        // The first non-blank byte says which protocol: only '{' is the
        // JSON-lines protocol. Anything else is HTTP, which answers GET (a
        // WebSocket upgrade, the page, the routes) and nothing else: a
        // browser's cross-site no-cors POST must never reach the line
        // protocol, whose body line would run as a trusted local op
        QByteArray buf;
        auto firstByte = [&buf]() -> int {
            for (char c : std::as_const(buf))
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                    return uchar(c);
            return -1;
        };
        while (firstByte() < 0 && !m_stopping && sock.state() == QAbstractSocket::ConnectedState) {
            if (sock.bytesAvailable() > 0 || sock.waitForReadyRead(20))
                buf += sock.readAll();
        }
        if (firstByte() == '{')
            serveLines(sock, buf, *connection);
        else if (firstByte() >= 0)
            serveHttp(sock, buf, *connection);

        connection->close();   // its subscriptions go (on the GUI thread)
        t_connection.reset();
    }

    // Wait for more bytes (short waits, so queued events go out within
    // ~20 ms); false once the client is gone or the server stops
    bool pump(QTcpSocket &sock, QByteArray &buf)
    {
        if (m_stopping || sock.state() != QAbstractSocket::ConnectedState)
            return false;
        // Closed from elsewhere (its device revoked): drop the socket
        if (const auto c = t_connection; c && c->isClosed())
            return false;
        // Data that waitForBytesWritten() already pulled in counts too (else
        // a pipelined request would sit there until the next one came)
        if (sock.bytesAvailable() > 0 || sock.waitForReadyRead(20))
            buf += sock.readAll();
        return sock.state() == QAbstractSocket::ConnectedState || sock.bytesAvailable() > 0;
    }

    static void flush(QTcpSocket &sock)
    {
        if (sock.bytesToWrite() > 0)
            sock.waitForBytesWritten(10000);
    }

    void serveLines(QTcpSocket &sock, QByteArray buf, Connection &connection)
    {
        do {
            int nl;
            while ((nl = buf.indexOf('\n')) >= 0) {
                const QByteArray line = buf.left(nl).trimmed();
                buf.remove(0, nl + 1);
                if (line.isEmpty())
                    continue;
                // Every line is a JSON object; anything else (HTTP that got
                // here, garbage) ends the connection after its error reply
                if (!line.startsWith('{')) {
                    sock.write(m_owner->handleLine(line));
                    flush(sock);
                    return;
                }
                sock.write(m_owner->handleLine(line));
            }
            // Events only after the replies: a subscribe's reply (its sub
            // id) always precedes that subscription's first event. Only this
            // thread writes to the socket, so the order holds. A stream
            // record goes out as 0xFF, u32 length, the record: a JSON line
            // never starts with 0xFF
            for (const Connection::Item &item : connection.takeAll()) {
                if (item.binary) {
                    char head[5] = {char(0xFF)};
                    qToBigEndian<quint32>(quint32(item.bytes.size()), head + 1);
                    sock.write(head, 5);
                }
                sock.write(item.bytes);
            }
            flush(sock);
        } while (pump(sock, buf));
    }

    void serveHttp(QTcpSocket &sock, QByteArray buf, Connection &connection)
    {
        namespace Ws = OctaveService::WebSocket;
        constexpr int kMaxHead = 16 * 1024;
        int headLength = 0;
        Ws::HttpRequest request = Ws::parseRequest(buf, &headLength);
        QElapsedTimer waited;
        waited.start();
        while (!request.valid && buf.size() < kMaxHead && waited.elapsed() < 5000 && pump(sock, buf))
            request = Ws::parseRequest(buf, &headLength);
        if (!request.valid) {
            sock.write(Ws::httpResponse(400, "Bad Request", "text/plain", "bad request\n"));
            flush(sock);
            return;
        }
        // GET only (the page, the routes, the WebSocket upgrade)
        if (request.method != "GET") {
            sock.write(Ws::httpResponse(405, "Method Not Allowed", "text/plain", "GET only\n"));
            flush(sock);
            return;
        }
        if (const QString why = Ws::refusal(request); !why.isEmpty()) {
            qCWarning(lcCommand).noquote() << "refused" << sock.peerAddress().toString() << ":" << why;
            sock.write(Ws::httpResponse(403, "Forbidden", "text/plain", why.toUtf8() + '\n'));
            flush(sock);
            return;
        }
        // A page on another site may not load anything from here either:
        // an <img> or <script> sends no Origin, but browsers say where it
        // came from ("none" is the address bar)
        const QByteArray fetchSite = request.headers.value("sec-fetch-site");
        if (!Ws::isUpgrade(request) && !fetchSite.isEmpty() && fetchSite != "same-origin" && fetchSite != "none") {
            qCWarning(lcCommand).noquote() << "refused" << sock.peerAddress().toString() << ": a" << fetchSite << "load";
            sock.write(Ws::httpResponse(403, "Forbidden", "text/plain", "not from this service's page\n"));
            flush(sock);
            return;
        }
        if (!Ws::isUpgrade(request) && request.method == "GET") {
            if (const CommandServer::Route route = m_owner->route(StaticFiles::pathOf(request.path))) {
                if (!m_owner->httpTrusted(connection, request.headers, request.path)) {
                    sock.write(Ws::httpResponse(401, "Unauthorized", "text/plain", "pair first; use a media ticket\n",
                                                Ws::webRootCsp(request.headers.value("host"))));
                    flush(sock);
                    return;
                }
                StaticFiles::Reply r;
                try {
                    r = route(request.path);
                } catch (const CommandError &e) {
                    r = StaticFiles::Reply{503, "Service Unavailable", "text/plain", QByteArray(e.what()) + '\n'};
                }
                sock.write(Ws::httpResponse(r.status, r.reason, r.contentType, r.body,
                                            Ws::webRootCsp(request.headers.value("host")), r.headers));
                flush(sock);
                return;
            }
        }
        if (!Ws::isUpgrade(request) && !m_owner->webRoot().isEmpty()) {
            // A frontend's own files (--web-root), same-origin with the socket
            const StaticFiles::Reply r = request.method == "GET"
                ? StaticFiles::serve(m_owner->webRoot(), request.path)
                : StaticFiles::Reply{405, "Method Not Allowed", "text/plain", "GET only\n"};
            sock.write(Ws::httpResponse(r.status, r.reason, r.contentType, r.body,
                                        Ws::webRootCsp(request.headers.value("host"))));
            flush(sock);
            return;
        }
        if (!Ws::isUpgrade(request)) {
            // The reference client (sdk/web), so a browser reaches the
            // service same-origin; nothing else is served
            const QByteArray page = m_owner->page();
            const bool root = request.path == "/" || request.path == "/index.html";
            if (request.method == "GET" && root && !page.isEmpty())
                sock.write(Ws::httpResponse(200, "OK", "text/html; charset=utf-8", page));
            else
                sock.write(Ws::httpResponse(404, "Not Found", "text/plain", "not found\n"));
            flush(sock);
            return;
        }

        sock.write(Ws::acceptResponse(request));
        qCInfo(lcCommand) << "command client is a WebSocket";
        Ws::Decoder decoder;
        decoder.feed(buf.mid(headLength));
        auto sendText = [&sock](QByteArray json) {
            if (json.endsWith('\n'))
                json.chop(1);
            sock.write(Ws::encodeFrame(Ws::Text, json));
        };
        QByteArray incoming;
        do {
            decoder.feed(incoming);
            incoming.clear();
            Ws::Decoder::Message message;
            while (decoder.next(message)) {
                switch (message.opcode) {
                case Ws::Text:
                case Ws::Binary:
                    sendText(m_owner->handleLine(message.payload.trimmed()));
                    break;
                case Ws::Ping:
                    sock.write(Ws::encodeFrame(Ws::Pong, message.payload));
                    break;
                case Ws::Close:
                    sock.write(Ws::encodeFrame(Ws::Close, message.payload.left(2)));
                    flush(sock);
                    return;
                default:
                    break;
                }
            }
            if (!decoder.error().isEmpty()) {
                qCWarning(lcCommand).noquote() << "WebSocket client dropped:" << decoder.error();
                sock.write(Ws::encodeFrame(Ws::Close, QByteArray::fromHex("03ea")));   // 1002 protocol error
                flush(sock);
                return;
            }
            // As for lines: replies first, then the events queued meanwhile;
            // a stream record is one binary frame
            for (const Connection::Item &item : connection.takeAll()) {
                if (item.binary)
                    sock.write(Ws::encodeFrame(Ws::Binary, item.bytes));
                else
                    sendText(item.bytes);
            }
            flush(sock);
        } while (pump(sock, incoming));
    }

    CommandServer *m_owner;
    std::atomic<bool> m_stopping{false};
    QMutex m_lock;
    QList<QThread *> m_clients;
};

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

Connection::~Connection()
{
    close();
}

void Connection::push(const QByteArray &jsonLine)
{
    QMutexLocker l(&m_lock);
    if (m_closed)
        return;
    m_queue.append(Item{jsonLine, false});
    while (m_queue.size() - m_records > kMaxQueuedEvents) {
        for (int i = 0; i < m_queue.size(); ++i) {
            if (!m_queue.at(i).binary) {
                m_queue.removeAt(i);
                break;
            }
        }
        ++m_dropped;
    }
}

bool Connection::pushRecord(const QByteArray &record)
{
    QMutexLocker l(&m_lock);
    if (m_closed)
        return false;
    if (m_records + 1 > kMaxQueuedRecords || m_recordBytes + record.size() > kMaxQueuedRecordBytes) {
        m_queue.removeIf([](const Item &i) { return i.binary; });
        m_records = 0;
        m_recordBytes = 0;
        return false;
    }
    m_queue.append(Item{record, true});
    ++m_records;
    m_recordBytes += record.size();
    return true;
}

QList<Connection::Item> Connection::takeAll()
{
    QMutexLocker l(&m_lock);
    QList<Item> out;
    if (m_dropped > 0) {
        out.append(Item{QJsonDocument(QJsonObject{{QStringLiteral("event"), QStringLiteral("dropped")},
                                                  {QStringLiteral("count"), m_dropped}})
                            .toJson(QJsonDocument::Compact) + '\n', false});
        m_dropped = 0;
    }
    out.append(m_queue);
    m_queue.clear();
    m_records = 0;
    m_recordBytes = 0;
    return out;
}

void Connection::onClose(std::function<void()> cleanup)
{
    QMutexLocker l(&m_lock);
    if (m_closed) {
        l.unlock();
        CommandServer::postGui(std::move(cleanup));
        return;
    }
    m_cleanups.push_back(std::move(cleanup));
}

void Connection::close()
{
    std::vector<std::function<void()>> cleanups;
    {
        QMutexLocker l(&m_lock);
        if (m_closed)
            return;
        m_closed = true;
        m_queue.clear();
        cleanups.swap(m_cleanups);
    }
    for (auto &c : cleanups)
        CommandServer::postGui(std::move(c));
}

void Connection::setPeer(bool loopback, const QString &address)
{
    QMutexLocker l(&m_lock);
    m_peerLoopback = loopback;
    m_peerAddress = address;
}

bool Connection::peerLoopback() const
{
    QMutexLocker l(&m_lock);
    return m_peerLoopback;
}

QString Connection::peerAddress() const
{
    QMutexLocker l(&m_lock);
    return m_peerAddress;
}

void Connection::setTokenId(const QString &id)
{
    QMutexLocker l(&m_lock);
    m_tokenId = id;
}

QString Connection::tokenId() const
{
    QMutexLocker l(&m_lock);
    return m_tokenId;
}

void CommandServer::trackConnection(const std::shared_ptr<Connection> &c)
{
    QMutexLocker l(&m_connectionsLock);
    m_connections.removeIf([](const std::weak_ptr<Connection> &w) { return w.expired(); });
    m_connections.append(c);
}

bool CommandServer::httpTrusted(const Connection &c, const QHash<QByteArray, QByteArray> &headers,
                                const QByteArray &target) const
{
    return !m_httpTrusted || m_httpTrusted(c, headers, target);
}

void CommandServer::closeConnectionsOf(const QString &tokenId)
{
    if (tokenId.isEmpty())
        return;
    QList<std::shared_ptr<Connection>> doomed;
    {
        QMutexLocker l(&m_connectionsLock);
        for (const auto &w : std::as_const(m_connections))
            if (auto c = w.lock(); c && c->tokenId() == tokenId)
                doomed.append(c);
    }
    for (const auto &c : doomed)
        c->close();   // its cleanups (subscriptions, streams, demand) run; its thread drops the socket
}

bool Connection::isClosed() const
{
    QMutexLocker l(&m_lock);
    return m_closed;
}

std::shared_ptr<Connection> CommandServer::currentConnection()
{
    return t_connection;
}

// ---------------------------------------------------------------------------
// CommandServer
// ---------------------------------------------------------------------------

CommandServer::CommandServer(quint16 port, QObject *parent)
    : QObject(parent)
    , m_port(port)
{
    registerAction(QStringLiteral("ping"), [](const QVariantMap &) { return QVariant(QStringLiteral("pong")); });
    registerAction(QStringLiteral("list_actions"), [this](const QVariantMap &) { return QVariant(actions()); });
}

CommandServer::~CommandServer()
{
    stop();
}

void CommandServer::registerAction(const QString &action, Handler handler)
{
    QWriteLocker l(&m_lock);
    if (!m_handlers.contains(action))
        m_order.append(action);
    m_handlers.insert(action, std::move(handler));
}

QStringList CommandServer::actions() const
{
    QReadLocker l(&m_lock);
    return m_order;
}

void CommandServer::registerOp(const QString &op, Handler handler)
{
    QWriteLocker l(&m_lock);
    if (!m_ops.contains(op))
        m_opOrder.append(op);
    m_ops.insert(op, std::move(handler));
}

QStringList CommandServer::ops() const
{
    QReadLocker l(&m_lock);
    return m_opOrder;
}

bool CommandServer::start()
{
    if (m_thread)
        return true;
    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("cmd-server"));
    m_server = new CommandTcpServer(this);
    m_server->moveToThread(m_thread);
    m_thread->start();
    bool ok = false;
    QMetaObject::invokeMethod(m_server, [this, &ok] {
        ok = m_server->listen(m_bind, m_port);
        if (!ok)
            qCWarning(lcCommand).noquote() << "command server could not listen on" << m_bind.toString() + ':' + QString::number(m_port) << ":"
                                 << m_server->errorString();
    }, Qt::BlockingQueuedConnection);
    if (!ok) {
        stop();
        return false;
    }
    m_port = m_server->serverPort();
    qCInfo(lcCommand).noquote() << "command server listening on" << m_bind.toString() + ':' + QString::number(m_port);
    if (!m_bind.isLoopback())
        qCWarning(lcCommand).noquote() << "command server reachable beyond this machine (" + m_bind.toString()
                                              + "): it has no authentication";
    std::fprintf(stdout, "\033[1;36m[CMD]\033[0m   Command server on port %u\n", m_port);
    std::fflush(stdout);
    return true;
}

void CommandServer::stop()
{
    if (!m_thread)
        return;
    // close() on the listener's own thread (its socket notifier lives there),
    // delete once that thread has stopped
    QMetaObject::invokeMethod(m_server, [this] {
        m_server->close();
        m_server->shutdown();
    }, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait(3000);
    delete m_server;
    m_server = nullptr;
    delete m_thread;
    m_thread = nullptr;
    qCInfo(lcCommand) << "command server stopped";
}

QByteArray CommandServer::handleLine(const QByteArray &line)
{
    QJsonObject reply;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        reply[QStringLiteral("ok")] = false;
        reply[QStringLiteral("error")] = QStringLiteral("Invalid JSON: %1")
                                             .arg(err.error != QJsonParseError::NoError ? err.errorString()
                                                                                        : QStringLiteral("not an object"));
    } else {
        QJsonObject cmd = doc.object();
        // Service ops: {"op", "id"?, ...args}; else the dev `action` form
        const bool isOp = cmd.contains(QStringLiteral("op"));
        const QString name = cmd.value(isOp ? QStringLiteral("op") : QStringLiteral("action")).toString();
        if (cmd.contains(QStringLiteral("id")))
            reply[QStringLiteral("id")] = cmd.value(QStringLiteral("id"));
        // Trust is checked as each op runs, so a revoke takes effect at once
        // Only the op form is ever exempt: {"action": "hello"} is checked
        if (m_trusted && !(isOp && m_openOps.contains(name))) {
            const std::shared_ptr<Connection> c = currentConnection();
            if (!c || !m_trusted(*c)) {
                reply[QStringLiteral("ok")] = false;
                reply[QStringLiteral("error")] = QStringLiteral("not paired: pair with the code shown in OCTAVE's Settings");
                reply[QStringLiteral("code")] = QStringLiteral("unauthorised");
                return QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
            }
        }
        try {
            QVariant result;
            if (isOp) {
                cmd.remove(QStringLiteral("op"));
                cmd.remove(QStringLiteral("id"));
                result = jsonSafe(dispatchOp(name, cmd.toVariantMap()));
            } else {
                result = jsonSafe(dispatch(name, cmd.value(QStringLiteral("args")).toObject().toVariantMap()));
            }
            reply[QStringLiteral("ok")] = true;
            reply[QStringLiteral("result")] = QJsonValue::fromVariant(result);
        } catch (const std::exception &e) {
            qCWarning(lcCommand).noquote() << "command error [" + name + "]:" << e.what();
            reply[QStringLiteral("ok")] = false;
            reply[QStringLiteral("error")] = QString::fromUtf8(e.what());
        }
    }
    return QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
}

QVariant CommandServer::dispatch(const QString &action, const QVariantMap &args)
{
    Handler handler;
    {
        QReadLocker l(&m_lock);
        handler = m_handlers.value(action);
    }
    if (!handler)
        throw CommandError(QStringLiteral("Unknown action: %1").arg(action));
    return handler(args);
}

QVariant CommandServer::dispatchOp(const QString &op, const QVariantMap &args)
{
    Handler handler;
    {
        QReadLocker l(&m_lock);
        handler = m_ops.value(op);
    }
    if (!handler)
        throw CommandError(QStringLiteral("Unknown op: %1").arg(op));
    return handler(args);
}

QVariant CommandServer::onGui(std::function<QVariant()> fn, int timeoutMs)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread())
        return fn();
    struct Call {
        QSemaphore done;
        QVariant result;
        QString error;
    };
    auto call = std::make_shared<Call>();
    QMetaObject::invokeMethod(app, [call, fn = std::move(fn)] {
        try {
            call->result = fn();
        } catch (const std::exception &e) {
            call->error = QString::fromUtf8(e.what());
        } catch (...) {
            call->error = QStringLiteral("unknown error");
        }
        call->done.release();
    }, Qt::QueuedConnection);
    if (!call->done.tryAcquire(1, timeoutMs))
        throw CommandError(QStringLiteral("GUI thread did not answer within %1 ms").arg(timeoutMs));
    if (!call->error.isEmpty())
        throw CommandError(call->error);
    return call->result;
}

void CommandServer::postGui(std::function<void()> fn)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), [fn = std::move(fn)] {
        try {
            fn();
        } catch (const std::exception &e) {
            qCWarning(lcCommand) << "queued command failed:" << e.what();
        }
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QVariant jsonSafe(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return QVariant();
    const QMetaType type = value.metaType();
    if (type == QMetaType::fromType<QJSValue>())
        return jsonSafe(value.value<QJSValue>().toVariant());
    // A QVariant holding a QVariant: the value inside
    if (type == QMetaType::fromType<QVariant>())
        return jsonSafe(value.value<QVariant>());
    if (type == QMetaType::fromType<QJsonValue>())
        return jsonSafe(value.value<QJsonValue>().toVariant());
    if (type == QMetaType::fromType<QJsonObject>())
        return jsonSafe(value.value<QJsonObject>().toVariantMap());
    if (type == QMetaType::fromType<QJsonArray>())
        return jsonSafe(value.value<QJsonArray>().toVariantList());
    if (type == QMetaType::fromType<QJsonDocument>()) {
        const QJsonDocument doc = value.value<QJsonDocument>();
        return doc.isArray() ? jsonSafe(doc.array().toVariantList()) : jsonSafe(doc.object().toVariantMap());
    }
    switch (type.id()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::QString:
        return value;
    case QMetaType::Float:
        return static_cast<double>(value.toFloat());
    case QMetaType::QStringList:
        return value;
    case QMetaType::QUrl:
        return value.toUrl().toString();
    case QMetaType::QColor:
        return value.value<QColor>().name(QColor::HexArgb);
    case QMetaType::QPointF:
    case QMetaType::QPoint: {
        const QPointF p = value.toPointF();
        return QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
    }
    case QMetaType::QSizeF:
    case QMetaType::QSize: {
        const QSizeF s = value.toSizeF();
        return QVariantMap{{QStringLiteral("width"), s.width()}, {QStringLiteral("height"), s.height()}};
    }
    case QMetaType::QRectF:
    case QMetaType::QRect: {
        const QRectF r = value.toRectF();
        return QVariantMap{{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                           {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
    }
    default:
        break;
    }
    if (type.flags() & QMetaType::PointerToQObject) {
        QObject *obj = value.value<QObject *>();
        if (!obj)
            return QVariant();
        return QStringLiteral("<%1%2>").arg(QString::fromLatin1(obj->metaObject()->className()),
                                            obj->objectName().isEmpty() ? QString() : QLatin1Char('#') + obj->objectName());
    }
    if (value.canConvert<QVariantList>() && type.id() != QMetaType::QByteArray) {
        QVariantList out;
        for (const QVariant &v : value.toList())
            out.append(jsonSafe(v));
        return out;
    }
    if (value.canConvert<QVariantMap>()) {
        QVariantMap out;
        const QVariantMap in = value.toMap();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it)
            out.insert(it.key(), jsonSafe(it.value()));
        return out;
    }
    if (value.canConvert<QString>())
        return value.toString();
    return QString::fromLatin1(type.name());
}

bool convertTo(QVariant &value, QMetaType type)
{
    if (value.metaType() == type)
        return true;
    if (type.flags() & QMetaType::IsEnumeration) {
        // A Q_ENUM: its enclosing class knows the keys ("Connecting" or 1)
        const QMetaObject *mo = type.metaObject();
        QByteArray name = type.name();
        name = name.mid(name.lastIndexOf(':') + 1);
        const int ei = mo ? mo->indexOfEnumerator(name.constData()) : -1;
        int number = 0;
        if (value.typeId() == QMetaType::QString) {
            if (ei < 0)
                return false;
            bool ok = false;
            number = mo->enumerator(ei).keyToValue(value.toString().toUtf8().constData(), &ok);
            if (!ok)
                return false;
        } else {
            bool ok = false;
            number = value.toInt(&ok);
            if (!ok || (ei >= 0 && !mo->enumerator(ei).valueToKey(number)))
                return false;
        }
        QVariant out(type);
        if (type.sizeOf() != int(sizeof(int)))
            return false;
        *static_cast<int *>(out.data()) = number;
        value = out;
        return true;
    }
    return value.convert(type);
}

QVariant invokeByName(QObject *obj, const char *method, const QVariantList &args, bool serviceOnly)
{
    if (!obj)
        throw CommandError(QStringLiteral("no object for %1()").arg(QLatin1String(method)));
    const QMetaObject *mo = obj->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.name() != method || m.parameterCount() != args.size())
            continue;
        if (serviceOnly && !MemberScope::callable(mo, i))
            continue;
        // Convert each argument to the parameter's type; a QVariant parameter
        // (QML JS functions) takes the value as is
        QVariantList converted;
        bool ok = true;
        for (int p = 0; p < m.parameterCount() && ok; ++p) {
            const QMetaType pt = m.parameterMetaType(p);
            QVariant a = args.at(p);
            if (pt == QMetaType::fromType<QVariant>()) {
                converted.append(a);
                continue;
            }
            ok = convertTo(a, pt);
            converted.append(a);
        }
        if (!ok)
            continue;
        // A QVariant return is written into `ret` itself; QVariant(rt) would
        // wrap a second QVariant around it
        QVariant ret;
        const QMetaType rt = m.returnMetaType();
        const bool variantReturn = rt == QMetaType::fromType<QVariant>();
        if (rt.isValid() && rt.id() != QMetaType::Void && !variantReturn)
            ret = QVariant(rt);
        QVarLengthArray<void *, 8> argv;
        argv.append(variantReturn ? &ret : (ret.isValid() ? ret.data() : nullptr));
        for (QVariant &c : converted) {
            if (m.parameterMetaType(argv.size() - 1) == QMetaType::fromType<QVariant>())
                argv.append(&c);   // the QVariant wrapping the value
            else
                argv.append(c.data());
        }
        QMetaObject::metacall(obj, QMetaObject::InvokeMetaMethod, m.methodIndex(), argv.data());
        return ret;
    }
    throw CommandError(QStringLiteral("%1 has no method %2 taking %3 argument(s)")
                           .arg(QString::fromLatin1(mo->className()), QLatin1String(method))
                           .arg(args.size()));
}

} // namespace OctaveService
