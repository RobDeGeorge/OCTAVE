#include "airplayreceiver.h"

#include "../util/audiooutput.h"
#include "../util/h264decoder.h"

#include <QAudioSink>
#include <QLoggingCategory>
#include <QVideoSink>
#include <QtEndian>

#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include "dnssd.h"
#include "logger.h"
#include "raop.h"
}

Q_LOGGING_CATEGORY(lcAirPlay, "octave.airplay")

namespace {
constexpr int kAudioQueueMaxMs = 500;       // older audio is dropped beyond this
constexpr int kAudioDeviceBufferMs = 200;
constexpr int kSampleRate = 44100;          // every AirPlay stream format UxPlay takes is 44.1 kHz stereo

// Stream formats (UxPlay's "ct"): 2 ALAC (music), 4 AAC-LC, 8 AAC-ELD (mirroring)
constexpr int kCtAlac = 2;
constexpr int kCtAacLc = 4;
constexpr int kCtAacEld = 8;

// The decoder configurations UxPlay's renderer hands GStreamer: the ALAC
// magic cookie (352 samples a frame, 16 bit, 2 ch, 44100 Hz) and the
// MPEG-4 AudioSpecificConfigs for AAC-LC and AAC-ELD
const unsigned char kAlacCookie[] = {
    0x00, 0x00, 0x00, 0x24, 'a', 'l', 'a', 'c', 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x60, 0x00, 0x10, 0x28, 0x0a, 0x0e, 0x02, 0x00, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xac, 0x44};
const unsigned char kAacLcConfig[] = {0x12, 0x10};
const unsigned char kAacEldConfig[] = {0xf8, 0xe8, 0x50, 0x00};

const char *formatName(int ct)
{
    switch (ct) {
    case kCtAlac: return "ALAC";
    case kCtAacLc: return "AAC-LC";
    case kCtAacEld: return "AAC-ELD";
    }
    return "unknown";
}

inline qint16 toS16(float f)
{
    return qint16(qBound(-32768, int(std::lrint(f * 32767.0f)), 32767));
}
}

// ─── audio decoder (libavcodec) ──────────────────────────────────────

class AirPlayAudioDecoder
{
public:
    ~AirPlayAudioDecoder() { close(); }

    bool open(int ct)
    {
        close();
        const unsigned char *config = nullptr;
        int configLen = 0;
        AVCodecID id = AV_CODEC_ID_NONE;
        switch (ct) {
        case kCtAlac: id = AV_CODEC_ID_ALAC; config = kAlacCookie; configLen = sizeof(kAlacCookie); break;
        case kCtAacLc: id = AV_CODEC_ID_AAC; config = kAacLcConfig; configLen = sizeof(kAacLcConfig); break;
        case kCtAacEld: id = AV_CODEC_ID_AAC; config = kAacEldConfig; configLen = sizeof(kAacEldConfig); break;
        default: return false;
        }
        const AVCodec *codec = avcodec_find_decoder(id);
        if (!codec) {
            qCWarning(lcAirPlay) << "this build's FFmpeg has no" << formatName(ct) << "decoder: AirPlay audio will not play";
            return false;
        }
        m_ctx = avcodec_alloc_context3(codec);
        m_frame = av_frame_alloc();
        m_pkt = av_packet_alloc();
        if (!m_ctx || !m_frame || !m_pkt) {
            close();
            return false;
        }
        m_ctx->sample_rate = kSampleRate;
        av_channel_layout_default(&m_ctx->ch_layout, 2);
        m_ctx->extradata = static_cast<uint8_t *>(av_mallocz(configLen + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!m_ctx->extradata) {
            close();
            return false;
        }
        std::memcpy(m_ctx->extradata, config, configLen);
        m_ctx->extradata_size = configLen;
        if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
            qCWarning(lcAirPlay) << "could not open the" << formatName(ct) << "decoder";
            close();
            return false;
        }
        m_ct = ct;
        return true;
    }

    void close()
    {
        if (m_ctx)
            avcodec_free_context(&m_ctx);
        if (m_frame)
            av_frame_free(&m_frame);
        if (m_pkt)
            av_packet_free(&m_pkt);
        m_ct = 0;
    }

    int format() const { return m_ct; }

    // One packet in, interleaved signed 16-bit stereo out (empty on error)
    QByteArray decode(const unsigned char *data, int length)
    {
        QByteArray out;
        if (!m_ctx || length <= 0)
            return out;
        if (av_new_packet(m_pkt, length) < 0)
            return out;
        std::memcpy(m_pkt->data, data, length);
        if (avcodec_send_packet(m_ctx, m_pkt) < 0) {
            av_packet_unref(m_pkt);
            return out;
        }
        av_packet_unref(m_pkt);
        while (avcodec_receive_frame(m_ctx, m_frame) == 0) {
            append(out);
            av_frame_unref(m_frame);
        }
        return out;
    }

private:
    void append(QByteArray &out) const
    {
        const int n = m_frame->nb_samples;
        const int channels = m_frame->ch_layout.nb_channels;
        if (n <= 0 || channels <= 0)
            return;
        const qsizetype start = out.size();
        out.resize(start + qsizetype(n) * 2 * sizeof(qint16));
        auto *dst = reinterpret_cast<qint16 *>(out.data() + start);
        // Mono goes to both sides; anything past two channels is dropped
        auto plane = [&](int c) { return c < channels ? c : 0; };
        switch (m_frame->format) {
        case AV_SAMPLE_FMT_FLTP:
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                    dst[2 * i + c] = toS16(reinterpret_cast<const float *>(m_frame->extended_data[plane(c)])[i]);
            break;
        case AV_SAMPLE_FMT_FLT:
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                    dst[2 * i + c] = toS16(reinterpret_cast<const float *>(m_frame->data[0])[i * channels + plane(c)]);
            break;
        case AV_SAMPLE_FMT_S16P:
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                    dst[2 * i + c] = reinterpret_cast<const qint16 *>(m_frame->extended_data[plane(c)])[i];
            break;
        case AV_SAMPLE_FMT_S16:
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                    dst[2 * i + c] = reinterpret_cast<const qint16 *>(m_frame->data[0])[i * channels + plane(c)];
            break;
        case AV_SAMPLE_FMT_S32P:
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                    dst[2 * i + c] = qint16(reinterpret_cast<const qint32 *>(m_frame->extended_data[plane(c)])[i] >> 16);
            break;
        case AV_SAMPLE_FMT_S32:
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 2; ++c)
                    dst[2 * i + c] = qint16(reinterpret_cast<const qint32 *>(m_frame->data[0])[i * channels + plane(c)] >> 16);
            break;
        default:
            out.resize(start);      // a format no AirPlay decoder produces
            break;
        }
    }

    AVCodecContext *m_ctx = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_pkt = nullptr;
    int m_ct = 0;
};

// ─── UxPlay callbacks (its threads) ──────────────────────────────────

struct AirPlayCallbacks
{
    static AirPlayReceiver *self(void *cls) { return static_cast<AirPlayReceiver *>(cls); }

    static void log(void *, int level, const char *msg)
    {
        QByteArray text(msg);
        while (text.endsWith('\n'))
            text.chop(1);
        if (level <= LOGGER_ERR)
            qCWarning(lcAirPlay).noquote() << text;
        else if (level <= LOGGER_INFO)
            qCInfo(lcAirPlay).noquote() << text;
        else
            qCDebug(lcAirPlay).noquote() << text;
    }

    static void connInit(void *cls)
    {
        AirPlayReceiver *r = self(cls);
        const int open = ++r->m_connections;
        QMetaObject::invokeMethod(r, [r, open] { r->setConnections(open); }, Qt::QueuedConnection);
    }

    static void connDestroy(void *cls)
    {
        AirPlayReceiver *r = self(cls);
        const int open = qMax(0, --r->m_connections);
        QMetaObject::invokeMethod(r, [r, open] { r->setConnections(open); }, Qt::QueuedConnection);
    }

    static void connReset(void *cls, int reason)
    {
        AirPlayReceiver *r = self(cls);
        qCInfo(lcAirPlay) << "connection reset, reason" << reason;
        QMetaObject::invokeMethod(r, [r] { emit r->connectionLost(); }, Qt::QueuedConnection);
    }

    static void connFeedback(void *) {}

    static void reportClient(void *cls, char *deviceId, char *model, char *name, bool *admit)
    {
        AirPlayReceiver *r = self(cls);
        *admit = true;          // the PIN (when on) is the gate, not a list
        const QString n = QString::fromUtf8(name), m = QString::fromUtf8(model), d = QString::fromUtf8(deviceId);
        qCInfo(lcAirPlay) << "connection from" << n << m << d;
        QMetaObject::invokeMethod(r, [r, n, m, d] { emit r->clientConnecting(n, m, d); }, Qt::QueuedConnection);
    }

    static void displayPin(void *cls, char *pin)
    {
        AirPlayReceiver *r = self(cls);
        const QString p = QString::fromUtf8(pin);
        QMetaObject::invokeMethod(r, [r, p] { emit r->pinRequested(p); }, Qt::QueuedConnection);
    }

    static void registerClient(void *cls, const char *deviceId, const char *pk, const char *name)
    {
        AirPlayReceiver *r = self(cls);
        const QString key = QString::fromUtf8(pk), d = QString::fromUtf8(deviceId), n = QString::fromUtf8(name);
        {
            std::lock_guard<std::mutex> lock(r->m_keysMutex);
            r->m_registeredKeys.insert(key);
        }
        QMetaObject::invokeMethod(r, [r, key, d, n] {
            emit r->pinRequested(QString());
            emit r->clientRegistered(key, d, n);
        }, Qt::QueuedConnection);
    }

    static bool checkRegister(void *cls, const char *pk)
    {
        AirPlayReceiver *r = self(cls);
        return !r->m_requirePin || r->isRegistered(QString::fromUtf8(pk));
    }

    static const char *passwd(void *, int *len)
    {
        *len = 0;               // no password; the PIN is pin_pw 1
        return nullptr;
    }

    static void exportDacp(void *, const char *, const char *) {}

    static int videoCodec(void *cls, video_codec_t codec, unsigned short, unsigned short,
                          unsigned short, unsigned short, unsigned short, unsigned short)
    {
        if (codec != VIDEO_CODEC_H264) {
            qCWarning(lcAirPlay) << "the sender chose H.265, which is not offered";
            return -1;
        }
        Q_UNUSED(cls)
        return 0;
    }

    static void videoProcess(void *cls, raop_ntp_t *, video_decode_struct *data)
    {
        if (!data->is_h265)
            self(cls)->onVideo(data->data, data->data_len);
    }

    static void videoReset(void *cls, reset_type_t) { self(cls)->onVideoReset(); }
    static void videoFlush(void *) {}
    static void videoPause(void *) {}
    static void videoResume(void *) {}

    static void mirrorRunning(void *cls, bool running)
    {
        AirPlayReceiver *r = self(cls);
        qCInfo(lcAirPlay) << (running ? "screen mirroring starts" : "screen mirroring stops");
        if (!running)
            r->onVideoReset();
        QMetaObject::invokeMethod(r, [r, running] { emit r->mirroringChanged(running); }, Qt::QueuedConnection);
    }

    static void audioFormat(void *cls, unsigned char *ct, unsigned short *spf, bool *usingScreen, bool *isMedia, uint64_t *)
    {
        qCInfo(lcAirPlay) << "audio format" << formatName(*ct) << *spf << "samples a frame"
                          << (*usingScreen ? "(with the screen)" : "") << (*isMedia ? "(media)" : "");
        self(cls)->onAudioFormat(*ct);
    }

    static void audioProcess(void *cls, raop_ntp_t *, audio_decode_struct *data)
    {
        self(cls)->onAudio(data->data, data->data_len);
    }

    static void audioFlush(void *) {}

    static double clientVolume(void *) { return 0.0; }    // 0 dB: the sender keeps its own slider

    static void audioVolume(void *cls, float db)
    {
        AirPlayReceiver *r = self(cls);
        const double fraction = AirPlayReceiver::volumeFraction(db);
        QMetaObject::invokeMethod(r, [r, fraction] { emit r->senderVolumeChanged(fraction); }, Qt::QueuedConnection);
    }

    static void metadata(void *cls, const void *buffer, int length)
    {
        AirPlayReceiver *r = self(cls);
        QString title, artist, album;
        AirPlayReceiver::parseMetadata(QByteArray(static_cast<const char *>(buffer), length), &title, &artist, &album);
        QMetaObject::invokeMethod(r, [r, title, artist, album] { emit r->metadataChanged(title, artist, album); },
                                  Qt::QueuedConnection);
    }

    static void coverArt(void *cls, const void *buffer, int length)
    {
        AirPlayReceiver *r = self(cls);
        const QByteArray jpeg = buffer ? QByteArray(static_cast<const char *>(buffer), length) : QByteArray();
        QMetaObject::invokeMethod(r, [r, jpeg] { emit r->coverArtChanged(jpeg); }, Qt::QueuedConnection);
    }

    static void stopCoverArt(void *) {}
    static void progress(void *, uint32_t *, uint32_t *, uint32_t *) {}

    // AirPlay video (HLS URLs from apps): not offered (feature bits 0 and 4
    // off), but UxPlay calls these without checking, so they exist
    static void videoPlay(void *, const char *, const float) {}
    static void videoScrub(void *, const float) {}
    static void videoRate(void *, const float) {}
    static void videoStop(void *) {}
    static void playbackInfo(void *, playback_info_t *info) { std::memset(info, 0, sizeof(*info)); }
    static float playlistRemove(void *) { return 0.0f; }
    static void customProfile(void *, const char **profile) { *profile = nullptr; }
};

// ─── receiver ────────────────────────────────────────────────────────

AirPlayReceiver::AirPlayReceiver(QObject *parent)
    : QObject(parent)
{
    m_audioTimer.setInterval(20);
    connect(&m_audioTimer, &QTimer::timeout, this, &AirPlayReceiver::drainAudio);
}

AirPlayReceiver::~AirPlayReceiver()
{
    stop();
}

bool AirPlayReceiver::start(const Options &options, QString *error)
{
    if (m_raop)
        return true;
    auto fail = [error](const QString &why) {
        qCWarning(lcAirPlay).noquote() << "AirPlay receiver not started:" << why;
        if (error)
            *error = why;
        return false;
    };

    QByteArray hwAddr;
    for (const QString &part : options.deviceId.split(QLatin1Char(':'))) {
        bool ok = false;
        hwAddr.append(char(part.toUInt(&ok, 16)));
        if (!ok)
            return fail(QStringLiteral("bad device id %1").arg(options.deviceId));
    }
    if (hwAddr.size() != 6)
        return fail(QStringLiteral("bad device id %1").arg(options.deviceId));

    m_requirePin = options.requirePin;
    const QByteArray name = options.name.toUtf8();
    int dnssdError = 0;
    // pin_pw 1: a new sender types the PIN shown on screen once and is remembered
    m_dnssd = dnssd_init(name.constData(), name.size(), hwAddr.constData(), hwAddr.size(),
                         options.requirePin ? 1 : 0, &dnssdError);
    if (!m_dnssd || dnssdError)
        return fail(QStringLiteral("mDNS setup failed (error %1)").arg(dnssdError));

    // UxPlay's feature set for mirroring + audio with FairPlay; no AirPlay
    // video (HLS, bits 0 and 4) and no H.265 (bit 42). Bit 27: legacy pairing,
    // which is what the PIN uses.
    const int on[] = {1, 2, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 25, 28, 30};
    for (int bit = 0; bit < 64; ++bit)
        dnssd_set_airplay_features(m_dnssd, bit, 0);
    for (int bit : on)
        dnssd_set_airplay_features(m_dnssd, bit, 1);
    dnssd_set_airplay_features(m_dnssd, 27, options.requirePin ? 1 : 0);

    raop_callbacks_t cbs;
    std::memset(&cbs, 0, sizeof(cbs));
    cbs.cls = this;
    cbs.conn_init = AirPlayCallbacks::connInit;
    cbs.conn_destroy = AirPlayCallbacks::connDestroy;
    cbs.conn_reset = AirPlayCallbacks::connReset;
    cbs.conn_feedback = AirPlayCallbacks::connFeedback;
    cbs.audio_process = AirPlayCallbacks::audioProcess;
    cbs.video_process = AirPlayCallbacks::videoProcess;
    cbs.audio_flush = AirPlayCallbacks::audioFlush;
    cbs.video_flush = AirPlayCallbacks::videoFlush;
    cbs.video_pause = AirPlayCallbacks::videoPause;
    cbs.video_resume = AirPlayCallbacks::videoResume;
    cbs.video_reset = AirPlayCallbacks::videoReset;
    cbs.video_set_codec_and_size = AirPlayCallbacks::videoCodec;
    cbs.mirror_video_running = AirPlayCallbacks::mirrorRunning;
    cbs.audio_set_client_volume = AirPlayCallbacks::clientVolume;
    cbs.audio_set_volume = AirPlayCallbacks::audioVolume;
    cbs.audio_get_format = AirPlayCallbacks::audioFormat;
    cbs.audio_set_metadata = AirPlayCallbacks::metadata;
    cbs.audio_set_coverart = AirPlayCallbacks::coverArt;
    cbs.audio_stop_coverart_rendering = AirPlayCallbacks::stopCoverArt;
    cbs.audio_set_progress = AirPlayCallbacks::progress;
    cbs.report_client_request = AirPlayCallbacks::reportClient;
    cbs.display_pin = AirPlayCallbacks::displayPin;
    cbs.register_client = AirPlayCallbacks::registerClient;
    cbs.check_register = AirPlayCallbacks::checkRegister;
    cbs.passwd = AirPlayCallbacks::passwd;
    cbs.export_dacp = AirPlayCallbacks::exportDacp;
    cbs.on_video_play = AirPlayCallbacks::videoPlay;
    cbs.on_video_scrub = AirPlayCallbacks::videoScrub;
    cbs.on_video_rate = AirPlayCallbacks::videoRate;
    cbs.on_video_stop = AirPlayCallbacks::videoStop;
    cbs.on_video_acquire_playback_info = AirPlayCallbacks::playbackInfo;
    cbs.on_video_playlist_remove = AirPlayCallbacks::playlistRemove;
    cbs.get_custom_profile = AirPlayCallbacks::customProfile;

    m_raop = raop_init(&cbs);
    if (!m_raop) {
        stop();
        return fail(QStringLiteral("could not create the AirPlay server"));
    }
    raop_set_log_callback(m_raop, AirPlayCallbacks::log, nullptr);
    raop_set_log_level(m_raop, lcAirPlay().isDebugEnabled() ? LOGGER_DEBUG : LOGGER_INFO);
    const QByteArray deviceId = options.deviceId.toUtf8();
    const QByteArray keyFile = options.keyFile.toUtf8();
    if (raop_init2(m_raop, 1, deviceId.constData(), keyFile.constData())) {
        free(m_raop);           // what UxPlay does when the second step fails
        m_raop = nullptr;
        stop();
        return fail(QStringLiteral("could not set up the receiver's pairing key"));
    }
    raop_set_plist(m_raop, "width", options.width);
    raop_set_plist(m_raop, "height", options.height);
    raop_set_plist(m_raop, "refreshRate", options.fps);
    raop_set_plist(m_raop, "maxFPS", options.fps);
    unsigned short tcp[2] = {0, 0};
    unsigned short udp[3] = {0, 0, 0};
    raop_set_tcp_ports(m_raop, tcp);
    raop_set_udp_ports(m_raop, udp);

    unsigned short port = raop_get_port(m_raop);
    if (raop_start_httpd(m_raop, &port) < 0) {
        stop();
        return fail(QStringLiteral("could not open a listening port"));
    }
    raop_set_port(m_raop, port);
    raop_set_dnssd(m_raop, m_dnssd);
    m_port = port;
    if (dnssd_register_raop(m_dnssd, port) || dnssd_register_airplay(m_dnssd, port)) {
        stop();
        return fail(QStringLiteral("could not advertise on the local network (mDNS port 5353)"));
    }
    qCInfo(lcAirPlay) << "AirPlay receiver" << options.name << "on port" << port
                      << (options.requirePin ? "(PIN for new devices)" : "(no PIN)");
    return true;
}

void AirPlayReceiver::stop()
{
    if (m_dnssd) {
        dnssd_unregister_raop(m_dnssd);
        dnssd_unregister_airplay(m_dnssd);
    }
    if (m_raop) {
        raop_destroy(m_raop);   // stops the server and joins its threads
        m_raop = nullptr;
        qCInfo(lcAirPlay) << "AirPlay receiver stopped";
    }
    if (m_dnssd) {
        dnssd_destroy(m_dnssd);
        m_dnssd = nullptr;
    }
    m_port = 0;
    onVideoReset();
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioDecoder.reset();
    }
    closeAudio();
    m_connections = 0;
    setConnections(0);
}

void AirPlayReceiver::setVideoSink(QVideoSink *sink)
{
    m_sink = sink;
}

void AirPlayReceiver::setVolume(float linear)
{
    m_volume = qBound(0.0f, linear, 1.0f);
    if (m_audioSink)
        m_audioSink->setVolume(m_volume);
}

void AirPlayReceiver::setAudioOutputDevice(const QString &id)
{
    if (id == m_audioDeviceId)
        return;
    m_audioDeviceId = id;
    closeAudio();               // reopened on the new device with the next packet
}

void AirPlayReceiver::setRegisteredKeys(const QStringList &keys)
{
    std::lock_guard<std::mutex> lock(m_keysMutex);
    m_registeredKeys = QSet<QString>(keys.begin(), keys.end());
}

bool AirPlayReceiver::isRegistered(const QString &key) const
{
    std::lock_guard<std::mutex> lock(m_keysMutex);
    return m_registeredKeys.contains(key);
}

void AirPlayReceiver::parseMetadata(const QByteArray &dmap, QString *title, QString *artist, QString *album)
{
    // An "mlit" listing item holding tag (4 chars) + big-endian length + value records
    auto header = [](const QByteArray &b, qsizetype at, QByteArray *tag, quint32 *len) {
        if (at + 8 > b.size())
            return false;
        *tag = b.mid(at, 4);
        *len = qFromBigEndian<quint32>(b.constData() + at + 4);
        return true;
    };
    QByteArray tag;
    quint32 len = 0;
    if (!header(dmap, 0, &tag, &len) || tag != "mlit" || len > quint32(dmap.size() - 8))
        return;
    const qsizetype end = 8 + qsizetype(len);
    for (qsizetype at = 8; header(dmap, at, &tag, &len) && at + 8 + qsizetype(len) <= end; at += 8 + qsizetype(len)) {
        const QString value = QString::fromUtf8(dmap.mid(at + 8, len));
        if (tag == "minm" && title)
            *title = value;
        else if (tag == "asar" && artist)
            *artist = value;
        else if (tag == "asal" && album)
            *album = value;
    }
}

double AirPlayReceiver::volumeFraction(float airplayDb)
{
    if (airplayDb <= -30.0f)    // -144 is AirPlay's mute
        return 0.0;
    if (airplayDb >= 0.0f)
        return 1.0;
    return double(30.0f + airplayDb) / 30.0;
}

// ─── video ───────────────────────────────────────────────────────────

void AirPlayReceiver::onVideo(const unsigned char *data, int length)
{
    // UxPlay's mirror thread: Annex B access units, SPS/PPS in front of keyframes
    std::lock_guard<std::mutex> lock(m_videoMutex);
    if (!m_decoder) {
        m_decoder = std::make_unique<H264Decoder>();
        if (!m_decoder->open())
            qCWarning(lcAirPlay) << "no H.264 decoder: the mirrored screen will not show";
    }
    if (!m_decoder->isOpen())
        return;
    m_decoder->decode(QByteArray(reinterpret_cast<const char *>(data), length), [this](const AVFrame *f) {
        const QVideoFrame vf = H264Decoder::toVideoFrame(f);
        if (!vf.isValid())
            return;
        {
            std::lock_guard<std::mutex> frameLock(m_frameMutex);
            m_latestFrame = vf;
        }
        ++m_frameCount;
        if (!m_framePending.exchange(true))
            QMetaObject::invokeMethod(this, &AirPlayReceiver::deliverFrame, Qt::QueuedConnection);
    });
}

void AirPlayReceiver::onVideoReset()
{
    // A new stream starts with its own SPS/PPS: a fresh decoder for it
    std::lock_guard<std::mutex> lock(m_videoMutex);
    m_decoder.reset();
}

void AirPlayReceiver::deliverFrame()
{
    QVideoFrame frame;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        frame = m_latestFrame;
        m_latestFrame = QVideoFrame();
        m_framePending = false;
    }
    if (!frame.isValid())
        return;
    if (frame.size() != m_frameSize) {
        qCInfo(lcAirPlay) << "mirrored screen" << frame.size() << (m_sink ? "" : "(no video sink yet)");
        m_frameSize = frame.size();
        emit frameSizeChanged(m_frameSize.width(), m_frameSize.height());
    }
    if (m_sink)
        m_sink->setVideoFrame(frame);
}

// ─── audio ───────────────────────────────────────────────────────────

void AirPlayReceiver::onAudioFormat(int ct)
{
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        m_audioDecoder = std::make_unique<AirPlayAudioDecoder>();
        if (!m_audioDecoder->open(ct))
            m_audioDecoder.reset();
    }
    QMetaObject::invokeMethod(this, [this] {
        if (!m_audioStreaming) {
            m_audioStreaming = true;
            emit audioStreamingChanged(true);
        }
    }, Qt::QueuedConnection);
}

void AirPlayReceiver::onAudio(const unsigned char *data, int length)
{
    QByteArray pcm;
    {
        std::lock_guard<std::mutex> lock(m_audioMutex);
        if (!m_audioDecoder)
            return;
        pcm = m_audioDecoder->decode(data, length);
    }
    if (!pcm.isEmpty())
        QMetaObject::invokeMethod(this, [this, pcm] { playPcm(pcm); }, Qt::QueuedConnection);
}

void AirPlayReceiver::playPcm(const QByteArray &pcm)
{
    if (!m_audioSink) {
        m_audioFormat.setSampleRate(kSampleRate);
        m_audioFormat.setChannelCount(2);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
        const QAudioDevice device = AudioOutput::preferred(m_audioDeviceId);
        if (device.isNull()) {
            if (!m_audioWarned)
                qCWarning(lcAirPlay) << "no audio output device; AirPlay audio will not play";
            m_audioWarned = true;
            return;
        }
        m_audioSink = new QAudioSink(device, m_audioFormat, this);
        m_audioSink->setBufferSize(m_audioFormat.bytesForDuration(kAudioDeviceBufferMs * 1000));
        m_audioSink->setVolume(m_volume);
        m_audioIo = m_audioSink->start();
        if (!m_audioIo) {
            if (!m_audioWarned)
                qCWarning(lcAirPlay) << "could not start audio output" << device.description() << m_audioSink->error();
            m_audioWarned = true;
            return;
        }
        qCInfo(lcAirPlay) << "AirPlay audio plays on" << device.description();
    }
    if (!m_audioIo)
        return;
    m_audioQueue.enqueue(pcm);
    m_audioQueued += pcm.size();
    const qsizetype max = m_audioFormat.bytesForDuration(qint64(kAudioQueueMaxMs) * 1000);
    while (m_audioQueued > max && m_audioQueue.size() > 1)
        m_audioQueued -= m_audioQueue.dequeue().size();
    drainAudio();
    if (!m_audioTimer.isActive())
        m_audioTimer.start();
}

void AirPlayReceiver::drainAudio()
{
    if (!m_audioIo) {
        m_audioTimer.stop();
        return;
    }
    while (!m_audioQueue.isEmpty()) {
        QByteArray &head = m_audioQueue.head();
        const qsizetype free = m_audioSink->bytesFree();
        if (free <= 0)
            break;
        const qint64 n = m_audioIo->write(head.constData(), qMin(free, head.size()));
        if (n <= 0)
            break;
        m_audioQueued -= n;
        if (n == head.size())
            m_audioQueue.dequeue();
        else
            head.remove(0, n);
    }
    if (m_audioQueue.isEmpty())
        m_audioTimer.stop();
}

void AirPlayReceiver::closeAudio()
{
    m_audioTimer.stop();
    if (m_audioSink) {
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
    }
    m_audioIo = nullptr;
    m_audioQueue.clear();
    m_audioQueued = 0;
    m_audioWarned = false;
}

void AirPlayReceiver::setConnections(int open)
{
    emit connectionsChanged(open);
    if (open == 0) {
        closeAudio();
        if (m_audioStreaming) {
            m_audioStreaming = false;
            emit audioStreamingChanged(false);
        }
    }
}
