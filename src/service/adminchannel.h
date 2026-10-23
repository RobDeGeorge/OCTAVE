// adminchannel.h
//
// A local admin channel to the running app, for its own user on its own
// machine: `octave --pair-code` gets a pairing code from it when the QML
// Settings page isn't up (an installed exec frontend is the screen, or the
// app runs headless). Never on the network.
//
// A QLocalServer: a Unix socket in the user's runtime folder
// ($XDG_RUNTIME_DIR, mode 0700 on Linux; the temp folder elsewhere) or a
// Windows named pipe, opened with QLocalServer::UserAccessOption (socket
// file mode 0700 / a DACL for the user's own SID). On Unix the server also
// checks the peer's uid (SO_PEERCRED / getpeereid) and closes anything
// from another user. The name comes from a hash of the user and the
// canonical config folder, so an instance with another --config is a
// different channel and cannot be reached by mistake.
//
// One line of JSON each way, then the connection closes:
//   {"op": "pairCode"} -> {"ok": true, "code": "123456", "secondsLeft": 120}
//   anything else      -> {"ok": false, "error": "..."}
// pairCode is the only op. It shows a code exactly as Settings > Device >
// Service would (ServicePairing::startPairing, on the GUI thread).
// main.cpp starts the channel once the service is up, desktop only.

#ifndef OCTAVE_SERVICE_ADMINCHANNEL_H
#define OCTAVE_SERVICE_ADMINCHANNEL_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class QLocalServer;
class ServicePairing;

namespace OctaveService {

class AdminChannel : public QObject
{
    Q_OBJECT

public:
    explicit AdminChannel(ServicePairing *pairing, QObject *parent = nullptr);
    ~AdminChannel() override;

    // Listen for the instance whose config folder is `configDir`
    bool listen(const QString &configDir);
    QString serverName() const { return m_name; }

    // The channel name (a socket path on Unix, a pipe name on Windows)
    static QString nameFor(const QString &configDir);
    // `octave --pair-code`: {ok, code, secondsLeft} or {ok: false, error}
    static QVariantMap requestPairCode(const QString &configDir, int timeoutMs = 3000);

private:
    void serve();

    QPointer<ServicePairing> m_pairing;
    QLocalServer *m_server = nullptr;
    QString m_name;
};

} // namespace OctaveService

#endif // OCTAVE_SERVICE_ADMINCHANNEL_H
