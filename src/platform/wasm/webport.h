#ifndef OCTAVE_WASM_WEBPORT_H
#define OCTAVE_WASM_WEBPORT_H

// The byte pipe between a browser link on the page's main thread (Web
// Bluetooth, Web Serial: browser objects only exist there) and the QIODevice
// the OBD worker thread opens for it. Shared by webbluetooth and webserial.
//
// The link calls connecting() / up() / down() / received() from the main
// thread. createDevice() makes the worker's device: reads wait on received(),
// writes are queued to the main thread. Opening it while the link is down
// asks the link to come back (Hooks::reopen) and fails at once: OBDManager
// retries the port on its own schedule, and waiting here for the main thread
// froze the page when the main thread was waiting on the worker.

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <functional>

class QIODevice;
class QObject;

class WebPortChannel
{
public:
    struct Hooks {
        QObject *context = nullptr;                    // main-thread object the two calls are queued to
        std::function<void()> reopen;                  // bring the last adapter back
        std::function<void(const QByteArray &)> write; // send to the adapter
        QString reopening;                             // open() error while the link is down
        QString lost;                                  // read error when the link went without a reason
    };

    // A connect is under way: down, with no error yet
    void connecting();
    // The link is up; `describe` goes in the connection log
    void up(const QString &describe);
    void down(const QString &error);
    void received(const QByteArray &bytes);

    QIODevice *createDevice(QObject *parent, const Hooks &hooks);

private:
    friend class WebPortDevice;

    QMutex m_mutex;
    QWaitCondition m_changed;
    QByteArray m_rx;
    bool m_up = false;
    QString m_error;
    QString m_describe;
};

#endif
