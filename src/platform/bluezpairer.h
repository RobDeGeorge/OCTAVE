#ifndef OCTAVE_BLUEZPAIRER_H
#define OCTAVE_BLUEZPAIRER_H

// Pairs and trusts a Bluetooth Classic device through BlueZ over D-Bus
// (desktop Linux / Raspberry Pi / Orange Pi only). Qt 6 has no way to answer
// a legacy PIN request on BlueZ, and ELM327 clones all use one (1234, or 0000
// on some), so OBDAdapterFinder pairs through here: it registers its own
// org.bluez.Agent1 for the duration of the Pair() call, answers the PIN
// itself, retries the next common PIN on an authentication failure, then sets
// Trusted so BlueZ lets the adapter reconnect without asking again.
// BlueZ routes the pairing callbacks to the agent of the client that called
// Pair(), so the desktop's own agent (and its PIN dialog) is never involved.
// Mirrors backend/obd_adapter_finder.py (_BluezPairer).

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDBusObjectPath>

class QDBusPendingCallWatcher;

class BluezPinAgent : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
public:
    explicit BluezPinAgent(QObject *parent = nullptr) : QObject(parent) {}
    QString pin = QStringLiteral("1234");

public slots:
    void Release() {}
    QString RequestPinCode(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void Cancel() {}
};

class BluezPairer : public QObject
{
    Q_OBJECT
public:
    explicit BluezPairer(QObject *parent = nullptr);
    ~BluezPairer() override;

    // Pair (when not already paired) and trust `mac`. Emits finished() once.
    // When pairing fails but the adapter was reachable, it is still trusted
    // and `tryAnyway` is set: some adapters (NEXAS) accept an RFCOMM
    // connection from a trusted, unpaired host.
    void start(const QString &mac);
    void cancel();
    bool isRunning() const { return !m_mac.isEmpty(); }

    // PINs tried in order; ELM327 clones ship with 1234 or 0000
    static const QStringList &pins();

signals:
    void progress(const QString &message);
    void finished(bool ok, bool tryAnyway, const QString &message);

private:
    QString findDevicePath(const QString &mac) const;
    bool registerAgent();
    void unregisterAgent();
    void callPair();
    void onPairReply(QDBusPendingCallWatcher *watcher);
    bool trust();
    void trustAndFinish();
    void finish(bool ok, bool tryAnyway, const QString &message);

    BluezPinAgent *m_agent = nullptr;
    bool m_agentRegistered = false;
    QString m_mac;
    QString m_devicePath;
    int m_pinIndex = 0;
};

#endif  // OCTAVE_BLUEZPAIRER_H
