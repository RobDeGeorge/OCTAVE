#include "adminchannel.h"

#include "servicepairing.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>

#if defined(Q_OS_UNIX) && !defined(Q_OS_WASM)
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcAdmin, "octave.service.admin")

namespace OctaveService {

namespace {
constexpr int kMaxLine = 256;

QString userName()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
#if defined(Q_OS_UNIX) && !defined(Q_OS_WASM)
    user += QLatin1Char(':') + QString::number(getuid());
#endif
    return user;
}

#if defined(Q_OS_UNIX) && !defined(Q_OS_WASM)
// The uid at the other end of a connected Unix socket, or -1
long long peerUid(qintptr fd)
{
#if defined(SO_PEERCRED)
    struct ucred cred {};
    socklen_t len = sizeof cred;
    if (getsockopt(int(fd), SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
        return cred.uid;
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (getpeereid(int(fd), &uid, &gid) == 0)
        return uid;
#endif
    return -1;
}
#endif

QByteArray reply(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}
} // namespace

AdminChannel::AdminChannel(ServicePairing *pairing, QObject *parent)
    : QObject(parent)
    , m_pairing(pairing)
{
}

AdminChannel::~AdminChannel()
{
    if (m_server)
        m_server->close();   // removes the socket file
}

QString AdminChannel::nameFor(const QString &configDir)
{
    const QString canonical = QFileInfo(configDir).canonicalFilePath().isEmpty()
                                  ? QDir::cleanPath(QDir(configDir).absolutePath())
                                  : QFileInfo(configDir).canonicalFilePath();
    const QByteArray hash = QCryptographicHash::hash((userName() + QLatin1Char('\n') + canonical).toUtf8(),
                                                     QCryptographicHash::Sha256).toHex().left(16);
    const QString name = QStringLiteral("octave-admin-%1").arg(QString::fromLatin1(hash));
#if defined(Q_OS_UNIX) && !defined(Q_OS_WASM)
    // A folder only this user can enter, when there is one (and the path
    // fits a Unix socket address); else the bare name in the temp folder,
    // where the socket's own mode keeps others out
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty() && (runtime + QLatin1Char('/') + name).toLocal8Bit().size() < 100)
        return runtime + QLatin1Char('/') + name;
#endif
    return name;
}

bool AdminChannel::listen(const QString &configDir)
{
    m_name = nameFor(configDir);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // A socket left by an instance that crashed: nothing answers on it
    {
        QLocalSocket probe;
        probe.connectToServer(m_name);
        if (probe.waitForConnected(300)) {
            qCWarning(lcAdmin).noquote() << "another OCTAVE with this config already has the admin channel; --pair-code reaches that one";
            return false;
        }
        QLocalServer::removeServer(m_name);
    }
    if (!m_server->listen(m_name)) {
        qCWarning(lcAdmin).noquote() << "admin channel not started:" << m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &AdminChannel::serve);
    qCInfo(lcAdmin).noquote() << "admin channel (octave --pair-code) on" << m_server->fullServerName();
    return true;
}

void AdminChannel::serve()
{
    while (QLocalSocket *s = m_server->nextPendingConnection()) {
        connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
#if defined(Q_OS_UNIX) && !defined(Q_OS_WASM)
        const long long uid = peerUid(s->socketDescriptor());
        if (uid != (long long)getuid()) {
            qCWarning(lcAdmin) << "refused a connection from uid" << uid;
            s->abort();
            s->deleteLater();
            continue;
        }
#endif
        // One line, soon, or nothing
        QTimer::singleShot(5000, s, [s] { s->abort(); });
        connect(s, &QLocalSocket::readyRead, s, [this, s] {
            if (!s->canReadLine()) {
                if (s->bytesAvailable() > kMaxLine)
                    s->abort();
                return;
            }
            const QJsonObject req = QJsonDocument::fromJson(s->readLine(kMaxLine)).object();
            QJsonObject out;
            if (req.value(QLatin1String("op")).toString() != QLatin1String("pairCode")) {
                out = {{"ok", false}, {"error", "unknown op (the admin channel has only pairCode)"}};
            } else if (!m_pairing) {
                out = {{"ok", false}, {"error", "no pairing in this run"}};
            } else {
                m_pairing->startPairing();
                out = {{"ok", true}, {"code", m_pairing->pairingCode()}, {"secondsLeft", m_pairing->pairingSecondsLeft()}};
                qCInfo(lcAdmin) << "pairing code shown for octave --pair-code";
            }
            s->write(reply(out));
            s->flush();
            s->disconnectFromServer();
        });
    }
}

QVariantMap AdminChannel::requestPairCode(const QString &configDir, int timeoutMs)
{
    QLocalSocket s;
    s.connectToServer(nameFor(configDir));
    if (!s.waitForConnected(timeoutMs))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("no running OCTAVE with the service on for %1").arg(configDir)}};
    s.write("{\"op\":\"pairCode\"}\n");
    s.flush();
    QByteArray line;
    while (!line.endsWith('\n') && line.size() <= kMaxLine * 4) {
        if (!s.bytesAvailable() && !s.waitForReadyRead(timeoutMs))
            break;
        line += s.readAll();
    }
    const QJsonObject o = QJsonDocument::fromJson(line).object();
    if (o.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no answer from OCTAVE")}};
    return o.toVariantMap();
}

} // namespace OctaveService
