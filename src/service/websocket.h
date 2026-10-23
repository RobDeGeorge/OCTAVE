// websocket.h
//
// Just enough RFC 6455 for the service to talk to a browser on the command
// server's own port (no Qt WebSockets module, which no build ships): the
// HTTP upgrade, text frames each way, ping / pong and close. A client
// that opens with "GET " is a WebSocket (or a request for the reference
// page, sdk/web); anything else is the JSON-lines protocol. Each text frame
// carries one request or one reply / event, the same JSON as a line.
//
// Origin rule (a browser on the car must not let any website drive it):
// no Origin header (not a browser: scripts, the SDK) is allowed; a browser
// is allowed only from the service's own page, i.e. the Origin's host:port
// equals the Host header, and that host is an IP address or "localhost"
// (a domain name could be DNS-rebound to this machine). "null" (file://
// pages, sandboxed iframes on any site) is refused.
//
// No extensions are negotiated (no Sec-WebSocket-Extensions in the reply),
// so permessage-deflate and the reserved bits stay off.
//
// Pure functions and a decoder; commandserver.cpp does the socket work.

#ifndef OCTAVE_SERVICE_WEBSOCKET_H
#define OCTAVE_SERVICE_WEBSOCKET_H

#include <QByteArray>
#include <QHash>
#include <QString>

namespace OctaveService::WebSocket {

// An HTTP request head (up to the blank line)
struct HttpRequest {
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers;   // names lower-cased, values trimmed
    bool valid = false;
};

// The head of `data` if it holds one complete (ends with \r\n\r\n);
// `consumed` is its length. Not valid while incomplete.
HttpRequest parseRequest(const QByteArray &data, int *consumed);

bool isUpgrade(const HttpRequest &request);

// Empty if the request may connect, else why not (for the log and a 403)
QString refusal(const HttpRequest &request);

// The 101 Switching Protocols reply (Sec-WebSocket-Accept from the key)
QByteArray acceptResponse(const HttpRequest &request);

// The reference page's policy: its script is inline; it may only talk to
// this service
inline constexpr char kInlinePageCsp[] = "default-src 'self' 'unsafe-inline'; connect-src 'self' ws: wss:";

// The policy for a --web-root frontend: its own files only (no inline
// script; inline styles, data: / blob: images and fonts, and WebAssembly
// allowed, which bundlers emit), sockets only to this service. `host` is
// the request's Host header, named in connect-src as ws:// and wss:// when
// it is an IP address or localhost with an optional port; 'self' alone
// covers it in current browsers.
QByteArray webRootCsp(const QByteArray &host);

// A plain HTTP reply (the reference page, 403, 404). nosniff and
// no-store on every reply.
QByteArray httpResponse(int status, const QByteArray &reason, const QByteArray &contentType, const QByteArray &body,
                        const QByteArray &csp = kInlinePageCsp);

enum Opcode : quint8 { Continuation = 0x0, Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

// A server frame: unmasked, FIN set
QByteArray encodeFrame(Opcode opcode, const QByteArray &payload);

// Client frames in, whole messages out. Client frames must be masked;
// a message over kMaxMessage, an unmasked or malformed frame is an error
// (the connection closes).
class Decoder
{
public:
    static constexpr qint64 kMaxMessage = 1 << 20;

    struct Message {
        Opcode opcode = Text;   // Text, Binary, Close, Ping or Pong
        QByteArray payload;
    };

    void feed(const QByteArray &bytes) { m_buf += bytes; }
    // True with `out` filled when a message is complete; false when more
    // bytes are needed or after an error (check error())
    bool next(Message &out);
    const QString &error() const { return m_error; }

private:
    QByteArray m_buf;
    QByteArray m_partial;
    Opcode m_partialOpcode = Text;
    bool m_inFragment = false;
    QString m_error;
};

} // namespace OctaveService::WebSocket

#endif // OCTAVE_SERVICE_WEBSOCKET_H
