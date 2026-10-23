// servicebridge.h
//
// The service protocol over CommandServer's socket: the ops any frontend
// uses to read, write, call and follow the managers in a ServiceRegistry
// (TODO/open-backend.md, contract step 1). Ops so far:
//
//   hello -> {protocolVersion, app: {version, build, headless}, objects}
//   describe {object?} -> one object's properties / methods / signals
//       (metadescribe.h), or every object's as {protocolVersion, objects}
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
// Everything that touches an object runs on its (GUI) thread.

#ifndef OCTAVE_SERVICE_SERVICEBRIDGE_H
#define OCTAVE_SERVICE_SERVICEBRIDGE_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QObject;

namespace OctaveService {

class CommandServer;
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

private:
    QObject *object(const QString &name) const;   // throws CommandError if unknown
    QVariant subscribe(const QVariantMap &args);
    QVariant unsubscribe(const QVariantMap &args);

    // GUI thread only: live subscriptions by connection, then id. Shared so
    // a connection's cleanup, which can run after the bridge is gone, can
    // tell (it holds a weak reference)
    using Subscriptions = QHash<const void *, QHash<int, QPointer<QObject>>>;
    std::shared_ptr<Subscriptions> m_subscriptions = std::make_shared<Subscriptions>();

    ServiceRegistry *m_registry;
    AppInfo m_app;
};

} // namespace OctaveService

#endif // OCTAVE_SERVICE_SERVICEBRIDGE_H
