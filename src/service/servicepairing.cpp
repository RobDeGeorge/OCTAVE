#include "servicepairing.h"

#include "../managers/settingsmanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRandomGenerator>

Q_LOGGING_CATEGORY(lcPairing, "octave.service.pairing")

namespace {
QString randomHex(int bytes)
{
    QByteArray b(bytes, '\0');
    QRandomGenerator *rng = QRandomGenerator::system();   // the OS CSPRNG
    for (int i = 0; i < bytes; ++i)
        b[i] = char(rng->bounded(256));
    return QString::fromLatin1(b.toHex());
}

// Equal without returning early on the first difference
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    quint8 diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= quint8(a.at(i)) ^ quint8(b.at(i));
    return diff == 0;
}
} // namespace

ServicePairing::ServicePairing(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (m_settings) {
        m_serviceEnabled = m_settings->get_setting(QStringLiteral("serviceEnabled")).toBool();
        const QVariant open = m_settings->get_setting(QStringLiteral("serviceLoopbackOpen"));
        m_loopbackOpen = open.isValid() ? open.toBool() : true;
        for (const QVariant &v : m_settings->get_setting(QStringLiteral("serviceTokens")).toList()) {
            const QVariantMap m = v.toMap();
            Device d{m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("name")).toString(),
                     m.value(QStringLiteral("hash")).toString(), m.value(QStringLiteral("created")).toLongLong(),
                     m.value(QStringLiteral("lastUsed")).toLongLong()};
            if (!d.id.isEmpty() && d.hash.size() == 64)
                m_devices.append(d);
        }
    }
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        bool expired = false;
        {
            QMutexLocker l(&m_lock);
            if (!m_code.isEmpty() && m_codeAge.elapsed() >= kCodeLifetimeMs) {
                m_code.clear();
                expired = true;
            }
        }
        if (expired)
            m_tick.stop();
        emit pairingChanged();
    });
}

QString ServicePairing::hashOf(const QString &token)
{
    return QString::fromLatin1(QCryptographicHash::hash(token.toLatin1(), QCryptographicHash::Sha256).toHex());
}

QString ServicePairing::pairingCode() const
{
    QMutexLocker l(&m_lock);
    return m_code;
}

int ServicePairing::pairingSecondsLeft() const
{
    QMutexLocker l(&m_lock);
    return m_code.isEmpty() ? 0 : int(qMax<qint64>(0, kCodeLifetimeMs - m_codeAge.elapsed()) / 1000);
}

QVariantList ServicePairing::devices() const
{
    QMutexLocker l(&m_lock);
    QVariantList out;
    for (const Device &d : m_devices)
        out.append(QVariantMap{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name},
                               {QStringLiteral("created"), d.created}, {QStringLiteral("lastUsed"), d.lastUsed}});
    return out;
}

void ServicePairing::startPairing()
{
    {
        QMutexLocker l(&m_lock);
        m_code = QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QLatin1Char('0'));
        m_codeAge.start();
        m_tries = 0;
    }
    m_tick.start();
    qCInfo(lcPairing) << "pairing code shown";
    emit pairingChanged();
}

void ServicePairing::cancelPairing()
{
    {
        QMutexLocker l(&m_lock);
        m_code.clear();
    }
    m_tick.stop();
    emit pairingChanged();
}

QString ServicePairing::pair(const QString &code, const QString &name, const QString &peer, QString *error)
{
    QString token, deviceName;
    {
        QMutexLocker l(&m_lock);
        auto refuse = [&](const QString &why) {
            qCWarning(lcPairing).noquote() << "pairing refused for" << peer << ":" << why;   // never the code
            if (error)
                *error = why;
            return QString();
        };
        if (m_failuresByPeer.value(peer) >= kMaxFailuresPerPeer)
            return refuse(QStringLiteral("too many wrong codes from this address"));
        // One attempt a second across every client
        if (m_lastAttempt.isValid() && m_lastAttempt.elapsed() < kAttemptIntervalMs)
            return refuse(QStringLiteral("too fast, try again in a second"));
        m_lastAttempt.start();
        if (m_code.isEmpty() || m_codeAge.elapsed() >= kCodeLifetimeMs)
            return refuse(QStringLiteral("no pairing code is showing on the head unit"));
        if (!sameBytes(code.toLatin1(), m_code.toLatin1())) {
            ++m_failuresByPeer[peer];
            if (++m_tries >= kMaxTries)
                m_code.clear();   // void: the head unit has to show a new one
            return refuse(QStringLiteral("wrong code"));
        }
        m_code.clear();   // single use
        token = randomHex(32);
        deviceName = name.simplified().left(64);
        if (deviceName.isEmpty())
            deviceName = QStringLiteral("Device");
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_devices.append(Device{randomHex(8), deviceName, hashOf(token), now, now});
    }
    qCInfo(lcPairing).noquote() << "paired" << deviceName << "from" << peer;
    QMetaObject::invokeMethod(this, [this, deviceName] {
        m_tick.stop();
        persist();
        emit pairingChanged();
        emit devicesChanged();
        emit paired(deviceName);
    }, Qt::QueuedConnection);
    return token;
}

QString ServicePairing::checkToken(const QString &token)
{
    if (token.size() != 64)
        return {};
    const QByteArray hash = hashOf(token).toLatin1();
    QString id;
    {
        QMutexLocker l(&m_lock);
        for (Device &d : m_devices) {
            if (sameBytes(hash, d.hash.toLatin1())) {
                id = d.id;
                d.lastUsed = QDateTime::currentMSecsSinceEpoch();
            }
        }
    }
    if (!id.isEmpty())
        persistLater();
    return id;
}

bool ServicePairing::tokenIdValid(const QString &id) const
{
    QMutexLocker l(&m_lock);
    for (const Device &d : m_devices)
        if (d.id == id)
            return true;
    return false;
}

QString ServicePairing::mintTicket(const QString &tokenId)
{
    QMutexLocker l(&m_lock);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_tickets.begin(); it != m_tickets.end();)
        it = it->expires <= now ? m_tickets.erase(it) : std::next(it);
    const QString ticket = randomHex(16);
    m_tickets.insert(hashOf(ticket), Ticket{tokenId, now + kTicketLifetimeMs});
    return ticket;
}

bool ServicePairing::checkTicket(const QString &ticket, bool peerLoopback) const
{
    if (ticket.size() != 32)
        return false;
    QMutexLocker l(&m_lock);
    const auto it = m_tickets.constFind(hashOf(ticket));
    if (it == m_tickets.constEnd() || it->expires <= QDateTime::currentMSecsSinceEpoch())
        return false;
    if (it->tokenId.isEmpty())   // minted by a trusted local connection: local use only
        return peerLoopback && m_loopbackOpen;
    for (const Device &d : m_devices)
        if (d.id == it->tokenId)
            return true;
    return false;   // its token was revoked
}

void ServicePairing::unpair(const QString &id)
{
    QMetaObject::invokeMethod(this, [this, id] { revoke(id); }, Qt::QueuedConnection);
}

void ServicePairing::revoke(const QString &id)
{
    bool found = false;
    {
        QMutexLocker l(&m_lock);
        for (int i = m_devices.size() - 1; i >= 0; --i) {
            if (m_devices.at(i).id == id) {
                m_devices.removeAt(i);
                found = true;
            }
        }
        for (auto it = m_tickets.begin(); it != m_tickets.end();)
            it = it->tokenId == id ? m_tickets.erase(it) : std::next(it);
    }
    if (!found)
        return;
    qCInfo(lcPairing).noquote() << "revoked device" << id;
    persist();
    emit devicesChanged();
    emit revoked(id);
}

void ServicePairing::setServiceEnabled(bool on)
{
    if (on == m_serviceEnabled)
        return;
    m_serviceEnabled = on;
    if (m_settings)
        m_settings->saveFileOnlySetting(QStringLiteral("serviceEnabled"), on);
    emit serviceEnabledChanged();
}

void ServicePairing::persistLater()
{
    // lastUsed is a convenience: at most one write a minute for it
    QMetaObject::invokeMethod(this, [this] {
        if (!m_lastUsedWrite.isValid() || m_lastUsedWrite.elapsed() > 60000) {
            m_lastUsedWrite.start();
            persist();
            emit devicesChanged();
        }
    }, Qt::QueuedConnection);
}

void ServicePairing::persist()
{
    if (!m_settings)
        return;
    QVariantList list;
    {
        QMutexLocker l(&m_lock);
        for (const Device &d : m_devices)
            list.append(QVariantMap{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name},
                                    {QStringLiteral("hash"), d.hash}, {QStringLiteral("created"), d.created},
                                    {QStringLiteral("lastUsed"), d.lastUsed}});
    }
    m_settings->saveFileOnlySetting(QStringLiteral("serviceTokens"), list);
}
