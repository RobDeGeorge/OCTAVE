// streamhub.cpp

#include "streamhub.h"

#include "commandserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace OctaveService {

// ── H.264 helpers ──────────────────────────────────────────────────────

namespace H264 {

namespace {
// Start code positions: (offset of the NAL, length of its start code)
QList<QPair<int, int>> startCodes(const QByteArray &d)
{
    QList<QPair<int, int>> out;
    const int n = d.size();
    for (int i = 0; i + 2 < n;) {
        if (d.at(i) == 0 && d.at(i + 1) == 0) {
            if (d.at(i + 2) == 1) {
                out.append({i + 3, 3});
                i += 3;
                continue;
            }
            if (i + 3 < n && d.at(i + 2) == 0 && d.at(i + 3) == 1) {
                out.append({i + 4, 4});
                i += 4;
                continue;
            }
        }
        ++i;
    }
    return out;
}

// RBSP: emulation prevention bytes (00 00 03) removed
QByteArray rbsp(const QByteArray &nal)
{
    QByteArray out;
    out.reserve(nal.size());
    int zeros = 0;
    for (char c : nal) {
        if (zeros >= 2 && c == 3) {
            zeros = 0;
            continue;
        }
        out.append(c);
        zeros = c == 0 ? zeros + 1 : 0;
    }
    return out;
}

class Bits
{
public:
    explicit Bits(const QByteArray &d) : m_d(d) {}
    bool ok() const { return m_pos <= m_d.size() * 8; }
    unsigned bit()
    {
        if (m_pos >= m_d.size() * 8) {
            m_pos = m_d.size() * 8 + 1;
            return 0;
        }
        const unsigned b = (quint8(m_d.at(m_pos / 8)) >> (7 - m_pos % 8)) & 1;
        ++m_pos;
        return b;
    }
    unsigned bits(int n)
    {
        unsigned v = 0;
        while (n--)
            v = (v << 1) | bit();
        return v;
    }
    unsigned ue()
    {
        int zeros = 0;
        while (bit() == 0 && ok() && zeros < 32)
            ++zeros;
        return zeros ? ((1u << zeros) - 1 + bits(zeros)) : 0;
    }
    int se()
    {
        const unsigned k = ue();
        return (k & 1) ? int((k + 1) / 2) : -int(k / 2);
    }

private:
    const QByteArray &m_d;
    int m_pos = 0;
};

int nalType(const QByteArray &nal) { return nal.isEmpty() ? -1 : quint8(nal.at(0)) & 0x1F; }
} // namespace

QList<QByteArray> nalUnits(const QByteArray &data)
{
    QList<QByteArray> out;
    const auto codes = startCodes(data);
    for (int i = 0; i < codes.size(); ++i) {
        const int begin = codes.at(i).first;
        int end = i + 1 < codes.size() ? codes.at(i + 1).first - codes.at(i + 1).second : data.size();
        while (end > begin && data.at(end - 1) == 0)   // trailing zeros belong to the next start code
            --end;
        if (end > begin)
            out.append(data.mid(begin, end - begin));
    }
    return out;
}

bool parseSps(const QByteArray &sps, QString *codec, int *width, int *height)
{
    if (nalType(sps) != 7 || sps.size() < 4)
        return false;
    const quint8 profile = quint8(sps.at(1)), constraints = quint8(sps.at(2)), level = quint8(sps.at(3));
    if (codec)
        *codec = QStringLiteral("avc1.%1%2%3")
                     .arg(profile, 2, 16, QLatin1Char('0'))
                     .arg(constraints, 2, 16, QLatin1Char('0'))
                     .arg(level, 2, 16, QLatin1Char('0'));
    const QByteArray body = rbsp(sps.mid(4));
    Bits b(body);
    b.ue();   // seq_parameter_set_id
    unsigned chroma = 1;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83
        || profile == 86 || profile == 118 || profile == 128 || profile == 138 || profile == 139 || profile == 134) {
        chroma = b.ue();
        if (chroma == 3)
            b.bit();   // separate_colour_plane_flag
        b.ue();        // bit_depth_luma_minus8
        b.ue();        // bit_depth_chroma_minus8
        b.bit();       // qpprime_y_zero_transform_bypass_flag
        if (b.bit()) { // seq_scaling_matrix_present_flag
            for (int i = 0; i < (chroma != 3 ? 8 : 12); ++i) {
                if (b.bit()) {   // a scaling list: skip it
                    int last = 8, next = 8;
                    for (int j = 0; j < (i < 6 ? 16 : 64); ++j) {
                        if (next != 0)
                            next = (last + b.se() + 256) % 256;
                        last = next == 0 ? last : next;
                    }
                }
            }
        }
    }
    b.ue();   // log2_max_frame_num_minus4
    const unsigned pocType = b.ue();
    if (pocType == 0) {
        b.ue();
    } else if (pocType == 1) {
        b.bit();
        b.se();
        b.se();
        const unsigned n = b.ue();
        for (unsigned i = 0; i < n && b.ok(); ++i)
            b.se();
    }
    b.ue();    // max_num_ref_frames
    b.bit();   // gaps_in_frame_num_value_allowed_flag
    const unsigned wMbs = b.ue() + 1, hMapUnits = b.ue() + 1;
    const unsigned frameMbsOnly = b.bit();
    if (!frameMbsOnly)
        b.bit();
    b.bit();   // direct_8x8_inference_flag
    unsigned cropL = 0, cropR = 0, cropT = 0, cropB = 0;
    if (b.bit()) {
        cropL = b.ue();
        cropR = b.ue();
        cropT = b.ue();
        cropB = b.ue();
    }
    if (!b.ok())
        return false;
    const unsigned subW = chroma == 1 || chroma == 2 ? 2 : 1, subH = chroma == 1 ? 2 : 1;
    const int w = int(wMbs * 16 - subW * (cropL + cropR));
    const int h = int((2 - frameMbsOnly) * hMapUnits * 16 - subH * (2 - frameMbsOnly) * (cropT + cropB));
    if (w <= 0 || h <= 0)
        return false;
    if (width)
        *width = w;
    if (height)
        *height = h;
    return true;
}

QList<QByteArray> accessUnits(const QByteArray &stream)
{
    static const QByteArray kStart("\x00\x00\x00\x01", 4);
    QList<QByteArray> out;
    QByteArray current;
    bool hasSlice = false;
    for (const QByteArray &nal : nalUnits(stream)) {
        const int type = nalType(nal);
        // A new picture: an AUD, parameter sets or SEI after a slice, or a
        // slice whose first_mb_in_slice is 0 after a slice
        bool starts = false;
        if (type == 9 || ((type == 7 || type == 8 || type == 6) && hasSlice)) {
            starts = true;
        } else if ((type == 1 || type == 5) && hasSlice && nal.size() > 1) {
            starts = (quint8(nal.at(1)) & 0x80) != 0;   // ue(first_mb_in_slice) == 0 starts with a 1 bit
        }
        if (starts && !current.isEmpty()) {
            out.append(current);
            current.clear();
            hasSlice = false;
        }
        current += kStart + nal;
        if (type == 1 || type == 5)
            hasSlice = true;
    }
    if (!current.isEmpty())
        out.append(current);
    return out;
}

} // namespace H264

// ── Hub ────────────────────────────────────────────────────────────────

StreamHub &StreamHub::instance()
{
    static StreamHub hub;
    return hub;
}

void StreamHub::declare(const QString &name, std::function<void()> requestKey)
{
    QMutexLocker l(&m_lock);
    auto &s = m_streams[name];
    if (!s)
        s = std::make_shared<Stream>();
    s->requestKey = std::move(requestKey);
    s->jpeg = name.endsWith(QLatin1String(".frames"));
    if (s->jpeg)
        s->info = {{QStringLiteral("format"), QStringLiteral("jpeg")}};
}

bool StreamHub::wanted(const QString &name) const
{
    QMutexLocker l(&m_lock);
    const auto it = m_streams.constFind(name);
    return it != m_streams.constEnd() && it.value()->count.load() > 0;
}

QByteArray StreamHub::record(quint8 streamId, quint8 flags, qint64 ptsUs, const QByteArray &payload)
{
    QByteArray r(12, '\0');
    r[0] = 1;
    r[1] = char(streamId);
    r[2] = char(flags);
    qToBigEndian<quint64>(quint64(ptsUs), r.data() + 4);
    return r + payload;
}

void StreamHub::pushEvent(const std::shared_ptr<Connection> &c, const QVariantMap &event)
{
    c->push(QJsonDocument(QJsonObject::fromVariantMap(event)).toJson(QJsonDocument::Compact) + '\n');
}

void StreamHub::maybeRequestKey(Stream &s)
{
    // One request a second across every stream and subscriber, so joiners
    // can't make the phone re-key constantly
    if (!s.requestKey)
        return;
    if (m_lastKeyRequest.isValid() && m_lastKeyRequest.elapsed() < kKeyRequestIntervalMs)
        return;
    m_lastKeyRequest.start();
    s.requestKey();
}

void StreamHub::deliver(Stream &s, quint8 flags, qint64 ptsUs, const QByteArray &payload, bool key)
{
    bool needKey = false;
    for (int i = s.subscribers.size() - 1; i >= 0; --i) {
        Subscriber &sub = s.subscribers[i];
        const std::shared_ptr<Connection> c = sub.connection.lock();
        if (!c || c->isClosed()) {
            s.subscribers.removeAt(i);
            s.count.store(int(s.subscribers.size()));
            continue;
        }
        if (sub.waitingForKey && !key) {
            needKey = true;
            continue;
        }
        const quint8 f = flags | (sub.waitingForKey ? Discontinuity : 0);
        if (c->pushRecord(record(quint8(sub.id), f, ptsUs, payload))) {
            sub.waitingForKey = false;
            continue;
        }
        // Fell behind: everything queued is gone; start again at a keyframe,
        // this one if it is one (the queue is empty now)
        ++sub.dropped;
        pushEvent(c, {{QStringLiteral("event"), QStringLiteral("streamDropped")},
                      {QStringLiteral("stream"), sub.id},
                      {QStringLiteral("count"), sub.dropped}});
        if (key && c->pushRecord(record(quint8(sub.id), flags | Discontinuity, ptsUs, payload))) {
            sub.waitingForKey = false;
            continue;
        }
        sub.waitingForKey = true;
        needKey = true;
    }
    if (needKey)
        maybeRequestKey(s);
}

void StreamHub::publishH264(const QString &name, const QByteArray &annexB, qint64 ptsUs, bool key, bool config)
{
    QMutexLocker l(&m_lock);
    const auto it = m_streams.constFind(name);
    if (it == m_streams.constEnd())
        return;
    Stream &s = *it.value();
    if (config) {
        if (annexB == s.config)
            return;
        s.config = annexB;
        QString codec;
        int w = 0, h = 0;
        for (const QByteArray &nal : H264::nalUnits(annexB))
            if (H264::parseSps(nal, &codec, &w, &h))
                break;
        s.info = {{QStringLiteral("format"), QStringLiteral("annexb")},
                  {QStringLiteral("codec"), codec},
                  {QStringLiteral("width"), w},
                  {QStringLiteral("height"), h}};
        for (const Subscriber &sub : std::as_const(s.subscribers)) {
            if (const auto c = sub.connection.lock()) {
                QVariantMap e = s.info;
                e.insert(QStringLiteral("event"), QStringLiteral("streamConfig"));
                e.insert(QStringLiteral("stream"), sub.id);
                pushEvent(c, e);
            }
        }
        return;
    }
    if (s.count.load() == 0)
        return;
    // Every keyframe carries the parameter sets, so any keyframe can start
    // a decoder
    QByteArray payload = annexB;
    quint8 flags = key ? Key : 0;
    if (key && !s.config.isEmpty()) {
        const QList<QByteArray> nals = H264::nalUnits(annexB);
        const bool hasSps = !nals.isEmpty() && (quint8(nals.first().at(0)) & 0x1F) == 7;
        if (!hasSps)
            payload.prepend(s.config);
        flags |= Config;
    }
    deliver(s, flags, ptsUs, payload, key);
}

void StreamHub::publishJpeg(const QString &name, const QByteArray &jpeg, qint64 ptsUs)
{
    QMutexLocker l(&m_lock);
    const auto it = m_streams.constFind(name);
    if (it == m_streams.constEnd() || it.value()->count.load() == 0)
        return;
    deliver(*it.value(), Key | Jpeg, ptsUs, jpeg, true);
}

int StreamHub::subscribe(const QString &name, const std::shared_ptr<Connection> &connection)
{
    QMutexLocker l(&m_lock);
    const auto it = m_streams.constFind(name);
    if (it == m_streams.constEnd() || !connection)
        return 0;
    int &next = m_nextId[connection.get()];
    if (next >= 255)
        return 0;
    const int id = ++next;
    Stream &s = *it.value();
    s.subscribers.append(Subscriber{connection, connection.get(), id, true, 0});
    s.count.store(int(s.subscribers.size()));
    maybeRequestKey(s);
    return id;
}

void StreamHub::unsubscribe(const std::shared_ptr<Connection> &connection, int streamId)
{
    QMutexLocker l(&m_lock);
    for (auto &s : m_streams) {
        s->subscribers.removeIf([&](const Subscriber &sub) { return sub.key == connection.get() && sub.id == streamId; });
        s->count.store(int(s->subscribers.size()));
    }
}

void StreamHub::unsubscribeAll(const Connection *connection)
{
    QMutexLocker l(&m_lock);
    for (auto &s : m_streams) {
        s->subscribers.removeIf([&](const Subscriber &sub) { return sub.key == connection; });
        s->count.store(int(s->subscribers.size()));
    }
    m_nextId.remove(connection);
}

QVariantMap StreamHub::info(const QString &name) const
{
    QMutexLocker l(&m_lock);
    const auto it = m_streams.constFind(name);
    return it == m_streams.constEnd() ? QVariantMap() : it.value()->info;
}

QStringList StreamHub::names() const
{
    QMutexLocker l(&m_lock);
    return m_streams.keys();
}

} // namespace OctaveService
