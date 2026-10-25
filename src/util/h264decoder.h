#ifndef H264DECODER_H
#define H264DECODER_H

// H.264 (Annex B) → YUV 4:2:0 with libavcodec, shared by every projection
// source: the phone-mirror client and the Android Auto receiver. Without
// OCTAVE_HAVE_FFMPEG open() fails and nothing decodes.

#include <QByteArray>
#include <QVideoFrame>

#include <functional>

struct AVCodecContext;
struct AVPacket;
struct AVFrame;

class H264Decoder
{
public:
    H264Decoder() = default;
    ~H264Decoder();
    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    // Slice threading only: frame threading delays every picture by one
    // frame per thread
    bool open();
    bool isOpen() const { return m_ctx != nullptr; }
    void close();

    // SPS/PPS sent apart from the pictures: goes in front of the next packet
    void setConfig(const QByteArray &spsPps) { m_pendingConfig = spsPps; }

    // One access unit. onFrame runs for every decoded YUV420P / YUVJ420P
    // picture (other pixel formats are dropped); the frame is only valid
    // during the call.
    void decode(QByteArray data, const std::function<void(const AVFrame *)> &onFrame);

    // A copy of a decoded picture as a mapped-and-filled QVideoFrame
    // (invalid if mapping fails)
    static QVideoFrame toVideoFrame(const AVFrame *frame);

private:
    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_frame = nullptr;
    QByteArray m_pendingConfig;
};

#endif // H264DECODER_H
