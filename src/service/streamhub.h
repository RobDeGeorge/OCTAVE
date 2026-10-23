// streamhub.h
//
// Media streams for service clients (TODO/open-backend.md, contract step 6):
// the phone mirror's H.264 as the phone sends it (no re-encode) and Android
// Auto's frames as JPEG, to any client that asks with the `stream` op.
// Producers publish from any thread; with no subscriber a publish costs one
// atomic load. Each subscriber's records go into its connection's queue
// (Connection::pushRecord) and out on that connection's thread.
//
// Record (one WebSocket binary frame; on TCP prefixed by 0xFF + u32 big-
// endian length):
//   u8 version (1) | u8 stream id | u8 flags | u8 reserved | u64 pts (us,
//   big-endian) | payload
//   flags: 1 key, 2 config (SPS/PPS inline before the IDR), 4 jpeg,
//          8 discontinuity (first record after a join or a drop)
// H.264 payloads are Annex-B access units; every keyframe record carries
// the current SPS/PPS first, so a decoder can start at any keyframe
// (WebCodecs: VideoDecoder.configure({codec}) with no description = Annex-B).
//
// Joining and falling behind: a subscriber receives nothing until the next
// keyframe; the stream's keyframe requester (the phone's RESET_VIDEO) is
// asked at most once a second across all subscribers. A subscriber whose
// connection queue overflows loses every queued record and waits for the
// next keyframe again ({"event": "streamDropped"}).
//
// Security: a read, under the same Origin / Sec-Fetch rules as every op;
// when pairing lands (TODO/open-backend.md, item 11) `stream` must require an
// authorised client like the others, because the phone screen is
// sensitive.

#ifndef OCTAVE_SERVICE_STREAMHUB_H
#define OCTAVE_SERVICE_STREAMHUB_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

namespace OctaveService {

class Connection;

namespace H264 {
// Annex-B: the NAL units in `data` (start codes stripped)
QList<QByteArray> nalUnits(const QByteArray &data);
// From an SPS NAL unit (with its header byte): the WebCodecs / RFC 6381
// codec string ("avc1.42c01e") and the picture size; false if unreadable
bool parseSps(const QByteArray &sps, QString *codec, int *width, int *height);
// Annex-B access units of a raw .h264 stream (a new one at each AUD or
// first slice of a picture; SPS / PPS / SEI start one too)
QList<QByteArray> accessUnits(const QByteArray &stream);
} // namespace H264

class StreamHub
{
public:
    enum Flag : quint8 { Key = 1, Config = 2, Jpeg = 4, Discontinuity = 8 };
    static constexpr int kKeyRequestIntervalMs = 1000;

    static StreamHub &instance();

    // A producer's stream ("phoneMirrorManager.video"); `requestKey` asks
    // the source for a keyframe (may be empty), called from any thread
    void declare(const QString &name, std::function<void()> requestKey = {});
    bool wanted(const QString &name) const;   // any subscriber at all

    // H.264: `annexB` one access unit or a config (SPS/PPS) packet
    void publishH264(const QString &name, const QByteArray &annexB, qint64 ptsUs, bool key, bool config);
    // A complete image as JPEG (Android Auto); every one is a keyframe
    void publishJpeg(const QString &name, const QByteArray &jpeg, qint64 ptsUs);

    // A subscriber: returns its record stream id (1-255), 0 if `name` is
    // not a stream. Its first record waits for a keyframe.
    int subscribe(const QString &name, const std::shared_ptr<Connection> &connection);
    void unsubscribe(const std::shared_ptr<Connection> &connection, int streamId);
    void unsubscribeAll(quint64 connectionSerial);   // Connection::serial()
    // {codec, width, height, format} as known now ({format: "jpeg"} for JPEG)
    QVariantMap info(const QString &name) const;
    QStringList names() const;

    static QByteArray record(quint8 streamId, quint8 flags, qint64 ptsUs, const QByteArray &payload);

private:
    struct Subscriber {
        std::weak_ptr<Connection> connection;
        quint64 key = 0;   // Connection::serial()
        int id = 0;
        bool waitingForKey = true;
        int dropped = 0;
    };
    struct Stream {
        std::function<void()> requestKey;
        QList<Subscriber> subscribers;
        QByteArray config;      // the latest SPS/PPS (Annex-B)
        QVariantMap info;
        bool jpeg = false;
        std::atomic<int> count{0};
    };

    void deliver(Stream &s, quint8 flags, qint64 ptsUs, const QByteArray &payload, bool key);
    void maybeRequestKey(Stream &s);
    void pushEvent(const std::shared_ptr<Connection> &c, const QVariantMap &event);

    mutable QMutex m_lock;
    QHash<QString, std::shared_ptr<Stream>> m_streams;
    QHash<quint64, int> m_nextId;   // by Connection::serial()
    QElapsedTimer m_lastKeyRequest;
};

} // namespace OctaveService

#endif // OCTAVE_SERVICE_STREAMHUB_H
