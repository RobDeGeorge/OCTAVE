// servicebridge.h
//
// The service protocol over CommandServer's socket: the ops any frontend
// uses to read, write, call and follow the managers in a ServiceRegistry
// (TODO/open-backend.md, contract step 1). Ops so far:
//
//   hello -> {protocolVersion, app: {version, build, headless}, objects}
//   describe {object?} -> one object's properties / methods / signals
//       (metadescribe.h), or every object's as {protocolVersion, objects,
//       ops: [every op name this server answers]}. The envelope itself
//       (ops, errors, events, records, routes) is docs/api/protocol.json
//   get {object, property?} -> a property's value; on a key-value object
//       (QQmlPropertyMap: paramStore) a key's, or every key's without one
//   set {object, property, value} -> writes a writable property (converted
//       to its type, enums by key), returns the value read back
//   call {object, method, args?: [], timeoutMs?} -> a slot / invokable's
//       return value
//   subscribe {object, signals?: [...], properties?: [...], throttleMs?}
//       -> {sub}; then events on the same connection:
//       {"event": "signal", sub, object, name, args: [...], t}
//       {"event": "property", sub, object, name, value, t} (the current
//           value at once, then on every notify)
//       {"event": "params", sub, object, values: {id: v}, t} on paramStore
//           (its 33 ms batches; `properties` = ids to keep, none = all)
//       throttleMs > 0 coalesces: at most one event per name (one merged
//       params event) per interval, the latest value winning; `t` is when
//       the event is sent (a throttled one: the flush), in monotonic ms
//   unsubscribe {sub}. A connection's subscriptions end with it.
// Claims (releaseOnClose): a `call` to a consumer-keyed method such as
// obdManager.setParameterDemand(consumer, ids) is remembered against its
// connection; when that connection closes, every consumer it still holds
// is released by calling the method again with the release value ([] /
// false), so a killed client never leaves the car polled. A consumer
// released by the client itself, or claimed since by another connection,
// is left alone.
// List models (a property holding a QAbstractItemModel; no service object
// has one today, tst_servicebridge covers them): get gives {model: true, count, roles}; rows {object, property,
// from?, count?} the rows as {role: value} maps; subscribing to the
// property sends {"event": "model", ..., name, change: "reset" | "insert" |
// "remove" | "update", first, last, rows?} (rows for reset / insert /
// update), starting with a reset of every row. List events ignore
// throttleMs: they are a sequence a client replays. Moves and layout
// changes are not forwarded (VariantListModel never emits them; a model
// that does needs them added), and an update resends whole rows.
// Everything that touches an object runs on its (GUI) thread.

#ifndef OCTAVE_SERVICE_SERVICEBRIDGE_H
#define OCTAVE_SERVICE_SERVICEBRIDGE_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

#include <memory>

class QObject;

class ServicePairing;

namespace OctaveService {

class CommandServer;
class Connection;
class ServiceRegistry;

struct AppInfo {
    QString version;      // PROJECT_VERSION
    bool debugBuild = false;
    bool headless = false;
};

class ServiceBridge
{
public:
    ServiceBridge(ServiceRegistry *registry, AppInfo app);

    // Register the ops on `server` (before or after it starts)
    void install(CommandServer &server);
    // Protocol v2 trust (servicepairing.h): with it, every op but hello and
    // pair needs a trusted connection. Before install(). None = all trusted.
    void setPairing(ServicePairing *pairing) { m_pairing = pairing; }
    bool trusted(const Connection &c) const;
    // Permissions (servicescopes.h): whether a connection may do what needs
    // `scope`, and what it holds (the device's own screen holds full)
    bool connectionAllows(const Connection &c, const QString &scope) const;
    QStringList connectionScopes(const Connection &c) const;
    // An installed web frontend is the page this port serves
    // (frontendmanager.h): a browser on this device (a loopback connection
    // marked Connection::browser) then holds `scopes`, the frontend's
    // manifest, instead of the full trust loopback gives programs; a token
    // it presents counts as usual. Before the server starts
    void setLoopbackBrowserScopes(const QStringList &scopes) { m_browserScopes = scopes; }

    // `object`.`method`(consumer, value) claims `consumer` for the calling
    // connection unless `value` is the release value (an empty list for a
    // list, false for a bool, else equal to `release`); the connection's
    // close calls `method`(consumer, release) for each claim it still
    // holds. Before the server starts.
    void releaseOnClose(const QString &object, const QString &method, const QVariant &release);

private:
    // Throws "forbidden" unless the calling connection may call / set
    // `member` of `o` ("" = the object's own scope)
    void requireScope(const QObject *o, const QString &objectName, const QString &member) const;
    QObject *object(const QString &name) const;   // throws CommandError if unknown
    QVariant subscribe(const QVariantMap &args);
    QVariant unsubscribe(const QVariantMap &args);

    // GUI thread only: live subscriptions by connection, then id. Shared so
    // a connection's cleanup, which can run after the bridge is gone, can
    // tell (it holds a weak reference)
    // Keyed by Connection::serial(), never the address: close cleanups run
    // later on the GUI thread, by when a new connection may have it
    using Subscriptions = QHash<quint64, QHash<int, QPointer<QObject>>>;
    std::shared_ptr<Subscriptions> m_subscriptions = std::make_shared<Subscriptions>();

    // GUI thread only: who holds each claimed consumer
    struct Claim {
        QPointer<QObject> object;
        QByteArray method;
        QString consumer;
        QVariant release;
        quint64 connection = 0;   // Connection::serial()
    };
    struct Claims {
        QHash<QString, Claim> byKey;              // "object.method\nconsumer"
        QHash<quint64, bool> hooked;         // connections with a close hook
    };
    void noteClaim(const QString &objectName, QObject *o, const QByteArray &method, const QVariantList &args,
                   const std::shared_ptr<Connection> &connection);
    std::shared_ptr<Claims> m_claims = std::make_shared<Claims>();
    QHash<QString, QVariant> m_releases;          // "object.method" -> release value

    ServiceRegistry *m_registry;
    AppInfo m_app;
    ServicePairing *m_pairing = nullptr;
    QStringList m_browserScopes;   // setLoopbackBrowserScopes
};

} // namespace OctaveService

#endif // OCTAVE_SERVICE_SERVICEBRIDGE_H
