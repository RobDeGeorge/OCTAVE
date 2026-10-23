#ifndef OCTAVE_SERVICE_COMMANDSERVER_H
#define OCTAVE_SERVICE_COMMANDSERVER_H

// TCP command server for driving OCTAVE from scripts (`octave --profile`).
// One JSON request per line, one JSON reply per line; the dev/ clients (MCP
// servers dev/mcp_server.py and dev/mcp_benchmark.py, the perf loops, the
// screenshot stories) all speak it:
//
//     -> {"action": "navigate", "args": {"page": "obd"}}\n
//     <- {"ok": true, "result": {"navigated_to": "obd"}}\n
//
// The service protocol (ServiceBridge; wiki utilities.html#service-protocol) uses
// `op` instead, with its arguments at the top level and an optional `id`
// echoed in the reply so a client can pipeline:
//
//     -> {"op": "hello", "id": 1}\n
//     <- {"ok": true, "id": 1, "result": {"protocolVersion": 1, ...}}\n
//
// A request carrying both keys is an op (`action` is ignored).
//
// The same port takes WebSocket clients (a browser): one JSON request or
// reply / event per text frame (src/service/websocket.h, which also holds
// the Origin rule), and serves the reference client page (setPage) at
// GET / so a browser can reach it same-origin.
//
// Listens on 127.0.0.1 (default port 9876) unless setBindAddress() says
// otherwise (src/service/serviceoptions.h). Each client connection gets
// its own thread, so a slow GUI call never
// blocks get_metrics on another connection. Handlers run on that connection
// thread and use onGui() / postGui() for anything that touches Qt objects.
//
// The transport of the service API (TODO/open-backend.md, contract
// step 1): built in every configuration; the dev runtime (src/dev/)
// registers its UI / perf / simulator actions on it.

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QReadWriteLock>
#include <QStringList>
#include <QVariant>

#include <QMutex>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

class QThread;

namespace OctaveService {

class CommandTcpServer;

// One client connection, for ops that send it more than their reply (the
// service protocol's subscriptions). Events are queued from any thread and
// written by the connection's own thread between requests; a client that
// stops reading loses the oldest events (a {"event": "dropped"} line says
// how many) instead of growing the queue without bound.
class Connection
{
public:
    static constexpr int kMaxQueuedEvents = 1000;

    ~Connection();
    void push(const QByteArray &jsonLine);   // thread-safe
    QList<QByteArray> takeAll();             // thread-safe
    // Run on the GUI thread when the client goes (subscriptions use it)
    void onClose(std::function<void()> cleanup);
    void close();
    bool isClosed() const;
    int nextId() { return ++m_lastId; }

private:
    mutable QMutex m_lock;
    QList<QByteArray> m_queue;
    int m_dropped = 0;
    bool m_closed = false;
    std::atomic<int> m_lastId{0};
    std::vector<std::function<void()>> m_cleanups;
};

// Thrown by handlers; becomes {"ok": false, "error": what()}
class CommandError : public std::runtime_error
{
public:
    explicit CommandError(const QString &message) : std::runtime_error(message.toStdString()) {}
};

class CommandServer : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<QVariant(const QVariantMap &args)>;

    explicit CommandServer(quint16 port = 9876, QObject *parent = nullptr);
    ~CommandServer() override;

    // Register before or after start(); re-registering replaces the handler.
    // `handler` runs on a connection thread.
    void registerAction(const QString &action, Handler handler);
    QStringList actions() const;
    // A service op: `handler` gets the whole request object (minus op / id)
    void registerOp(const QString &op, Handler handler);
    QStringList ops() const;

    // Before start(); 127.0.0.1 by default. The protocol has no
    // authentication: anything else lets the network drive the app.
    void setBindAddress(const QHostAddress &address) { m_bind = address; }
    // The HTML served at GET / (the reference client, sdk/web); none = 404.
    // Before start().
    void setPage(const QByteArray &html) { m_page = html; }
    QByteArray page() const { return m_page; }
    bool start();
    void stop();
    quint16 port() const { return m_port; }   // the bound port once started (0 asks for any)

    // Run `fn` on the GUI thread and wait for its result (or its error).
    // Throws CommandError after `timeoutMs` if the GUI thread is stuck.
    static QVariant onGui(std::function<QVariant()> fn, int timeoutMs = 30000);
    // Queue `fn` on the GUI thread and return at once.
    static void postGui(std::function<void()> fn);
    // The connection whose request is being handled (connection thread
    // only; null outside a request)
    static std::shared_ptr<Connection> currentConnection();

    // Called from connection threads
    QByteArray handleLine(const QByteArray &line);

private:
    QVariant dispatch(const QString &action, const QVariantMap &args);
    QVariant dispatchOp(const QString &op, const QVariantMap &args);

    quint16 m_port;
    QHostAddress m_bind = QHostAddress(QHostAddress::LocalHost);
    QByteArray m_page;
    mutable QReadWriteLock m_lock;
    QHash<QString, Handler> m_handlers;
    QStringList m_order;   // registration order, for list_actions
    QHash<QString, Handler> m_ops;
    QStringList m_opOrder;
    QThread *m_thread = nullptr;
    CommandTcpServer *m_server = nullptr;
};

// QVariant -> something QJsonValue::fromVariant() can serialise: QJSValue
// unwrapped, QObject / geometry / colour types turned into strings.
QVariant jsonSafe(const QVariant &value);

// Convert a JSON-decoded value to `type` in place: QVariant::convert, plus
// Q_ENUM types by key name or number. False if it can't. QFlags (combined
// values) are not handled; no manager has one.
bool convertTo(QVariant &value, QMetaType type);

// Invoke a slot / Q_INVOKABLE by name, converting `args` to its parameter
// types. Throws CommandError when there is no such method or a conversion
// fails. GUI thread only for GUI objects.
QVariant invokeByName(QObject *obj, const char *method, const QVariantList &args = {});

} // namespace OctaveService

// The dev tooling (src/dev/) predates the move to src/service/
namespace OctaveDev {
using OctaveService::CommandError;
using OctaveService::CommandServer;
using OctaveService::convertTo;
using OctaveService::invokeByName;
using OctaveService::jsonSafe;
} // namespace OctaveDev

#endif // OCTAVE_SERVICE_COMMANDSERVER_H
