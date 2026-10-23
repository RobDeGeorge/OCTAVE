#include "servicepairing.h"
#include "servicescopes.h"

#include "../managers/settingsmanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
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
        m_serviceEnabled = m_settings->fileOnlySetting(QStringLiteral("serviceEnabled")).toBool();
        const QVariant open = m_settings->fileOnlySetting(QStringLiteral("serviceLoopbackOpen"));
        m_loopbackOpen = open.isValid() ? open.toBool() : true;
        for (const QVariant &v : m_settings->fileOnlySetting(QStringLiteral("serviceTokens")).toList()) {
            const QVariantMap m = v.toMap();
            Device d{m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("name")).toString(),
                     m.value(QStringLiteral("hash")).toString(), m.value(QStringLiteral("created")).toLongLong(),
                     m.value(QStringLiteral("lastUsed")).toLongLong()};
            if (m.contains(QStringLiteral("scopes")))
                d.scopes = normalizedScopes(m.value(QStringLiteral("scopes")).toStringList());
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

QString ServicePairing::peerKey(const QString &address)
{
    const QHostAddress a(address);
    if (a.protocol() != QAbstractSocket::IPv6Protocol)
        return address;
    bool v4 = false;
    const quint32 mapped = a.toIPv4Address(&v4);
    if (v4)
        return QHostAddress(mapped).toString();
    Q_IPV6ADDR bytes = a.toIPv6Address();
    for (int i = 8; i < 16; ++i)
        bytes[i] = 0;
    return QHostAddress(bytes).toString() + QStringLiteral("/64");
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
                               {QStringLiteral("created"), d.created}, {QStringLiteral("lastUsed"), d.lastUsed},
                               {QStringLiteral("scopes"), d.scopes}, {QStringLiteral("session"), d.session}});
    return out;
}

QString ServicePairing::addSessionDevice(const QString &name, const QStringList &scopes, QString *id)
{
    const QStringList granted = normalizedScopes(scopes);
    const QString token = randomHex(32);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Device d{randomHex(8), name.simplified().left(64), hashOf(token), now, now, granted};
    d.session = true;
    {
        QMutexLocker l(&m_lock);
        m_devices.append(d);
    }
    if (id)
        *id = d.id;
    qCInfo(lcPairing).noquote() << "session token for" << d.name << "with" << granted.join(QLatin1Char(' '));
    emit devicesChanged();
    return token;
}

void ServicePairing::startPairing()
{
    {
        QMutexLocker l(&m_lock);
        m_code = QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QLatin1Char('0'));
        m_codeAge.start();
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

QString ServicePairing::pair(const QString &code, const QString &name, const QString &peer, QString *error,
                            const QStringList &scopes)
{
    QString token, deviceName;
    // A bad request costs no attempt and doesn't use the code
    QString unknown;
    const QStringList granted = normalizedScopes(scopes, &unknown);
    if (!unknown.isEmpty()) {
        if (error)
            *error = QStringLiteral("unknown permission %1 (known: %2)").arg(unknown, OctaveService::knownScopes().join(QStringLiteral(", ")));
        return {};
    }
    {
        QMutexLocker l(&m_lock);
        auto refuse = [&](const QString &why) {
            qCWarning(lcPairing).noquote() << "pairing refused for" << peer << ":" << why;   // never the code
            if (error)
                *error = why;
            return QString();
        };
        // Guesses are limited per address, never across addresses, so no
        // number of wrong codes from elsewhere voids this code or holds up
        // the owner's attempt; and never for this device's own processes,
        // which are trusted anyway (and a local loop must not lock the
        // network out). A refusal here costs the address nothing.
        const QString key = peerKey(peer);
        const bool local = peer.isEmpty() || QHostAddress(peer).isLoopback();
        if (!m_clock.isValid())
            m_clock.start();
        const qint64 now = m_clock.elapsed();
        if (!local) {
            if (m_failuresByPeer.value(key) >= kMaxFailuresPerPeer)
                return refuse(QStringLiteral("too many wrong codes from this address"));
            if (const auto it = m_lastAttemptByPeer.constFind(key);
                it != m_lastAttemptByPeer.cend() && now - it.value() < kAttemptIntervalMs)
                return refuse(QStringLiteral("too fast, try again in a second"));
            // The ceiling over every address: a guesser spread over many
            // still sees at most kMaxAttemptsPerSecond codes a second
            if (now - m_windowStart >= 1000) {
                m_windowStart = now;
                m_windowAttempts = 0;
            }
            if (m_windowAttempts >= kMaxAttemptsPerSecond)
                return refuse(QStringLiteral("too many pairing attempts right now, try again in a second"));
            ++m_windowAttempts;
            // A table of last attempts by address, kept to the recent ones
            if (m_lastAttemptByPeer.size() > 4096)
                m_lastAttemptByPeer.removeIf([now](const auto &e) { return now - e.value() >= kAttemptIntervalMs; });
            m_lastAttemptByPeer.insert(key, now);
        }
        if (m_code.isEmpty() || m_codeAge.elapsed() >= kCodeLifetimeMs)
            return refuse(QStringLiteral("no pairing code is showing on the head unit"));
        if (!sameBytes(code.toLatin1(), m_code.toLatin1())) {
            if (!local)
                ++m_failuresByPeer[key];
            return refuse(QStringLiteral("wrong code"));
        }
        m_code.clear();   // single use
        token = randomHex(32);
        deviceName = name.simplified().left(64);
        if (deviceName.isEmpty())
            deviceName = QStringLiteral("Device");
        const qint64 when = QDateTime::currentMSecsSinceEpoch();
        m_devices.append(Device{randomHex(8), deviceName, hashOf(token), when, when, granted});
    }
    qCInfo(lcPairing).noquote() << "paired" << deviceName << "from" << peer << "with" << granted.join(QLatin1Char(' '));
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

QStringList ServicePairing::normalizedScopes(const QStringList &scopes, QString *unknown)
{
    QStringList out;
    for (const QString &s : scopes) {
        const QString scope = s.trimmed().toLower();
        if (!OctaveService::knownScopes().contains(scope)) {
            if (unknown && unknown->isEmpty())
                *unknown = s;
            continue;
        }
        if (!out.contains(scope))
            out.append(scope);
    }
    if (out.contains(QStringLiteral("full")) || out.isEmpty())
        return {QStringLiteral("full")};
    if (!out.contains(QStringLiteral("read")))
        out.prepend(QStringLiteral("read"));
    return out;
}

QStringList ServicePairing::scopesOf(const QString &id) const
{
    QMutexLocker l(&m_lock);
    for (const Device &d : m_devices)
        if (d.id == id)
            return d.scopes;
    return {};
}

bool ServicePairing::allows(const QString &id, const QString &scope) const
{
    QMutexLocker l(&m_lock);
    for (const Device &d : m_devices)
        if (d.id == id)
            return OctaveService::scopeAllows(d.scopes, scope);
    return false;
}

void ServicePairing::setDeviceScopes(const QString &id, const QStringList &scopes)
{
    const QStringList granted = normalizedScopes(scopes);
    bool changed = false;
    {
        QMutexLocker l(&m_lock);
        for (Device &d : m_devices)
            if (d.id == id && d.scopes != granted) {
                d.scopes = granted;
                changed = true;
            }
    }
    if (!changed)
        return;
    persist();
    emit devicesChanged();
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
    // A bounded table: past kMaxTickets the one expiring first goes
    while (m_tickets.size() >= kMaxTickets) {
        auto oldest = m_tickets.begin();
        for (auto it = m_tickets.begin(); it != m_tickets.end(); ++it)
            if (it->seq < oldest->seq)
                oldest = it;
        m_tickets.erase(oldest);
    }
    const QString ticket = randomHex(16);
    m_tickets.insert(hashOf(ticket), Ticket{tokenId, now + kTicketLifetimeMs, ++m_ticketSeq});
    return ticket;
}

bool ServicePairing::ticketAllows(const QString &ticket, bool peerLoopback, const QString &scope) const
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
            return OctaveService::scopeAllows(d.scopes, scope);
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
        for (const Device &d : m_devices) {
            if (d.session)
                continue;
            list.append(QVariantMap{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name},
                                    {QStringLiteral("hash"), d.hash}, {QStringLiteral("created"), d.created},
                                    {QStringLiteral("lastUsed"), d.lastUsed}, {QStringLiteral("scopes"), d.scopes}});
        }
    }
    m_settings->saveFileOnlySetting(QStringLiteral("serviceTokens"), list);
}
