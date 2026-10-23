// servicebridge.cpp

#include "servicebridge.h"

#include "commandserver.h"
#include "metadescribe.h"
#include "protocol.h"
#include "serviceregistry.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQmlPropertyMap>
#include <QTimer>

namespace OctaveService {

ServiceBridge::ServiceBridge(ServiceRegistry *registry, AppInfo app)
    : m_registry(registry)
    , m_app(std::move(app))
{
}

namespace {

qint64 monotonicMs()
{
    static QElapsedTimer clock = [] { QElapsedTimer t; t.start(); return t; }();
    return clock.elapsed();
}

// Catches any signal by its QMetaMethod (QObject::connect needs a known
// slot): connected to the index just past QObject's own methods, which
// qt_metacall then sees as id 0. QSignalSpy works the same way.
class SignalRelay : public QObject
{
public:
    explicit SignalRelay(std::function<void(void **)> fn, QObject *parent)
        : QObject(parent), m_fn(std::move(fn)) {}

    bool attach(QObject *sender, const QMetaMethod &signal)
    {
        // Auto: an emit from another thread queues here (the GUI thread),
        // so a subscription never runs on a manager's worker thread
        return QMetaObject::connect(sender, signal.methodIndex(), this, metaObject()->methodCount(),
                                    Qt::AutoConnection, nullptr);
    }

    int qt_metacall(QMetaObject::Call call, int id, void **argv) override
    {
        id = QObject::qt_metacall(call, id, argv);
        if (id < 0)
            return id;
        if (call == QMetaObject::InvokeMetaMethod) {
            if (id == 0)
                m_fn(argv);
            --id;
        }
        return id;
    }

private:
    std::function<void(void **)> m_fn;
};

// One `subscribe`: lives on the GUI thread, pushes events onto its
// connection's queue (throttled if asked)
class Subscription : public QObject
{
public:
    Subscription(int id, QString objectName, std::weak_ptr<Connection> connection, int throttleMs)
        : m_id(id), m_object(std::move(objectName)), m_connection(std::move(connection)), m_throttleMs(throttleMs)
    {
        m_timer.setSingleShot(true);
        m_timer.setInterval(throttleMs);
        connect(&m_timer, &QTimer::timeout, this, &Subscription::flush);
    }

    void send(const QString &kind, const QString &name, QJsonObject fields, bool now = false)
    {
        fields.insert(QStringLiteral("event"), kind);
        fields.insert(QStringLiteral("sub"), m_id);
        fields.insert(QStringLiteral("object"), m_object);
        if (!name.isEmpty())
            fields.insert(QStringLiteral("name"), name);
        if (m_throttleMs <= 0 || now) {
            push(fields);
            return;
        }
        // The latest per name; params events merge their values
        const QString key = kind + QLatin1Char('/') + name;
        if (kind == QLatin1String("params") && m_pending.contains(key)) {
            QJsonObject merged = m_pending.value(key).value(QStringLiteral("values")).toObject();
            const QJsonObject add = fields.value(QStringLiteral("values")).toObject();
            for (auto it = add.begin(); it != add.end(); ++it)
                merged.insert(it.key(), it.value());
            fields.insert(QStringLiteral("values"), merged);
        }
        if (!m_pending.contains(key))
            m_order.append(key);
        m_pending.insert(key, fields);
        if (!m_timer.isActive())
            m_timer.start();
    }

private:
    void flush()
    {
        for (const QString &key : std::as_const(m_order))
            push(m_pending.value(key));
        m_pending.clear();
        m_order.clear();
    }

    void push(QJsonObject fields)
    {
        fields.insert(QStringLiteral("t"), monotonicMs());
        if (auto c = m_connection.lock())
            c->push(QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n');
    }

    int m_id;
    QString m_object;
    std::weak_ptr<Connection> m_connection;
    int m_throttleMs;
    QTimer m_timer;
    QHash<QString, QJsonObject> m_pending;
    QStringList m_order;
};

QJsonValue toJson(const QVariant &v)
{
    return QJsonValue::fromVariant(jsonSafe(v));
}

QMetaProperty findProperty(QObject *o, const QString &name)
{
    const int i = o->metaObject()->indexOfProperty(name.toUtf8().constData());
    if (i < 0)
        throw CommandError(QStringLiteral("%1 has no property %2")
                               .arg(QString::fromLatin1(o->metaObject()->className()), name));
    return o->metaObject()->property(i);
}

int timeoutOf(const QVariantMap &args)
{
    const int t = args.value(QStringLiteral("timeoutMs")).toInt();
    return t > 0 ? t : 30000;
}

} // namespace

QObject *ServiceBridge::object(const QString &name) const
{
    QObject *o = m_registry->object(name);
    if (!o)
        throw CommandError(QStringLiteral("Unknown object: %1").arg(name));
    return o;
}

void ServiceBridge::install(CommandServer &server)
{
    server.registerOp(QStringLiteral("hello"), [this](const QVariantMap &) {
        return QVariant(QVariantMap{
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("app"), QVariantMap{{QStringLiteral("version"), m_app.version},
                                                {QStringLiteral("build"), m_app.debugBuild ? QStringLiteral("debug")
                                                                                           : QStringLiteral("release")},
                                                {QStringLiteral("headless"), m_app.headless}}},
            {QStringLiteral("objects"), m_registry->names()},
        });
    });

    server.registerOp(QStringLiteral("get"), [this](const QVariantMap &args) {
        QPointer<QObject> o = object(args.value(QStringLiteral("object")).toString());
        const QString property = args.value(QStringLiteral("property")).toString();
        return CommandServer::onGui([o, property]() -> QVariant {
            if (!o)
                throw CommandError(QStringLiteral("object is gone"));
            if (auto *map = qobject_cast<QQmlPropertyMap *>(o.data())) {
                if (property.isEmpty()) {
                    QVariantMap all;
                    for (const QString &k : map->keys()) {
                        const QVariant v = map->value(k);
                        if (v.isValid())
                            all.insert(k, v);
                    }
                    return all;
                }
                return map->contains(property) ? map->value(property) : QVariant();
            }
            if (property.isEmpty())
                throw CommandError(QStringLiteral("get needs a property"));
            return findProperty(o, property).read(o);
        }, timeoutOf(args));
    });

    server.registerOp(QStringLiteral("set"), [this](const QVariantMap &args) {
        QPointer<QObject> o = object(args.value(QStringLiteral("object")).toString());
        const QString property = args.value(QStringLiteral("property")).toString();
        const QVariant value = args.value(QStringLiteral("value"));
        return CommandServer::onGui([o, property, value]() -> QVariant {
            if (!o)
                throw CommandError(QStringLiteral("object is gone"));
            if (qobject_cast<QQmlPropertyMap *>(o.data()))
                throw CommandError(QStringLiteral("%1 is read-only").arg(property));
            const QMetaProperty p = findProperty(o, property);
            if (!p.isWritable())
                throw CommandError(QStringLiteral("%1 is read-only").arg(property));
            QVariant v = value;
            if (p.metaType() != QMetaType::fromType<QVariant>() && !convertTo(v, p.metaType()))
                throw CommandError(QStringLiteral("%1 takes a %2").arg(property, QString::fromLatin1(p.metaType().name())));
            if (!p.write(o, v))
                throw CommandError(QStringLiteral("could not write %1").arg(property));
            return p.read(o);
        }, timeoutOf(args));
    });

    server.registerOp(QStringLiteral("call"), [this](const QVariantMap &args) {
        QPointer<QObject> o = object(args.value(QStringLiteral("object")).toString());
        const QByteArray method = args.value(QStringLiteral("method")).toString().toUtf8();
        const QVariantList callArgs = args.value(QStringLiteral("args")).toList();
        return CommandServer::onGui([o, method, callArgs]() -> QVariant {
            if (!o)
                throw CommandError(QStringLiteral("object is gone"));
            return invokeByName(o, method.constData(), callArgs);
        }, timeoutOf(args));
    });

    server.registerOp(QStringLiteral("subscribe"), [this](const QVariantMap &args) { return subscribe(args); });
    server.registerOp(QStringLiteral("unsubscribe"), [this](const QVariantMap &args) { return unsubscribe(args); });

    // Meta-objects are static data: no GUI-thread hop needed
    server.registerOp(QStringLiteral("describe"), [this](const QVariantMap &args) {
        const QString name = args.value(QStringLiteral("object")).toString();
        if (!name.isEmpty())
            return QVariant(describeObject(object(name)));
        QVariantMap all;
        for (const QString &n : m_registry->names())
            all.insert(n, describeObject(m_registry->object(n)));
        return QVariant(QVariantMap{{QStringLiteral("protocolVersion"), kProtocolVersion},
                                    {QStringLiteral("objects"), all}});
    });
}

QVariant ServiceBridge::subscribe(const QVariantMap &args)
{
    const QString name = args.value(QStringLiteral("object")).toString();
    QPointer<QObject> o = object(name);
    const QStringList signalNames = args.value(QStringLiteral("signals")).toStringList();
    const QStringList propertyNames = args.value(QStringLiteral("properties")).toStringList();
    const int throttleMs = qMax(0, args.value(QStringLiteral("throttleMs")).toInt());
    std::shared_ptr<Connection> connection = CommandServer::currentConnection();
    if (!connection)
        throw CommandError(QStringLiteral("subscribe needs a connection"));
    const int id = connection->nextId();

    CommandServer::onGui([this, o, name, signalNames, propertyNames, throttleMs, connection, id]() -> QVariant {
        if (!o)
            throw CommandError(QStringLiteral("object is gone"));
        auto *sub = new Subscription(id, name, connection, throttleMs);
        auto fail = [sub](const QString &why) {
            delete sub;
            throw CommandError(why);
        };
        const QMetaObject *mo = o->metaObject();

        if (auto *map = qobject_cast<QQmlPropertyMap *>(o.data())) {
            // paramStore: its changed(QStringList) batches, filtered to the ids asked for
            const int si = mo->indexOfSignal("changed(QStringList)");
            if (si < 0)
                fail(QStringLiteral("%1 has no change batches to follow").arg(name));
            QPointer<QQmlPropertyMap> m = map;
            const QSet<QString> wanted(propertyNames.cbegin(), propertyNames.cend());
            auto *relay = new SignalRelay([sub, m, wanted](void **argv) {
                if (!m)
                    return;
                const QStringList ids = *reinterpret_cast<QStringList *>(argv[1]);
                QJsonObject values;
                for (const QString &k : ids)
                    if (wanted.isEmpty() || wanted.contains(k))
                        values.insert(k, toJson(m->value(k)));
                if (!values.isEmpty())
                    sub->send(QStringLiteral("params"), QString(), {{QStringLiteral("values"), values}});
            }, sub);
            relay->attach(o, mo->method(si));
            QJsonObject now;   // what it holds already
            for (const QString &k : map->keys())
                if ((wanted.isEmpty() || wanted.contains(k)) && map->value(k).isValid())
                    now.insert(k, toJson(map->value(k)));
            sub->send(QStringLiteral("params"), QString(), {{QStringLiteral("values"), now}}, true);
        } else {
            for (const QString &sig : signalNames) {
                QMetaMethod signal;
                for (int i = 0; i < mo->methodCount(); ++i)
                    if (mo->method(i).methodType() == QMetaMethod::Signal && mo->method(i).name() == sig.toUtf8())
                        signal = mo->method(i);
                if (!signal.isValid())
                    fail(QStringLiteral("%1 has no signal %2").arg(name, sig));
                auto *relay = new SignalRelay([sub, signal, sig](void **argv) {
                    QJsonArray params;
                    for (int p = 0; p < signal.parameterCount(); ++p) {
                        const QMetaType t = signal.parameterMetaType(p);
                        params.append(t == QMetaType::fromType<QVariant>()
                                          ? toJson(*reinterpret_cast<QVariant *>(argv[p + 1]))
                                          : toJson(QVariant(t, argv[p + 1])));
                    }
                    sub->send(QStringLiteral("signal"), sig, {{QStringLiteral("args"), params}});
                }, sub);
                relay->attach(o, signal);
            }
            for (const QString &prop : propertyNames) {
                const int pi = mo->indexOfProperty(prop.toUtf8().constData());
                if (pi < 0)
                    fail(QStringLiteral("%1 has no property %2").arg(name, prop));
                const QMetaProperty p = mo->property(pi);
                QPointer<QObject> target = o;
                auto read = [sub, target, p, prop](bool now) {
                    if (target)
                        sub->send(QStringLiteral("property"), prop, {{QStringLiteral("value"), toJson(p.read(target))}}, now);
                };
                if (p.hasNotifySignal()) {
                    auto *relay = new SignalRelay([read](void **) { read(false); }, sub);
                    relay->attach(o, p.notifySignal());
                }
                read(true);   // the current value at once
            }
            if (signalNames.isEmpty() && propertyNames.isEmpty())
                fail(QStringLiteral("subscribe needs signals or properties"));
        }

        // Ends with the object, an unsubscribe or the connection
        QObject::connect(o, &QObject::destroyed, sub, [sub] { sub->deleteLater(); });
        const void *key = connection.get();
        (*m_subscriptions)[key].insert(id, sub);
        QPointer<QObject> guard = sub;
        std::weak_ptr<Subscriptions> subs = m_subscriptions;
        connection->onClose([subs, key, guard] {
            if (auto s = subs.lock())
                s->remove(key);
            delete guard.data();
        });
        return {};
    });
    return QVariantMap{{QStringLiteral("sub"), id}};
}

QVariant ServiceBridge::unsubscribe(const QVariantMap &args)
{
    const int id = args.value(QStringLiteral("sub")).toInt();
    std::shared_ptr<Connection> connection = CommandServer::currentConnection();
    if (!connection)
        throw CommandError(QStringLiteral("unsubscribe needs a connection"));
    const void *key = connection.get();
    return CommandServer::onGui([this, key, id]() -> QVariant {
        // Null too once its object went (it ended with it)
        QPointer<QObject> sub = m_subscriptions->value(key).value(id);
        (*m_subscriptions)[key].remove(id);
        if (!sub)
            throw CommandError(QStringLiteral("No subscription %1").arg(id));
        delete sub.data();
        return true;
    });
}

} // namespace OctaveService
