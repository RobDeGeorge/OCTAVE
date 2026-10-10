#include "obdadapterfinder.h"

#include "obdmanager.h"
#include "rfcommsocket.h"
#include "settingsmanager.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QBluetoothServiceDiscoveryAgent>
#include <QBluetoothServiceInfo>
#include <QBluetoothUuid>
#include <QDebug>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

#ifdef OCTAVE_BLUEZ
#include "../platform/bluezpairer.h"
#endif

namespace {
// Discovery runs this long, then stops on its own
constexpr int kScanMs = 15000;
// SDP channel lookup gives up and falls back to channel 1 after this
constexpr int kSdpTimeoutMs = 8000;

// Upper-case BLE service UUIDs ELM327 BLE adapters advertise (the same three
// vendor profiles android/src/org/octave/app/OctaveOBDBridge.java speaks)
const QStringList &obdServiceUuids()
{
    static const QStringList u = {
        QStringLiteral("0000FFF0-0000-1000-8000-00805F9B34FB"),
        QStringLiteral("0000FFE0-0000-1000-8000-00805F9B34FB"),
        QStringLiteral("0000FFE1-0000-1000-8000-00805F9B34FB"),
        QStringLiteral("49535343-FE7D-4AE5-8FA9-9FAFD205E455"),
    };
    return u;
}
}  // namespace

OBDAdapterFinder::OBDAdapterFinder(OBDManager *obd, SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_obd(obd)
    , m_settings(settings)
{
    if (!supported())
        return;

    m_local = new QBluetoothLocalDevice(this);
    connect(m_local, &QBluetoothLocalDevice::hostModeStateChanged, this, &OBDAdapterFinder::bluetoothOnChanged);

    m_agent = new QBluetoothDeviceDiscoveryAgent(this);
    m_agent->setLowEnergyDiscoveryTimeout(kScanMs);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &OBDAdapterFinder::onDeviceFound);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
            [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { onDeviceFound(info); });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this] {
        setScanning(false);
        if (m_busyAddress.isEmpty() && !m_statusIsError)
            setStatus(m_deviceList.isEmpty() ? QStringLiteral("No Bluetooth devices found. Is the ignition on?")
                                             : QString());
    });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, [this] { setScanning(false); });
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [this] {
        qWarning() << "[OBD-BT] Discovery error:" << m_agent->errorString();
        setScanning(false);
        setStatus(m_agent->error() == QBluetoothDeviceDiscoveryAgent::PoweredOffError
                      ? QStringLiteral("Bluetooth is off.")
                      : QStringLiteral("Bluetooth scan failed: %1").arg(m_agent->errorString()),
                  true);
    });

#ifdef OCTAVE_BLUEZ
    m_pairer = new BluezPairer(this);
    connect(m_pairer, &BluezPairer::progress, this, [this](const QString &m) { setStatus(m); });
    connect(m_pairer, &BluezPairer::finished, this, &OBDAdapterFinder::onPaired);
#endif

    m_sdpTimeout = new QTimer(this);
    m_sdpTimeout->setSingleShot(true);
    connect(m_sdpTimeout, &QTimer::timeout, this, [this] {
        qDebug() << "[OBD-BT] SDP lookup timed out, using channel 1";
        if (m_sdp) m_sdp->stop();
        handOff(1);
    });

    if (m_obd)
        connect(m_obd, &OBDManager::connectionStatusChanged, this, &OBDAdapterFinder::onObdStatus);
}

OBDAdapterFinder::~OBDAdapterFinder()
{
    if (m_agent)
        m_agent->stop();
}

bool OBDAdapterFinder::supported() const
{
#ifdef OCTAVE_BLUEZ
    return true;
#else
    return false;
#endif
}

bool OBDAdapterFinder::bluetoothOn() const
{
    return m_local && m_local->isValid() && m_local->hostMode() != QBluetoothLocalDevice::HostPoweredOff;
}

bool OBDAdapterFinder::looksLikeObd(const QString &name, const QStringList &serviceUuids)
{
    // Names the common ELM327-compatible adapters advertise: generic clones
    // ("OBDII", "OBD2", "V-LINK", "Android-Vlink"), OBDLink LX/MX+/CX,
    // Vgate iCar, Veepeak, BAFX, Konnwei, NEXAS, LELink, Viecar, Kiwi.
    static const QRegularExpression re(
        QStringLiteral("obd|elm ?327|\\belm\\b|v-?link|vgate|icar|veepeak|bafx|konnwei|\\bkw ?\\d|nexas|"
                       "lelink|viecar|kiwi ?\\d|carista|panlong|scan ?tool|\\bvlink"),
        QRegularExpression::CaseInsensitiveOption);
    if (re.match(name).hasMatch())
        return true;
    for (const QString &u : serviceUuids) {
        if (obdServiceUuids().contains(u.toUpper().remove(QLatin1Char('{')).remove(QLatin1Char('}'))))
            return true;
    }
    return false;
}

void OBDAdapterFinder::startScan()
{
    if (!supported() || !m_agent)
        return;
    if (!bluetoothOn()) {
        setStatus(QStringLiteral("Bluetooth is off."), true);
        return;
    }
    if (!m_busyAddress.isEmpty() && !m_handedOff)
        return;  // pairing; discovery would disturb it
    m_devices.clear();
    rebuildList();
    setStatus(QStringLiteral("Searching for adapters… plug it in and turn the ignition on."));
    m_agent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod
                   | QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    setScanning(m_agent->isActive());
}

void OBDAdapterFinder::stopScan()
{
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    setScanning(false);
}

void OBDAdapterFinder::onDeviceFound(const QBluetoothDeviceInfo &info)
{
    const QString address = info.address().toString().toUpper();
    if (address.isEmpty() || address == QLatin1String("00:00:00:00:00:00"))
        return;
    // BlueZ names an unnamed device after its address ("40-58-99-A6-80-9A")
    QString name = info.name().trimmed();
    if (name.isEmpty() || name.compare(QString(address).replace(QLatin1Char(':'), QLatin1Char('-')),
                                       Qt::CaseInsensitive) == 0)
        return;

    QStringList uuids;
    for (const QBluetoothUuid &u : info.serviceUuids())
        uuids << u.toString(QUuid::WithoutBraces).toUpper();

    QVariantMap d = m_devices.value(address);

    // Dual-mode adapters (NEXAS NexLink) show up as Classic in one report and
    // LE-only in the next; keep every mode seen this scan
    const auto cfg = info.coreConfigurations();
    const QString prevKind = d.value(QStringLiteral("kind")).toString();
    const bool classic = cfg.testFlag(QBluetoothDeviceInfo::BaseRateCoreConfiguration)
                         || prevKind == QLatin1String("classic") || prevKind == QLatin1String("dual");
    const bool ble = cfg.testFlag(QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
                     || prevKind == QLatin1String("ble") || prevKind == QLatin1String("dual");

    d[QStringLiteral("address")] = address;
    d[QStringLiteral("name")] = name;
    if (info.rssi() != 0 || !d.contains(QStringLiteral("rssi")))
        d[QStringLiteral("rssi")] = int(info.rssi());
    d[QStringLiteral("kind")] = classic && ble ? QStringLiteral("dual")
                                : ble          ? QStringLiteral("ble")
                                               : QStringLiteral("classic");
    d[QStringLiteral("likelyObd")] = looksLikeObd(name, uuids) || d.value(QStringLiteral("likelyObd")).toBool();
    const auto pairing = m_local ? m_local->pairingStatus(info.address()) : QBluetoothLocalDevice::Unpaired;
    d[QStringLiteral("paired")] = pairing != QBluetoothLocalDevice::Unpaired;
    // BLE ELM327 needs a GATT transport the desktop OBD worker doesn't have yet
    d[QStringLiteral("connectable")] = classic;
    d[QStringLiteral("note")] = classic ? QString()
                                        : QStringLiteral("Bluetooth LE adapters aren't supported on this device yet");
    m_devices.insert(address, d);
    rebuildList();
}

void OBDAdapterFinder::rebuildList()
{
    QList<QVariantMap> rows = m_devices.values();
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const bool la = a.value(QStringLiteral("likelyObd")).toBool();
        const bool lb = b.value(QStringLiteral("likelyObd")).toBool();
        if (la != lb) return la;
        // rssi 0 = cached (not heard this scan); sort it after live devices
        int ra = a.value(QStringLiteral("rssi")).toInt(), rb = b.value(QStringLiteral("rssi")).toInt();
        if (ra == 0) ra = -1000;
        if (rb == 0) rb = -1000;
        if (ra != rb) return ra > rb;
        return a.value(QStringLiteral("name")).toString() < b.value(QStringLiteral("name")).toString();
    });
    QVariantList list;
    for (const QVariantMap &r : rows)
        list.append(r);
    if (list != m_deviceList) {
        m_deviceList = list;
        emit devicesChanged();
    }
}

void OBDAdapterFinder::connectAdapter(const QString &address)
{
    if (!supported())
        return;
    const QString addr = address.trimmed().toUpper();
    const QVariantMap d = m_devices.value(addr);
    if (d.isEmpty()) {
        setStatus(QStringLiteral("Scan again: that adapter is no longer in the list."), true);
        return;
    }
    if (!d.value(QStringLiteral("connectable")).toBool()) {
        setStatus(d.value(QStringLiteral("note")).toString(), true);
        return;
    }
    if (!m_busyAddress.isEmpty() && !m_handedOff)
        return;  // one pairing at a time

    // Discovery and pairing on the same controller slow each other down
    stopScan();
    m_busyName = d.value(QStringLiteral("name")).toString();
    m_handedOff = false;
    setBusy(addr);
    qDebug() << "[OBD-BT] Connect" << m_busyName << addr;
#ifdef OCTAVE_BLUEZ
    setStatus(QStringLiteral("Pairing…"));
    m_pairer->start(addr);
#endif
}

void OBDAdapterFinder::onPaired(bool ok, bool tryAnyway, const QString &message)
{
    if (m_busyAddress.isEmpty())
        return;
    if (!ok && !tryAnyway) {
        setBusy(QString());
        setStatus(message, true);
        return;
    }
    m_pairError = ok ? QString() : message;
    if (ok && m_devices.contains(m_busyAddress)) {
        m_devices[m_busyAddress][QStringLiteral("paired")] = true;
        rebuildList();
    }
    lookUpChannel();
}

void OBDAdapterFinder::lookUpChannel()
{
    // Most ELM327 clones serve SPP on channel 1, but OBDLink and a few others
    // don't; ask the adapter's SDP record and fall back to 1
    setStatus(QStringLiteral("Finding the adapter's serial port…"));
    delete m_sdp;
    m_sdp = new QBluetoothServiceDiscoveryAgent(this);
    m_sdp->setRemoteAddress(QBluetoothAddress(m_busyAddress));
    m_sdp->setUuidFilter(QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort));
    connect(m_sdp, &QBluetoothServiceDiscoveryAgent::serviceDiscovered, this,
            [this](const QBluetoothServiceInfo &info) {
                const int ch = info.serverChannel();
                if (ch > 0 && m_sdpTimeout->isActive()) {
                    qDebug() << "[OBD-BT] SPP on channel" << ch;
                    m_sdpTimeout->stop();
                    m_sdp->stop();
                    handOff(ch);
                }
            });
    auto fallback = [this] {
        if (m_sdpTimeout->isActive()) {
            m_sdpTimeout->stop();
            handOff(1);
        }
    };
    connect(m_sdp, &QBluetoothServiceDiscoveryAgent::finished, this, fallback);
    connect(m_sdp, &QBluetoothServiceDiscoveryAgent::errorOccurred, this, [this, fallback] {
        qDebug() << "[OBD-BT] SDP lookup failed:" << m_sdp->errorString();
        fallback();
    });
    m_sdpTimeout->start(kSdpTimeoutMs);
    m_sdp->start(QBluetoothServiceDiscoveryAgent::FullDiscovery);
}

void OBDAdapterFinder::handOff(int channel)
{
    if (m_busyAddress.isEmpty() || m_handedOff)
        return;
    m_handedOff = true;
    m_sawConnecting = false;
    setStatus(m_pairError.isEmpty() ? QStringLiteral("Connecting to the vehicle…")
                                    : QStringLiteral("Couldn't pair; trying to connect anyway…"));
    if (m_obd)
        m_obd->connect_direct(m_busyAddress, channel);
    // connect_direct saved the URL as an unnamed chip; give it the real name
    if (m_settings)
        m_settings->add_obd_saved_adapter(m_busyName, RfcommSocket::makeUrl(m_busyAddress, channel));
}

void OBDAdapterFinder::onObdStatus(const QString &status)
{
    if (m_busyAddress.isEmpty() || !m_handedOff)
        return;
    // force_connect() tears the old link down ("Disconnected") before it
    // starts; only judge the attempt once it has begun
    if (status == QLatin1String("Connecting") || status == QLatin1String("Reconnecting")) {
        m_sawConnecting = true;
        return;
    }
    if (!m_sawConnecting)
        return;
    const QString name = m_busyName;
    setBusy(QString());
    if (status == QLatin1String("Connected"))
        setStatus(QStringLiteral("Connected to %1").arg(name));
    else if (status == QLatin1String("No Vehicle"))
        setStatus(QStringLiteral("%1 is connected, but the vehicle isn't answering. Turn the ignition on.").arg(name));
    else if (!m_pairError.isEmpty())
        setStatus(m_pairError, true);
    else
        setStatus(QStringLiteral("Paired with %1, but the connection failed (%2). OCTAVE keeps retrying while the "
                                 "ignition is on.").arg(name, status),
                  true);
}

void OBDAdapterFinder::cancel()
{
#ifdef OCTAVE_BLUEZ
    if (m_pairer)
        m_pairer->cancel();
#endif
    if (m_sdpTimeout)
        m_sdpTimeout->stop();
    if (m_sdp)
        m_sdp->stop();
    stopScan();
    setBusy(QString());
    setStatus(QString());
}

void OBDAdapterFinder::powerOn()
{
    if (m_local && m_local->isValid())
        m_local->powerOn();
}

void OBDAdapterFinder::setScanning(bool on)
{
    if (m_scanning == on)
        return;
    m_scanning = on;
    emit scanningChanged();
}

void OBDAdapterFinder::setStatus(const QString &message, bool isError)
{
    if (m_status == message && m_statusIsError == isError)
        return;
    m_status = message;
    m_statusIsError = isError;
    if (!message.isEmpty())
        qDebug() << "[OBD-BT]" << message;
    emit statusChanged();
}

void OBDAdapterFinder::setBusy(const QString &address)
{
    if (m_busyAddress == address)
        return;
    m_busyAddress = address;
    if (address.isEmpty())
        m_handedOff = false;
    emit busyChanged();
}
