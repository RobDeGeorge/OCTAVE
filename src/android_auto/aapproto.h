#ifndef AAPPROTO_H
#define AAPPROTO_H

// The handful of Android Auto Protocol messages OCTAVE sends and reads,
// hand-encoded in the protobuf wire format (no libprotobuf). Field numbers
// come from aasdk's .proto files and are listed with every message in
// docs/ANDROID_AUTO_PROTOCOL.md.

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

namespace Aap {

// ─── wire format ─────────────────────────────────────────────────────

class ProtoWriter
{
public:
    ProtoWriter &varint(int field, quint64 value);
    ProtoWriter &int32(int field, qint32 value);       // negative → 10-byte varint, as protobuf does
    ProtoWriter &sint32(int field, qint32 value);      // zigzag
    ProtoWriter &boolean(int field, bool value) { return varint(field, value ? 1 : 0); }
    ProtoWriter &bytes(int field, const QByteArray &value);
    ProtoWriter &string(int field, const QString &value) { return bytes(field, value.toUtf8()); }
    ProtoWriter &message(int field, const ProtoWriter &sub) { return bytes(field, sub.data()); }
    const QByteArray &data() const { return m_data; }

private:
    void key(int field, int wireType);
    void rawVarint(quint64 v);
    QByteArray m_data;
};

// Reads the fields of one message, in order. Unknown wire types stop it.
class ProtoReader
{
public:
    explicit ProtoReader(const QByteArray &data) : m_data(data) {}

    struct Field {
        int number = 0;
        int wireType = 0;
        quint64 varint = 0;     // wire type 0 (and fixed32 / fixed64 values)
        QByteArray bytes;       // wire type 2
    };
    // false at the end or on malformed input (ok() tells which)
    bool next(Field &out);
    bool ok() const { return m_ok; }

    // First occurrence of a field, scanning the whole message
    static std::optional<quint64> varintField(const QByteArray &msg, int number);
    static std::optional<QByteArray> bytesField(const QByteArray &msg, int number);
    static QList<QByteArray> repeatedBytes(const QByteArray &msg, int number);

private:
    bool readVarint(quint64 &v);
    QByteArray m_data;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

// ─── message ids (docs/ANDROID_AUTO_PROTOCOL.md) ──────────────────────

namespace Msg {
// control channel
constexpr quint16 VersionRequest = 1, VersionResponse = 2, EncapsulatedSsl = 3, AuthComplete = 4;
constexpr quint16 ServiceDiscoveryRequest = 5, ServiceDiscoveryResponse = 6;
constexpr quint16 ChannelOpenRequest = 7, ChannelOpenResponse = 8;
constexpr quint16 PingRequest = 11, PingResponse = 12;
constexpr quint16 NavFocusRequest = 13, NavFocusNotification = 14;
constexpr quint16 ByeByeRequest = 15, ByeByeResponse = 16;
constexpr quint16 VoiceSessionNotification = 17;
constexpr quint16 AudioFocusRequest = 18, AudioFocusNotification = 19;
// media sink / source channels
constexpr quint16 MediaData = 0x0000, MediaCodecConfig = 0x0001;
constexpr quint16 MediaSetup = 0x8000, MediaStart = 0x8001, MediaStop = 0x8002;
constexpr quint16 MediaConfig = 0x8003, MediaAck = 0x8004;
constexpr quint16 MicrophoneRequest = 0x8005, MicrophoneResponse = 0x8006;
constexpr quint16 VideoFocusRequest = 0x8007, VideoFocusNotification = 0x8008;
// sensor channel
constexpr quint16 SensorRequest = 0x8001, SensorResponse = 0x8002, SensorBatch = 0x8003;
// input channel
constexpr quint16 InputReport = 0x8001, KeyBindingRequest = 0x8002, KeyBindingResponse = 0x8003;
}

// What a channel carries; the channel ids OCTAVE announces (aasdk's numbering)
namespace Channel {
constexpr quint8 Control = 0, Sensor = 1, Video = 3;
constexpr quint8 MediaAudio = 4, GuidanceAudio = 5, SystemAudio = 6;
constexpr quint8 Input = 8, Microphone = 9;
}

enum class AudioStream { Media, Guidance, System };

struct AudioFormat {
    int sampleRate;
    int channels;
};
AudioFormat audioFormat(AudioStream stream);
std::optional<AudioStream> audioStreamForChannel(quint8 channel);

// ─── what OCTAVE sends ───────────────────────────────────────────────

struct HeadUnitConfig {
    int width = 1280;           // 800x480, 1280x720 or 1920x1080
    int height = 720;
    int fps = 30;               // 30 or 60
    int dpi = 160;
    QString displayName = QStringLiteral("OCTAVE");
    QString softwareVersion;
};
// The resolution code Android Auto knows, or 0 for an unsupported size
int videoResolutionCode(int width, int height);

QByteArray versionRequest(quint16 major, quint16 minor);   // raw, not protobuf
QByteArray authComplete(int status = 0);
QByteArray serviceDiscoveryResponse(const HeadUnitConfig &config);
QByteArray channelOpenResponse(int status = 0);
QByteArray mediaConfig(int maxUnacked = 1);
QByteArray mediaAck(int sessionId);
QByteArray videoFocusNotification(bool projected = true, bool unsolicited = false);
QByteArray audioFocusNotification(int focusState, bool unsolicited = false);
QByteArray navFocusNotification(int focusType = 2);
QByteArray sensorResponse(int status = 0);
QByteArray sensorBatchDrivingStatus(int status);
QByteArray sensorBatchNightMode(bool night);
QByteArray keyBindingResponse(int status = 0);
QByteArray microphoneResponse(int status, int sessionId);
QByteArray pingRequest(qint64 timestampUs);
QByteArray pingResponse(qint64 timestampUs);
QByteArray byeByeRequest(int reason = 1);
QByteArray byeByeResponse();

struct TouchPointer {
    int x = 0;          // in video pixels
    int y = 0;
    int id = 0;
};
enum class TouchAction { Down = 0, Up = 1, Moved = 2, PointerDown = 5, PointerUp = 6 };
QByteArray touchInputReport(qint64 timestampUs, const QList<TouchPointer> &pointers,
                            int actionIndex, TouchAction action);

// The audio focus state answering a request type (RELEASE → LOSS, else GAIN)
int audioFocusStateFor(int requestType);

} // namespace Aap

Q_DECLARE_METATYPE(Aap::AudioStream)

#endif // AAPPROTO_H
