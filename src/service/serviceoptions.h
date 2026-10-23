#ifndef OCTAVE_SERVICE_SERVICEOPTIONS_H
#define OCTAVE_SERVICE_SERVICEOPTIONS_H

// Whether the service (the command server + the service protocol) runs,
// and where it listens. Off unless asked for; the protocol has no
// authentication, so it only listens beyond loopback when told to:
//
//   --service [PORT]          on, port 9876 or PORT (also --service=PORT)
//   --service-bind ADDRESS    listen on ADDRESS instead of 127.0.0.1
//                             (0.0.0.0 for every interface)
//   --command-port PORT       the port, older spelling (dev/ clients use it)
//   setting serviceEnabled    on at every start (default false)
//   --profile                 implies --service in a build with the dev
//                             tools (OCTAVE_DEV_TOOLS), as it always has
//   --headless                implies --service: with no UI it is the only
//                             way in
//
// Pure, so tst_serviceoptions covers the parsing.

#include <QHostAddress>
#include <QStringList>

namespace OctaveService {

struct ServiceOptions {
    static constexpr quint16 kDefaultPort = 9876;

    bool enabled = false;
    quint16 port = kDefaultPort;
    QHostAddress bind = QHostAddress(QHostAddress::LocalHost);
    QString error;   // a bad --service-bind or port; the service stays off

    bool loopbackOnly() const { return bind.isLoopback(); }
};

ServiceOptions parseServiceOptions(const QStringList &arguments, bool settingEnabled, bool devToolsBuild);

} // namespace OctaveService

#endif // OCTAVE_SERVICE_SERVICEOPTIONS_H
