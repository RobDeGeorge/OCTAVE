// servicebridge.cpp

#include "servicebridge.h"

#include "commandserver.h"
#include "metadescribe.h"
#include "protocol.h"
#include "serviceregistry.h"
#include "streamhub.h"
#include "servicepairing.h"
#include "memberscope.h"
#include "staticfiles.h"

#include <QAbstractItemModel>
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
        // List changes are a sequence (an insert then an update of it): never coalesced
        if (m_throttleMs <= 0 || now || kind == QLatin1String("model")) {
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

// The model a property holds, or null
QAbstractItemModel *modelIn(const QVariant &value)
{
    return (value.metaType().flags() & QMetaType::PointerToQObject)
        ? qobject_cast<QAbstractItemModel *>(value.value<QObject *>()) : nullptr;
}

QJsonArray modelRows(QAbstractItemModel *model, int first, int last)
{
    QJsonArray rows;
    const QHash<int, QByteArray> roles = model->roleNames();
    for (int r = qMax(0, first); r <= last && r < model->rowCount(); ++r) {
        QJsonObject row;
        const QModelIndex index = model->index(r, 0);
        for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
            row.insert(QString::fromUtf8(it.value()), QJsonValue::fromVariant(jsonSafe(model->data(index, it.key()))));
        rows.append(row);
    }
    return rows;
}

QVariant modelSummary(QAbstractItemModel *model)
{
    QStringList roles;
    for (const QByteArray &r : model->roleNames())
        roles.append(QString::fromUtf8(r));
    roles.sort();
    return QVariantMap{{QStringLiteral("model"), true}, {QStringLiteral("count"), model->rowCount()},
                       {QStringLiteral("roles"), roles}};
}

QMetaProperty findProperty(QObject *o, const QString &name)
{
    // Only the object's own properties (memberscope.h): not objectName
    const int i = o->metaObject()->indexOfProperty(name.toUtf8().constData());
    if (i < MemberScope::propertyStart(o->metaObject()))
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
    // hello {token?}: a paired device's token makes this connection
    // trusted; the reply says whether it is
    server.registerOp(QStringLiteral("hello"), [this](const QVariantMap &args) {
        const std::shared_ptr<Connection> c = CommandServer::currentConnection();
        const QString token = args.value(QStringLiteral("token")).toString();
        // An unknown or revoked token is not an error: the reply says so,
        // and the client shows its pairing screen
        bool tokenRejected = false;
        if (c && m_pairing && !token.isEmpty()) {
            const QString id = m_pairing->checkToken(token);
            tokenRejected = id.isEmpty();
            c->setTokenId(id);   // a rejected token also drops an earlier one
        }
        const bool isTrusted = !c || trusted(*c);
        QVariantMap reply{
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("app"), QVariantMap{{QStringLiteral("version"), m_app.version},
                                                {QStringLiteral("build"), m_app.debugBuild ? QStringLiteral("debug")
                                                                                           : QStringLiteral("release")},
                                                {QStringLiteral("headless"), m_app.headless}}},
            {QStringLiteral("objects"), m_registry->names()},
            {QStringLiteral("auth"), isTrusted ? QStringLiteral("trusted") : QStringLiteral("untrusted")},
            {QStringLiteral("loopbackOpen"), m_pairing ? m_pairing->loopbackOpen() : true},
        };
        // An untrusted client learns only enough to pair: no version, no build, no objects
        if (!isTrusted) {
            reply.remove(QStringLiteral("app"));
            reply.remove(QStringLiteral("objects"));
        }
        if (tokenRejected)
            reply.insert(QStringLiteral("tokenRejected"), true);
        return QVariant(reply);
    });

    if (m_pairing) {
        // pair {code, name} -> {token}: the 6-digit code the head unit shows
        server.registerOp(QStringLiteral("pair"), [this](const QVariantMap &args) -> QVariant {
            const std::shared_ptr<Connection> c = CommandServer::currentConnection();
            QString error;
            const QString token = m_pairing->pair(args.value(QStringLiteral("code")).toString(),
                                                  args.value(QStringLiteral("name")).toString(),
                                                  c ? c->peerAddress() : QString(), &error);
            if (token.isEmpty())
                throw CommandError(error);
            if (c)
                c->setTokenId(m_pairing->checkToken(token));
            return QVariantMap{{QStringLiteral("token"), token}};
        });
        // unpair: forget this connection's own device
        server.registerOp(QStringLiteral("unpair"), [this](const QVariantMap &) -> QVariant {
            const std::shared_ptr<Connection> c = CommandServer::currentConnection();
            if (!c || c->tokenId().isEmpty())
                throw CommandError(QStringLiteral("this connection is not a paired device"));
            m_pairing->unpair(c->tokenId());
            return true;
        });
        // mediaTicket -> {ticket, expiresIn}: for /art?ticket= and
        // /export?ticket=, so no token goes in a URL
        server.registerOp(QStringLiteral("mediaTicket"), [this](const QVariantMap &) -> QVariant {
            const std::shared_ptr<Connection> c = CommandServer::currentConnection();
            if (!c)
                throw CommandError(QStringLiteral("mediaTicket needs a connection"));
            return QVariantMap{{QStringLiteral("ticket"), m_pairing->mintTicket(c->tokenId())},
                               {QStringLiteral("expiresIn"), int(ServicePairing::kTicketLifetimeMs / 1000)}};
        });
        server.setTrust([this](const Connection &c) { return trusted(c); },
                        [this](const Connection &c, const QHash<QByteArray, QByteArray> &headers, const QByteArray &target) {
                            if (trusted(c))
                                return true;
                            const QByteArray auth = headers.value("authorization");
                            if (auth.startsWith("Bearer ")
                                && !m_pairing->checkToken(QString::fromLatin1(auth.mid(7).trimmed())).isEmpty())
                                return true;
                            const QString ticket = StaticFiles::queryValue(target, QStringLiteral("ticket"));
                            return !ticket.isEmpty() && m_pairing->checkTicket(ticket, c.peerLoopback());
                        },
                        {QStringLiteral("hello"), QStringLiteral("pair")});
    }

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
            const QVariant v = findProperty(o, property).read(o);
            if (QAbstractItemModel *model = modelIn(v))
                return modelSummary(model);
            return v;
        }, timeoutOf(args));
    });

    server.registerOp(QStringLiteral("rows"), [this](const QVariantMap &args) {
        QPointer<QObject> o = object(args.value(QStringLiteral("object")).toString());
        const QString property = args.value(QStringLiteral("property")).toString();
        const int from = qMax(0, args.value(QStringLiteral("from"), 0).toInt());
        const int count = args.value(QStringLiteral("count"), -1).toInt();
        return CommandServer::onGui([o, property, from, count]() -> QVariant {
            if (!o)
                throw CommandError(QStringLiteral("object is gone"));
            QAbstractItemModel *model = modelIn(findProperty(o, property).read(o));
            if (!model)
                throw CommandError(QStringLiteral("%1 is not a list model").arg(property));
            const int last = count < 0 ? model->rowCount() - 1 : from + count - 1;
            return modelRows(model, from, last).toVariantList();
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
        const QString name = args.value(QStringLiteral("object")).toString();
        QPointer<QObject> o = object(name);
        const QByteArray method = args.value(QStringLiteral("method")).toString().toUtf8();
        const QVariantList callArgs = args.value(QStringLiteral("args")).toList();
        const bool claims = callArgs.size() == 2 && m_releases.contains(name + QLatin1Char('.') + QString::fromUtf8(method));
        std::shared_ptr<Connection> connection = claims ? CommandServer::currentConnection() : nullptr;
        return CommandServer::onGui([this, o, name, method, callArgs, connection]() -> QVariant {
            if (!o)
                throw CommandError(QStringLiteral("object is gone"));
            QVariant result = invokeByName(o, method.constData(), callArgs, true);
            if (connection)
                noteClaim(name, o, method, callArgs, connection);
            return result;
        }, timeoutOf(args));
    });

    server.registerOp(QStringLiteral("subscribe"), [this](const QVariantMap &args) { return subscribe(args); });

    // Media streams (streamhub.h): binary records on this connection.
    // When pairing lands this needs an authorised client like every op.
    server.registerOp(QStringLiteral("stream"), [this](const QVariantMap &args) -> QVariant {
        const QString objectName = args.value(QStringLiteral("object")).toString();
        object(objectName);   // an unknown object is an error, as for any op
        const QString name = objectName + QLatin1Char('.') + args.value(QStringLiteral("name")).toString();
        std::shared_ptr<Connection> connection = CommandServer::currentConnection();
        if (!connection)
            throw CommandError(QStringLiteral("stream needs a connection"));
        StreamHub &hub = StreamHub::instance();
        const int id = hub.subscribe(name, connection);
        if (id == 0)
            throw CommandError(hub.names().contains(name) ? QStringLiteral("too many streams on this connection")
                                                          : QStringLiteral("%1 has no stream %2")
                                                                .arg(objectName, args.value(QStringLiteral("name")).toString()));
        const quint64 key = connection->serial();
        connection->onClose([key] { StreamHub::instance().unsubscribeAll(key); });
        QVariantMap reply = hub.info(name);
        reply.insert(QStringLiteral("stream"), id);
        return reply;
    });
    server.registerOp(QStringLiteral("unstream"), [](const QVariantMap &args) -> QVariant {
        std::shared_ptr<Connection> connection = CommandServer::currentConnection();
        if (!connection)
            throw CommandError(QStringLiteral("unstream needs a connection"));
        StreamHub::instance().unsubscribe(connection, args.value(QStringLiteral("stream")).toInt());
        return true;
    });
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

bool ServiceBridge::trusted(const Connection &c) const
{
    if (!m_pairing)
        return true;
    if (c.peerLoopback() && m_pairing->loopbackOpen())
        return true;
    const QString id = c.tokenId();
    return !id.isEmpty() && m_pairing->tokenIdValid(id);
}

void ServiceBridge::releaseOnClose(const QString &object, const QString &method, const QVariant &release)
{
    m_releases.insert(object + QLatin1Char('.') + method, release);
}

void ServiceBridge::noteClaim(const QString &objectName, QObject *o, const QByteArray &method, const QVariantList &args,
                              const std::shared_ptr<Connection> &connection)
{
    const QVariant release = m_releases.value(objectName + QLatin1Char('.') + QString::fromUtf8(method));
    const QString consumer = args.at(0).toString();
    const QVariant value = args.at(1);
    bool releasing = false;
    if (release.typeId() == QMetaType::Bool)
        releasing = !value.toBool();
    else if (release.canConvert<QVariantList>() && release.typeId() != QMetaType::QString)
        releasing = value.toList().isEmpty();
    else
        releasing = value == release;

    const QString key = objectName + QLatin1Char('.') + QString::fromUtf8(method) + QLatin1Char('\n') + consumer;
    const quint64 conn = connection->serial();
    if (releasing) {
        m_claims->byKey.remove(key);
        return;
    }
    m_claims->byKey.insert(key, Claim{o, method, consumer, release, conn});   // the latest caller holds it
    if (m_claims->hooked.contains(conn))
        return;
    m_claims->hooked.insert(conn, true);
    std::weak_ptr<Claims> weak = m_claims;
    connection->onClose([weak, conn] {
        auto claims = weak.lock();
        if (!claims)
            return;
        claims->hooked.remove(conn);
        for (auto it = claims->byKey.begin(); it != claims->byKey.end();) {
            if (it->connection != conn) {
                ++it;
                continue;
            }
            const Claim c = *it;
            it = claims->byKey.erase(it);
            if (!c.object)
                continue;
            try {
                invokeByName(c.object, c.method.constData(), {c.consumer, c.release});
            } catch (const CommandError &e) {
                qWarning("service: releasing %s for a closed client failed: %s", qPrintable(c.consumer), e.what());
            }
        }
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
                for (int i = MemberScope::methodStart(mo); i < mo->methodCount(); ++i)
                    if (mo->method(i).methodType() == QMetaMethod::Signal && mo->method(i).access() == QMetaMethod::Public
                        && mo->method(i).name() == sig.toUtf8())
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
                if (pi < MemberScope::propertyStart(mo))
                    fail(QStringLiteral("%1 has no property %2").arg(name, prop));
                const QMetaProperty p = mo->property(pi);
                if (QAbstractItemModel *model = modelIn(p.read(o))) {
                    // A list: its row changes, starting with all of it
                    QPointer<QAbstractItemModel> m = model;
                    auto send = [sub, prop](const QString &change, int first, int last, const QJsonArray *rows,
                                            bool now = false) {
                        QJsonObject f{{QStringLiteral("change"), change}, {QStringLiteral("first"), first},
                                      {QStringLiteral("last"), last}};
                        if (rows)
                            f.insert(QStringLiteral("rows"), *rows);
                        sub->send(QStringLiteral("model"), prop, f, now);
                    };
                    QObject::connect(model, &QAbstractItemModel::modelReset, sub, [m, send] {
                        const QJsonArray rows = modelRows(m, 0, m->rowCount() - 1);
                        send(QStringLiteral("reset"), 0, m->rowCount() - 1, &rows);
                    });
                    QObject::connect(model, &QAbstractItemModel::rowsInserted, sub, [m, send](const QModelIndex &, int first, int last) {
                        const QJsonArray rows = modelRows(m, first, last);
                        send(QStringLiteral("insert"), first, last, &rows);
                    });
                    QObject::connect(model, &QAbstractItemModel::rowsRemoved, sub, [send](const QModelIndex &, int first, int last) {
                        send(QStringLiteral("remove"), first, last, nullptr);
                    });
                    QObject::connect(model, &QAbstractItemModel::dataChanged, sub, [m, send](const QModelIndex &tl, const QModelIndex &br) {
                        const QJsonArray rows = modelRows(m, tl.row(), br.row());
                        send(QStringLiteral("update"), tl.row(), br.row(), &rows);
                    });
                    const QJsonArray all = modelRows(model, 0, model->rowCount() - 1);
                    send(QStringLiteral("reset"), 0, model->rowCount() - 1, &all, true);
                    continue;
                }
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
        const quint64 key = connection->serial();
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
    const quint64 key = connection->serial();
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
