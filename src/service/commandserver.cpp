#include "commandserver.h"

#include <QColor>
#include <QCoreApplication>
#include <QJSValue>
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

#include <atomic>
#include <cstdio>
#include <memory>

Q_LOGGING_CATEGORY(lcCommand, "octave.service")

namespace OctaveService {

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
        QByteArray buf;
        while (!m_stopping && sock.state() == QAbstractSocket::ConnectedState) {
            if (!sock.waitForReadyRead(200)) {
                if (sock.state() != QAbstractSocket::ConnectedState)
                    break;
                continue;
            }
            buf += sock.readAll();
            int nl;
            while ((nl = buf.indexOf('\n')) >= 0) {
                const QByteArray line = buf.left(nl).trimmed();
                buf.remove(0, nl + 1);
                if (line.isEmpty())
                    continue;
                sock.write(m_owner->handleLine(line));
                sock.waitForBytesWritten(10000);
            }
        }
    }

    CommandServer *m_owner;
    std::atomic<bool> m_stopping{false};
    QMutex m_lock;
    QList<QThread *> m_clients;
};

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
        ok = m_server->listen(QHostAddress::LocalHost, m_port);
        if (!ok)
            qCWarning(lcCommand) << "command server could not listen on 127.0.0.1:" << m_port << ":"
                                 << m_server->errorString();
    }, Qt::BlockingQueuedConnection);
    if (!ok) {
        stop();
        return false;
    }
    qCInfo(lcCommand) << "command server listening on 127.0.0.1:" << m_port;
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
        const QJsonObject cmd = doc.object();
        const QString action = cmd.value(QStringLiteral("action")).toString();
        const QVariantMap args = cmd.value(QStringLiteral("args")).toObject().toVariantMap();
        try {
            const QVariant result = jsonSafe(dispatch(action, args));
            reply[QStringLiteral("ok")] = true;
            reply[QStringLiteral("result")] = QJsonValue::fromVariant(result);
        } catch (const std::exception &e) {
            qCWarning(lcCommand).noquote() << "command error [" + action + "]:" << e.what();
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

QVariant invokeByName(QObject *obj, const char *method, const QVariantList &args)
{
    if (!obj)
        throw CommandError(QStringLiteral("no object for %1()").arg(QLatin1String(method)));
    const QMetaObject *mo = obj->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.name() != method || m.parameterCount() != args.size())
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
            ok = a.convert(pt);
            converted.append(a);
        }
        if (!ok)
            continue;
        QVariant ret;
        const QMetaType rt = m.returnMetaType();
        if (rt.isValid() && rt.id() != QMetaType::Void)
            ret = QVariant(rt);
        QVarLengthArray<void *, 8> argv;
        argv.append(ret.isValid() ? ret.data() : nullptr);
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
