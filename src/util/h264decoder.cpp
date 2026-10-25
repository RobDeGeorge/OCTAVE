#include "h264decoder.h"

#include <QVideoFrameFormat>

#include <cstring>

#ifdef OCTAVE_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

H264Decoder::~H264Decoder()
{
    close();
}

bool H264Decoder::open()
{
#ifdef OCTAVE_HAVE_FFMPEG
    close();
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    m_ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    // Must be set before avcodec_open2 to take effect
    if (m_ctx)
        m_ctx->thread_type = FF_THREAD_SLICE;
    if (!m_ctx || avcodec_open2(m_ctx, codec, nullptr) < 0) {
        close();
        return false;
    }
    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    return true;
#else
    return false;
#endif
}

void H264Decoder::close()
{
#ifdef OCTAVE_HAVE_FFMPEG
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_pkt)
        av_packet_free(&m_pkt);
    if (m_ctx)
        avcodec_free_context(&m_ctx);
#endif
    m_pendingConfig.clear();
}

void H264Decoder::decode(QByteArray data, const std::function<void(const AVFrame *)> &onFrame)
{
#ifdef OCTAVE_HAVE_FFMPEG
    if (!m_ctx)
        return;
    if (!m_pendingConfig.isEmpty()) {
        data.prepend(m_pendingConfig);
        m_pendingConfig.clear();
    }
    // libavcodec needs AV_INPUT_BUFFER_PADDING_SIZE zero bytes after the data
    data.append(AV_INPUT_BUFFER_PADDING_SIZE, '\0');
    m_pkt->data = reinterpret_cast<uint8_t *>(data.data());
    m_pkt->size = data.size() - AV_INPUT_BUFFER_PADDING_SIZE;
    if (avcodec_send_packet(m_ctx, m_pkt) < 0)
        return;
    while (avcodec_receive_frame(m_ctx, m_frame) == 0) {
        if (m_frame->format == AV_PIX_FMT_YUV420P || m_frame->format == AV_PIX_FMT_YUVJ420P)
            onFrame(m_frame);
        av_frame_unref(m_frame);
    }
#else
    Q_UNUSED(data) Q_UNUSED(onFrame)
#endif
}

QVideoFrame H264Decoder::toVideoFrame(const AVFrame *frame)
{
#ifdef OCTAVE_HAVE_FFMPEG
    const int w = frame->width, h = frame->height;
    QVideoFrameFormat fmt(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
    QVideoFrame vf(fmt);
    if (!vf.map(QVideoFrame::WriteOnly))
        return QVideoFrame();
    for (int i = 0; i < 3; ++i) {
        const int rows = i == 0 ? h : (h + 1) / 2;
        const int rowBytes = i == 0 ? w : (w + 1) / 2;
        const int srcStride = frame->linesize[i];
        const int dstStride = vf.bytesPerLine(i);
        uchar *dst = vf.bits(i);
        const uint8_t *src = frame->data[i];
        if (srcStride == dstStride) {
            std::memcpy(dst, src, size_t(srcStride) * rows);
        } else {
            for (int r = 0; r < rows; ++r)
                std::memcpy(dst + r * dstStride, src + r * srcStride, size_t(rowBytes));
        }
    }
    vf.unmap();
    return vf;
#else
    Q_UNUSED(frame)
    return QVideoFrame();
#endif
}
