#include "aapframer.h"

#include <QString>
#include <QtEndian>

namespace Aap {

bool FrameReader::next(Frame &out)
{
    if (m_buf.size() < 4)
        return false;
    const quint8 flags = quint8(m_buf[1]);
    const bool first = (flags & Flag::FrameTypeMask) == Flag::First;
    const int header = first ? 8 : 4;    // FIRST frames also carry the total size
    if (m_buf.size() < header)
        return false;
    const quint16 size = qFromBigEndian<quint16>(m_buf.constData() + 2);
    if (m_buf.size() < header + size)
        return false;
    out.channel = quint8(m_buf[0]);
    out.flags = flags;
    out.payload = m_buf.mid(header, size);
    m_buf.remove(0, header + size);
    return true;
}

bool MessageAssembler::add(quint8 channel, quint8 flags, const QByteArray &plain, Message &out, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        m_partial.remove(channel);
        return false;
    };
    const quint8 type = flags & Flag::FrameTypeMask;
    QByteArray whole;
    quint8 msgFlags = flags & ~Flag::FrameTypeMask;
    if (type == Flag::Bulk) {
        if (m_partial.contains(channel))
            m_partial.remove(channel);   // a new message abandons a half-received one
        whole = plain;
    } else if (type == Flag::First) {
        m_partial[channel] = Partial{msgFlags, plain};
        return false;
    } else {
        auto it = m_partial.find(channel);
        if (it == m_partial.end())
            return fail(QStringLiteral("fragment on channel %1 without a first frame").arg(channel));
        if (it->data.size() + plain.size() > kMaxMessage)
            return fail(QStringLiteral("message on channel %1 is too large").arg(channel));
        it->data.append(plain);
        if (type != Flag::Last)
            return false;
        msgFlags = it->flags;
        whole = std::move(it->data);
        m_partial.erase(it);
    }
    if (whole.size() < 2)
        return fail(QStringLiteral("message on channel %1 has no id").arg(channel));
    out.channel = channel;
    out.flags = msgFlags;
    out.id = qFromBigEndian<quint16>(whole.constData());
    out.body = whole.mid(2);
    return true;
}

QByteArray encodeFrames(quint8 channel, quint8 flags, const QByteArray &payload)
{
    QByteArray out;
    const quint8 base = flags & ~Flag::FrameTypeMask;
    auto append = [&](quint8 type, const QByteArray &chunk, qint64 total) {
        out.append(char(channel));
        out.append(char(base | type));
        char size[2];
        qToBigEndian(quint16(chunk.size()), size);
        out.append(size, 2);
        if (total >= 0) {
            char t[4];
            qToBigEndian(quint32(total), t);
            out.append(t, 4);
        }
        out.append(chunk);
    };
    if (payload.size() <= FrameReader::kMaxFrame) {
        append(Flag::Bulk, payload, -1);
        return out;
    }
    const int chunk = 0x4000;
    for (qsizetype pos = 0; pos < payload.size(); pos += chunk) {
        const QByteArray part = payload.mid(pos, chunk);
        if (pos == 0)
            append(Flag::First, part, payload.size());
        else if (pos + chunk >= payload.size())
            append(Flag::Last, part, -1);
        else
            append(0, part, -1);
    }
    return out;
}

QByteArray messageBytes(quint16 id, const QByteArray &body)
{
    QByteArray b(2, '\0');
    qToBigEndian(id, b.data());
    b.append(body);
    return b;
}

} // namespace Aap
