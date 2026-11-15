#ifndef OCTAVE_WASM_WEBSERIAL_H
#define OCTAVE_WASM_WEBSERIAL_H

// OBD-II adapters on a cable in the browser build (WebAssembly only), over
// Web Serial: Chrome and Edge on a computer, no flag needed (Linux included,
// where Web Bluetooth is behind one). The browser's port list also carries
// Bluetooth Classic adapters paired in the system's settings, so those work
// through it too. Not Safari or Firefox, which have no Web Serial.
//
//   WebSerialLink       main thread. Owns the port through a small JS helper
//                       (Module.octaveWebSerial): the browser's port chooser,
//                       open at 38400 baud (what OBDConnectionWorker opens a
//                       QSerialPort at), a read loop, queued writes.
//   the port device     the QIODevice the OBD worker thread opens for port
//                       "webserial://<id>" (webport.h). Opening it while the
//                       link is down reopens the last port and fails;
//                       OBDManager's next retry finds it up.
//
// The chooser needs a user gesture: OBDAdapterFinder::pickSerialPort() (the
// wasm one, obdadapterfinder_wasm.cpp) calls choose() from the tap on the
// adapter sheet's "Pick USB Adapter".

#include <QObject>
#include <QString>

class WebSerialLink : public QObject
{
    Q_OBJECT

public:
    // Created on first use; call from the main thread
    static WebSerialLink *instance();
    // Serve "webserial://" ports to the OBD worker. Call once, on the main thread.
    static void registerTransport();
    static QString portFor(const QString &portId);

    // navigator.serial exists (Chrome / Edge in a secure context)
    bool available() const;

    // Opens the browser's port chooser
    void choose();
    // Opens a port choose() returned
    void connectPort(const QString &id);
    // Closes the port; reconnect() does nothing until the next connectPort()
    void disconnectPort();
    void reconnect();
    void write(const QByteArray &bytes);

    // From the JS helper (embind)
    void handleEvent(const QString &kind, const QString &a, const QString &b, const QString &c = QString());
    void handleBytes(const QByteArray &bytes);

signals:
    void chosen(const QString &id, const QString &name);
    void chooseFailed(const QString &message, bool cancelled);
    void linkUp(const QString &id, const QString &name);
    // An open failed or the port went away
    void linkDown(const QString &reason);

private:
    explicit WebSerialLink(QObject *parent = nullptr);

    QString m_connectingId;
    bool m_connecting = false;   // an open is under way in the JS helper
    QString m_lastId;   // the port reconnect() goes back to
};

#endif
