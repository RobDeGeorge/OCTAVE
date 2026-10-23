#include "websocket.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QList>
#include <QUrl>

namespace OctaveService::WebSocket {

namespace {
const QByteArray kGuid = QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

// "host:port" of a Host header or an origin URL's authority, normalised
// (lower case, default port 80 for http, IPv6 brackets kept)
QString authority(const QString &host, int port)
{
    return host.toLower() + QLatin1Char(':') + QString::number(port);
}

bool hostIsLiteral(const QString &host)
{
    QString h = host;
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']')))
        h = h.mid(1, h.size() - 2);
    return h.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || !QHostAddress(h).isNull();
}
} // namespace

HttpRequest parseRequest(const QByteArray &data, int *consumed)
{
    HttpRequest r;
    const int end = data.indexOf("\r\n\r\n");
    if (end < 0)
        return r;
    if (consumed)
        *consumed = end + 4;
    const QList<QByteArray> lines = data.left(end).split('\n');
    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    if (first.size() < 3)
        return r;
    r.method = first.at(0);
    r.path = first.at(1);
    for (int i = 1; i < lines.size(); ++i) {
        const int colon = lines.at(i).indexOf(':');
        if (colon > 0)
            r.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
    }
    r.valid = true;
    return r;
}

bool isUpgrade(const HttpRequest &request)
{
    return request.valid && request.method == "GET"
        && request.headers.value("upgrade").toLower() == "websocket"
        && request.headers.value("connection").toLower().contains("upgrade")
        && !request.headers.value("sec-websocket-key").isEmpty();
}

QString refusal(const HttpRequest &request)
{
    if (!request.valid)
        return QStringLiteral("malformed request");
    const QByteArray origin = request.headers.value("origin");
    if (origin.isEmpty())
        return {};   // not a browser
    if (origin == "null")
        return QStringLiteral("origin null (a file:// page or a sandboxed frame)");

    const QUrl url(QString::fromLatin1(origin));
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return QStringLiteral("unusable origin %1").arg(QString::fromLatin1(origin));
    // The Host header: "host[:port]", IPv6 in brackets
    const QString hostHeader = QString::fromLatin1(request.headers.value("host"));
    const QUrl host(QStringLiteral("http://") + hostHeader);
    if (hostHeader.isEmpty() || !host.isValid() || host.host().isEmpty())
        return QStringLiteral("no usable Host header");
    if (!hostIsLiteral(host.host()))
        return QStringLiteral("host %1 is a name, not an address (DNS rebinding)").arg(host.host());
    const int defaultPort = url.scheme() == QLatin1String("https") ? 443 : 80;
    if (authority(url.host(), url.port(defaultPort)) != authority(host.host(), host.port(80)))
        return QStringLiteral("origin %1 is not this service's page").arg(QString::fromLatin1(origin));
    return {};
}

QByteArray acceptResponse(const HttpRequest &request)
{
    const QByteArray accept =
        QCryptographicHash::hash(request.headers.value("sec-websocket-key") + kGuid, QCryptographicHash::Sha1).toBase64();
    return QByteArrayLiteral("HTTP/1.1 101 Switching Protocols\r\n"
                             "Upgrade: websocket\r\n"
                             "Connection: Upgrade\r\n"
                             "Sec-WebSocket-Accept: ")
           + accept + QByteArrayLiteral("\r\n\r\n");
}

QByteArray webRootCsp(const QByteArray &host)
{
    QByteArray connect = "'self'";
    // Only a literal host[:port] goes into the header: never a name, never
    // anything that could end the directive
    const QString text = QString::fromLatin1(host);
    const QUrl url(QStringLiteral("http://") + text);
    static const QByteArray allowed = "0123456789abcdefABCDEF.:[]localhost";
    bool plain = !host.isEmpty();
    for (char c : host)
        plain = plain && allowed.contains(c);
    if (plain && url.isValid() && hostIsLiteral(url.host()) && url.authority() == text)
        connect += " ws://" + host + " wss://" + host;
    return "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; "
           "img-src 'self' data: blob:; font-src 'self' data:; media-src 'self' blob:; connect-src " + connect
           + "; object-src 'none'; base-uri 'self'; frame-ancestors 'none'";
}

QByteArray httpResponse(int status, const QByteArray &reason, const QByteArray &contentType, const QByteArray &body,
                        const QByteArray &csp, const QByteArray &extraHeaders)
{
    return QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason
           + QByteArrayLiteral("\r\nContent-Type: ") + contentType
           + QByteArrayLiteral("\r\nContent-Length: ") + QByteArray::number(body.size())
           + QByteArrayLiteral("\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff"
                               "\r\nContent-Security-Policy: ") + csp
           + QByteArrayLiteral("\r\n") + extraHeaders
           + QByteArrayLiteral("Connection: close\r\n\r\n")
           + body;
}

QByteArray encodeFrame(Opcode opcode, const QByteArray &payload)
{
    QByteArray frame;
    frame.append(char(0x80 | opcode));
    const qint64 n = payload.size();
    if (n < 126) {
        frame.append(char(n));
    } else if (n <= 0xFFFF) {
        frame.append(char(126));
        frame.append(char((n >> 8) & 0xFF));
        frame.append(char(n & 0xFF));
    } else {
        frame.append(char(127));
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.append(char((n >> shift) & 0xFF));
    }
    return frame + payload;
}

bool Decoder::next(Message &out)
{
    while (m_error.isEmpty()) {
        if (m_buf.size() < 2)
            return false;
        const auto b0 = quint8(m_buf.at(0)), b1 = quint8(m_buf.at(1));
        const bool fin = b0 & 0x80;
        const auto opcode = Opcode(b0 & 0x0F);
        const bool masked = b1 & 0x80;
        qint64 len = b1 & 0x7F;
        int header = 2;
        if (b0 & 0x70) {
            m_error = QStringLiteral("reserved bits set");
            return false;
        }
        if (!masked) {
            m_error = QStringLiteral("unmasked client frame");
            return false;
        }
        if (len == 126) {
            if (m_buf.size() < 4)
                return false;
            len = (qint64(quint8(m_buf.at(2))) << 8) | quint8(m_buf.at(3));
            header = 4;
        } else if (len == 127) {
            if (m_buf.size() < 10)
                return false;
            len = 0;
            for (int i = 2; i < 10; ++i)
                len = (len << 8) | quint8(m_buf.at(i));
            header = 10;
        }
        if (len < 0 || len > kMaxMessage || m_partial.size() + len > kMaxMessage) {
            m_error = QStringLiteral("message over %1 bytes").arg(kMaxMessage);
            return false;
        }
        if (m_buf.size() < header + 4 + len)
            return false;
        const QByteArray mask = m_buf.mid(header, 4);
        QByteArray payload = m_buf.mid(header + 4, int(len));
        for (int i = 0; i < payload.size(); ++i)
            payload[i] = char(payload.at(i) ^ mask.at(i % 4));
        m_buf.remove(0, int(header + 4 + len));

        const bool control = opcode & 0x8;
        if (control) {
            if (!fin || len > 125) {
                m_error = QStringLiteral("bad control frame");
                return false;
            }
            if (opcode != Close && opcode != Ping && opcode != Pong) {
                m_error = QStringLiteral("unknown opcode");
                return false;
            }
            out = {opcode, payload};
            return true;
        }
        if (opcode == Continuation) {
            if (!m_inFragment) {
                m_error = QStringLiteral("continuation without a message");
                return false;
            }
            m_partial += payload;
        } else if (opcode == Text || opcode == Binary) {
            if (m_inFragment) {
                m_error = QStringLiteral("new message inside a fragmented one");
                return false;
            }
            m_partial = payload;
            m_partialOpcode = opcode;
            m_inFragment = true;
        } else {
            m_error = QStringLiteral("unknown opcode");
            return false;
        }
        if (fin) {
            out = {m_partialOpcode, m_partial};
            m_partial.clear();
            m_inFragment = false;
            return true;
        }
    }
    return false;
}

} // namespace OctaveService::WebSocket
