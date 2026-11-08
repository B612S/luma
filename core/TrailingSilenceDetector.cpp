#include "TrailingSilenceDetector.hpp"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

namespace {

// Decode roughly the last 30 seconds of an audio file and return the position
// after which only silence remains, or -1 when there is no meaningful silent
// tail. This runs in a worker thread: decoding must not block the GUI.
double detectTrailingSilenceEnd(const QString &path)
{
    if (path.isEmpty() || path.contains(QStringLiteral("://")))
        return -1.0;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return -1.0;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return -1.0;
    }

    const int audioIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIndex < 0) {
        avformat_close_input(&fmt);
        return -1.0;
    }
    AVStream *stream = fmt->streams[audioIndex];
    double duration = fmt->duration > 0
        ? fmt->duration / static_cast<double>(AV_TIME_BASE) : 0.0;
    if (duration <= 0.0 && stream->duration > 0)
        duration = stream->duration * av_q2d(stream->time_base);
    if (duration < 1.0) {
        avformat_close_input(&fmt);
        return -1.0;
    }

    constexpr double scanSeconds = 30.0;
    const double windowStart = std::max(0.0, duration - scanSeconds);
    const int64_t seekTarget = static_cast<int64_t>(windowStart / av_q2d(stream->time_base));
    if (av_seek_frame(fmt, audioIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        avformat_close_input(&fmt);
        return -1.0;
    }

    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        avformat_close_input(&fmt);
        return -1.0;
    }
    AVCodecContext *codec = avcodec_alloc_context3(decoder);
    if (!codec) {
        avformat_close_input(&fmt);
        return -1.0;
    }
    if (avcodec_parameters_to_context(codec, stream->codecpar) < 0
            || avcodec_open2(codec, decoder, nullptr) < 0) {
        avcodec_free_context(&codec);
        avformat_close_input(&fmt);
        return -1.0;
    }
    avcodec_flush_buffers(codec);

    // Anything at or below this level (-50 dBFS) counts as silence.
    const double linearThreshold = 0.0031622776601683794;
    auto thresholdFor = [linearThreshold](AVSampleFormat format) -> double {
        switch (av_get_packed_sample_fmt(format)) {
        case AV_SAMPLE_FMT_U8:  return 128.0 * linearThreshold;
        case AV_SAMPLE_FMT_S16: return 32768.0 * linearThreshold;
        case AV_SAMPLE_FMT_S32: return 2147483648.0 * linearThreshold;
        case AV_SAMPLE_FMT_S64: return 9.2233720368547758e18 * linearThreshold;
        case AV_SAMPLE_FMT_FLT:
        case AV_SAMPLE_FMT_DBL: return linearThreshold;
        default: return -1.0;
        }
    };

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
        avformat_close_input(&fmt);
        return -1.0;
    }

    double lastEnd = windowStart;
    double lastAudible = -1.0;
    bool hasAudible = false;

    auto processFrame = [&](AVFrame *decoded) {
        const int channels = decoded->ch_layout.nb_channels > 0
            ? decoded->ch_layout.nb_channels : 1;
        const int samples = decoded->nb_samples;
        const int sampleRate = decoded->sample_rate > 0
            ? decoded->sample_rate : stream->codecpar->sample_rate;
        if (samples <= 0 || sampleRate <= 0)
            return;

        int64_t timestamp = decoded->best_effort_timestamp;
        if (timestamp == AV_NOPTS_VALUE)
            timestamp = decoded->pts;
        double start = timestamp != AV_NOPTS_VALUE
            ? timestamp * av_q2d(stream->time_base) : lastEnd;
        if (start < 0.0)
            start = 0.0;
        const double end = start + samples / static_cast<double>(sampleRate);
        lastEnd = std::max(lastEnd, end);

        const AVSampleFormat format = static_cast<AVSampleFormat>(decoded->format);
        const double threshold = thresholdFor(format);
        if (threshold < 0.0) {
            // Unknown sample format: do not risk cutting real audio.
            hasAudible = true;
            lastAudible = std::max(lastAudible, end);
            return;
        }

        const int bytes = av_get_bytes_per_sample(format);
        const bool planar = av_sample_fmt_is_planar(format) != 0;
        const int planes = planar ? channels : 1;
        const int count = planar ? samples : samples * channels;
        bool audible = false;
        for (int plane = 0; plane < planes && !audible; ++plane) {
            const uint8_t *data = decoded->data[plane];
            if (!data)
                continue;
            for (int i = 0; i < count; ++i) {
                const uint8_t *sample = data + i * bytes;
                double value = 0.0;
                switch (av_get_packed_sample_fmt(format)) {
                case AV_SAMPLE_FMT_U8: {
                    uint8_t v; memcpy(&v, sample, 1); value = double(v) - 128.0; break;
                }
                case AV_SAMPLE_FMT_S16: {
                    int16_t v; memcpy(&v, sample, 2); value = v; break;
                }
                case AV_SAMPLE_FMT_S32: {
                    int32_t v; memcpy(&v, sample, 4); value = v; break;
                }
                case AV_SAMPLE_FMT_S64: {
                    int64_t v; memcpy(&v, sample, 8); value = double(v); break;
                }
                case AV_SAMPLE_FMT_FLT: {
                    float v; memcpy(&v, sample, 4); value = v; break;
                }
                case AV_SAMPLE_FMT_DBL: {
                    double v; memcpy(&v, sample, 8); value = v; break;
                }
                default:
                    value = 1.0;
                    break;
                }
                if (std::abs(value) > threshold) {
                    audible = true;
                    break;
                }
            }
        }
        if (audible) {
            hasAudible = true;
            lastAudible = std::max(lastAudible, end);
        }
    };

    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == audioIndex) {
            if (avcodec_send_packet(codec, packet) >= 0) {
                while (avcodec_receive_frame(codec, frame) >= 0) {
                    processFrame(frame);
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(packet);
    }
    if (avcodec_send_packet(codec, nullptr) >= 0) {
        while (avcodec_receive_frame(codec, frame) >= 0) {
            processFrame(frame);
            av_frame_unref(frame);
        }
    }

    double effectiveEnd = -1.0;
    if (hasAudible) {
        constexpr double minTrailingSilence = 5.0;
        // +0.15 is the safety margin that will be kept audible; a tail a little
        // under five seconds should still count as five.
        if (lastAudible >= 0.5
                && duration - lastAudible + 0.15 >= minTrailingSilence) {
            effectiveEnd = std::min(duration, lastAudible + 0.15);
        }
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec);
    avformat_close_input(&fmt);
    return effectiveEnd;
}

} // namespace

TrailingSilenceDetector::TrailingSilenceDetector(QObject *parent)
    : QObject(parent)
{
}

void TrailingSilenceDetector::request(const QString &path, quint64 generation)
{
    auto *watcher = new QFutureWatcher<double>(this);
    connect(watcher, &QFutureWatcher<double>::finished, this,
            [this, watcher, path, generation]() {
        const double end = watcher->result();
        watcher->deleteLater();
        emit detected(path, generation, end);
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        return detectTrailingSilenceEnd(path);
    }));
}
