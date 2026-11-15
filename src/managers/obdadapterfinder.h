#ifndef OCTAVE_OBDADAPTERFINDER_H
#define OCTAVE_OBDADAPTERFINDER_H

// Finds, pairs, trusts and connects a Bluetooth OBD-II adapter from the GUI,
// so nobody has to know the adapter's MAC or pair it in the OS first.
//
//   startScan()        Bluetooth discovery (Classic + BLE). Every named
//                      device lands in `devices`; likely OBD adapters
//                      (name or BLE service match) are flagged and sorted
//                      first.
//   connectAdapter(a)  Classic: pair with the adapter's PIN (1234, 0000, ...)
//                      answered by OCTAVE itself, trust it, look up its serial
//                      port channel, then hand off to
//                      OBDManager::connect_direct(). BLE: hand the address to
//                      OBDManager::connect_to_adapter() where a BLE transport
//                      exists.
//
// Phase 1 of TODO/obd-adapter-discovery.md: the full flow runs on desktop
// Linux (Pi / Orange Pi included). The browser build has its own over Web
// Bluetooth and Web Serial (src/platform/wasm/obdadapterfinder_wasm.cpp),
// where pickSerialPort() adds an adapter on a USB cable. Elsewhere
// `supported` is false and the QML sheet falls back to
// obdManager.availableAdapters.
// QML context property "obdAdapterFinder".

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariantList>

class OBDManager;
class SettingsManager;
class QBluetoothDeviceDiscoveryAgent;
class QBluetoothDeviceInfo;
class QBluetoothLocalDevice;
class QBluetoothServiceDiscoveryAgent;
class QTimer;
class BluezPairer;

class OBDAdapterFinder : public QObject
{
    Q_OBJECT
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "vehicle")
    Q_CLASSINFO("octave.experimental", "canPickSerial pickSerialPort")
    // Discovery + pairing + connect work on this platform
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // startScan() can look for adapters (false in a browser without Web
    // Bluetooth, where the sheet offers only the simulated car)
    Q_PROPERTY(bool canScan READ canScan CONSTANT)
    // pickSerialPort() can open a list of serial ports: the browser build in
    // a browser with Web Serial (Chrome / Edge on a computer). False in the
    // app, where a cable adapter is a port in obdManager.availableAdapters.
    Q_PROPERTY(bool canPickSerial READ canPickSerial CONSTANT)
    // A Bluetooth controller exists and is powered
    Q_PROPERTY(bool bluetoothOn READ bluetoothOn NOTIFY bluetoothOnChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // [{address, name, rssi, kind: "classic"|"ble"|"dual"|"serial"|"sim", likelyObd,
    //   paired, connectable, note}] -- likely adapters first, then by signal
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // Address being paired / connected, "" when idle
    Q_PROPERTY(QString busyAddress READ busyAddress NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)

public:
    explicit OBDAdapterFinder(OBDManager *obd, SettingsManager *settings, QObject *parent = nullptr);
    ~OBDAdapterFinder() override;

    bool supported() const;
    bool canScan() const;
    bool canPickSerial() const;
    bool bluetoothOn() const;
    bool scanning() const { return m_scanning; }
    QVariantList devices() const { return m_deviceList; }
    QString busyAddress() const { return m_busyAddress; }
    QString status() const { return m_status; }
    bool statusIsError() const { return m_statusIsError; }

    // Name / BLE-service heuristics, exposed for tests
    static bool looksLikeObd(const QString &name, const QStringList &serviceUuids);

public slots:
    void startScan();
    // Browser build: the browser's serial port list (a USB adapter, or a
    // Bluetooth Classic one paired in the system); the port picked is listed
    // and connected. Needs a tap, like startScan() there. Nothing elsewhere.
    void pickSerialPort();
    void stopScan();
    void connectAdapter(const QString &address);
    void cancel();
    void powerOn();

signals:
    void bluetoothOnChanged();
    void scanningChanged();
    void devicesChanged();
    void busyChanged();
    void statusChanged();

private:
    // tests/cpp drives the pairing outcome and list order directly
    friend class TstObdAdapterFinder;

    // Creates the QtBluetooth objects once org.bluez is on the system bus.
    // Never at startup otherwise: with bluetoothd stopped (or no adapter)
    // QBluetoothLocalDevice's constructor D-Bus-activates org.bluez and
    // blocks the GUI thread for the activation timeout, twice (~45 s).
    bool ensureBluetooth();
    static bool bluezRunning();
    void setScanning(bool on);
    void setStatus(const QString &message, bool isError = false);
    void setBusy(const QString &address);
    void onDeviceFound(const QBluetoothDeviceInfo &info);
    void rebuildList();
    void onPaired(bool ok, bool tryAnyway, const QString &message);
    void lookUpChannel();
    void handOff(int channel);
    void onObdStatus(const QString &status);

    OBDManager *m_obd = nullptr;
    SettingsManager *m_settings = nullptr;
    QBluetoothLocalDevice *m_local = nullptr;
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QBluetoothServiceDiscoveryAgent *m_sdp = nullptr;
    BluezPairer *m_pairer = nullptr;
    QTimer *m_sdpTimeout = nullptr;

    QHash<QString, QVariantMap> m_devices;  // by upper-case address
    QVariantList m_deviceList;
    bool m_scanning = false;
    QString m_busyAddress;
    QString m_busyName;
    bool m_handedOff = false;
    bool m_sawConnecting = false;
    QString m_pairError;  // pairing failed but we're trying to connect anyway
    QString m_status;
    bool m_statusIsError = false;
    // Browser build: the next chooser lists every device, not likely
    // adapters only (after the user closed one without a pick)
    bool m_everyDevice = false;
};

#endif  // OCTAVE_OBDADAPTERFINDER_H
