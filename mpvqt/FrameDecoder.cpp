// Luma-specific file: not part of upstream KDE mpvqt (see mpvqt/README.md).
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

void FrameDecoder::clearCache() {
    m_cache.clear();
}

void FrameDecoder::cleanup() {
    // The cached frames belong to the file being closed.
    m_cache.clear();
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); m_codecCtx = nullptr; }
    if (m_fmtCtx) { avformat_close_input(&m_fmtCtx); m_fmtCtx = nullptr; }
    m_currentPath.clear();
    m_videoStreamIndex = -1;
}

bool FrameDecoder::openFile(const QString &filePath) {
    cleanup();

    if (avformat_open_input(&m_fmtCtx, filePath.toUtf8().constData(), nullptr, nullptr) != 0) {
        cleanup();
        return false;
    }
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        cleanup();
        return false;
    }

    m_videoStreamIndex = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0) {
        cleanup();
        return false;
    }

    AVStream *stream = m_fmtCtx->streams[m_videoStreamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        cleanup();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        cleanup();
        return false;
    }
    if (avcodec_parameters_to_context(m_codecCtx, stream->codecpar) < 0) {
        cleanup();
        return false;
    }

    m_codecCtx->thread_count = 1;
    m_codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        cleanup();
        return false;
    }

    m_currentPath = filePath;
    return true;
}

QImage FrameDecoder::decodeFrame(const QString &filePath, double timeInSeconds) {
    m_cancelFlag.storeRelaxed(0);

    QString cacheKey = filePath + "_" + QString::number(qRound(timeInSeconds * 10));
    if (m_cache.contains(cacheKey)) {
        return *m_cache.object(cacheKey);
    }

    // The file is already open — just seek, without reopening it.
    if (m_currentPath != filePath || !m_fmtCtx || !m_codecCtx) {
        if (!openFile(filePath)) {
            return QImage();
        }
    }

    if (m_cancelFlag.loadRelaxed()) { return QImage(); }

    int videoStreamIndex = m_videoStreamIndex;
    int64_t seekTarget = static_cast<int64_t>(timeInSeconds * AV_TIME_BASE);
    if (av_seek_frame(m_fmtCtx, -1, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        return QImage();
    }
    avcodec_flush_buffers(m_codecCtx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        return QImage();
    }
    QImage result;

    int targetWidth = 320;
    int targetHeight = 180;

    const AVStream *videoStream = m_fmtCtx->streams[videoStreamIndex];
    const AVRational timeBase = videoStream->time_base;
    auto frameTime = [&](const AVFrame *f) {
        int64_t ts = f->best_effort_timestamp;
        if (ts == AV_NOPTS_VALUE) ts = f->pts;
        return ts == AV_NOPTS_VALUE ? -1.0 : ts * av_q2d(timeBase);
    };

    auto toImage = [&](const AVFrame *f) {
        m_swsCtx = sws_getCachedContext(m_swsCtx, f->width, f->height,
                                        (AVPixelFormat)f->format,
                                        targetWidth, targetHeight, AV_PIX_FMT_RGB32,
                                        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsCtx) return QImage();

        QImage image(targetWidth, targetHeight, QImage::Format_RGB32);
        const int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
                                                      targetWidth, targetHeight, 1);
        uint8_t *buffer = nullptr;
        if (!image.isNull() && numBytes > 0)
            buffer = static_cast<uint8_t *>(av_malloc(size_t(numBytes)));

        if (!buffer) return QImage();

        uint8_t *destData[4] = { buffer, nullptr, nullptr, nullptr };
        int destLinesize[4] = { targetWidth * 4, 0, 0, 0 };
        sws_scale(m_swsCtx, f->data, f->linesize, 0, f->height, destData, destLinesize);
        for (int y = 0; y < targetHeight; ++y) {
            memcpy(image.scanLine(y), buffer + y * destLinesize[0],
                   size_t(targetWidth) * 4);
        }
        av_free(buffer);
        return image;
    };

    // av_seek_frame() stops at the keyframe before the requested position.
    // Return the frame at the requested time instead of that keyframe, so the
    // preview advances with the video rather than in keyframe-sized steps
    // (often every 10 seconds).
    bool gotFrame = false;
    auto drainDecoder = [&]() {
        while (!gotFrame && !m_cancelFlag.loadRelaxed()) {
            const int ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            const double t = frameTime(frame);
            if (t >= 0.0 && t + 0.001 < timeInSeconds) {
                av_frame_unref(frame);
                continue;
            }

            result = toImage(frame);
            av_frame_unref(frame);
            gotFrame = !result.isNull();
        }
    };

    bool eof = false;
    while (!gotFrame && !m_cancelFlag.loadRelaxed()) {
        if (av_read_frame(m_fmtCtx, packet) < 0) {
            eof = true;
            break;
        }
        if (packet->stream_index != videoStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        int sendRet = 0;
        while (!gotFrame) {
            sendRet = avcodec_send_packet(m_codecCtx, packet);
            if (sendRet != AVERROR(EAGAIN)) break;
            drainDecoder();
        }
        av_packet_unref(packet);
        if (gotFrame) break;
        if (sendRet < 0) continue;
        drainDecoder();
    }

    // Frames may still be buffered in the decoder when the file ends.
    if (!gotFrame && eof && !m_cancelFlag.loadRelaxed()) {
        avcodec_send_packet(m_codecCtx, nullptr);
        drainDecoder();
    }

    av_packet_free(&packet);
    av_frame_free(&frame);

    if (!result.isNull() && !m_cancelFlag.loadRelaxed()) {
        m_cache.insert(cacheKey, new QImage(result));
    }

    return result;
}
