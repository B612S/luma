#include "FrameDecoder.hpp"
#include <QDebug>
#include <cstring>

FrameDecoder::FrameDecoder(QObject *parent) : QObject(parent), m_cache(30) {
    avformat_network_init();
}

FrameDecoder::~FrameDecoder() {
    cleanup();
}

void FrameDecoder::cancelCurrentDecode() {
    m_cancelFlag.storeRelaxed(1);
}

void FrameDecoder::cleanup() {
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); m_codecCtx = nullptr; }
    if (m_fmtCtx) { avformat_close_input(&m_fmtCtx); m_fmtCtx = nullptr; }
}

QImage FrameDecoder::decodeFrame(const QString &filePath, double timeInSeconds) {
    m_cancelFlag.storeRelaxed(0);

    QString cacheKey = filePath + "_" + QString::number(qRound(timeInSeconds * 10));
    if (m_cache.contains(cacheKey)) {
        return *m_cache.object(cacheKey);
    }

    cleanup();

    if (avformat_open_input(&m_fmtCtx, filePath.toUtf8().constData(), nullptr, nullptr) != 0) {
        return QImage();
    }

    if (m_cancelFlag.loadRelaxed()) { cleanup(); return QImage(); }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        cleanup(); return QImage();
    }

    int videoStreamIndex = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) { cleanup(); return QImage(); }

    AVStream *stream = m_fmtCtx->streams[videoStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { cleanup(); return QImage(); }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, stream->codecpar);

    m_codecCtx->thread_count = 1;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        cleanup(); return QImage();
    }

    if (m_cancelFlag.loadRelaxed()) { cleanup(); return QImage(); }

    int64_t seekTarget = static_cast<int64_t>(timeInSeconds * AV_TIME_BASE);
    if (av_seek_frame(m_fmtCtx, -1, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        cleanup(); return QImage();
    }
    avcodec_flush_buffers(m_codecCtx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QImage result;

    int targetWidth = 320;
    int targetHeight = 180;

    while (av_read_frame(m_fmtCtx, packet) >= 0) {
        if (m_cancelFlag.loadRelaxed()) {
            av_packet_unref(packet);
            break;
        }

        if (packet->stream_index == videoStreamIndex) {
            if (avcodec_send_packet(m_codecCtx, packet) == 0) {
                if (avcodec_receive_frame(m_codecCtx, frame) == 0) {

                    m_swsCtx = sws_getCachedContext(m_swsCtx, frame->width, frame->height,
                                                    (AVPixelFormat)frame->format,
                                                    targetWidth, targetHeight, AV_PIX_FMT_RGB32,
                                                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

                    if (m_swsCtx) {
                        result = QImage(targetWidth, targetHeight, QImage::Format_RGB32);
                        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, targetWidth, targetHeight, 1);
                        uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

                        uint8_t *destData[4] = { buffer, nullptr, nullptr, nullptr };
                        int destLinesize[4] = { targetWidth * 4, 0, 0, 0 };

                        sws_scale(m_swsCtx, frame->data, frame->linesize, 0, frame->height, destData, destLinesize);

                        for (int y = 0; y < targetHeight; ++y) {
                            memcpy(result.scanLine(y), buffer + y * destLinesize[0], targetWidth * 4);
                        }
                        av_free(buffer);
                    }

                    av_packet_unref(packet);
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    cleanup();

    if (!result.isNull() && !m_cancelFlag.loadRelaxed()) {
        m_cache.insert(cacheKey, new QImage(result));
    }

    return result;
}
