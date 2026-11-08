#include "MediaInfoService.hpp"
#include "TagManager.hpp"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QUrl>
#include <QVector>
#include <QtConcurrent/QtConcurrentRun>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

namespace {

// Matroska/WebM store no per-track bitrate, so AVCodecParameters::bit_rate is
// 0 for every stream there even though the container total and the duration
// are known. Estimate the missing values from a bounded packet scan: average
// packet bytes per stream over the scanned span, capped in media time and
// bytes so that a large file cannot keep the information dialog waiting.
// Runs on the worker thread that builds the file info.
constexpr double kBitrateScanSeconds = 20.0;
constexpr qint64 kBitrateScanMaxBytes = 64 * 1024 * 1024;

struct StreamBitrateSample {
    qint64 bytes = 0;
    bool hasTimestamps = false;
    double first = 0.0;
    double last = 0.0;
};

QHash<int, qint64> estimateStreamBitrates(AVFormatContext *fmt_ctx) {
    QHash<int, qint64> estimates;
    AVPacket *packet = av_packet_alloc();
    if (!packet) return estimates;

    const int streamCount = static_cast<int>(fmt_ctx->nb_streams);
    QVector<StreamBitrateSample> samples(streamCount);

    qint64 bytesRead = 0;
    bool haveTimeline = false;
    double timelineStart = 0.0;
    double timelineEnd = 0.0;

    while (av_read_frame(fmt_ctx, packet) >= 0) {
        const int index = packet->stream_index;
        bytesRead += packet->size;

        if (index >= 0 && index < streamCount) {
            StreamBitrateSample &sample = samples[index];
            sample.bytes += packet->size;

            const AVStream *stream = fmt_ctx->streams[index];
            double timestamp = 0.0;
            bool haveTimestamp = false;
            if (packet->pts != AV_NOPTS_VALUE) {
                timestamp = packet->pts * av_q2d(stream->time_base);
                haveTimestamp = true;
            } else if (packet->dts != AV_NOPTS_VALUE) {
                timestamp = packet->dts * av_q2d(stream->time_base);
                haveTimestamp = true;
            }

            if (haveTimestamp) {
                const double duration = packet->duration > 0
                    ? packet->duration * av_q2d(stream->time_base) : 0.0;
                const double end = timestamp + duration;
                if (!sample.hasTimestamps) {
                    sample.hasTimestamps = true;
                    sample.first = timestamp;
                    sample.last = end;
                } else {
                    sample.first = qMin(sample.first, timestamp);
                    sample.last = qMax(sample.last, end);
                }

                if (!haveTimeline) {
                    haveTimeline = true;
                    timelineStart = timestamp;
                    timelineEnd = end;
                } else {
                    timelineStart = qMin(timelineStart, timestamp);
                    timelineEnd = qMax(timelineEnd, end);
                }
            }
        }

        av_packet_unref(packet);

        if (bytesRead >= kBitrateScanMaxBytes) break;
        if (haveTimeline && timelineEnd - timelineStart >= kBitrateScanSeconds) break;
    }
    av_packet_free(&packet);

    const double timelineSpan = haveTimeline ? timelineEnd - timelineStart : 0.0;
    for (int index = 0; index < streamCount; ++index) {
        const StreamBitrateSample &sample = samples[index];
        const double span = sample.hasTimestamps
            ? sample.last - sample.first : timelineSpan;
        if (sample.bytes <= 0 || span <= 0.05) continue;
        estimates.insert(index, static_cast<qint64>(sample.bytes * 8.0 / span));
    }
    return estimates;
}

// Bitrate of the selected streams over a short window around a playback
// position. The statistics panel uses this for video: mpv's "video-bitrate"
// is the average of the last GOP and only changes at keyframes, so it can keep
// an unrepresentative GOP's value for many seconds. Audio has the same
// property, but every audio packet is a keyframe, so its window is ~0.5 s.
// Runs on a worker thread and opens the file read-only.
constexpr double kLiveBitrateWindow = 2.0;
constexpr qint64 kLiveBitrateMaxBytes = 16 * 1024 * 1024;

struct LiveBitrate {
    qint64 videoBps = 0;
    qint64 audioBps = 0;
};

LiveBitrate sampleLiveBitrate(const QString &path, double position) {
    LiveBitrate result;

    // Only the stream list is needed, not decoder setup.
    AVDictionary *options = nullptr;
    av_dict_set(&options, "probesize", "2097152", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);

    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path.toUtf8().constData(), nullptr, &options) < 0) {
        av_dict_free(&options);
        return result;
    }
    av_dict_free(&options);
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return result;
    }

    const int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (video_idx < 0 && audio_idx < 0) {
        avformat_close_input(&fmt_ctx);
        return result;
    }

    const double file_end = fmt_ctx->duration > 0
        ? fmt_ctx->duration / static_cast<double>(AV_TIME_BASE)
        : position + kLiveBitrateWindow;
    const double window_start = qMax(0.0, qMin(position, file_end) - kLiveBitrateWindow);
    const double window_end = qMin(file_end, window_start + kLiveBitrateWindow);
    if (window_end - window_start <= 0.05) {
        avformat_close_input(&fmt_ctx);
        return result;
    }

    if (av_seek_frame(fmt_ctx, -1, static_cast<int64_t>(window_start * AV_TIME_BASE),
                      AVSEEK_FLAG_BACKWARD) < 0) {
        avformat_close_input(&fmt_ctx);
        return result;
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        avformat_close_input(&fmt_ctx);
        return result;
    }

    qint64 video_bytes = 0;
    qint64 audio_bytes = 0;
    qint64 bytes_read = 0;
    double span_start = 0.0;
    double span_end = 0.0;
    bool have_span = false;

    while (av_read_frame(fmt_ctx, packet) >= 0) {
        const int index = packet->stream_index;
        if (index < 0 || index >= static_cast<int>(fmt_ctx->nb_streams)) {
            av_packet_unref(packet);
            continue;
        }
        const AVStream *stream = fmt_ctx->streams[index];
        double ts = 0.0;
        bool have_ts = false;
        if (packet->pts != AV_NOPTS_VALUE) {
            ts = packet->pts * av_q2d(stream->time_base);
            have_ts = true;
        } else if (packet->dts != AV_NOPTS_VALUE) {
            ts = packet->dts * av_q2d(stream->time_base);
            have_ts = true;
        }

        // Allow a little reordering slack, then stop at the window's end.
        if (have_ts && ts > window_end + 0.5) {
            av_packet_unref(packet);
            break;
        }
        if (!have_ts || ts < window_start || ts > window_end
                || (index != video_idx && index != audio_idx)) {
            av_packet_unref(packet);
            continue;
        }

        const double packet_end = packet->duration > 0
            ? ts + packet->duration * av_q2d(stream->time_base) : ts;
        if (index == video_idx) video_bytes += packet->size;
        else audio_bytes += packet->size;
        bytes_read += packet->size;

        if (!have_span) {
            have_span = true;
            span_start = ts;
            span_end = packet_end;
        } else {
            span_start = qMin(span_start, ts);
            span_end = qMax(span_end, packet_end);
        }

        av_packet_unref(packet);
        if (bytes_read >= kLiveBitrateMaxBytes) break;
    }
    av_packet_free(&packet);
    avformat_close_input(&fmt_ctx);

    const double span = span_end - span_start;
    if (have_span && span > 0.1) {
        if (video_bytes > 0)
            result.videoBps = static_cast<qint64>(video_bytes * 8.0 / span);
        if (audio_bytes > 0)
            result.audioBps = static_cast<qint64>(audio_bytes * 8.0 / span);
    }
    return result;
}

// Collects format/stream/tag information. This is deliberately a free function:
// it is called from a worker thread and must not touch MediaManager state.
QVariantMap buildFileInfo(const QString &path) {
    QVariantMap info;

    // Stream URL: don't open the file, show only the address.
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://"))) {
        info["path"] = path;
        info["name"] = QUrl(path).host();
        info["isStream"] = true;
        info["isAudio"] = true;
        return info;
    }
    QFileInfo fi(path);
    if (!fi.exists()) return info;

    info["path"] = fi.absoluteFilePath();
    info["size"] = fi.size();
    info["size_str"] = QString::number(fi.size() / (1024.0 * 1024.0), 'f', 2) + " MiB";
    info["name"] = fi.fileName();

    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path.toUtf8().constData(), nullptr, nullptr) == 0) {
        if (avformat_find_stream_info(fmt_ctx, nullptr) >= 0) {
            double duration = (double)fmt_ctx->duration / AV_TIME_BASE;
            info["duration"] = duration;

            if (fmt_ctx->iformat && fmt_ctx->iformat->long_name) {
                info["format"] = QString::fromUtf8(fmt_ctx->iformat->long_name);
            } else {
                info["format"] = "N/A";
            }

            info["bitrate"] = static_cast<qlonglong>(fmt_ctx->bit_rate / 1000);

            int video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            // Cover art (attached_pic) is not video, so the file is still audio.
            bool hasRealVideo = false;
            if (video_stream_idx >= 0) {
                hasRealVideo = (fmt_ctx->streams[video_stream_idx]->disposition
                                & AV_DISPOSITION_ATTACHED_PIC) == 0;
            }

            if (video_stream_idx >= 0) {
                AVStream *stream = fmt_ctx->streams[video_stream_idx];
                AVCodecParameters *codecpar = stream->codecpar;
                info["width"] = codecpar->width;
                info["height"] = codecpar->height;
                info["video_bitrate"] = static_cast<qlonglong>(codecpar->bit_rate / 1000);

                const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
                info["video_codec"] = codec ? QString::fromUtf8(codec->long_name) : "Unknown";
            } else {
                info["width"] = 0;
                info["height"] = 0;
                info["video_bitrate"] = 0;
                info["video_codec"] = "N/A";
            }

            int audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (audio_stream_idx >= 0) {
                AVStream *stream = fmt_ctx->streams[audio_stream_idx];
                AVCodecParameters *codecpar = stream->codecpar;
                info["audio_bitrate"] = static_cast<qlonglong>(codecpar->bit_rate / 1000);
                info["sample_rate"] = codecpar->sample_rate;
                info["channels"] = codecpar->ch_layout.nb_channels;

                const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
                info["audio_codec"] = codec ? QString::fromUtf8(codec->long_name) : "Unknown";
            } else {
                info["audio_bitrate"] = 0;
                info["sample_rate"] = 0;
                info["channels"] = 0;
                info["audio_codec"] = "N/A";
            }

            // Matroska/WebM store no per-track bitrate: both values above are 0
            // even though the container total is known. Estimate the missing
            // ones instead of reporting "0 kbps".
            const bool videoBitrateMissing = hasRealVideo
                && fmt_ctx->streams[video_stream_idx]->codecpar->bit_rate <= 0;
            const bool audioBitrateMissing = audio_stream_idx >= 0
                && fmt_ctx->streams[audio_stream_idx]->codecpar->bit_rate <= 0;
            info["video_bitrate_estimated"] = videoBitrateMissing;
            info["audio_bitrate_estimated"] = audioBitrateMissing;
            if (videoBitrateMissing || audioBitrateMissing) {
                const QHash<int, qint64> estimates = estimateStreamBitrates(fmt_ctx);
                const auto bitrateFor = [&](int index) -> qint64 {
                    const qint64 stored = fmt_ctx->streams[index]->codecpar->bit_rate;
                    return stored > 0 ? stored : estimates.value(index, 0);
                };

                if (videoBitrateMissing) {
                    qint64 videoBps = estimates.value(video_stream_idx, 0);
                    // The container total is exact, while a short scan of
                    // variable-bitrate video is not: the video track gets what
                    // the stable audio/subtitle/attachment streams leave over.
                    if (fmt_ctx->bit_rate > 0) {
                        qint64 othersBps = 0;
                        for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
                            if (static_cast<int>(i) == video_stream_idx) continue;
                            othersBps += bitrateFor(static_cast<int>(i));
                        }
                        const qint64 remainder = static_cast<qint64>(fmt_ctx->bit_rate) - othersBps;
                        if (remainder > 0) videoBps = remainder;
                    }
                    info["video_bitrate"] = static_cast<qlonglong>(videoBps / 1000);
                }
                if (audioBitrateMissing) {
                    info["audio_bitrate"] = static_cast<qlonglong>(
                        estimates.value(audio_stream_idx, 0) / 1000);
                }
            }

            // Tags for the "Properties" window.
            info["isAudio"] = !hasRealVideo;

            QVariantMap allTags;
            const AVDictionaryEntry *metaEntry = nullptr;
            while ((metaEntry = av_dict_get(fmt_ctx->metadata, "", metaEntry, AV_DICT_IGNORE_SUFFIX))) {
                allTags.insert(QString::fromUtf8(metaEntry->key).toLower(),
                               QString::fromUtf8(metaEntry->value));
            }
            info["tagsAll"] = allTags;

            // Editable tags: read with TagLib, fill empty fields from FFmpeg.
            QVariantMap tags = !hasRealVideo ? TagManager::read(path) : QVariantMap();
            if (!hasRealVideo) {
                struct Fallback { const char *field; const char *tag; };
                static const Fallback fallbacks[] = {
                    { "title", "title" }, { "artist", "artist" }, { "album", "album" },
                    { "genre", "genre" }, { "year", "date" }, { "track", "track" }
                };
                for (const Fallback &f : fallbacks) {
                    if (!tags.value(QLatin1String(f.field)).toString().isEmpty()) continue;
                    const QString value = allTags.value(QLatin1String(f.tag)).toString().trimmed();
                    if (!value.isEmpty()) tags[QLatin1String(f.field)] = value;
                }
            }
            info["tags"] = tags;
        }
        avformat_close_input(&fmt_ctx);
    } else {
        info["duration"] = 0;
        info["format"] = "N/A";
        info["width"] = 0;
        info["height"] = 0;
        info["video_codec"] = "N/A";
        info["audio_codec"] = "N/A";
    }
    return info;
}

} // namespace

MediaInfoService::MediaInfoService(QObject *parent)
    : QObject(parent)
{
}

void MediaInfoService::requestFileInfo(const QString &path)
{
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher, path]() {
        const QVariantMap info = watcher->result();
        watcher->deleteLater();
        emit fileInfoReady(path, info);
    });
    watcher->setFuture(QtConcurrent::run([path]() { return buildFileInfo(path); }));
}

void MediaInfoService::requestLiveBitrate(const QString &path, double position)
{
    if (path.isEmpty() || position < 0.0) return;
    auto *watcher = new QFutureWatcher<LiveBitrate>(this);
    connect(watcher, &QFutureWatcher<LiveBitrate>::finished, this,
            [this, watcher, path, position]() {
        const LiveBitrate result = watcher->result();
        watcher->deleteLater();
        emit liveBitrateReady(path, position, result.videoBps, result.audioBps);
    });
    watcher->setFuture(QtConcurrent::run([path, position]() {
        return sampleLiveBitrate(path, position);
    }));
}

QVariantMap MediaInfoService::readTags(const QString &path) const
{
    return TagManager::read(path);
}

bool MediaInfoService::saveTags(const QString &path, const QVariantMap &tags) const
{
    if (path.isEmpty()) return false;
    return TagManager::write(path, tags);
}

void MediaInfoService::requestSaveTags(const QString &path, const QVariantMap &tags)
{
    if (path.isEmpty()) {
        emit tagsSaved(path, false);
        return;
    }

    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, path]() {
        const bool success = watcher->result();
        watcher->deleteLater();
        emit tagsSaved(path, success);
    });
    watcher->setFuture(QtConcurrent::run([path, tags]() {
        return TagManager::write(path, tags);
    }));
}
