#ifndef OCTAVE_WASM_WEBBLUETOOTH_H
#define OCTAVE_WASM_WEBBLUETOOTH_H

// Real OBD-II adapters in the browser demo (WebAssembly build only), over
// Web Bluetooth: Chrome and Edge, desktop and Android. Not Safari, iOS or
// Firefox, which have no Web Bluetooth.
//
// An ELM327 Bluetooth LE dongle is a serial line carried by two GATT
// characteristics: notify (adapter -> us) and write (us -> adapter). These
// are the vendor profiles the Android bridge speaks
// (android/src/org/octave/app/OctaveOBDBridge.java), plus vLinker's and
// 18F0.
//
//   WebBluetoothLink    main thread. Owns the GATT link through a small JS
//                       helper (Module.octaveWebBt): the browser's device
//                       chooser, connect, notifications, writes split into
//                       20-byte chunks. Browser objects only exist on the
//                       page's main thread.
//   the port device     the QIODevice the OBD worker thread opens for port
//                       "webbt://<id>" (registerTransport(), through
//                       OBDConnectionWorker::addCustomTransport()). Reads
//                       wait on the notifications; writes are queued to the
//                       link. Opening it while the link is down reconnects
//                       the last adapter (the browser allows that without a
//                       new pick), so OBDManager's retries survive an
//                       ignition cycle.
//
// The chooser needs a user gesture: OBDAdapterFinder::startScan() (the
// wasm one, obdadapterfinder_wasm.cpp) calls choose() from the tap that
// opened the adapter sheet.

#include <QObject>
#include <QString>

class WebBluetoothLink : public QObject
{
    Q_OBJECT

public:
    // Created on first use; call from the main thread
    static WebBluetoothLink *instance();
    // Serve "webbt://" ports to the OBD worker. Call once, on the main thread.
    static void registerTransport();
    static QString portFor(const QString &deviceId);

    // navigator.bluetooth exists (Chrome / Edge in a secure context)
    bool available() const;
    // Why it isn't: "insecure", "linux-chromium", "firefox", "apple" or "other"
    QString browserKind() const;

    // Opens the browser's chooser; `everyDevice` lists every Bluetooth LE
    // device nearby instead of likely OBD adapters only
    void choose(bool everyDevice);
    // GATT connect + notifications on a device chooser() returned
    void connectDevice(const QString &id);
    void disconnectDevice();
    void reconnect();
    void write(const QByteArray &bytes);

    // From the JS helper (embind)
    void handleEvent(const QString &kind, const QString &a, const QString &b, const QString &c = QString());
    void handleBytes(const QByteArray &bytes);

signals:
    void chosen(const QString &id, const QString &name);
    void chooseFailed(const QString &message, bool cancelled);
    void linkUp(const QString &id, const QString &name);
    // A connect failed or the link was lost
    void linkDown(const QString &reason);

private:
    explicit WebBluetoothLink(QObject *parent = nullptr);

    QString m_connectingId;
    QString m_lastId;   // the adapter reconnect() goes back to
};

#endif
