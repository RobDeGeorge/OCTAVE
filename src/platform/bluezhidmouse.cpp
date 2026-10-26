#include "bluezhidmouse.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QList>
#include <QLoggingCategory>
#include <QMap>
#include <QStringList>
#include <QThread>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcHidMouse, "octave.bt.hid")

// a{sa{sv}} and a{oa{sa{sv}}}: one object's interfaces, and
// ObjectManager.GetManagedObjects
typedef QMap<QString, QVariantMap> BluezInterfaceMap;
typedef QMap<QDBusObjectPath, BluezInterfaceMap> BluezManagedObjects;
Q_DECLARE_METATYPE(BluezInterfaceMap)
Q_DECLARE_METATYPE(BluezManagedObjects)

namespace {
const QString kBluez = QStringLiteral("org.bluez");
const QString kPropsIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kObjMgrIface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kAdapterIface = QStringLiteral("org.bluez.Adapter1");
const QString kDeviceIface = QStringLiteral("org.bluez.Device1");
const QString kGattMgrIface = QStringLiteral("org.bluez.GattManager1");
const QString kAdvMgrIface = QStringLiteral("org.bluez.LEAdvertisingManager1");
const QString kServiceIface = QStringLiteral("org.bluez.GattService1");
const QString kChrcIface = QStringLiteral("org.bluez.GattCharacteristic1");
const QString kDescIface = QStringLiteral("org.bluez.GattDescriptor1");
const QString kAdvIface = QStringLiteral("org.bluez.LEAdvertisement1");
const QString kAgentIface = QStringLiteral("org.bluez.Agent1");

const QString kRoot = QStringLiteral("/org/octave/hidmouse");
const QString kAdvPath = kRoot + QStringLiteral("/adv0");
const QString kAgentPath = kRoot + QStringLiteral("/agent");
const QString kReportPath = kRoot + QStringLiteral("/service0/char4");
const QString kBatteryPath = kRoot + QStringLiteral("/service1/char0");

constexpr int kCallTimeoutMs = 3000;
// Legacy advertising data is 31 bytes: flags (3) + the HID UUID (4) +
// appearance (4) leave 20 for the name's own 2-byte header and text
constexpr int kMaxNameBytes = 18;

QString uuid16(quint16 u)
{
    return QStringLiteral("%1-0000-1000-8000-00805f9b34fb").arg(u, 8, 16, QLatin1Char('0'));
}

qint8 clampRel(int v)
{
    return static_cast<qint8>(qBound(-127, v, 127));
}

QDBusMessage busCall(const QString &path, const QString &iface, const QString &method,
                     const QVariantList &args = {}, int timeout = kCallTimeoutMs)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, path, iface, method);
    msg.setArguments(args);
    return QDBusConnection::systemBus().call(msg, QDBus::Block, timeout);
}

BluezManagedObjects bluezObjects(QString *error = nullptr)
{
    const QDBusMessage reply = busCall(QStringLiteral("/"), kObjMgrIface, QStringLiteral("GetManagedObjects"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (error)
            *error = reply.errorName();
        return {};
    }
    return qdbus_cast<BluezManagedObjects>(reply.arguments().constFirst());
}
}  // namespace

// ---------------------------------------------------------------------------
// The objects bluetoothd talks to: one virtual subtree under kRoot holding
// the GATT application (ObjectManager at the root, services, characteristics,
// the descriptor), the advertisement and the agent. A virtual object rather
// than adaptors because BlueZ wants a{oa{sa{sv}}} from GetManagedObjects and
// PropertiesChanged on each characteristic, which is plainer to write by hand.
// Qt delivers the calls in the owner's (GUI) thread.
// ---------------------------------------------------------------------------

class BluezHidMouseTree : public QDBusVirtualObject
{
public:
    struct Node {
        QString path;
        QString iface;   // kServiceIface / kChrcIface / kDescIface
        QString uuid;
        QString parent;  // the service of a characteristic, the characteristic of a descriptor
        QStringList flags;
        QByteArray value;
    };

    explicit BluezHidMouseTree(BluezHidMouse *owner) : m_owner(owner)
    {
        // HOGP wants every HID service characteristic behind an encrypted
        // link (security mode 1 level 2). BlueZ's "encrypt-read" answers an
        // unencrypted read with Insufficient Encryption, which is what makes
        // iOS bond; "encrypt-notify" puts the same rule on the CCC write.
        const QString hid = kRoot + QStringLiteral("/service0");
        const QString bat = kRoot + QStringLiteral("/service1");
        const QStringList encRead{QStringLiteral("encrypt-read")};
        const QStringList wwr{QStringLiteral("write-without-response")};
        m_nodes = {
            {hid, kServiceIface, uuid16(0x1812), {}, {}, {}},
            // HID Information: bcdHID 1.11, country 0, flags NormallyConnectable
            {hid + QStringLiteral("/char0"), kChrcIface, uuid16(0x2A4A), hid, encRead,
             QByteArray::fromHex("110100" "02")},
            {hid + QStringLiteral("/char1"), kChrcIface, uuid16(0x2A4B), hid, encRead,
             BluezHidMouse::reportMap()},
            // HID Control Point: 0 suspend, 1 exit suspend; nothing to do
            {hid + QStringLiteral("/char2"), kChrcIface, uuid16(0x2A4C), hid, wwr, QByteArray(1, '\0')},
            // Protocol Mode: 1 = report protocol (no boot protocol here)
            {hid + QStringLiteral("/char3"), kChrcIface, uuid16(0x2A4E), hid,
             {QStringLiteral("encrypt-read"), QStringLiteral("write-without-response")}, QByteArray(1, '\1')},
            {kReportPath, kChrcIface, uuid16(0x2A4D), hid,
             {QStringLiteral("encrypt-read"), QStringLiteral("encrypt-notify")},
             BluezHidMouse::encodeReport(0, 0, 0, 0, 0)},
            // Report Reference: report id 0 (none), type 1 = input
            {kReportPath + QStringLiteral("/desc0"), kDescIface, uuid16(0x2908), kReportPath, encRead,
             QByteArray::fromHex("0001")},
            {bat, kServiceIface, uuid16(0x180F), {}, {}, {}},
            {kBatteryPath, kChrcIface, uuid16(0x2A19), bat,
             {QStringLiteral("read"), QStringLiteral("notify")}, QByteArray(1, char(100))},
        };
    }

    QString advName;
    bool reportNotifying = false;

    void setReportValue(const QByteArray &value)
    {
        Node *n = find(kReportPath);
        n->value = value;
        // BlueZ turns a Value change into a notification to every
        // subscribed device
        QDBusMessage sig = QDBusMessage::createSignal(kReportPath, kPropsIface,
                                                      QStringLiteral("PropertiesChanged"));
        sig << kChrcIface << QVariantMap{{QStringLiteral("Value"), value}} << QStringList();
        QDBusConnection::systemBus().send(sig);
    }

    QString introspect(const QString &path) const override
    {
        if (path == kRoot)
            return QStringLiteral(
                "<interface name=\"org.freedesktop.DBus.ObjectManager\">"
                "<method name=\"GetManagedObjects\"><arg type=\"a{oa{sa{sv}}}\" direction=\"out\"/></method>"
                "</interface>");
        if (path == kAdvPath)
            return QStringLiteral("<interface name=\"org.bluez.LEAdvertisement1\">"
                                  "<method name=\"Release\"/></interface>");
        if (path == kAgentPath)
            return QStringLiteral("<interface name=\"org.bluez.Agent1\"/>");
        if (const Node *n = find(path)) {
            if (n->iface == kServiceIface)
                return QStringLiteral("<interface name=\"org.bluez.GattService1\"/>");
            return QStringLiteral("<interface name=\"%1\">"
                                  "<method name=\"ReadValue\"><arg type=\"a{sv}\" direction=\"in\"/>"
                                  "<arg type=\"ay\" direction=\"out\"/></method>"
                                  "<method name=\"WriteValue\"><arg type=\"ay\" direction=\"in\"/>"
                                  "<arg type=\"a{sv}\" direction=\"in\"/></method>"
                                  "%2</interface>")
                .arg(n->iface,
                     n->iface == kChrcIface ? QStringLiteral("<method name=\"StartNotify\"/>"
                                                             "<method name=\"StopNotify\"/>")
                                            : QString());
        }
        return {};
    }

    bool handleMessage(const QDBusMessage &msg, const QDBusConnection &bus) override
    {
        const QString path = msg.path();
        const QString iface = msg.interface();
        const QString member = msg.member();

        if (iface == kPropsIface) {
            const QVariantList args = msg.arguments();
            const QString wanted = args.value(0).toString();
            QVariantMap props;
            if (!propertiesOf(path, wanted, &props))
                return reply(bus, msg.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                                                       QStringLiteral("No such interface")));
            if (member == QLatin1String("GetAll"))
                return reply(bus, msg.createReply(QVariant(props)));
            if (member == QLatin1String("Get")) {
                const QString name = args.value(1).toString();
                if (!props.contains(name))
                    return reply(bus, msg.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"),
                                                           QStringLiteral("No such property")));
                return reply(bus, msg.createReply(QVariant::fromValue(QDBusVariant(props.value(name)))));
            }
            return reply(bus, msg.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.PropertyReadOnly"),
                                                   QStringLiteral("Read-only")));
        }

        if (path == kRoot && iface == kObjMgrIface && member == QLatin1String("GetManagedObjects")) {
            BluezManagedObjects objects;
            for (const Node &n : std::as_const(m_nodes)) {
                QVariantMap props;
                propertiesOf(n.path, n.iface, &props);
                objects.insert(QDBusObjectPath(n.path), BluezInterfaceMap{{n.iface, props}});
            }
            return reply(bus, msg.createReply(QVariant::fromValue(objects)));
        }

        if (path == kAdvPath && iface == kAdvIface && member == QLatin1String("Release")) {
            reply(bus, msg.createReply());
            m_owner->onAdvertisementReleased();
            return true;
        }

        if (path == kAgentPath && iface == kAgentIface)
            return handleAgent(msg, bus);

        Node *n = find(path);
        if (!n || n->iface == kServiceIface || iface != n->iface)
            return false;  // Qt answers UnknownMethod / UnknownObject

        if (member == QLatin1String("ReadValue")) {
            const QVariantMap opts = qdbus_cast<QVariantMap>(msg.arguments().value(0));
            const int offset = opts.value(QStringLiteral("offset")).toInt();
            if (offset > n->value.size())
                return reply(bus, msg.createErrorReply(QStringLiteral("org.bluez.Error.InvalidOffset"),
                                                       QStringLiteral("Invalid offset")));
            return reply(bus, msg.createReply(QVariant(n->value.mid(offset))));
        }
        if (member == QLatin1String("WriteValue")) {
            if (!n->flags.contains(QStringLiteral("write-without-response")))
                return reply(bus, msg.createErrorReply(QStringLiteral("org.bluez.Error.NotPermitted"),
                                                       QStringLiteral("Write not permitted")));
            const QByteArray value = msg.arguments().value(0).toByteArray();
            qCDebug(lcHidMouse) << "[BT-HID] write" << n->uuid.left(8) << value.toHex();
            if (!value.isEmpty())
                n->value = value.left(1);
            return reply(bus, msg.createReply());
        }
        if (n->iface == kChrcIface && (member == QLatin1String("StartNotify")
                                       || member == QLatin1String("StopNotify"))) {
            const bool on = member == QLatin1String("StartNotify");
            reply(bus, msg.createReply());
            if (path == kReportPath && on != reportNotifying) {
                reportNotifying = on;
                m_owner->onNotifyChanged(on);
            }
            return true;
        }
        return false;
    }

private:
    static bool reply(const QDBusConnection &bus, const QDBusMessage &r)
    {
        bus.send(r);
        return true;
    }

    Node *find(const QString &path)
    {
        for (Node &n : m_nodes)
            if (n.path == path)
                return &n;
        return nullptr;
    }
    const Node *find(const QString &path) const { return const_cast<BluezHidMouseTree *>(this)->find(path); }

    bool propertiesOf(const QString &path, const QString &iface, QVariantMap *out) const
    {
        if (path == kAdvPath && iface == kAdvIface) {
            *out = {
                {QStringLiteral("Type"), QStringLiteral("peripheral")},
                {QStringLiteral("ServiceUUIDs"), QStringList{uuid16(0x1812)}},
                {QStringLiteral("Appearance"), QVariant::fromValue<quint16>(0x03C2)},  // HID mouse
                {QStringLiteral("LocalName"), advName},
                {QStringLiteral("Discoverable"), true},
            };
            return true;
        }
        const Node *n = find(path);
        if (!n || n->iface != iface)
            return false;
        out->insert(QStringLiteral("UUID"), n->uuid);
        if (n->iface == kServiceIface) {
            out->insert(QStringLiteral("Primary"), true);
            return true;
        }
        out->insert(n->iface == kChrcIface ? QStringLiteral("Service") : QStringLiteral("Characteristic"),
                    QVariant::fromValue(QDBusObjectPath(n->parent)));
        out->insert(QStringLiteral("Flags"), n->flags);
        out->insert(QStringLiteral("Value"), n->value);
        if (n->iface == kChrcIface && n->flags.join(QLatin1Char(' ')).contains(QLatin1String("notify")))
            out->insert(QStringLiteral("Notifying"), n->path == kReportPath && reportNotifying);
        return true;
    }

    // Just Works: accept whatever bluetoothd asks inside the pairing window
    // (the agent is only registered then; anything after it closes is refused)
    bool handleAgent(const QDBusMessage &msg, const QDBusConnection &bus)
    {
        const QString member = msg.member();
        const QString device = msg.arguments().isEmpty()
                                   ? QString()
                                   : qvariant_cast<QDBusObjectPath>(msg.arguments().constFirst()).path();
        if (!m_owner->m_pairing && member != QLatin1String("Release") && member != QLatin1String("Cancel")) {
            qCInfo(lcHidMouse) << "[BT-HID] agent" << member << device << "-> refused (no pairing window)";
            return reply(bus, msg.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                                                   QStringLiteral("not pairing")));
        }
        qCInfo(lcHidMouse) << "[BT-HID] agent" << member << device << "-> accepted";
        if (member == QLatin1String("RequestPinCode"))
            return reply(bus, msg.createReply(QStringLiteral("0000")));
        if (member == QLatin1String("RequestPasskey"))
            return reply(bus, msg.createReply(QVariant::fromValue<uint>(0)));
        return reply(bus, msg.createReply());
    }

    BluezHidMouse *m_owner;
    QList<Node> m_nodes;
};

// ---------------------------------------------------------------------------

BluezHidMouse::BluezHidMouse(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<BluezInterfaceMap>();
    qDBusRegisterMetaType<BluezManagedObjects>();
    m_status = QStringLiteral("Off");
}

BluezHidMouse::~BluezHidMouse()
{
    stop();
}

QByteArray BluezHidMouse::reportMap()
{
    static const QByteArray map = QByteArray::fromHex(
        "0501"      // Usage Page (Generic Desktop)
        "0902"      // Usage (Mouse)
        "a101"      // Collection (Application)
        "0901"      //   Usage (Pointer)
        "a100"      //   Collection (Physical)
        "0509"      //     Usage Page (Button)
        "1901"      //     Usage Minimum (1)
        "2903"      //     Usage Maximum (3)
        "1500"      //     Logical Minimum (0)
        "2501"      //     Logical Maximum (1)
        "9503"      //     Report Count (3)
        "7501"      //     Report Size (1)
        "8102"      //     Input (Data, Var, Abs): left, right, middle
        "9501"      //     Report Count (1)
        "7505"      //     Report Size (5)
        "8103"      //     Input (Const): padding
        "0501"      //     Usage Page (Generic Desktop)
        "0930"      //     Usage (X)
        "0931"      //     Usage (Y)
        "0938"      //     Usage (Wheel)
        "1581"      //     Logical Minimum (-127)
        "257f"      //     Logical Maximum (127)
        "7508"      //     Report Size (8)
        "9503"      //     Report Count (3)
        "8106"      //     Input (Data, Var, Rel)
        "050c"      //     Usage Page (Consumer)
        "0a3802"    //     Usage (AC Pan)
        "1581"      //     Logical Minimum (-127)
        "257f"      //     Logical Maximum (127)
        "7508"      //     Report Size (8)
        "9501"      //     Report Count (1)
        "8106"      //     Input (Data, Var, Rel)
        "c0"        //   End Collection
        "c0");      // End Collection
    return map;
}

QByteArray BluezHidMouse::encodeReport(quint8 buttons, int dx, int dy, int wheel, int pan)
{
    QByteArray r(5, '\0');
    r[0] = char(buttons & 0x07);
    r[1] = char(clampRel(dx));
    r[2] = char(clampRel(dy));
    r[3] = char(clampRel(wheel));
    r[4] = char(clampRel(pan));
    return r;
}

void BluezHidMouse::sendReport(quint8 buttons, int dx, int dy, int wheel, int pan)
{
    if (!isConnected())
        return;
    m_tree->setReportValue(encodeReport(buttons, dx, dy, wheel, pan));
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

bool BluezHidMouse::start(const QString &name)
{
    if (m_running)
        stop();
    ++m_generation;

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected() || !bus.interface()
        || !bus.interface()->isServiceRegistered(kBluez).value()) {
        qCWarning(lcHidMouse) << "[BT-HID] BlueZ is not on the system bus" << bus.lastError().message();
        setStatus(QStringLiteral("Bluetooth isn't available (BlueZ isn't running)"));
        return false;
    }
    if (!findAdapter())
        return false;  // status set

    if (!m_tree)
        m_tree = new BluezHidMouseTree(this);
    m_tree->reportNotifying = false;
    QByteArray utf8 = (name.trimmed().isEmpty() ? QStringLiteral("OCTAVE") : name.trimmed()).toUtf8();
    if (utf8.size() > kMaxNameBytes) {
        // Cut on a character boundary
        utf8.truncate(kMaxNameBytes);
        while (!utf8.isEmpty() && (uchar(utf8.back()) & 0xC0) == 0x80)
            utf8.chop(1);
        if (!utf8.isEmpty() && uchar(utf8.back()) >= 0xC0)
            utf8.chop(1);
    }
    m_tree->advName = QString::fromUtf8(utf8);

    if (!bus.registerVirtualObject(kRoot, m_tree, QDBusConnection::SubPath)) {
        qCWarning(lcHidMouse) << "[BT-HID] could not export" << kRoot << bus.lastError().message();
        setStatus(QStringLiteral("Couldn't start the Bluetooth mouse (is another one running?)"));
        teardown();
        return false;
    }
    m_exported = true;

    if (!m_watching) {
        m_watching = bus.connect(kBluez, QString(), kPropsIface, QStringLiteral("PropertiesChanged"),
                                 this, SLOT(onBluezPropertiesChanged(QDBusMessage)));
    }

    // No agent and no Pairable change yet: new pairings are only taken
    // inside a window the user opens (setPairing). A bonded iPhone
    // reconnects without one.

    m_running = true;
    m_notifying = false;
    m_peerPath.clear();
    m_peerName.clear();
    setStatus(QStringLiteral("Starting the Bluetooth mouse…"));
    registerApplication();
    return true;
}

bool BluezHidMouse::findAdapter()
{
    QString error;
    const BluezManagedObjects objects = bluezObjects(&error);
    m_adapterPath.clear();
    QVariantMap adapter;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it->contains(kAdapterIface)) {
            m_adapterPath = it.key().path();
            adapter = it->value(kAdapterIface);
            if (!it->contains(kGattMgrIface) || !it->contains(kAdvMgrIface)) {
                qCWarning(lcHidMouse) << "[BT-HID]" << m_adapterPath << "has no GattManager1 / LEAdvertisingManager1";
                setStatus(QStringLiteral("This Bluetooth adapter can't act as a Bluetooth LE device"));
                return false;
            }
            break;
        }
    }
    if (m_adapterPath.isEmpty()) {
        qCWarning(lcHidMouse) << "[BT-HID] no adapter" << error;
        setStatus(QStringLiteral("No Bluetooth adapter found"));
        return false;
    }

    // Power a merely-off adapter on (the user asked for the mouse) and off
    // again in stop(); rfkill-blocked stays off
    if (!adapter.value(QStringLiteral("Powered")).toBool()) {
        if (adapter.value(QStringLiteral("PowerState")).toString() == QLatin1String("off-blocked")
            || !setAdapterProperty(QStringLiteral("Powered"), true)) {
            setStatus(QStringLiteral("Bluetooth is off"));
            return false;
        }
        qCInfo(lcHidMouse) << "[BT-HID] powered" << m_adapterPath << "on";
        m_restorePowered = true;
    }
    return true;
}

void BluezHidMouse::setPairing(bool allowed)
{
    if (allowed == m_pairing || (allowed && !m_running))
        return;
    m_pairing = allowed;
    if (allowed) {
        // Accept the iPhone's pairing request. Not fatal: a desktop with its
        // own agent still pairs, through its dialog.
        registerAgent();
        QDBusMessage reply = busCall(m_adapterPath, kPropsIface, QStringLiteral("Get"),
                                     {kAdapterIface, QStringLiteral("Pairable")});
        const bool pairable = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
                              && qvariant_cast<QDBusVariant>(reply.arguments().constFirst()).variant().toBool();
        if (!pairable) {
            if (setAdapterProperty(QStringLiteral("Pairable"), true))
                m_restorePairable = true;
            else
                qCWarning(lcHidMouse) << "[BT-HID] could not make" << m_adapterPath << "pairable";
        }
        qCInfo(lcHidMouse) << "[BT-HID] pairing window open";
    } else {
        // Pairable goes back before the agent leaves (see teardown)
        if (m_restorePairable)
            setAdapterProperty(QStringLiteral("Pairable"), false);
        m_restorePairable = false;
        unregisterAgent();
        qCInfo(lcHidMouse) << "[BT-HID] pairing window closed";
    }
    refreshStatus();
}

bool BluezHidMouse::setAdapterProperty(const QString &name, bool value)
{
    QDBusMessage reply;
    // Busy: a change of the same setting is still in flight (bluetoothd
    // makes some itself); it settles within a few hundred ms
    for (int attempt = 0; attempt < 10; ++attempt) {
        reply = busCall(m_adapterPath, kPropsIface, QStringLiteral("Set"),
                        {kAdapterIface, name, QVariant::fromValue(QDBusVariant(value))}, 6000);
        if (reply.errorName() != QLatin1String("org.bluez.Error.Busy"))
            break;
        QThread::msleep(100);
    }
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qCWarning(lcHidMouse) << "[BT-HID] set" << name << value << "failed:" << reply.errorName() << reply.errorMessage();
        return false;
    }
    return true;
}

bool BluezHidMouse::registerAgent()
{
    // One agent per D-Bus connection: this fails with AlreadyExists while
    // BluezPairer (OBD) is pairing in the same process
    QDBusMessage reply = busCall(QStringLiteral("/org/bluez"), QStringLiteral("org.bluez.AgentManager1"),
                                 QStringLiteral("RegisterAgent"),
                                 {QVariant::fromValue(QDBusObjectPath(kAgentPath)), QStringLiteral("NoInputNoOutput")});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qCWarning(lcHidMouse) << "[BT-HID] RegisterAgent failed:" << reply.errorName() << reply.errorMessage();
        return false;
    }
    m_agentRegistered = true;
    // BlueZ hands the default role back to the previous default agent when
    // ours unregisters
    reply = busCall(QStringLiteral("/org/bluez"), QStringLiteral("org.bluez.AgentManager1"),
                    QStringLiteral("RequestDefaultAgent"), {QVariant::fromValue(QDBusObjectPath(kAgentPath))});
    if (reply.type() != QDBusMessage::ReplyMessage)
        qCWarning(lcHidMouse) << "[BT-HID] RequestDefaultAgent failed:" << reply.errorMessage();
    return true;
}

void BluezHidMouse::unregisterAgent()
{
    if (!m_agentRegistered)
        return;
    busCall(QStringLiteral("/org/bluez"), QStringLiteral("org.bluez.AgentManager1"),
            QStringLiteral("UnregisterAgent"), {QVariant::fromValue(QDBusObjectPath(kAgentPath))}, 2000);
    m_agentRegistered = false;
}

// RegisterApplication / RegisterAdvertisement go out asynchronously:
// bluetoothd reads our objects (GetManagedObjects, Properties.GetAll) before
// it answers, and a blocking call would hold the thread that has to reply.
void BluezHidMouse::registerApplication()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, m_adapterPath, kGattMgrIface,
                                                      QStringLiteral("RegisterApplication"));
    msg << QVariant::fromValue(QDBusObjectPath(kRoot)) << QVariantMap();
    const int gen = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, gen](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (gen != m_generation || !m_running) {
            if (!reply.isError())  // stop() came first: take it back
                QDBusConnection::systemBus().call(
                    QDBusMessage::createMethodCall(kBluez, m_adapterPath, kGattMgrIface,
                                                   QStringLiteral("UnregisterApplication"))
                        << QVariant::fromValue(QDBusObjectPath(kRoot)),
                    QDBus::NoBlock);
            return;
        }
        if (reply.isError()) {
            qCWarning(lcHidMouse) << "[BT-HID] RegisterApplication failed:" << reply.error().name()
                                  << reply.error().message();
            fail(QStringLiteral("Bluetooth refused the mouse service: %1").arg(reply.error().message()));
            return;
        }
        qCInfo(lcHidMouse) << "[BT-HID] HID service registered on" << m_adapterPath;
        m_appRegistered = true;
        registerAdvertisement();
    });
}

void BluezHidMouse::registerAdvertisement()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kBluez, m_adapterPath, kAdvMgrIface,
                                                      QStringLiteral("RegisterAdvertisement"));
    msg << QVariant::fromValue(QDBusObjectPath(kAdvPath)) << QVariantMap();
    const int gen = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, gen](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (gen != m_generation || !m_running) {
            if (!reply.isError())
                QDBusConnection::systemBus().call(
                    QDBusMessage::createMethodCall(kBluez, m_adapterPath, kAdvMgrIface,
                                                   QStringLiteral("UnregisterAdvertisement"))
                        << QVariant::fromValue(QDBusObjectPath(kAdvPath)),
                    QDBus::NoBlock);
            return;
        }
        if (reply.isError()) {
            qCWarning(lcHidMouse) << "[BT-HID] RegisterAdvertisement failed:" << reply.error().name()
                                  << reply.error().message();
            fail(QStringLiteral("Bluetooth couldn't advertise the mouse: %1").arg(reply.error().message()));
            return;
        }
        qCInfo(lcHidMouse) << "[BT-HID] advertising as" << m_tree->advName;
        m_advRegistered = true;
        refreshStatus();
    });
}

void BluezHidMouse::stop()
{
    const bool wasActive = m_running || m_exported;
    ++m_generation;
    teardown();
    if (wasActive) {
        qCInfo(lcHidMouse) << "[BT-HID] stopped";
        setStatus(QStringLiteral("Off"));
    }
}

void BluezHidMouse::fail(const QString &status)
{
    ++m_generation;
    teardown();
    setStatus(status);
}

void BluezHidMouse::teardown()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (m_advRegistered)
        busCall(m_adapterPath, kAdvMgrIface, QStringLiteral("UnregisterAdvertisement"),
                {QVariant::fromValue(QDBusObjectPath(kAdvPath))}, 2000);
    if (m_appRegistered)
        busCall(m_adapterPath, kGattMgrIface, QStringLiteral("UnregisterApplication"),
                {QVariant::fromValue(QDBusObjectPath(kRoot))}, 2000);
    m_advRegistered = m_appRegistered = false;
    // Pairable goes back before the agent leaves: bluetoothd itself turns
    // Pairable off when the last default agent unregisters (and on when one
    // appears), and our Set would meet that change in flight (Busy)
    if (m_restorePairable)
        setAdapterProperty(QStringLiteral("Pairable"), false);
    unregisterAgent();
    m_pairing = false;
    if (m_restorePowered)
        setAdapterProperty(QStringLiteral("Powered"), false);
    m_restorePairable = m_restorePowered = false;
    if (m_exported) {
        bus.unregisterObject(kRoot, QDBusConnection::UnregisterTree);
        m_exported = false;
    }
    if (m_watching) {
        bus.disconnect(kBluez, QString(), kPropsIface, QStringLiteral("PropertiesChanged"),
                       this, SLOT(onBluezPropertiesChanged(QDBusMessage)));
        m_watching = false;
    }
    if (m_tree)
        m_tree->reportNotifying = false;
    const bool changed = m_running || m_notifying;
    m_running = false;
    m_notifying = false;
    m_peerPath.clear();
    m_peerName.clear();
    if (changed)
        emit stateChanged();
}

// ---------------------------------------------------------------------------
// Connection tracking
// ---------------------------------------------------------------------------

// Two views of "subscribed": the tree's reportNotifying is BlueZ's
// (StartNotify / StopNotify, which BlueZ calls only when the first device
// subscribes / the last one leaves), m_notifying ours (that, and the peer
// still connected). For a bonded device BlueZ may keep the subscription over
// a disconnect and never call StartNotify again, so a reconnect while BlueZ
// still has notifications on counts as connected too.
void BluezHidMouse::onNotifyChanged(bool notifying)
{
    if (!m_running)
        return;
    m_notifying = notifying;
    if (notifying) {
        resolvePeer();
    } else {
        m_peerPath.clear();
        m_peerName.clear();
    }
    qCInfo(lcHidMouse) << "[BT-HID] input report notifications" << (notifying ? "on," : "off")
                       << (notifying ? m_peerName : QString());
    refreshStatus();
}

void BluezHidMouse::onAdvertisementReleased()
{
    qCWarning(lcHidMouse) << "[BT-HID] bluetoothd released the advertisement";
    m_advRegistered = false;
    if (m_running && !m_notifying)
        fail(QStringLiteral("Bluetooth stopped advertising the mouse"));
}

// BlueZ doesn't say which device turned notifications on: take the connected
// LE device on our adapter (an iPhone advertises a random address), else any
// connected one
void BluezHidMouse::resolvePeer()
{
    m_peerPath.clear();
    m_peerName.clear();
    const BluezManagedObjects objects = bluezObjects();
    QString fallbackPath, fallbackName;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it->contains(kDeviceIface))
            continue;
        const QVariantMap dev = it->value(kDeviceIface);
        if (dev.value(QStringLiteral("Adapter")).value<QDBusObjectPath>().path() != m_adapterPath
            || !dev.value(QStringLiteral("Connected")).toBool())
            continue;
        const QString alias = dev.value(QStringLiteral("Alias")).toString();
        if (dev.value(QStringLiteral("AddressType")).toString() == QLatin1String("random")) {
            m_peerPath = it.key().path();
            m_peerName = alias;
            return;
        }
        if (fallbackPath.isEmpty()) {
            fallbackPath = it.key().path();
            fallbackName = alias;
        }
    }
    m_peerPath = fallbackPath;
    m_peerName = fallbackName;
}

void BluezHidMouse::onBluezPropertiesChanged(const QDBusMessage &msg)
{
    if (!m_running || msg.arguments().size() < 2)
        return;
    const QString iface = msg.arguments().at(0).toString();
    const QVariantMap changed = qdbus_cast<QVariantMap>(msg.arguments().at(1));
    const QString path = msg.path();

    if (iface == kAdapterIface && path == m_adapterPath) {
        if (changed.contains(QStringLiteral("Powered")) && !changed.value(QStringLiteral("Powered")).toBool()) {
            qCWarning(lcHidMouse) << "[BT-HID] adapter powered off";
            m_restorePowered = false;  // someone else turned it off; leave it
            fail(QStringLiteral("Bluetooth is off"));
        }
        return;
    }
    if (iface != kDeviceIface || !path.startsWith(m_adapterPath + QLatin1Char('/')))
        return;

    // A device paired with us inside the pairing window: trust it so it
    // reconnects without asking again
    if (m_pairing && changed.value(QStringLiteral("Paired")).toBool()) {
        qCInfo(lcHidMouse) << "[BT-HID] paired with" << path << "- trusting it";
        busCall(path, kPropsIface, QStringLiteral("Set"),
                {kDeviceIface, QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))});
    }
    if (changed.value(QStringLiteral("Connected")).toBool() && !m_notifying && m_tree->reportNotifying) {
        resolvePeer();
        m_notifying = true;
        qCInfo(lcHidMouse) << "[BT-HID]" << m_peerName << "reconnected with notifications still on";
        refreshStatus();
        return;
    }
    if (path != m_peerPath)
        return;
    if (changed.contains(QStringLiteral("Connected")) && !changed.value(QStringLiteral("Connected")).toBool()) {
        qCInfo(lcHidMouse) << "[BT-HID]" << m_peerName << "disconnected";
        m_notifying = false;
        m_peerPath.clear();
        m_peerName.clear();
        refreshStatus();
        return;
    }
    if (changed.contains(QStringLiteral("Alias"))) {
        m_peerName = changed.value(QStringLiteral("Alias")).toString();
        refreshStatus();
    }
}

void BluezHidMouse::refreshStatus()
{
    if (!m_running)
        return;
    if (m_notifying)
        m_status = m_peerName.isEmpty() ? QStringLiteral("Connected")
                                        : QStringLiteral("Connected to %1").arg(m_peerName);
    else if (m_advRegistered && m_pairing)
        m_status = QStringLiteral("Ready to pair: tap \"%1\" in the iPhone's Bluetooth settings").arg(m_tree->advName);
    else if (m_advRegistered)
        m_status = QStringLiteral("Waiting for a paired iPhone");
    else
        m_status = QStringLiteral("Starting the Bluetooth mouse…");
    emit stateChanged();  // connected / peer may change under the same text
}

void BluezHidMouse::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit stateChanged();
}
