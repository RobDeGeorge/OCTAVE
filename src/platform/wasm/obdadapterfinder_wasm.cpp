// WebAssembly build: OBDAdapterFinder over Web Bluetooth (webbluetooth.h)
// and Web Serial (webserial.h), plus the shared "Simulated car" demo drive
// (src/sim/demodrive.h). The browser does the scanning in its own chooser, so
// startScan() opens the Bluetooth one and pickSerialPort() the serial one
// (an adapter on a USB cable); the adapter picked there is listed and
// connected at once, and tapping the row later reconnects it. Pairing is the
// browser's business (BLE ELM327 adapters need none). Replaces
// src/managers/obdadapterfinder.cpp when OCTAVE_WASM. Without Web Bluetooth
// (Safari, Firefox, Linux Chromium without its flag) canScan is false, without
// Web Serial (Safari, Firefox, phones) canPickSerial is; with neither the
// sheet offers only the simulated car and the status says what this browser
// would need.

#include "../../managers/obdadapterfinder.h"

#include "../../managers/obdmanager.h"
#include "../../sim/simelm327.h"
#include "webbluetooth.h"
#include "webserial.h"

#include <QDebug>

#include <algorithm>

namespace {
QString simAddress() { return QString::fromLatin1(OctaveSim::kSimPort); }
QString noBluetooth()
{
    const QString kind = WebBluetoothLink::instance()->browserKind();
    if (kind == QLatin1String("linux-chromium"))
        return QStringLiteral("Bluetooth is switched off in Chrome / Chromium on Linux: open "
                              "chrome://flags/#enable-experimental-web-platform-features, set it to Enabled, relaunch "
                              "and reload this page. The simulated car works meanwhile.");
    if (kind == QLatin1String("firefox"))
        return QStringLiteral("Firefox can't reach Bluetooth devices from a web page. Open OCTAVE in Chrome or Edge, "
                              "or install the OCTAVE app, to connect a Bluetooth OBD2 adapter. The simulated car "
                              "works here.");
    if (kind == QLatin1String("apple"))
        return QStringLiteral("Safari can't reach Bluetooth devices from a web page. Open OCTAVE in Chrome or Edge on "
                              "a computer or Android to connect a Bluetooth LE OBD2 adapter. The simulated car works "
                              "here.");
    if (kind == QLatin1String("insecure"))
        return QStringLiteral("Bluetooth needs the page over https (or localhost). The simulated car works here.");
    return QStringLiteral("This browser has no Bluetooth access: open OCTAVE in Chrome or Edge (computer or Android) "
                          "to connect a Bluetooth LE OBD2 adapter. The simulated car works in any browser.");
}
}

OBDAdapterFinder::OBDAdapterFinder(OBDManager *obd, SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_obd(obd)
    , m_settings(settings)
{
    m_devices.insert(simAddress(), OBDManager::simulatedAdapter());
    rebuildList();
    WebBluetoothLink *link = WebBluetoothLink::instance();
    WebSerialLink *serial = WebSerialLink::instance();
    if (link->available())
        m_status = serial->available()
                       ? QStringLiteral("Pick your Bluetooth LE OBD2 adapter, or one on a USB cable, in the browser's "
                                        "list, or try the simulated car.")
                       : QStringLiteral("Pick your Bluetooth LE OBD2 adapter in the browser's list, or try the "
                                        "simulated car.");
    else
        m_status = serial->available()
                       ? QStringLiteral("An OBD2 adapter on a USB cable works here: tap Pick USB Adapter. ")
                             + noBluetooth()
                       : noBluetooth();
    connect(serial, &WebSerialLink::chosen, this, [this](const QString &id, const QString &name) {
        m_devices.insert(id, QVariantMap{
                                 {QStringLiteral("address"), id},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("rssi"), 0},
                                 {QStringLiteral("kind"), QStringLiteral("serial")},
                                 {QStringLiteral("likelyObd"), true},
                                 {QStringLiteral("paired"), false},
                                 {QStringLiteral("connectable"), true},
                                 {QStringLiteral("note"), QString()},
                             });
        rebuildList();
        connectAdapter(id);
    });
    connect(serial, &WebSerialLink::chooseFailed, this, [this](const QString &message, bool cancelled) {
        if (cancelled)
            setStatus(QStringLiteral("No port picked. Plug the adapter's USB cable in and try again."));
        else
            setStatus(message, true);
    });
    connect(serial, &WebSerialLink::linkUp, this, [this](const QString &id) {
        if (m_busyAddress != id || m_handedOff || !m_obd)
            return;
        m_handedOff = true;
        m_sawConnecting = false;
        setStatus(QStringLiteral("Connecting to the vehicle…"));
        m_obd->setPortOverride(WebSerialLink::portFor(id));
        m_obd->force_connect();
    });
    connect(serial, &WebSerialLink::linkDown, this, [this](const QString &reason) {
        if (m_busyAddress.isEmpty() || m_handedOff)
            return;
        setBusy(QString());
        setStatus(reason, true);
    });
    connect(link, &WebBluetoothLink::chosen, this, [this](const QString &id, const QString &name) {
        setScanning(false);
        m_everyDevice = false;
        m_devices.insert(id, QVariantMap{
                                 {QStringLiteral("address"), id},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("rssi"), 0},
                                 {QStringLiteral("kind"), QStringLiteral("ble")},
                                 {QStringLiteral("likelyObd"), true},
                                 {QStringLiteral("paired"), false},
                                 {QStringLiteral("connectable"), true},
                                 {QStringLiteral("note"), QString()},
                             });
        rebuildList();
        connectAdapter(id);
    });
    connect(link, &WebBluetoothLink::chooseFailed, this, [this](const QString &message, bool cancelled) {
        setScanning(false);
        if (cancelled) {
            m_everyDevice = true;
            setStatus(QStringLiteral("No adapter picked. Next time the list shows every Bluetooth device nearby, for an "
                                     "adapter with an unusual name."));
        } else {
            setStatus(message, true);
        }
    });
    connect(link, &WebBluetoothLink::linkUp, this, [this](const QString &id) {
        if (m_busyAddress != id || m_handedOff || !m_obd)
            return;
        m_handedOff = true;
        m_sawConnecting = false;
        setStatus(QStringLiteral("Connecting to the vehicle…"));
        // An override, not the saved port (nothing persists in the browser)
        m_obd->setPortOverride(WebBluetoothLink::portFor(id));
        m_obd->force_connect();
    });
    connect(link, &WebBluetoothLink::linkDown, this, [this](const QString &reason) {
        // After the hand-off OBDManager reports a lost link itself
        if (m_busyAddress.isEmpty() || m_handedOff)
            return;
        setBusy(QString());
        setStatus(reason, true);
    });
    if (m_obd)
        connect(m_obd, &OBDManager::connectionStatusChanged, this, &OBDAdapterFinder::onObdStatus);
}

OBDAdapterFinder::~OBDAdapterFinder() = default;

// The sheet always has the simulated car to offer
bool OBDAdapterFinder::supported() const { return true; }
// The browser can't say until its chooser opens; it explains a radio that's off
bool OBDAdapterFinder::bluetoothOn() const { return true; }
bool OBDAdapterFinder::canScan() const { return WebBluetoothLink::instance()->available(); }
bool OBDAdapterFinder::canPickSerial() const { return WebSerialLink::instance()->available(); }
bool OBDAdapterFinder::looksLikeObd(const QString &, const QStringList &) { return false; }

void OBDAdapterFinder::startScan()
{
    if (!WebBluetoothLink::instance()->available()) {
        setStatus(noBluetooth(), true);
        return;
    }
    if (!m_busyAddress.isEmpty() && !m_handedOff)
        return;  // connecting
    setScanning(true);
    setStatus(m_everyDevice ? QStringLiteral("Pick your adapter in the browser's list (every Bluetooth device nearby).")
                            : QStringLiteral("Pick your adapter in the browser's list. Plug it in and turn the "
                                             "ignition on first."));
    WebBluetoothLink::instance()->choose(m_everyDevice);
}

void OBDAdapterFinder::pickSerialPort()
{
    if (!WebSerialLink::instance()->available()) {
        setStatus(QStringLiteral("This browser can't reach serial ports. Open OCTAVE in Chrome or Edge on a computer "
                                 "to connect an adapter on a USB cable."),
                  true);
        return;
    }
    if (!m_busyAddress.isEmpty() && !m_handedOff)
        return;  // connecting
    setStatus(QStringLiteral("Pick the adapter's port in the browser's list. Plug its USB cable in and turn the "
                             "ignition on first."));
    WebSerialLink::instance()->choose();
}

// The browser's chooser can't be closed from the page
void OBDAdapterFinder::stopScan() { setScanning(false); }

void OBDAdapterFinder::connectAdapter(const QString &address)
{
    const QVariantMap d = m_devices.value(address);
    if (d.isEmpty()) {
        setStatus(QStringLiteral("That adapter is no longer in the list: pick it again."), true);
        return;
    }
    if (!m_busyAddress.isEmpty() && !m_handedOff && address != simAddress())
        return;  // the simulated adapter can always take over
    m_busyName = d.value(QStringLiteral("name")).toString();
    const bool serial = d.value(QStringLiteral("kind")).toString() == QLatin1String("serial");
    // One link at a time. Before the busy state changes: each link reports
    // "Disconnected", which must not pass for this attempt failing.
    if (address == simAddress() || !serial)
        WebSerialLink::instance()->disconnectPort();
    if (address == simAddress() || serial)
        WebBluetoothLink::instance()->disconnectDevice();
    if (address == simAddress()) {
        // Straight to OBDManager: judged by onObdStatus() like a handed-off adapter
        m_handedOff = true;
        m_sawConnecting = false;
        setBusy(address);
        setStatus(QStringLiteral("Starting the demo drive…"));
        if (m_obd) m_obd->connect_to_adapter(address);
        return;
    }
    m_handedOff = false;
    setBusy(address);
    if (serial) {
        qDebug() << "[OBD-BT] Open" << m_busyName << "over Web Serial";
        setStatus(QStringLiteral("Opening the %1…").arg(m_busyName));
        WebSerialLink::instance()->connectPort(address);
        return;
    }
    qDebug() << "[OBD-BT] Connect" << m_busyName << "over Web Bluetooth";
    setStatus(QStringLiteral("Connecting to %1 over Bluetooth LE…").arg(m_busyName));
    WebBluetoothLink::instance()->connectDevice(address);
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
        setStatus(QStringLiteral("Connected to %1.").arg(name));
    else if (status == QLatin1String("No Vehicle"))
        setStatus(QStringLiteral("%1 is connected, but the vehicle isn't answering. Turn the ignition on.").arg(name));
    else
        setStatus(QStringLiteral("Connected to %1, but the ELM327 didn't start (%2). OCTAVE keeps retrying while "
                                 "the ignition is on.").arg(name, status),
                  true);
}

void OBDAdapterFinder::cancel()
{
    if (!m_busyAddress.isEmpty() && !m_handedOff) {
        WebBluetoothLink::instance()->disconnectDevice();
        WebSerialLink::instance()->disconnectPort();
    }
    setScanning(false);
    setBusy(QString());
    setStatus(QString());
}

void OBDAdapterFinder::powerOn() {}

void OBDAdapterFinder::rebuildList()
{
    QList<QVariantMap> rows = m_devices.values();
    // Real adapters first, the simulated car last
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        const bool sa = a.value(QStringLiteral("kind")).toString() == QLatin1String("sim");
        const bool sb = b.value(QStringLiteral("kind")).toString() == QLatin1String("sim");
        if (sa != sb)
            return sb;
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
