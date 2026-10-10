#include "bluezpairer.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QTimer>

namespace {
const QString kBluez = QStringLiteral("org.bluez");
const QString kAgentPath = QStringLiteral("/org/octave/obd_pin_agent");
const QString kDeviceIface = QStringLiteral("org.bluez.Device1");
const QString kPropsIface = QStringLiteral("org.freedesktop.DBus.Properties");
// A legacy-PIN pairing waits on the adapter; BlueZ's own limit is ~30 s
constexpr int kPairTimeoutMs = 45000;
}

// ---------------------------------------------------------------------------
// Agent: answers whatever BlueZ asks with the PIN under test. SSP "just works"
// and numeric-comparison requests are accepted (an OBD dongle has no screen
// to compare against).
// ---------------------------------------------------------------------------

QString BluezPinAgent::RequestPinCode(const QDBusObjectPath &device)
{
    qDebug() << "[OBD-BT] PIN requested by" << device.path() << "-> answering" << pin;
    return pin;
}

void BluezPinAgent::DisplayPinCode(const QDBusObjectPath &, const QString &pincode)
{
    qDebug() << "[OBD-BT] DisplayPinCode" << pincode;
}

uint BluezPinAgent::RequestPasskey(const QDBusObjectPath &device)
{
    qDebug() << "[OBD-BT] Passkey requested by" << device.path() << "-> answering" << pin;
    return pin.toUInt();
}

void BluezPinAgent::DisplayPasskey(const QDBusObjectPath &, uint, ushort) {}
void BluezPinAgent::RequestConfirmation(const QDBusObjectPath &, uint) {}
void BluezPinAgent::RequestAuthorization(const QDBusObjectPath &) {}
void BluezPinAgent::AuthorizeService(const QDBusObjectPath &, const QString &) {}

// ---------------------------------------------------------------------------

BluezPairer::BluezPairer(QObject *parent)
    : QObject(parent)
    , m_agent(new BluezPinAgent(this))
{
}

BluezPairer::~BluezPairer()
{
    unregisterAgent();
}

const QStringList &BluezPairer::pins()
{
    static const QStringList p = {QStringLiteral("1234"), QStringLiteral("0000"),
                                  QStringLiteral("6789"), QStringLiteral("1111")};
    return p;
}

QString BluezPairer::findDevicePath(const QString &mac) const
{
    // BlueZ names device objects /org/bluez/hciN/dev_AA_BB_CC_DD_EE_FF.
    // Probe the first few controllers instead of parsing GetManagedObjects.
    const QString leaf = QStringLiteral("dev_") + mac.toUpper().replace(QLatin1Char(':'), QLatin1Char('_'));
    QDBusConnection bus = QDBusConnection::systemBus();
    for (int i = 0; i < 4; ++i) {
        const QString path = QStringLiteral("/org/bluez/hci%1/%2").arg(i).arg(leaf);
        QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, path, kPropsIface, QStringLiteral("Get"));
        msg << kDeviceIface << QStringLiteral("Address");
        const QDBusMessage reply = bus.call(msg, QDBus::Block, 2000);
        if (reply.type() == QDBusMessage::ReplyMessage)
            return path;
    }
    return {};
}

bool BluezPairer::registerAgent()
{
    if (m_agentRegistered)
        return true;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.registerObject(kAgentPath, m_agent, QDBusConnection::ExportAllSlots)) {
        qWarning() << "[OBD-BT] Could not export the PIN agent:" << bus.lastError().message();
        return false;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, QStringLiteral("/org/bluez"),
                                                      QStringLiteral("org.bluez.AgentManager1"),
                                                      QStringLiteral("RegisterAgent"));
    msg << QVariant::fromValue(QDBusObjectPath(kAgentPath)) << QStringLiteral("KeyboardDisplay");
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "[OBD-BT] RegisterAgent failed:" << reply.errorName() << reply.errorMessage();
        bus.unregisterObject(kAgentPath);
        return false;
    }
    m_agentRegistered = true;
    return true;
}

void BluezPairer::unregisterAgent()
{
    if (!m_agentRegistered)
        return;
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, QStringLiteral("/org/bluez"),
                                                      QStringLiteral("org.bluez.AgentManager1"),
                                                      QStringLiteral("UnregisterAgent"));
    msg << QVariant::fromValue(QDBusObjectPath(kAgentPath));
    bus.call(msg, QDBus::Block, 2000);
    bus.unregisterObject(kAgentPath);
    m_agentRegistered = false;
}

void BluezPairer::start(const QString &mac)
{
    if (isRunning())
        cancel();
    m_mac = mac.toUpper();
    m_pinIndex = 0;
    m_devicePath = findDevicePath(m_mac);
    if (m_devicePath.isEmpty()) {
        finish(false, false, QStringLiteral("The adapter dropped out of range. Scan again with the ignition on."));
        return;
    }

    // Already paired: only make sure it is trusted
    QDBusInterface dev(kBluez, m_devicePath, kDeviceIface, QDBusConnection::systemBus());
    if (dev.property("Paired").toBool()) {
        qDebug() << "[OBD-BT]" << m_mac << "already paired";
        trustAndFinish();
        return;
    }

    if (!registerAgent()) {
        finish(false, false, QStringLiteral("Couldn't talk to the Bluetooth service (BlueZ). Is bluetoothd running?"));
        return;
    }
    callPair();
}

void BluezPairer::callPair()
{
    m_agent->pin = pins().at(m_pinIndex);
    emit progress(m_pinIndex == 0 ? QStringLiteral("Pairing…")
                                  : QStringLiteral("Pairing… trying PIN %1").arg(m_agent->pin));
    qDebug() << "[OBD-BT] Pair" << m_mac << "PIN candidate" << m_agent->pin;
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, m_devicePath, kDeviceIface, QStringLiteral("Pair"));
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::systemBus().asyncCall(msg, kPairTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &BluezPairer::onPairReply);
}

void BluezPairer::onPairReply(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    if (!isRunning())
        return;  // cancelled
    QDBusPendingReply<> reply = *watcher;
    if (!reply.isError()) {
        trustAndFinish();
        return;
    }
    const QString err = reply.error().name();
    qDebug() << "[OBD-BT] Pair failed:" << err << reply.error().message();
    if (err == QLatin1String("org.bluez.Error.AlreadyExists")) {
        trustAndFinish();
        return;
    }
    if ((err == QLatin1String("org.bluez.Error.AuthenticationFailed")
         || err == QLatin1String("org.bluez.Error.AuthenticationRejected"))
        && m_pinIndex + 1 < pins().size()) {
        ++m_pinIndex;
        // The adapter needs a moment before it accepts a new attempt
        QTimer::singleShot(1500, this, [this] { if (isRunning()) callPair(); });
        return;
    }

    QString message;
    if (err.endsWith(QLatin1String("AuthenticationFailed")) || err.endsWith(QLatin1String("AuthenticationRejected")))
        message = QStringLiteral("The adapter refused every common PIN (%1). Check its manual for the PIN.")
                      .arg(pins().join(QStringLiteral(", ")));
    else if (err.endsWith(QLatin1String("AuthenticationTimeout")) || err.endsWith(QLatin1String("AuthenticationCanceled")))
        message = QStringLiteral("Pairing timed out. If the adapter has a pairing button, press it and try again.");
    else if (err.endsWith(QLatin1String("ConnectionAttemptFailed")) || err.endsWith(QLatin1String("NoReply"))
             || err == QLatin1String("org.freedesktop.DBus.Error.NoReply"))
        message = QStringLiteral("The adapter didn't answer. Turn the ignition on and check the adapter's light.");
    else if (err.endsWith(QLatin1String("InProgress")))
        message = QStringLiteral("Another pairing is already running. Try again in a few seconds.");
    else
        message = QStringLiteral("Pairing failed: %1").arg(reply.error().message());
    // An adapter that refused or ignored pairing may still take a connection
    // from a trusted host; one that never answered or is busy won't
    const bool tryAnyway = !err.endsWith(QLatin1String("InProgress"))
                           && !err.endsWith(QLatin1String("ConnectionAttemptFailed"))
                           && !err.endsWith(QLatin1String("NoReply"));
    if (tryAnyway)
        trust();
    finish(false, tryAnyway, message);
}

bool BluezPairer::trust()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, m_devicePath, kPropsIface, QStringLiteral("Set"));
    msg << kDeviceIface << QStringLiteral("Trusted") << QVariant::fromValue(QDBusVariant(true));
    const QDBusMessage reply = QDBusConnection::systemBus().call(msg, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "[OBD-BT] Could not trust" << m_mac << reply.errorMessage();
        return false;
    }
    return true;
}

void BluezPairer::trustAndFinish()
{
    trust();  // a paired adapter connects even if this fails
    finish(true, false, QStringLiteral("Paired"));
}

void BluezPairer::finish(bool ok, bool tryAnyway, const QString &message)
{
    unregisterAgent();
    m_mac.clear();
    m_devicePath.clear();
    emit finished(ok, tryAnyway, message);
}

void BluezPairer::cancel()
{
    if (!isRunning())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, m_devicePath, kDeviceIface,
                                                      QStringLiteral("CancelPairing"));
    QDBusConnection::systemBus().call(msg, QDBus::NoBlock);
    unregisterAgent();
    m_mac.clear();
    m_devicePath.clear();
}
