// servicepairing.h
//
// Who may use the service (protocol v2, TODO/open-backend.md item 11). A
// connection is trusted when its peer is on this device and
// serviceLoopbackOpen is on (the default), or when it has presented a token
// from pairing. Everything but `hello` and `pair` needs trust, checked
// as each op runs; /art and /export/* need it too (a media ticket in
// ?ticket=, or Authorization: Bearer <token> for non-browser clients).
//
// Pairing: the head unit shows a 6-digit code (Settings > Service), valid
// for kCodeLifetimeMs, single use, void after kMaxTries wrong attempts, and
// at most one attempt a second across all clients; an address with
// kMaxFailuresPerPeer wrong codes is refused until the app restarts, and
// every refusal is logged with the address (never the code). A right code
// buys a
// token (32 random bytes, hex); only its SHA-256 is kept, in the settings
// file key serviceTokens (file-only: no save_* slot, apply_settings or
// reset reaches it), with a name and dates. Revoking a token from Settings
// closes every connection using it (their subscriptions and streams end)
// and voids its media tickets.
//
// Permissions (servicescopes.h): `pair` may ask for scopes ("read",
// "media", ...); without them a device gets `full`, as every device did
// before scopes existed (and stored devices without a list still have).
// Every op checks the member's scope as it runs, so narrowing or revoking
// takes effect at once.
//
// Media tickets (`mediaTicket` op): no long-lived token in a URL. A trusted
// connection gets a random ticket valid kTicketLifetimeMs, bound to its
// token (revoked with it), for <img src="/art?ticket=..."> and downloads.
//
// serviceLoopbackOpen trusts any process on this device, a local reverse
// proxy included: a proxy setup must set it false. While an installed exec
// frontend is the screen (frontendmanager.h) main.cpp turns it off for the
// run, so that frontend, which is a process on this device, has only the
// scopes of the session token OCTAVE gives it (addSessionDevice).
//
// This object is a QML context property for the Settings page but NOT a
// service object: no client, paired or not, can mint a code, list devices,
// revoke or switch the service. The check functions are thread-safe (the
// connection threads call them); everything that writes the settings runs
// on the GUI thread.

#ifndef OCTAVE_SERVICE_SERVICEPAIRING_H
#define OCTAVE_SERVICE_SERVICEPAIRING_H

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <atomic>

class SettingsManager;

class ServicePairing : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY pairingChanged)   // "" when none
    Q_PROPERTY(int pairingSecondsLeft READ pairingSecondsLeft NOTIFY pairingChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)      // [{id, name, created, lastUsed, scopes, session}]
    Q_PROPERTY(bool serviceEnabled READ serviceEnabled NOTIFY serviceEnabledChanged)
    Q_PROPERTY(bool loopbackOpen READ loopbackOpen CONSTANT)

public:
    static constexpr int kCodeLifetimeMs = 120000;
    static constexpr int kMaxTries = 20;               // wrong tries, all addresses, before a code is void
    static constexpr int kMaxTickets = 256;
    static constexpr int kAttemptIntervalMs = 1000;
    static constexpr int kMaxFailuresPerPeer = 10;     // per address (an IPv6 /64), until restart
    static constexpr qint64 kTicketLifetimeMs = 10 * 60 * 1000;

    explicit ServicePairing(SettingsManager *settings, QObject *parent = nullptr);

    QString pairingCode() const;
    int pairingSecondsLeft() const;
    QVariantList devices() const;
    bool serviceEnabled() const { return m_serviceEnabled; }
    bool loopbackOpen() const { return m_loopbackOpen; }

    // Settings > Service
    Q_INVOKABLE void startPairing();
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void revoke(const QString &id);
    // Narrow (or widen) what a paired device may do; unknown scopes are ignored
    Q_INVOKABLE void setDeviceScopes(const QString &id, const QStringList &scopes);
    // A token for a program OCTAVE starts itself (an exec frontend): a
    // device held in memory only, never written to the settings file, gone
    // at exit or with revoke(*id). `scopes` as in pair() (empty: full).
    // GUI thread
    QString addSessionDevice(const QString &name, const QStringList &scopes, QString *id);
    // Loopback trust for this run only, not saved; before the service starts
    void setLoopbackOpenForRun(bool open) { m_loopbackOpen = open; }
    // The service at the next start (serviceEnabled); takes effect then
    Q_INVOKABLE void setServiceEnabled(bool on);

    // ── Connection threads ──
    // A token for `code` (from `peer`, an address for the log and the
    // per-address limit) holding `scopes` (empty: full), or "" with *error set
    QString pair(const QString &code, const QString &name, const QString &peer, QString *error,
                 const QStringList &scopes = {});
    // The scopes of a paired device (empty if there is none with that id)
    QStringList scopesOf(const QString &id) const;
    // Whether paired device `id` may do what needs `scope`
    bool allows(const QString &id, const QString &scope) const;
    // The token's id if it is a paired device's, else ""
    QString checkToken(const QString &token);
    bool tokenIdValid(const QString &id) const;
    void unpair(const QString &id);
    // A media ticket for a trusted connection (`tokenId` "" = trusted as a
    // local process: then only good from this device)
    QString mintTicket(const QString &tokenId);
    bool checkTicket(const QString &ticket, bool peerLoopback) const { return ticketAllows(ticket, peerLoopback, QStringLiteral("read")); }
    // A valid ticket whose device may do what needs `scope`
    bool ticketAllows(const QString &ticket, bool peerLoopback, const QString &scope) const;
    // The address a limit counts by: IPv4 as is, IPv6 by its /64
    static QString peerKey(const QString &address);

signals:
    void pairingChanged();
    void devicesChanged();
    void serviceEnabledChanged();
    // A device paired (the Settings card closes its code)
    void paired(const QString &name);
    // A device was revoked: close its connections
    void revoked(const QString &id);

private:
    struct Device {
        QString id, name, hash;
        qint64 created = 0, lastUsed = 0;
        QStringList scopes{QStringLiteral("full")};
        bool session = false;   // addSessionDevice: not persisted
    };
    void persist();   // GUI thread
    void persistLater();
    static QString hashOf(const QString &token);
    static QStringList normalizedScopes(const QStringList &scopes, QString *unknown = nullptr);


    SettingsManager *m_settings;
    mutable QMutex m_lock;
    QList<Device> m_devices;
    QString m_code;
    QElapsedTimer m_codeAge;
    int m_tries = 0;
    QElapsedTimer m_lastAttempt;
    QHash<QString, int> m_failuresByPeer;
    struct Ticket {
        QString tokenId;
        qint64 expires = 0;
        quint64 seq = 0;   // minting order: the oldest goes first when full
    };
    quint64 m_ticketSeq = 0;
    QHash<QString, Ticket> m_tickets;   // by the ticket's SHA-256
    QElapsedTimer m_lastUsedWrite;
    QTimer m_tick;   // the countdown on screen, and the code's expiry
    bool m_serviceEnabled = false;
    std::atomic<bool> m_loopbackOpen{true};
};

#endif // OCTAVE_SERVICE_SERVICEPAIRING_H
