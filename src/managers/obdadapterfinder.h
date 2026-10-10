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
// Linux (Pi / Orange Pi included). Elsewhere `supported` is false and the QML
// sheet falls back to obdManager.availableAdapters.
// Mirrors backend/obd_adapter_finder.py; QML context property
// "obdAdapterFinder".

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
    // Discovery + pairing + connect work on this platform
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // A Bluetooth controller exists and is powered
    Q_PROPERTY(bool bluetoothOn READ bluetoothOn NOTIFY bluetoothOnChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // [{address, name, rssi, kind: "classic"|"ble"|"dual", likelyObd,
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
};

#endif  // OCTAVE_OBDADAPTERFINDER_H
