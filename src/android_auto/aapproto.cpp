#include "aapproto.h"

#include <QtEndian>

namespace Aap {

// ─── ProtoWriter ─────────────────────────────────────────────────────

void ProtoWriter::rawVarint(quint64 v)
{
    while (v >= 0x80) {
        m_data.append(char((v & 0x7F) | 0x80));
        v >>= 7;
    }
    m_data.append(char(v));
}

void ProtoWriter::key(int field, int wireType)
{
    rawVarint((quint64(field) << 3) | quint64(wireType));
}

ProtoWriter &ProtoWriter::varint(int field, quint64 value)
{
    key(field, 0);
    rawVarint(value);
    return *this;
}

ProtoWriter &ProtoWriter::int32(int field, qint32 value)
{
    // Sign-extended to 64 bits, as every protobuf implementation encodes int32
    return varint(field, quint64(qint64(value)));
}

ProtoWriter &ProtoWriter::sint32(int field, qint32 value)
{
    return varint(field, quint64((quint32(value) << 1) ^ quint32(value >> 31)));
}

ProtoWriter &ProtoWriter::bytes(int field, const QByteArray &value)
{
    key(field, 2);
    rawVarint(quint64(value.size()));
    m_data.append(value);
    return *this;
}

// ─── ProtoReader ─────────────────────────────────────────────────────

bool ProtoReader::readVarint(quint64 &v)
{
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (m_pos >= m_data.size())
            return false;
        const quint8 b = quint8(m_data[m_pos++]);
        v |= quint64(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;
}

bool ProtoReader::next(Field &out)
{
    if (!m_ok || m_pos >= m_data.size())
        return false;
    quint64 k = 0;
    if (!readVarint(k)) {
        m_ok = false;
        return false;
    }
    out = Field();
    out.number = int(k >> 3);
    out.wireType = int(k & 7);
    switch (out.wireType) {
    case 0:
        if (!readVarint(out.varint))
            m_ok = false;
        break;
    case 1:
    case 5: {
        const int n = out.wireType == 1 ? 8 : 4;
        if (m_pos + n > m_data.size()) {
            m_ok = false;
            break;
        }
        out.varint = n == 8 ? qFromLittleEndian<quint64>(m_data.constData() + m_pos)
                            : qFromLittleEndian<quint32>(m_data.constData() + m_pos);
        m_pos += n;
        break;
    }
    case 2: {
        quint64 len = 0;
        if (!readVarint(len) || len > quint64(m_data.size() - m_pos)) {
            m_ok = false;
            break;
        }
        out.bytes = m_data.mid(m_pos, qsizetype(len));
        m_pos += qsizetype(len);
        break;
    }
    default:
        m_ok = false;
    }
    return m_ok;
}

std::optional<quint64> ProtoReader::varintField(const QByteArray &msg, int number)
{
    ProtoReader r(msg);
    Field f;
    while (r.next(f)) {
        if (f.number == number && f.wireType != 2)
            return f.varint;
    }
    return std::nullopt;
}

std::optional<QByteArray> ProtoReader::bytesField(const QByteArray &msg, int number)
{
    ProtoReader r(msg);
    Field f;
    while (r.next(f)) {
        if (f.number == number && f.wireType == 2)
            return f.bytes;
    }
    return std::nullopt;
}

QList<QByteArray> ProtoReader::repeatedBytes(const QByteArray &msg, int number)
{
    QList<QByteArray> out;
    ProtoReader r(msg);
    Field f;
    while (r.next(f)) {
        if (f.number == number && f.wireType == 2)
            out.append(f.bytes);
    }
    return out;
}

// ─── channels ────────────────────────────────────────────────────────

AudioFormat audioFormat(AudioStream stream)
{
    // Media in CD-like stereo; guidance and system sounds as Android Auto
    // falls back to anyway ("Failed to find 48kHz guidance config")
    return stream == AudioStream::Media ? AudioFormat{48000, 2} : AudioFormat{16000, 1};
}

std::optional<AudioStream> audioStreamForChannel(quint8 channel)
{
    switch (channel) {
    case Channel::MediaAudio: return AudioStream::Media;
    case Channel::GuidanceAudio: return AudioStream::Guidance;
    case Channel::SystemAudio: return AudioStream::System;
    default: return std::nullopt;
    }
}

int videoResolutionCode(int width, int height)
{
    if (width == 800 && height == 480) return 1;
    if (width == 1280 && height == 720) return 2;
    if (width == 1920 && height == 1080) return 3;
    return 0;
}

// ─── messages ────────────────────────────────────────────────────────

QByteArray versionRequest(quint16 major, quint16 minor)
{
    QByteArray b(4, '\0');
    qToBigEndian(major, b.data());
    qToBigEndian(minor, b.data() + 2);
    return b;
}

QByteArray authComplete(int status)
{
    return ProtoWriter().int32(1, status).data();
}

static ProtoWriter audioConfig(AudioStream stream)
{
    const AudioFormat f = audioFormat(stream);
    return ProtoWriter().varint(1, quint64(f.sampleRate)).varint(2, 16).varint(3, quint64(f.channels));
}

QByteArray serviceDiscoveryResponse(const HeadUnitConfig &c)
{
    ProtoWriter r;

    // Sensors: driving status (so the keyboard isn't locked) and night mode
    ProtoWriter sensors;
    sensors.message(1, ProtoWriter().varint(1, 13));   // SENSOR_DRIVING_STATUS_DATA
    sensors.message(1, ProtoWriter().varint(1, 10));   // SENSOR_NIGHT_MODE
    r.message(1, ProtoWriter().int32(1, Channel::Sensor).message(2, sensors));

    // Video: H.264 baseline at one resolution
    ProtoWriter video;
    video.varint(1, 3);                                 // MEDIA_CODEC_VIDEO_H264_BP
    video.message(4, ProtoWriter()
                         .varint(1, quint64(videoResolutionCode(c.width, c.height)))
                         .varint(2, c.fps >= 60 ? 1 : 2)    // VIDEO_FPS_60 / VIDEO_FPS_30
                         .varint(3, 0)                      // width margin
                         .varint(4, 0)                      // height margin
                         .varint(5, quint64(c.dpi)));
    video.boolean(5, true);                             // available while in a call
    r.message(1, ProtoWriter().int32(1, Channel::Video).message(3, video));

    // Audio sinks: Android Auto refuses a head unit without one ("No audio
    // playback service")
    struct Sink { quint8 channel; AudioStream stream; int type; };
    for (const Sink &s : {Sink{Channel::MediaAudio, AudioStream::Media, 3},          // AUDIO_STREAM_MEDIA
                          Sink{Channel::GuidanceAudio, AudioStream::Guidance, 1},    // AUDIO_STREAM_GUIDANCE
                          Sink{Channel::SystemAudio, AudioStream::System, 2}}) {     // AUDIO_STREAM_SYSTEM_AUDIO
        ProtoWriter sink;
        sink.varint(1, 1);                              // MEDIA_CODEC_AUDIO_PCM
        sink.varint(2, quint64(s.type));
        sink.message(3, audioConfig(s.stream));
        r.message(1, ProtoWriter().int32(1, s.channel).message(3, sink));
    }

    // Touchscreen, in video pixels
    ProtoWriter input;
    input.message(2, ProtoWriter().int32(1, c.width).int32(2, c.height));
    r.message(1, ProtoWriter().int32(1, Channel::Input).message(4, input));

    // Microphone: also required ("No audio/mic")
    ProtoWriter mic;
    mic.varint(1, 1);                                   // PCM
    mic.message(2, ProtoWriter().varint(1, 16000).varint(2, 16).varint(3, 1));
    r.message(1, ProtoWriter().int32(1, Channel::Microphone).message(5, mic));

    const QString version = c.softwareVersion.isEmpty() ? QStringLiteral("0") : c.softwareVersion;
    // Deprecated copies of the head unit info, still filled by aasdk-based units
    r.string(2, QStringLiteral("OCTAVE")).string(3, QStringLiteral("OCTAVE"))
        .string(4, QStringLiteral("2026")).string(5, QStringLiteral("octave"));
    r.int32(6, 0);                                      // DRIVER_POSITION_LEFT
    r.string(7, QStringLiteral("OCTAVE")).string(8, QStringLiteral("OCTAVE head unit"))
        .string(9, QStringLiteral("octave")).string(10, version);
    r.boolean(11, false);
    r.int32(13, 0);                                     // session configuration
    r.string(14, c.displayName);
    r.message(17, ProtoWriter()
                      .string(1, QStringLiteral("OCTAVE")).string(2, QStringLiteral("OCTAVE"))
                      .string(3, QStringLiteral("2026")).string(4, QStringLiteral("octave"))
                      .string(5, QStringLiteral("OCTAVE")).string(6, QStringLiteral("OCTAVE head unit"))
                      .string(7, QStringLiteral("octave")).string(8, version));
    return r.data();
}

QByteArray channelOpenResponse(int status)
{
    return ProtoWriter().int32(1, status).data();
}

QByteArray mediaConfig(int maxUnacked)
{
    return ProtoWriter()
        .varint(1, 2)                                   // STATUS_READY
        .varint(2, quint64(maxUnacked))
        .varint(3, 0)                                   // configuration index 0
        .data();
}

QByteArray mediaAck(int sessionId)
{
    return ProtoWriter().int32(1, sessionId).varint(2, 1).data();
}

QByteArray videoFocusNotification(bool projected, bool unsolicited)
{
    return ProtoWriter().varint(1, projected ? 1 : 2).boolean(2, unsolicited).data();
}

QByteArray audioFocusNotification(int focusState, bool unsolicited)
{
    return ProtoWriter().int32(1, focusState).boolean(2, unsolicited).data();
}

QByteArray navFocusNotification(int focusType)
{
    return ProtoWriter().int32(1, focusType).data();
}

QByteArray sensorResponse(int status)
{
    return ProtoWriter().int32(1, status).data();
}

QByteArray sensorBatchDrivingStatus(int status)
{
    return ProtoWriter().message(13, ProtoWriter().int32(1, status)).data();
}

QByteArray sensorBatchNightMode(bool night)
{
    return ProtoWriter().message(10, ProtoWriter().boolean(1, night)).data();
}

QByteArray keyBindingResponse(int status)
{
    return ProtoWriter().int32(1, status).data();
}

QByteArray microphoneResponse(int status, int sessionId)
{
    return ProtoWriter().int32(1, status).int32(2, sessionId).data();
}

QByteArray pingRequest(qint64 timestampUs)
{
    return ProtoWriter().varint(1, quint64(timestampUs)).data();
}

QByteArray pingResponse(qint64 timestampUs)
{
    return ProtoWriter().varint(1, quint64(timestampUs)).data();
}

QByteArray byeByeRequest(int reason)
{
    return ProtoWriter().int32(1, reason).data();
}

QByteArray byeByeResponse()
{
    return QByteArray();
}

QByteArray touchInputReport(qint64 timestampNs, const QList<TouchPointer> &pointers,
                            int actionIndex, TouchAction action)
{
    ProtoWriter touch;
    for (const TouchPointer &p : pointers) {
        touch.message(1, ProtoWriter()
                             .varint(1, quint64(qMax(0, p.x)))
                             .varint(2, quint64(qMax(0, p.y)))
                             .varint(3, quint64(qMax(0, p.id))));
    }
    touch.varint(2, quint64(qMax(0, actionIndex)));
    touch.varint(3, quint64(int(action)));
    return ProtoWriter().varint(1, quint64(timestampNs)).message(3, touch).data();
}

int audioFocusStateFor(int requestType)
{
    return requestType == 4 ? 3 : 1;   // AUDIO_FOCUS_RELEASE → LOSS, else GAIN
}

} // namespace Aap
