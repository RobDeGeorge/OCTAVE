#ifndef OCTAVE_BLUEZHIDMOUSE_H
#define OCTAVE_BLUEZHIDMOUSE_H

// Makes this Linux box a Bluetooth LE mouse (HID over GATT, HOGP) through
// BlueZ over D-Bus (desktop Linux / Raspberry Pi / Orange Pi only, built
// under OCTAVE_BLUEZ). Used to drive an iPhone whose screen is mirrored over
// AirPlay: iOS takes a Bluetooth mouse as an AssistiveTouch pointer, so a tap
// on OCTAVE's screen becomes pointer moves plus a click.
//
// What start() registers with bluetoothd (all of it on the system bus, no
// raw HCI and no root):
//   - a GATT application under /org/octave/hidmouse: the HID service (0x1812:
//     HID Information, Report Map, HID Control Point, Protocol Mode and one
//     input Report) and a Battery service (0x180F, always 100 %)
//   - an LE advertisement (connectable, general discoverable, appearance
//     "mouse", the HID service UUID and `name`)
// and it powers the adapter on when it was merely off, putting that back in
// stop(). New pairings are only taken inside a window the user opens
// (setPairing(true)): then a NoInputNoOutput (Just Works) agent is made the
// default agent, so the iPhone's request is accepted with no desktop dialog
// (the head unit has no other agent), and the adapter is made Pairable;
// both go back when the window closes. Outside it the agent is gone and
// anything that asks is refused, so a stray device (a keyboard) can't pair
// with the head unit. A bonded iPhone reconnects without the window. If the process dies, bluetoothd drops the
// application, advertisement and agent by itself; only Pairable / Powered
// would stay as set.
//
// The HID service's characteristics need an encrypted link, so the first
// read from the iPhone makes it bond (that is what puts the mouse in iOS's
// Bluetooth list). A paired device is marked Trusted.

#include <QObject>
#include <QByteArray>
#include <QString>

class QDBusMessage;
class BluezHidMouseTree;

class BluezHidMouse : public QObject
{
    Q_OBJECT
public:
    explicit BluezHidMouse(QObject *parent = nullptr);
    ~BluezHidMouse() override;  // stop()s

    // Register the GATT HID application + an LE advertisement named `name`
    // with BlueZ, and a pairing agent; false when BlueZ / an adapter is
    // missing (status() says why). The two registrations finish
    // asynchronously (bluetoothd calls back into us before it answers): a
    // late refusal turns isRunning() false and says why in status().
    bool start(const QString &name);
    void stop();  // unregister everything, restore adapter state we changed
    bool isRunning() const { return m_running; }
    // Accept new pairings (Just Works, the device is then Trusted) until
    // called with false; only while running
    void setPairing(bool allowed);
    bool isPairing() const { return m_running && m_pairing; }
    // A central (the iPhone) has notifications on for the input report
    bool isConnected() const { return m_running && m_notifying; }
    // Short sentence for the UI ("Waiting for the iPhone to pair", ...)
    QString status() const { return m_status; }
    // The connected device's alias, empty otherwise
    QString peerName() const { return isConnected() ? m_peerName : QString(); }

    // One relative mouse input report: buttons bit0 left, bit1 right, bit2
    // middle; dx/dy/wheel/pan clamped to -127..127. No-op unless connected.
    void sendReport(quint8 buttons, int dx, int dy, int wheel = 0, int pan = 0);

    // The HID report descriptor (no report ID: a single 5-byte input report)
    static QByteArray reportMap();
    // buttons (3 bits + 5 padding), X, Y, Wheel, AC Pan as int8
    static QByteArray encodeReport(quint8 buttons, int dx, int dy, int wheel, int pan);

signals:
    void stateChanged();  // running / connected / status / peerName changed

private slots:
    void onBluezPropertiesChanged(const QDBusMessage &msg);

private:
    friend class BluezHidMouseTree;

    // Called by the exported objects
    void onNotifyChanged(bool notifying);
    void onAdvertisementReleased();

    bool findAdapter();
    bool setAdapterProperty(const QString &name, bool value);
    bool registerAgent();
    void unregisterAgent();
    void registerApplication();
    void registerAdvertisement();
    void fail(const QString &status);  // async failure: tear down, keep status
    void teardown();
    void resolvePeer();
    void setStatus(const QString &status);
    void refreshStatus();

    BluezHidMouseTree *m_tree = nullptr;
    bool m_running = false;
    bool m_exported = false;
    bool m_agentRegistered = false;
    bool m_appRegistered = false;
    bool m_advRegistered = false;
    bool m_notifying = false;
    bool m_pairing = false;
    bool m_watching = false;
    int m_generation = 0;  // drops replies to registrations of an earlier start()

    QString m_adapterPath;
    bool m_restorePairable = false;  // we set Pairable; put it back to false
    bool m_restorePowered = false;   // we powered the adapter on; power it off

    QString m_status;
    QString m_peerPath;
    QString m_peerName;
};

#endif  // OCTAVE_BLUEZHIDMOUSE_H
