#ifndef AAPFRAMER_H
#define AAPFRAMER_H

// AAP framing (docs/ANDROID_AUTO_PROTOCOL.md, "Framing"): splits the byte
// stream into frames and joins fragmented messages back together.
//
// Encrypted frames must be decrypted in the order they arrive (TLS is one
// stream across every channel), so reading is two steps: FrameReader hands
// out raw frames, the caller decrypts each, and MessageAssembler joins the
// plaintext of a channel's fragments into whole messages.

#include <QByteArray>
#include <QHash>
#include <QList>

namespace Aap {

namespace Flag {
constexpr quint8 First = 0x01;
constexpr quint8 Last = 0x02;
constexpr quint8 Bulk = First | Last;      // a whole message in one frame
constexpr quint8 FrameTypeMask = Bulk;
constexpr quint8 Control = 0x04;           // control-type message on a service channel
constexpr quint8 Encrypted = 0x08;
}

struct Frame {
    quint8 channel = 0;
    quint8 flags = 0;
    QByteArray payload;
};

class FrameReader
{
public:
    // Largest frame payload accepted (the size field is 16 bits anyway)
    static constexpr int kMaxFrame = 0xFFFF;

    void feed(const QByteArray &bytes) { m_buf.append(bytes); }
    // The next complete frame, or false when more bytes are needed
    bool next(Frame &out);
    qsizetype buffered() const { return m_buf.size(); }

private:
    QByteArray m_buf;
};

struct Message {
    quint8 channel = 0;
    quint8 flags = 0;           // of the first fragment, frame type bits cleared
    quint16 id = 0;
    QByteArray body;            // after the 2-byte message id
};

class MessageAssembler
{
public:
    // Largest reassembled message (a 1080p key frame is well under this)
    static constexpr qsizetype kMaxMessage = 16 * 1024 * 1024;

    // Adds one (decrypted) frame. Returns true and fills `out` when the
    // frame completes a message; `error` is set on a framing violation.
    bool add(quint8 channel, quint8 flags, const QByteArray &plain, Message &out, QString *error = nullptr);
    void clear() { m_partial.clear(); }

private:
    struct Partial {
        quint8 flags = 0;
        QByteArray data;
    };
    QHash<quint8, Partial> m_partial;
};

// One message as frames on the wire. `payload` is the already-encrypted (or
// plain) bytes of the whole message for a BULK frame; longer payloads are
// split into FIRST / MIDDLE / LAST.
QByteArray encodeFrames(quint8 channel, quint8 flags, const QByteArray &payload);

// A message id followed by its body
QByteArray messageBytes(quint16 id, const QByteArray &body);

} // namespace Aap

#endif // AAPFRAMER_H
