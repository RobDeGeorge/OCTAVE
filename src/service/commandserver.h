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
// Listens on 127.0.0.1 only (default port 9876). Each client connection gets
// its own thread, so a slow GUI call never
// blocks get_metrics on another connection. Handlers run on that connection
// thread and use onGui() / postGui() for anything that touches Qt objects.
//
// Moved from src/dev/ to become the transport of the service API
// (TODO/open-backend.md, contract step 1).

#include <QHash>
#include <QObject>
#include <QReadWriteLock>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <stdexcept>

class QThread;

namespace OctaveService {

class CommandTcpServer;

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

    bool start();
    void stop();
    quint16 port() const { return m_port; }

    // Run `fn` on the GUI thread and wait for its result (or its error).
    // Throws CommandError after `timeoutMs` if the GUI thread is stuck.
    static QVariant onGui(std::function<QVariant()> fn, int timeoutMs = 30000);
    // Queue `fn` on the GUI thread and return at once.
    static void postGui(std::function<void()> fn);

    // Called from connection threads
    QByteArray handleLine(const QByteArray &line);

private:
    QVariant dispatch(const QString &action, const QVariantMap &args);

    quint16 m_port;
    mutable QReadWriteLock m_lock;
    QHash<QString, Handler> m_handlers;
    QStringList m_order;   // registration order, for list_actions
    QThread *m_thread = nullptr;
    CommandTcpServer *m_server = nullptr;
};

// QVariant -> something QJsonValue::fromVariant() can serialise: QJSValue
// unwrapped, QObject / geometry / colour types turned into strings.
QVariant jsonSafe(const QVariant &value);

// Invoke a slot / Q_INVOKABLE by name, converting `args` to its parameter
// types. Throws CommandError when there is no such method or a conversion
// fails. GUI thread only for GUI objects.
QVariant invokeByName(QObject *obj, const char *method, const QVariantList &args = {});

} // namespace OctaveService

// The dev tooling (src/dev/) predates the move to src/service/
namespace OctaveDev {
using OctaveService::CommandError;
using OctaveService::CommandServer;
using OctaveService::invokeByName;
using OctaveService::jsonSafe;
} // namespace OctaveDev

#endif // OCTAVE_SERVICE_COMMANDSERVER_H
