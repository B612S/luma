#include "MediaManager.hpp"
#include "AsyncFileWriter.hpp"
#include "IcyMetadataReader.hpp"
#include "PlaylistManager.hpp"
#include "MediaPaths.hpp"
#include "ConfigManager.hpp"
#include "TagManager.hpp"
#include "Logger.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QUrl>
#include <QStandardPaths>
#include <QDebug>
#include <QRandomGenerator>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QTimer>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QSettings>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QVector>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVersionNumber>
#include <algorithm>
#include <cmath>
#include <QtConcurrent/QtConcurrentRun>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

#include <QCryptographicHash>
#include <QImage>

namespace {

// Audio file metadata for the notification.
struct TrackTags {
    QString title;
    QString artist;
    QString album;
    QString coverPath;
};

// Result of reading tags in a worker thread.
struct TrackTagsResult {
    bool ok = false;
    TrackTags tags;
};

QString dictValue(AVDictionary *meta, const char *key) {
    if (!meta) return QString();
    const AVDictionaryEntry *entry = av_dict_get(meta, key, nullptr, 0);
    return entry ? QString::fromUtf8(entry->value).trimmed() : QString();
}

QMutex &coverMutex()
{
    static QMutex mutex;
    return mutex;
}

QString coverCacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (base.isEmpty()) base = QDir::homePath() + "/.cache";
        base += "/luma";
    }
    return base + "/covers";
}

// The notification only needs the cover of the file that is playing now.
// Caller must hold coverMutex().
void removeOtherCoversLocked(const QString &keep)
{
    QDir dir(coverCacheDir());
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    for (const QFileInfo &fi : files) {
        if (fi.absoluteFilePath() != keep)
            QFile::remove(fi.absoluteFilePath());
    }
}

void clearCoverCache()
{
    QMutexLocker locker(&coverMutex());
    removeOtherCoversLocked(QString());
}

QString coverCachePath(const QString &sourcePath) {
    const QFileInfo fi(sourcePath);
    const QByteArray hash = QCryptographicHash::hash(
        (sourcePath + QString::number(fi.lastModified().toSecsSinceEpoch())).toUtf8(),
        QCryptographicHash::Md5).toHex();

    const QString dir = coverCacheDir();
    QDir().mkpath(dir);
    return dir + "/" + QString::fromLatin1(hash) + ".jpg";
}

// Extracts the embedded cover art (ID3 APIC / MP4 covr, etc.). Attached
// pictures are normal JPEG/PNG bytes; letting Qt decode them avoids a separate
// FFmpeg decoder and the fragile sws_scale path for unusual covers.
QImage decodeAttachedCover(AVFormatContext *ctx) {
    int streamIndex = -1;
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        if (ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            streamIndex = static_cast<int>(i);
            break;
        }
    }
    if (streamIndex < 0) return QImage();

    AVStream *stream = ctx->streams[streamIndex];
    if (!stream->attached_pic.data || stream->attached_pic.size <= 0) return QImage();

    const QByteArray data(reinterpret_cast<const char *>(stream->attached_pic.data),
                          stream->attached_pic.size);
    return QImage::fromData(data);
}

// Reads the tags; returns true if anything was found.
bool readTrackTags(const QString &path, TrackTags &tags) {
    AVFormatContext *ctx = nullptr;
    if (avformat_open_input(&ctx, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;

    tags.title = dictValue(ctx->metadata, "title");
    tags.artist = dictValue(ctx->metadata, "artist");
    if (tags.artist.isEmpty()) tags.artist = dictValue(ctx->metadata, "album_artist");
    tags.album = dictValue(ctx->metadata, "album");

    const bool hasTags = !tags.title.isEmpty() || !tags.artist.isEmpty() || !tags.album.isEmpty();
    {
        // Notification tasks can run concurrently for two quick file switches;
        // serialize the cover-cache check/write. Only the current file keeps
        // its cover: everything else in the covers directory is removed.
        QMutexLocker locker(&coverMutex());
        if (hasTags) {
            const QString coverPath = coverCachePath(path);
            if (QFile::exists(coverPath)) {
                tags.coverPath = coverPath;
            } else {
                const QImage cover = decodeAttachedCover(ctx);
                if (!cover.isNull() && cover.save(coverPath, "JPG", 90))
                    tags.coverPath = coverPath;
            }
            removeOtherCoversLocked(tags.coverPath);
        } else {
            removeOtherCoversLocked(QString());
        }
    }

    avformat_close_input(&ctx);
    return hasTags;
}

// A direct stream URL (radio, m3u8, etc.) — it does not need to go through yt-dlp.
bool isDirectStreamUrl(const QUrl &url) {
    const QString scheme = url.scheme();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;

    // An explicit port almost always means Icecast/Shoutcast radio.
    if (url.port(-1) != -1) return true;

    const QString path = url.path().toLower();
    static const QStringList streamExts = {
        ".m3u8", ".mp3", ".aac", ".ogg", ".opus", ".flac", ".m4a",
        ".mp4", ".webm", ".mkv"
    };
    for (const QString &ext : streamExts) {
        if (path.endsWith(ext)) return true;
    }

    return path.isEmpty() || path == QStringLiteral("/");
}

// Only live/radio URLs should lose seeking. A direct .mp4/.mkv/.webm URL is
// still a finite video, even though it bypasses yt-dlp.
bool isLiveStreamUrl(const QUrl &url) {
    const QString scheme = url.scheme();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;

    if (url.port(-1) != -1) return true; // Icecast/Shoutcast usually uses a port

    const QString path = url.path().toLower();
    static const QStringList liveExts = {
        ".m3u8", ".mp3", ".aac", ".ogg", ".opus", ".flac", ".m4a"
    };
    for (const QString &ext : liveExts) {
        if (path.endsWith(ext)) return true;
    }
    return path.isEmpty() || path == QStringLiteral("/");
}

// yt-dlp -j output is normally a few hundred KB at most. Cap it so a broken or
// malicious helper cannot grow Luma's memory without bounds.
constexpr qsizetype kMaxYtdlpStdout = 8 * 1024 * 1024;

// Title for the notification: don't show "watch" or bare URLs.
QString urlTitle(const QString &url) {
    const QUrl u(url);
    QString title = QFileInfo(u.path()).completeBaseName();
    const QString host = u.host();
    if (title.isEmpty() || title == QLatin1String("watch")
            || host.contains(QStringLiteral("youtube"), Qt::CaseInsensitive)
            || host.contains(QStringLiteral("youtu.be"), Qt::CaseInsensitive))
        title = host.isEmpty() ? url : host;
    return title;
}

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

MediaManager::MediaManager(MpvItem *mpv, QObject *parent)
: QObject(parent), m_mpv(mpv), m_playlists(nullptr)
{
    // Do not keep cover art from previous sessions; at most the current file's
    // cover is allowed to stay.
    clearCoverCache();

    m_notifyInterface = new QDBusInterface(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", QDBusConnection::sessionBus(), this
    );

    // Resolving a URL via yt-dlp must not hang forever.
    m_ytdlpTimeout = new QTimer(this);
    m_ytdlpTimeout->setSingleShot(true);
    m_ytdlpTimeout->setInterval(30000);
    m_icyReader = new IcyMetadataReader(this);
    resolveDefaultFileManagerName();
    connect(m_icyReader, &IcyMetadataReader::titleChanged,
            this, &MediaManager::onIcyTitleChanged);
    m_icyTitleTimer = new QTimer(this);
    m_icyTitleTimer->setSingleShot(true);
    connect(m_icyTitleTimer, &QTimer::timeout, this, &MediaManager::applyIcyTitle);
    connect(m_ytdlpTimeout, &QTimer::timeout, this, [this]() {
        if (!m_ytdlpProcess || m_ytdlpProcess->state() == QProcess::NotRunning) return;
        m_ytdlpTimedOut = true;
        m_ytdlpProcess->kill();
    });

    // Resume positions live in the cache directory and are bounded in size.
    m_positions.load(positionsFilePath());
    m_positionWriter = new AsyncFileWriter(this);
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(10000);
    connect(m_positionTimer, &QTimer::timeout, this, [this]() { saveCurrentPosition(); });

    // The QML engine (and with it the MpvItem) is destroyed before this
    // object, so the final position must be saved while playback is alive.
    if (QCoreApplication *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            saveCurrentPosition();
            if (m_positionWriter) m_positionWriter->waitForIdle();
        });
    }
}

MediaManager::~MediaManager() {
    delete m_chaptersDir;
    m_chaptersDir = nullptr;
    if (m_positionWriter) m_positionWriter->waitForIdle();
    if (m_positionTimer) m_positionTimer->stop();
    if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
    if (m_ytdlpProcess) {
        m_ytdlpProcess->disconnect(this);
        m_ytdlpProcess->kill();
        m_ytdlpProcess->waitForFinished(1000);
    }
}

void MediaManager::setConfig(ConfigManager *config) {
    m_config = config;
    if (config) {
        connect(config, &ConfigManager::musicCrossfadeChanged,
                this, &MediaManager::updateCrossfade, Qt::UniqueConnection);
    }
    updateCrossfade();
}

void MediaManager::setCrossfadeFilter(const QString &filter) {
    if (m_crossfadeFilter == filter) return;
    m_crossfadeFilter = filter;
    emit crossfadeFilterChanged();
}

void MediaManager::updateCrossfade() {
    const int fade = m_config ? m_config->musicCrossfade() : 0;
    const QString source = m_mpv ? m_mpv->source() : QString();
    if (!m_mpv || !m_config || !m_playlists || fade <= 0
            || source.isEmpty() || source.contains(QStringLiteral("://"))
            || !isAudioFilePath(source)) {
        m_crossfadeFadeInCurrent = false;
        m_pendingCrossfadeIn = false;
        m_crossfadeOutScheduled = false;
        setCrossfadeFilter(QString());
        return;
    }

    QStringList filters;
    if (m_crossfadeFadeInCurrent)
        filters << QStringLiteral("afade=t=in:st=0:d=%1").arg(fade);

    m_crossfadeOutScheduled = false;
    const double duration = m_mpv->duration();
    PlaylistModel *model = m_playlists->playingModel();
    int next = -1;
    if (model) {
        const int current = model->currentIndex();
        const QString mode = m_playlists->playlistModeFor(model);
        if (mode == QLatin1String("repeat")) {
            next = current;
        } else if (mode != QLatin1String("stop")) {
            next = current + 1;
            if (next >= model->rowCount())
                next = -1;
        }
    }
    const QString nextPath = next >= 0 ? model->fileAt(next) : QString();
    const bool nextIsMusic = !nextPath.isEmpty()
            && !nextPath.contains(QStringLiteral("://"))
            && isAudioFilePath(nextPath)
            && QFileInfo::exists(nextPath);
    if (nextIsMusic) {
        if (m_silenceSource != source) {
            m_silenceSource = source;
            m_silenceEffectiveEnd = -1.0;
            m_silenceTransitionTriggered = false;
            startSilenceScan(source);
        }
        // A detected silent tail shortens the fade and the effective end, so
        // the next track starts at the last audible sample.
        const double effectiveEnd = m_silenceEffectiveEnd > 0.0
            ? qMin(m_silenceEffectiveEnd, duration) : duration;
        if (effectiveEnd > fade + 0.25) {
            const double start = effectiveEnd - fade;
            filters << QStringLiteral("afade=t=out:st=%1:d=%2")
                           .arg(QString::number(start, 'f', 3))
                           .arg(fade);
            m_crossfadeOutScheduled = true;
        }
    }

    setCrossfadeFilter(filters.join(QLatin1Char(',')));
}

void MediaManager::resetSilenceState() {
    ++m_silenceGeneration;
    m_silenceSource.clear();
    m_silenceEffectiveEnd = -1.0;
    m_silenceTransitionTriggered = false;
    m_silenceLoadTimer.invalidate();
}

void MediaManager::startSilenceScan(const QString &path) {
    if (path.isEmpty())
        return;
    const quint64 generation = m_silenceGeneration;
    auto *watcher = new QFutureWatcher<double>(this);
    connect(watcher, &QFutureWatcher<double>::finished, this,
            [this, watcher, path, generation]() {
        const double end = watcher->result();
        watcher->deleteLater();
        if (generation != m_silenceGeneration)
            return;
        if (!m_mpv || m_mpv->source() != path)
            return;
        m_silenceEffectiveEnd = end;
        updateCrossfade();
        // Do not advance from here: right after a source change mpv's position
        // can still be the previous file's position, which would immediately
        // skip the new track. positionChanged() below sees the new position.
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        return detectTrailingSilenceEnd(path);
    }));
}

void MediaManager::maybeAdvancePastSilence(double position) {
    if (m_silenceEffectiveEnd <= 0.0 || m_silenceTransitionTriggered)
        return;
    if (!m_mpv || !m_config || m_config->musicCrossfade() <= 0)
        return;
    if (m_mpv->paused() || m_silenceSource != m_mpv->source())
        return;
    if (!m_silenceLoadTimer.isValid() || m_silenceLoadTimer.elapsed() < 500)
        return;
    if (!m_crossfadeOutScheduled)
        return;
    const double end = qMin(m_silenceEffectiveEnd, m_mpv->duration());
    if (end <= 0.0 || position < end - 0.02)
        return;

    // The trailing silence is what the user wants to skip: advance at the last
    // audible position, carrying the fade-in flag to the next music track.
    m_silenceTransitionTriggered = true;
    m_pendingCrossfadeIn = true;
    playNext();
}

void MediaManager::setMpv(MpvItem *mpv) {
    if (m_mpv == mpv) return;
    if (m_mpv) disconnect(m_mpv, nullptr, this, nullptr);
    m_mpv = mpv;
    if (m_mpv) {
        connect(m_mpv, &MpvItem::mediaTitleChanged, this, &MediaManager::onMediaTitleChanged);
        connect(m_mpv, &MpvItem::sourceChanged, this, [this]() { setLoading(true); });
        connect(m_mpv, &MpvItem::fileLoaded, this, &MediaManager::onMpvFileLoaded);
        connect(m_mpv, &MpvItem::fileEnded, this, [this](const QString &reason) {
            saveCurrentPosition();
            setLoading(false);
            if (reason == QLatin1String("eof") && m_crossfadeOutScheduled)
                m_pendingCrossfadeIn = true;
        });
        connect(m_mpv, &MpvItem::durationChanged, this, [this]() { updateCrossfade(); });
        connect(m_mpv, &MpvItem::positionChanged, this, [this]() {
            if (m_mpv)
                maybeAdvancePastSilence(m_mpv->position());
        });
        connect(m_mpv, &MpvItem::pausedChanged, this, [this]() {
            if (m_mpv && m_mpv->paused()) saveCurrentPosition();
        });
        connect(m_mpv, &QObject::destroyed, this, [this]() { m_mpv = nullptr; });
    }
}

QString MediaManager::ytdlpLocalDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/luma/yt-dlp");
}

QString MediaManager::ytdlpEjsLocalDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/luma/yt-dlp-ejs");
}

QString MediaManager::denoLocalDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/luma/deno");
}

QString MediaManager::localPackageVersion(const QString &dir, const QString &module) const
{
    // Wheels contain either version.py (yt-dlp) or _version.py (yt-dlp-ejs).
    // Read the declared version instead of starting a Python process.
    const QStringList candidates = {
        dir + QLatin1Char('/') + module + QStringLiteral("/version.py"),
        dir + QLatin1Char('/') + module + QStringLiteral("/_version.py"),
        dir + QLatin1Char('/') + module + QStringLiteral("/__init__.py")
    };
    static const QRegularExpression versionPattern(
        QStringLiteral("(?:__)?version(?:__)?\\s*=\\s*['\"]([^'\"]+)['\"]"));
    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        const QRegularExpressionMatch match = versionPattern.match(text);
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

void MediaManager::probeSystemYtdlpVersion(const QString &exe)
{
    if (exe.isEmpty() || m_ytdlpVersionProbeRunning)
        return;
    m_ytdlpVersionProbeRunning = true;
    auto *probe = new QProcess(this);
    connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, probe](int code, QProcess::ExitStatus status) {
                m_ytdlpVersionProbeRunning = false;
                const QString version = QString::fromUtf8(
                    probe->readAllStandardOutput()).trimmed();
                probe->deleteLater();
                if (!m_ytdlpRuntime.value(QStringLiteral("ytdlpSystem")).toBool())
                    return;
                m_ytdlpRuntime.insert(QStringLiteral("ytdlpSystemVersion"), version);
                if (status == QProcess::NormalExit && code == 0)
                    m_ytdlpRuntime.insert(QStringLiteral("ytdlpVersion"), version);
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_ytdlpVersionProbeRunning = false;
        probe->deleteLater();
    });
    probe->start(exe, {QStringLiteral("--version")});
}

void MediaManager::probeSystemEjs(const QString &python)
{
    if (python.isEmpty() || m_ejsProbeRunning)
        return;
    m_ejsProbeRunning = true;
    auto *probe = new QProcess(this);
    connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, probe](int code, QProcess::ExitStatus status) {
                m_ejsProbeRunning = false;
                const QString version = QString::fromUtf8(
                    probe->readAllStandardOutput()).trimmed();
                probe->deleteLater();
                const bool available = status == QProcess::NormalExit && code == 0;

                QVariantMap info = m_ytdlpRuntime;
                const bool local = info.value(QStringLiteral("ejsLocal")).toBool();
                info.insert(QStringLiteral("ejsSystem"), available);
                info.insert(QStringLiteral("ejsSystemVersion"), available ? version : QString());
                info.insert(QStringLiteral("ejsSource"),
                            available ? QStringLiteral("system")
                                      : (local ? QStringLiteral("local") : QString()));
                info.insert(QStringLiteral("ejsAvailable"), available || local);
                info.insert(QStringLiteral("ejsVersion"),
                            available ? version
                                      : info.value(QStringLiteral("ejsLocalVersion")).toString());
                if (available) {
                    info.insert(QStringLiteral("ejsLatestVersion"), QString());
                    info.insert(QStringLiteral("ejsUpdateAvailable"), false);
                }
                m_ytdlpRuntime = info;
                emit ytdlpRuntimeReady(m_ytdlpRuntime);

                // If the system package disappeared, the local one (when
                // present) is now the active one and can be checked for updates.
                if (!available && local && !m_ejsUpdateCheckRunning)
                    checkLocalPackageUpdate(QStringLiteral("yt-dlp-ejs"),
                                            info.value(QStringLiteral("ejsLocalVersion")).toString(),
                                            QStringLiteral("ejsLatestVersion"),
                                            QStringLiteral("ejsUpdateAvailable"),
                                            &m_ejsUpdateCheckRunning);
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_ejsProbeRunning = false;
        probe->deleteLater();
    });
    probe->start(python, {QStringLiteral("-c"),
                          QStringLiteral("import yt_dlp_ejs as e; print(getattr(e, 'version', ''))")});
}

void MediaManager::checkLocalPackageUpdate(const QString &package,
                                           const QString &localVersion,
                                           const QString &latestKey,
                                           const QString &updateKey,
                                           bool *running)
{
    if (!running || *running || localVersion.isEmpty())
        return;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        return;
    *running = true;
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, package, localVersion, latestKey, updateKey, running]
            (int code, QProcess::ExitStatus status) {
                *running = false;
                const QString latest = QString::fromUtf8(
                    proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                if (status != QProcess::NormalExit || code != 0 || latest.isEmpty())
                    return;

                const bool system = package == QLatin1String("yt-dlp")
                    ? m_ytdlpRuntime.value(QStringLiteral("ytdlpSystem")).toBool()
                    : m_ytdlpRuntime.value(QStringLiteral("ejsSystem")).toBool();
                QVariantMap info = m_ytdlpRuntime;
                if (system) {
                    info.insert(latestKey, QString());
                    info.insert(updateKey, false);
                    m_ytdlpRuntime = info;
                    emit ytdlpRuntimeReady(m_ytdlpRuntime);
                    return;
                }

                const QVersionNumber current = QVersionNumber::fromString(localVersion);
                const QVersionNumber newest = QVersionNumber::fromString(latest);
                const bool newer = !current.isNull() && !newest.isNull()
                    ? newest > current : latest != localVersion;
                info.insert(latestKey, latest);
                info.insert(updateKey, newer);
                if (newer) {
                    qInfo().noquote() << "yt-dlp update available:"
                                      << package << localVersion << "->" << latest;
                }
                m_ytdlpRuntime = info;
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, running](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        *running = false;
        proc->deleteLater();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"),
                        QStringLiteral("import json, sys, urllib.request\n"
                                       "with urllib.request.urlopen("
                                       "\"https://pypi.org/pypi/%s/json\" % sys.argv[1], timeout=20) as r:\n"
                                       "    print(json.load(r)[\"info\"][\"version\"])"),
                        package});
    proc->start();
}

void MediaManager::probeDenoVersion(const QString &exe)
{
    if (exe.isEmpty() || m_denoVersionProbeRunning)
        return;
    m_denoVersionProbeRunning = true;
    auto *probe = new QProcess(this);
    connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, probe, exe](int code, QProcess::ExitStatus status) {
                m_denoVersionProbeRunning = false;
                const QString firstLine = QString::fromUtf8(
                    probe->readAllStandardOutput()).split(QLatin1Char('\n')).value(0).trimmed();
                probe->deleteLater();
                if (m_ytdlpRuntime.value(QStringLiteral("denoPath")).toString() != exe)
                    return;
                const QString version = status == QProcess::NormalExit && code == 0
                    ? firstLine.section(QLatin1Char(' '), 1, 1).trimmed() : QString();
                m_ytdlpRuntime.insert(QStringLiteral("denoVersion"), version);
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
                if (!version.isEmpty()
                        && !m_ytdlpRuntime.value(QStringLiteral("denoSystem")).toBool())
                    checkDenoUpdate(version);
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_denoVersionProbeRunning = false;
        probe->deleteLater();
    });
    probe->start(exe, {QStringLiteral("--version")});
}

void MediaManager::checkDenoUpdate(const QString &localVersion)
{
    if (m_denoUpdateCheckRunning || localVersion.isEmpty())
        return;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        return;
    m_denoUpdateCheckRunning = true;
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, localVersion](int code, QProcess::ExitStatus status) {
                m_denoUpdateCheckRunning = false;
                const QString latest = QString::fromUtf8(
                    proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                if (status != QProcess::NormalExit || code != 0 || latest.isEmpty())
                    return;
                QVariantMap info = m_ytdlpRuntime;
                if (info.value(QStringLiteral("denoSystem")).toBool()) {
                    info.insert(QStringLiteral("denoLatestVersion"), QString());
                    info.insert(QStringLiteral("denoUpdateAvailable"), false);
                    m_ytdlpRuntime = info;
                    emit ytdlpRuntimeReady(m_ytdlpRuntime);
                    return;
                }
                const QVersionNumber current = QVersionNumber::fromString(localVersion);
                const QVersionNumber newest = QVersionNumber::fromString(latest);
                const bool newer = !current.isNull() && !newest.isNull()
                    ? newest > current : latest != localVersion;
                info.insert(QStringLiteral("denoLatestVersion"), latest);
                info.insert(QStringLiteral("denoUpdateAvailable"), newer);
                m_ytdlpRuntime = info;
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_denoUpdateCheckRunning = false;
        proc->deleteLater();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"),
                        QStringLiteral("import json, urllib.request\n"
                                       "with urllib.request.urlopen("
                                       "\"https://api.github.com/repos/denoland/deno/releases/latest\", "
                                       "timeout=20) as r:\n"
                                       "    print(json.load(r)[\"tag_name\"].lstrip(\"v\"))")});
    proc->start();
}

void MediaManager::refreshYtdlpRuntime()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString systemYtdlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    const QString ytdlpDir = ytdlpLocalDir();
    const QString ejsDir = ytdlpEjsLocalDir();
    const bool localYtdlp = QFileInfo(ytdlpDir + QStringLiteral("/yt_dlp/__init__.py")).exists();
    const bool localEjs = QFileInfo(ejsDir + QStringLiteral("/yt_dlp_ejs/__init__.py")).exists();
    const QString systemDeno = QStandardPaths::findExecutable(QStringLiteral("deno"));
    const QString denoDir = denoLocalDir();
    const QString localDenoPath = denoDir + QStringLiteral("/deno");
    const bool localDeno = QFileInfo(localDenoPath).isExecutable();
    const QString denoPath = !systemDeno.isEmpty()
        ? systemDeno : (localDeno ? localDenoPath : QString());
    const bool denoSystem = !systemDeno.isEmpty();
    const QString previousDenoPath = m_ytdlpRuntime.value(QStringLiteral("denoPath")).toString();
    const QString denoVersion = previousDenoPath == denoPath
        ? m_ytdlpRuntime.value(QStringLiteral("denoVersion")).toString() : QString();

    // Keep the previous probe result until the new one arrives, so the status
    // in the dialog does not flicker while Python starts.
    const bool systemEjs = !python.isEmpty()
        && m_ytdlpRuntime.value(QStringLiteral("ejsSystem")).toBool();
    const QString systemYtdlpVersion = systemYtdlp.isEmpty()
        ? QString() : m_ytdlpRuntime.value(QStringLiteral("ytdlpSystemVersion")).toString();
    const QString systemEjsVersion = systemEjs
        ? m_ytdlpRuntime.value(QStringLiteral("ejsSystemVersion")).toString() : QString();
    const QString localYtdlpVersion = localPackageVersion(ytdlpDir, QStringLiteral("yt_dlp"));
    const QString localEjsVersion = localPackageVersion(ejsDir, QStringLiteral("yt_dlp_ejs"));

    const QString ytdlpSource = !systemYtdlp.isEmpty()
        ? QStringLiteral("system") : (localYtdlp ? QStringLiteral("local") : QString());
    const QString ejsSource = systemEjs
        ? QStringLiteral("system") : (localEjs ? QStringLiteral("local") : QString());

    QVariantMap info;
    info.insert(QStringLiteral("ytdlp"), !systemYtdlp.isEmpty() || localYtdlp);
    info.insert(QStringLiteral("ytdlpLocal"), localYtdlp);
    info.insert(QStringLiteral("ytdlpSystem"), !systemYtdlp.isEmpty());
    info.insert(QStringLiteral("ytdlpPath"),
                !systemYtdlp.isEmpty() ? systemYtdlp : (localYtdlp ? ytdlpDir : QString()));
    info.insert(QStringLiteral("ytdlpSource"), ytdlpSource);
    info.insert(QStringLiteral("ytdlpVersion"),
                !systemYtdlp.isEmpty() ? systemYtdlpVersion : localYtdlpVersion);
    info.insert(QStringLiteral("ytdlpLocalVersion"), localYtdlpVersion);
    info.insert(QStringLiteral("ytdlpSystemVersion"), systemYtdlpVersion);
    info.insert(QStringLiteral("ytdlpLatestVersion"), QString());
    info.insert(QStringLiteral("ytdlpUpdateAvailable"), false);

    info.insert(QStringLiteral("ejsLocal"), localEjs);
    info.insert(QStringLiteral("ejsSystem"), systemEjs);
    info.insert(QStringLiteral("ejsAvailable"), systemEjs || localEjs);
    info.insert(QStringLiteral("ejsSource"), ejsSource);
    info.insert(QStringLiteral("ejsVersion"),
                systemEjs ? systemEjsVersion : localEjsVersion);
    info.insert(QStringLiteral("ejsLocalVersion"), localEjsVersion);
    info.insert(QStringLiteral("ejsSystemVersion"), systemEjsVersion);
    info.insert(QStringLiteral("ejsLatestVersion"), QString());
    info.insert(QStringLiteral("ejsUpdateAvailable"), false);
    info.insert(QStringLiteral("ejsLocalDir"), ejsDir);
    info.insert(QStringLiteral("deno"), !denoPath.isEmpty());
    info.insert(QStringLiteral("denoPath"), denoPath);
    info.insert(QStringLiteral("denoSystem"), denoSystem);
    info.insert(QStringLiteral("denoSource"),
                denoSystem ? QStringLiteral("system")
                           : (localDeno ? QStringLiteral("local") : QString()));
    info.insert(QStringLiteral("denoVersion"), denoVersion);
    info.insert(QStringLiteral("denoLocalDir"), denoDir);
    info.insert(QStringLiteral("denoLatestVersion"), QString());
    info.insert(QStringLiteral("denoUpdateAvailable"), false);
    info.insert(QStringLiteral("python"), python);
    m_ytdlpRuntime = info;
    emit ytdlpRuntimeReady(m_ytdlpRuntime);

    if (!systemYtdlp.isEmpty())
        probeSystemYtdlpVersion(systemYtdlp);
    if (!python.isEmpty())
        probeSystemEjs(python);
    if (!denoPath.isEmpty())
        probeDenoVersion(denoPath);

    // Updates are checked only for our local copies, and only while no system
    // package of the same kind is installed.
    if (systemYtdlp.isEmpty() && localYtdlp)
        checkLocalPackageUpdate(QStringLiteral("yt-dlp"), localYtdlpVersion,
                                QStringLiteral("ytdlpLatestVersion"),
                                QStringLiteral("ytdlpUpdateAvailable"),
                                &m_ytdlpUpdateCheckRunning);
    if (!systemEjs && localEjs)
        checkLocalPackageUpdate(QStringLiteral("yt-dlp-ejs"), localEjsVersion,
                                QStringLiteral("ejsLatestVersion"),
                                QStringLiteral("ejsUpdateAvailable"),
                                &m_ejsUpdateCheckRunning);
}

void MediaManager::runWheelInstall(const QString &package, const QString &dir,
                                   std::function<void(bool, const QString &)> done)
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        done(false, tr("ytdlp_python_missing"));
        return;
    }
    const QString parent = QFileInfo(dir).absolutePath();
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        done(false, tr("ytdlp_install_dir_failed").arg(parent));
        return;
    }

    // Install the pure-Python wheel without pip. pip is not part of a Python
    // installation on many distributions, and installing into the system is
    // explicitly not wanted: the files only go to Luma's own data directory.
    const QString script = QStringLiteral(R"PY(
import json, os, shutil, sys, tempfile, urllib.request, zipfile
pkg, target = sys.argv[1], sys.argv[2]
request = urllib.request.Request("https://pypi.org/pypi/%s/json" % pkg,
                                 headers={"User-Agent": "Luma"})
with urllib.request.urlopen(request, timeout=30) as response:
    info = json.load(response)
version = info["info"]["version"]
wheel = next((u for u in info["urls"] if u["packagetype"] == "bdist_wheel"), None)
if wheel is None:
    raise RuntimeError("no wheel for " + pkg)
tmp = tempfile.mkdtemp(prefix="luma-wheel-")
stage = target + ".new"
try:
    archive = os.path.join(tmp, wheel["filename"])
    with urllib.request.urlopen(wheel["url"], timeout=120) as response, open(archive, "wb") as out:
        shutil.copyfileobj(response, out)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    with zipfile.ZipFile(archive) as z:
        z.extractall(stage)
    shutil.rmtree(target, ignore_errors=True)
    os.replace(stage, target)
    print(version)
except Exception:
    shutil.rmtree(stage, ignore_errors=True)
    raise
finally:
    shutil.rmtree(tmp, ignore_errors=True)
)PY");

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, done](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(
                    proc->readAllStandardOutput() + proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                done(status == QProcess::NormalExit && code == 0,
                     output.isEmpty() ? QString() : output.right(1000));
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        done(false, MediaManager::tr("ytdlp_install_start_failed"));
    });
    QTimer::singleShot(180000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"), script, package, dir});
    proc->start();
}

void MediaManager::installYtdlp()
{
    runWheelInstall(QStringLiteral("yt-dlp"), ytdlpLocalDir(),
                    [this](bool ok, const QString &message) {
                        if (ok)
                            refreshYtdlpRuntime();
                        emit ytdlpInstallFinished(ok, message);
                    });
}

void MediaManager::installYtdlpEjs()
{
    runWheelInstall(QStringLiteral("yt-dlp-ejs"), ytdlpEjsLocalDir(),
                    [this](bool ok, const QString &message) {
                        if (ok)
                            refreshYtdlpRuntime();
                        emit ytdlpEjsInstallFinished(ok, message);
                    });
}

void MediaManager::runDenoInstall(std::function<void(bool, const QString &)> done)
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        done(false, tr("ytdlp_python_missing"));
        return;
    }
    const QString cpu = QSysInfo::currentCpuArchitecture();
    QString arch;
    if (cpu == QLatin1String("x86_64"))
        arch = QStringLiteral("x86_64");
    else if (cpu == QLatin1String("arm64") || cpu == QLatin1String("aarch64"))
        arch = QStringLiteral("aarch64");
    else {
        done(false, tr("ytdlp_deno_arch_unsupported").arg(cpu));
        return;
    }
    const QString dir = denoLocalDir();
    const QString parent = QFileInfo(dir).absolutePath();
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        done(false, tr("ytdlp_install_dir_failed").arg(parent));
        return;
    }

    const QString script = QStringLiteral(R"PY(
import os, shutil, sys, tempfile, urllib.request, zipfile
target, arch = sys.argv[1], sys.argv[2]
url = ("https://github.com/denoland/deno/releases/latest/download/"
       "deno-%s-unknown-linux-gnu.zip" % arch)
tmp = tempfile.mkdtemp(prefix="luma-deno-")
stage = target + ".new"
try:
    archive = os.path.join(tmp, "deno.zip")
    with urllib.request.urlopen(url, timeout=180) as response, open(archive, "wb") as out:
        shutil.copyfileobj(response, out)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    with zipfile.ZipFile(archive) as z:
        z.extractall(stage)
    binary = os.path.join(stage, "deno")
    if not os.path.isfile(binary):
        raise RuntimeError("deno binary is missing from the archive")
    os.chmod(binary, 0o755)
    shutil.rmtree(target, ignore_errors=True)
    os.replace(stage, target)
except Exception:
    shutil.rmtree(stage, ignore_errors=True)
    raise
finally:
    shutil.rmtree(tmp, ignore_errors=True)
)PY");

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, done](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(
                    proc->readAllStandardOutput() + proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                done(status == QProcess::NormalExit && code == 0,
                     output.isEmpty() ? QString() : output.right(1000));
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        done(false, MediaManager::tr("ytdlp_install_start_failed"));
    });
    QTimer::singleShot(300000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"), script, dir, arch});
    proc->start();
}

void MediaManager::installDeno()
{
    runDenoInstall([this](bool ok, const QString &message) {
        if (ok)
            refreshYtdlpRuntime();
        emit denoInstallFinished(ok, message);
    });
}

void MediaManager::setLiveStream(bool live) {
    if (m_liveStream == live) return;
    m_liveStream = live;
    emit liveStreamChanged();
}

void MediaManager::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    emit loadingChanged();
}

void MediaManager::setHasMedia(bool hasMedia) {
    if (m_hasMedia == hasMedia) return;
    m_hasMedia = hasMedia;
    emit hasMediaChanged();
}

void MediaManager::onMediaTitleChanged() {
    if (!m_mpv) return;

    const QString source = m_mpv->source();
    if (!source.startsWith(QStringLiteral("http://")) && !source.startsWith(QStringLiteral("https://")))
        return;

    // For URLs resolved by yt-dlp, the notification has already been sent with
    // the original URL; media-title arrives as a googlevideo fragment and is not needed.
    if (m_streamFromYtdlp)
        return;

    const QString title = m_mpv->mediaTitle().trimmed();
    if (title.isEmpty() || title == m_lastStreamTitle) return;
    // For direct streams mpv often reports the URL (or just host:port) as the
    // title — don't show it in the playlist or notifications.
    const QUrl sourceUrl(source);
    if (title.startsWith(QStringLiteral("http://")) || title.startsWith(QStringLiteral("https://"))
            || title == sourceUrl.toString()
            || title == sourceUrl.authority()
            || title == sourceUrl.host()
            || title == sourceUrl.path())
        return;
    m_lastStreamTitle = title;

    // In the playlist, show the current tag instead of the URL.
    if (m_playlists) {
        PlaylistModel *model = m_playlists->activeModel();
        if (model) model->setStreamTitle(source, title);
    }

    if (m_config && m_config->notifications()) {
        const QUrl url(source);
        _sendNotification(title, url.host(), QString());
    }
}

void MediaManager::onIcyTitleChanged(const QString &title)
{
    if (title.isEmpty() || title == m_lastStreamTitle || title == m_pendingIcyTitle)
        return;

    // The reader sees the live ICY title, while mpv plays behind its forward
    // cache. Delay the playlist/notification update by that buffer so the title
    // changes when the song actually starts.
    double delay = 0.0;
    if (m_mpv) {
        const double cacheEnd = m_mpv->observedProperty("demuxer-cache-time").toDouble();
        const double position = m_mpv->position();
        if (cacheEnd > position && cacheEnd - position < 120.0)
            delay = cacheEnd - position;
    }
    m_pendingIcyTitle = title;
    if (!m_icyTitleTimer) return;
    m_icyTitleTimer->start(qRound(delay * 1000.0));
}

void MediaManager::applyIcyTitle()
{
    const QString title = m_pendingIcyTitle;
    m_pendingIcyTitle.clear();
    if (title.isEmpty() || title == m_lastStreamTitle) return;
    m_lastStreamTitle = title;

    if (m_playlists) {
        PlaylistModel *model = m_playlists->activeModel();
        if (model && m_mpv)
            model->setStreamTitle(m_mpv->source(), title);
    }

    if (m_config && m_config->notifications() && m_mpv) {
        const QUrl url(m_mpv->source());
        _sendNotification(title, url.host(), QString());
    }
}

PlaylistModel* MediaManager::playlist() const {
    return m_playlists ? m_playlists->activeModel() : nullptr;
}

void MediaManager::setPlaylistManager(PlaylistManager *pm) {
    if (m_playlists == pm) return;
    if (m_playlists) m_playlists->disconnect(this);
    m_playlists = pm;
    if (m_playlists) {
        // Each playlist has its own playback mode: follow the active one.
        connect(m_playlists, &PlaylistManager::activeModelChanged, this, [this]() {
            if (!m_playlists) return;
            const QString mode = m_playlists->playlistMode(m_playlists->activeId());
            if (m_playMode == mode) return;
            m_playMode = mode;
            emit playModeChanged();
        });
    }
    emit playlistChanged();
}

void MediaManager::setManualSwitch(bool val) {
    if (m_isManualSwitch != val) { m_isManualSwitch = val; emit isManualSwitchChanged(); }
}

void MediaManager::setPlayMode(const QString &mode) {
    static const QStringList modes = {
        QStringLiteral("sequential"), QStringLiteral("repeat"),
        QStringLiteral("shuffle"), QStringLiteral("stop")
    };
    const QString normalized = modes.contains(mode) ? mode : QStringLiteral("sequential");
    if (m_playMode == normalized) return;
    m_playMode = normalized;

    if (m_playlists) {
        // The settings only define the Default playlist's mode; every named
        // playlist stores its own mode in its playlist file.
        m_playlists->setPlaylistMode(m_playlists->activeId(), normalized);

        // Shuffling reorders the entries, so only do it for the Default
        // playlist: a named playlist's saved order must not be destroyed just
        // by selecting a playback mode.
        if (m_playlists->activeId() == PlaylistManager::DEFAULT_ID) {
            if (PlaylistModel *def = m_playlists->defaultModel())
                def->setShuffle(normalized == QLatin1String("shuffle"));
        }
    }

    emit playModeChanged();
}

void MediaManager::setAspectRatio(const QString &mode) {
    if (!m_mpv) return;
    if (mode == "original") {
        m_mpv->setProperty("video-aspect-override", "no");
    } else {
        m_mpv->setProperty("video-aspect-override", mode);
    }
}

QString MediaManager::getAspectRatio() const {
    if (!m_mpv) return "original";
    // Observed by MpvItem: reading it synchronously here could block while mpv
    // is opening a network stream.
    QVariant val = m_mpv->observedProperty("video-aspect-override");
    if (!val.isValid() || val.isNull()) return "original";
    if (val.typeId() == QMetaType::QString) {
        QString str = val.toString().trimmed();
        if (str == "no" || str.isEmpty()) return "original";
        return str;
    }
    if (val.canConvert<double>()) {
        double ratio = val.toDouble();
        if (ratio <= 0.0) return "original";
        if (qAbs(ratio - 1.777778) < 0.01) return "16:9";
        if (qAbs(ratio - 1.333333) < 0.01) return "4:3";
        if (qAbs(ratio - 2.333333) < 0.01) return "21:9";
        if (qAbs(ratio - 1.0) < 0.01)      return "1:1";
        if (qAbs(ratio - 0.5625) < 0.01)   return "9:16";
        return QString::number(ratio, 'f', 2);
    }
    return "original";
}

QString MediaManager::currentFileName() const {
    if (!m_mpv) return "";
    QString path = m_mpv->source();
    if (path.isEmpty()) return "";
    return QFileInfo(path).fileName();
}

bool MediaManager::hasVideo() const {
    if (!m_mpv) return false;

    // All three properties are observed by MpvItem. Using the cached values
    // avoids a blocking round-trip to the mpv thread on every menu/hotkey.
    const QString codec = m_mpv->observedProperty("video-codec").toString().trimmed();
    if (codec.isEmpty() || codec.compare(QStringLiteral("no"), Qt::CaseInsensitive) == 0)
        return false;

    auto flag = [this](const char *name) {
        const QVariant value = m_mpv->observedProperty(name);
        if (!value.isValid()) return false;
        if (value.toBool()) return true;
        const QString text = value.toString().trimmed().toLower();
        return text == QLatin1String("yes") || text == QLatin1String("true");
    };

    // Audio covers are exposed as image/album-art video tracks.
    return !flag("current-tracks/video/image")
        && !flag("current-tracks/video/albumart");
}

void MediaManager::_sendNotification(const QString &body) {
    _sendNotification(QString(), body, QString());
}

void MediaManager::_sendNotification(const QString &summary, const QString &body, const QString &coverPath) {
    if (m_config && !m_config->notifications()) return;
    if (!m_notifyInterface || !m_notifyInterface->isValid()) return;

    QVariantMap hints { { QStringLiteral("desktop-entry"), QStringLiteral("luma") } };
    if (!coverPath.isEmpty()) hints.insert(QStringLiteral("image-path"), coverPath);

    m_notifyInterface->asyncCall("Notify", "Luma", (uint)0, "luma", summary, body, QStringList(),
                                 hints, (int)-1);
}

void MediaManager::_notifyForFile(const QString &path) {
    if (m_config && !m_config->notifications()) return;
    if (!m_notifyInterface || !m_notifyInterface->isValid()) return;

    // A new file invalidates a notification that is still being prepared, and
    // the previous file's cover is no longer needed.
    const quint64 generation = ++m_notifyGeneration;
    clearCoverCache();

    // For URLs, wait for the ICY tag from onMediaTitleChanged — don't read tags over the network.
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return;

    // Reading tags opens the file with FFmpeg (and may decode cover art), so
    // keep it off the GUI thread. The worker captures only the path, never
    // `this`.
    auto *watcher = new QFutureWatcher<TrackTagsResult>(this);
    connect(watcher, &QFutureWatcher<TrackTagsResult>::finished, this,
            [this, watcher, path, generation]() {
        const TrackTagsResult result = watcher->result();
        watcher->deleteLater();

        if (generation != m_notifyGeneration) return;
        if (m_config && !m_config->notifications()) return;
        if (!m_notifyInterface || !m_notifyInterface->isValid()) return;

        if (!result.ok) {
            // Not audio with tags — as before, but without the file extension.
            _sendNotification(QFileInfo(path).completeBaseName());
            return;
        }

        const TrackTags &tags = result.tags;
        QString summary = tags.title;
        if (summary.isEmpty()) summary = QFileInfo(path).completeBaseName();

        // Artist and album go on separate lines.
        QStringList details;
        if (!tags.artist.isEmpty()) details << tags.artist;
        if (!tags.album.isEmpty()) details << tags.album;

        _sendNotification(summary, details.join(QLatin1Char('\n')), tags.coverPath);
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        TrackTagsResult result;
        result.ok = readTrackTags(path, result.tags);
        return result;
    }));
}

namespace {
// Runs on a worker thread: xdg-mime can wait for the desktop database and the
// first call used to stall the GUI for up to a second when the track menu was
// built.
QString lookupDefaultFileManagerName()
{
    QProcess process;
    process.start("xdg-mime", QStringList() << "query" << "default" << "inode/directory");
    process.waitForFinished(1000);

    // Only a desktop-file name is expected; never follow a path from the tool.
    const QString desktopFile = QFileInfo(
        QString::fromUtf8(process.readAllStandardOutput()).trimmed()).fileName();
    QString result;
    if (desktopFile.isEmpty()) {
        result = QStringLiteral("File Manager");
    } else {
        QStringList searchPaths = {
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
            "/usr/share/applications",
            "/usr/local/share/applications"
        };

        for (const QString &path : searchPaths) {
            QFileInfo fi(path + "/" + desktopFile);
            if (!fi.exists()) continue;
            QSettings desktop(fi.absoluteFilePath(), QSettings::IniFormat);
            desktop.beginGroup("Desktop Entry");
            const QString name = desktop.value("Name[" + QLocale::system().name() + "]",
                                               desktop.value("Name")).toString();
            if (!name.isEmpty()) {
                result = name;
                break;
            }
        }
        if (result.isEmpty()) result = QString(desktopFile).remove(".desktop");
    }
    return result;
}
} // namespace

void MediaManager::resolveDefaultFileManagerName()
{
    if (m_fileManagerLookupStarted) return;
    m_fileManagerLookupStarted = true;

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        const QString name = watcher->result();
        watcher->deleteLater();
        if (name.isEmpty() || name == m_defaultFileManagerName) return;
        m_defaultFileManagerName = name;
        emit defaultFileManagerNameChanged();
    });
    watcher->setFuture(QtConcurrent::run(&lookupDefaultFileManagerName));
}

void MediaManager::requestFileInfo(const QString &path) {
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher, path]() {
        const QVariantMap info = watcher->result();
        watcher->deleteLater();
        emit fileInfoReady(path, info);
    });
    watcher->setFuture(QtConcurrent::run([path]() { return buildFileInfo(path); }));
}

void MediaManager::requestLiveBitrate(const QString &path, double position) {
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

QVariantMap MediaManager::readTags(const QString &path) {
    return TagManager::read(path);
}

bool MediaManager::saveTags(const QString &path, const QVariantMap &tags) {
    if (path.isEmpty()) return false;
    return TagManager::write(path, tags);
}

void MediaManager::requestSaveTags(const QString &path, const QVariantMap &tags) {
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

bool MediaManager::updateStreamUrl(const QString &oldUrl, const QString &newUrl) {
    const QString url = newUrl.trimmed();
    if (url.isEmpty() || !m_playlists) return false;

    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return false;

    const int index = model->indexOf(oldUrl);
    int index2 = index;
    if (index2 < 0) {
        // The URL may differ by a trailing "/" — look for the closest matching entry.
        for (int i = 0; i < model->rowCount(); ++i) {
            const QString p = model->fileAt(i);
            if (p.isEmpty() || oldUrl.isEmpty()) continue;
            QString a = p; while (a.endsWith('/')) a.chop(1);
            QString b = oldUrl; while (b.endsWith('/')) b.chop(1);
            if (a == b) { index2 = i; break; }
        }
    }
    if (index2 < 0) return false;
    const int streamIndex = index2;

    const bool wasCurrent = (model->currentIndex() == streamIndex);
    QVariantMap entry;
    entry["path"] = url;
    entry["kind"] = "audio";
    model->removeItem(streamIndex);
    model->appendEntry(entry);

    // Fetch the title for the new link: after a broken URL was corrected the
    // entry used to stay without a title forever (the old title belonged to
    // the previous URL). This also reports failures instead of silently
    // keeping a stale/empty title.
    if (url.startsWith(QStringLiteral("http://")) || url.startsWith(QStringLiteral("https://")))
        m_playlists->fetchUrlTitle(m_playlists->activeId(), url);

    if (wasCurrent && m_mpv) {
        preparePlaybackPosition(url, m_liveStream);
        m_mpv->setSource(url, m_liveStream);
    }
    return true;
}

void MediaManager::openInFileManager(const QString &path) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;

    QString folderPath = fi.absolutePath();
    QProcess process;
    process.setProgram("xdg-open");
    process.setArguments({folderPath});
    if (!process.startDetached()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    }
}

void MediaManager::copyToClipboard(const QString &text) {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

bool MediaManager::isFileAvailable(const QString &path) const {
    if (path.isEmpty()) return false;
    // Network streams have no local file to stat; the information dialog still
    // works for them (it edits the URL).
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return true;
    return QFileInfo::exists(path);
}

void MediaManager::removeFromPlaylist(int index) {
    if (!m_playlists) return;
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    if (index == model->currentIndex()) {
        int nextIndex = model->currentIndex();
        model->removeItem(index);

        if (nextIndex >= 0 && nextIndex < model->rowCount()) {
            playIndex(nextIndex);
        } else {
            if (m_mpv) m_mpv->command(QStringList() << "stop");
        }
    } else {
        model->removeItem(index);
    }

    // PlaylistModel::removeItem() emits modified, so the named playlist is
    // autosaved in the background; no blocking save is needed here.
}

void MediaManager::addToPlaylist(const QString &path) {
    if (!m_mpv || !m_playlists) return;

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) return;

    // Explicit add goes to the ACTIVE playlist
    QString id = m_playlists->activeId();
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    int existingIndex = model->indexOf(cleanPath);
    if (existingIndex >= 0) {
        playIndex(existingIndex);
        return;
    }

    m_playlists->addFilesToPlaylist(id, {cleanPath});

    int newIdx = model->indexOf(cleanPath);
    if (newIdx >= 0 && model->currentIndex() < 0) {
        playIndex(newIdx);
    }
}

void MediaManager::openFile(const QString &path) {
    if (!m_mpv || !m_playlists) return;

    // URLs go to Default
    if (path.startsWith("http://") || path.startsWith("https://")) {
        ++m_folderScanGeneration;
        clearPendingUrlPlayback();
        if (isDirectStreamUrl(QUrl(path))) playResolvedUrl(path, path);
        else openUrlAsync(path);
        return;
    }

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) {
        emit fileUnavailable(cleanPath);
        return;
    }

    PlaylistModel *def = m_playlists->defaultModel();
    if (!def) return;

    // Append mode: add to Default
    if (m_config && m_config->appendToPlaylist() && def->rowCount() > 0) {
        m_playlists->addFilesToPlaylist(PlaylistManager::DEFAULT_ID, {cleanPath});
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        int idx = def->indexOf(cleanPath);
        if (idx >= 0) playIndex(idx);
        return;
    }

    if (def->currentFile() == cleanPath) return;

    int existingIndex = def->indexOf(cleanPath);
    if (existingIndex >= 0) {
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        playIndex(existingIndex);
        return;
    }

    // Play the opened file immediately. The other videos in its folder are
    // scanned on a worker thread and merged into the playlist afterwards, so
    // a folder with thousands of files cannot delay the first frame.
    const quint64 generation = ++m_folderScanGeneration;
    def->setFiles({cleanPath}, cleanPath);
    if (m_playlists) m_playlists->scanAudioPaths({cleanPath});
    if (m_playMode == "shuffle") def->setShuffle(true);

    if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
        m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    playIndex(def->currentIndex());
    scanFolderForPlaylist(cleanPath, generation);
}

void MediaManager::scanFolderForPlaylist(const QString &filePath, quint64 generation)
{
    auto *watcher = new QFutureWatcher<QVector<PlaylistModel::Entry>>(this);
    connect(watcher, &QFutureWatcher<QVector<PlaylistModel::Entry>>::finished, this,
            [this, watcher, filePath, generation]() {
        QVector<PlaylistModel::Entry> entries = watcher->result();
        watcher->deleteLater();
        if (generation != m_folderScanGeneration || !m_playlists)
            return;

        PlaylistModel *def = m_playlists->defaultModel();
        // Do not touch the playlist if the user changed it while the scan was
        // running; merging then could discard those changes.
        if (!def || def->count() != 1 || def->fileAt(0) != filePath)
            return;
        if (entries.size() < 2)
            return;

        def->setPreparedEntries(std::move(entries), filePath);
        if (m_playMode == "shuffle")
            def->setShuffle(true);
        m_playlists->scanAudioPaths({filePath});
    });

    watcher->setFuture(QtConcurrent::run([filePath]() {
        QVector<PlaylistModel::Entry> entries;
        const QFileInfo fi(filePath);
        QDir dir(fi.absolutePath());
        QStringList paths;
        for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"},
                                              QDir::Files, QDir::Name)) {
            paths << dir.absoluteFilePath(f);
        }
        if (!paths.contains(filePath)) paths.prepend(filePath);

        entries.reserve(paths.size());
        for (const QString &path : paths) {
            PlaylistModel::Entry entry;
            entry.path = path;
            entry.kind = QStringLiteral("video");
            entry.title = QFileInfo(path).completeBaseName();
            entries.append(entry);
        }
        return entries;
    }));
}

void MediaManager::openFolder(const QString &folder) {
    if (!m_mpv || !m_playlists) return;

    ++m_folderScanGeneration;
    QDir dir(folder);
    QStringList files;
    // Audio formats added
    QStringList filters = {
        "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov",
        "*.mp3", "*.flac", "*.aac", "*.wav", "*.ogg", "*.m4a", "*.opus"
    };

    for (const QString &f : dir.entryList(filters, QDir::Files, QDir::Name)) {
        files << dir.absoluteFilePath(f);
    }

    if (files.isEmpty()) return;

    PlaylistModel *def = m_playlists->defaultModel();
    if (!def) return;

    if (m_config && m_config->appendToPlaylist() && def->rowCount() > 0) {
        m_playlists->addFolderToPlaylist(PlaylistManager::DEFAULT_ID, folder);
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        int idx = def->indexOf(files.first());
        if (idx >= 0) playIndex(idx);
    } else {
        def->setFiles(files, files.first());
        if (m_playlists) m_playlists->scanAudioPaths(files);
        if (m_playMode == "shuffle") def->setShuffle(true);
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        playIndex(def->currentIndex());
    }
}


void MediaManager::openUrlAsync(const QString &url, bool keepPlaylist) {
    if (!m_mpv || !m_playlists) return;
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();
    setLoading(true);

    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString systemYtdlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    const bool localYtdlp = QFileInfo(ytdlpLocalDir()
                                      + QStringLiteral("/yt_dlp/__init__.py")).exists();
    const bool localEjs = QFileInfo(ytdlpEjsLocalDir()
                                    + QStringLiteral("/yt_dlp_ejs/__init__.py")).exists();
    const bool systemEjs = m_ytdlpRuntime.value(QStringLiteral("ejsSystem")).toBool();
    QString exe;
    QStringList runtimePrefix;
    // System packages always win over Luma's own copies.
    if (!systemYtdlp.isEmpty()) {
        exe = systemYtdlp;
    } else if (localYtdlp && !python.isEmpty()) {
        exe = python;
        runtimePrefix << QStringLiteral("-m") << QStringLiteral("yt_dlp");
    }
    if (exe.isEmpty()) {
        // yt-dlp is not installed — hand the URL to mpv (it has its own ytdl).
        playStream(url, url, QString(), QString(), keepPlaylist, false);
        return;
    }

    if (m_ytdlpProcess) {
        m_ytdlpProcess->disconnect(this);
        m_ytdlpProcess->kill();
        // Opening another link used to block the GUI for up to one second
        // here. SIGKILL is enough; the process object is deleted afterwards.
        m_ytdlpProcess->waitForFinished(100);
        m_ytdlpProcess->deleteLater();
        m_ytdlpProcess = nullptr;
    }
    if (m_ytdlpTimeout) m_ytdlpTimeout->stop();

    m_resolvedTitle.clear();
    m_resolvedVideoUrl.clear();
    m_resolvedAudioUrl.clear();
    m_resolvedChapters.clear();
    m_resolvedDuration = 0.0;
    m_resolvedIsLive = false;
    m_ytdlpStdoutBuffer.clear();
    m_ytdlpStderr.clear();
    m_ytdlpTimedOut = false;

    const QString quality = m_config ? m_config->ytdlpQuality() : QStringLiteral("best");
    QString formatExpr;
    if (quality == QLatin1String("audio")) {
        formatExpr = QStringLiteral("bestaudio[ext=m4a]/bestaudio");
    } else if (quality.isEmpty() || quality == QLatin1String("best")) {
        formatExpr = QStringLiteral("bestvideo+bestaudio/best");
    } else {
        const int height = quality.toInt();
        formatExpr = QStringLiteral("bestvideo[height<=%1]+bestaudio/best[height<=%1]").arg(height);
    }

    auto *proc = new QProcess(this);
    m_ytdlpProcess = proc;
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    const QString ytdlpLogMode = Logger::instance().componentMode(QStringLiteral("yt-dlp"));
    if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
        Logger::instance().writeComponent(QStringLiteral("yt-dlp"),
                                          QStringLiteral("yt-dlp %1").arg(url));

    QStringList args = m_config ? m_config->ytdlpConfigArgs() : QStringList();

    // YouTube's bot check needs the JavaScript challenge solved. yt-dlp uses
    // Deno for that; enable it explicitly when available and the user has not
    // chosen another runtime. No browser cookies are involved.
    bool hasJsRuntime = false;
    for (const QString &arg : std::as_const(args)) {
        if (arg == QLatin1String("--no-js-runtimes")
                || arg == QLatin1String("--js-runtimes")
                || arg.startsWith(QLatin1String("--js-runtimes="))) {
            hasJsRuntime = true;
            break;
        }
    }
    // System Deno wins; otherwise use Luma's private binary. The private
    // directory is added to PATH only for this yt-dlp process.
    QString denoPath = QStandardPaths::findExecutable(QStringLiteral("deno"));
    bool localDeno = false;
    if (denoPath.isEmpty()) {
        const QString candidate = denoLocalDir() + QStringLiteral("/deno");
        if (QFileInfo(candidate).isExecutable()) {
            denoPath = candidate;
            localDeno = true;
        }
    }
    if (!hasJsRuntime && !denoPath.isEmpty())
        args << QStringLiteral("--js-runtimes") << QStringLiteral("deno");

    if (!runtimePrefix.isEmpty())
        args = runtimePrefix + args;

    if (ytdlpLogMode == QLatin1String("debug"))
        args << QStringLiteral("--verbose");
    args << QStringLiteral("--no-playlist")
         << QStringLiteral("-j")
         << QStringLiteral("-f") << formatExpr
         << url;

    proc->setProgram(exe);
    proc->setArguments(args);

    // System packages take precedence. Luma's own copy is only put on
    // PYTHONPATH when it is the selected implementation, and its ejs package
    // only when the system one is absent.
    QStringList pythonPath;
    if (systemYtdlp.isEmpty() && localYtdlp)
        pythonPath << ytdlpLocalDir();
    if (!systemEjs && localEjs)
        pythonPath << ytdlpEjsLocalDir();

    QProcessEnvironment env;
    bool envChanged = false;
    if (!pythonPath.isEmpty()) {
        env = QProcessEnvironment::systemEnvironment();
        const QString oldPath = env.value(QStringLiteral("PYTHONPATH"));
        if (!oldPath.isEmpty()) pythonPath << oldPath;
        env.insert(QStringLiteral("PYTHONPATH"), pythonPath.join(QLatin1Char(':')));
        envChanged = true;
    }
    if (localDeno) {
        if (!envChanged)
            env = QProcessEnvironment::systemEnvironment();
        const QString oldPath = env.value(QStringLiteral("PATH"));
        env.insert(QStringLiteral("PATH"), denoLocalDir()
                   + (oldPath.isEmpty() ? QString() : QLatin1Char(':') + oldPath));
        envChanged = true;
    }
    if (envChanged)
        proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (m_ytdlpProcess != proc) return;
        m_ytdlpStdoutBuffer += proc->readAllStandardOutput();
        if (m_ytdlpStdoutBuffer.size() > kMaxYtdlpStdout) {
            m_ytdlpStdoutBuffer.truncate(kMaxYtdlpStdout);
            if (proc->state() == QProcess::Running) {
                m_ytdlpStderr += QStringLiteral("yt-dlp output is too large");
                proc->kill();
            }
        }
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc, ytdlpLogMode]() {
        const QByteArray chunk = proc->readAllStandardError();
        m_ytdlpStderr += QString::fromUtf8(chunk);
        if (m_ytdlpStderr.size() > 65536)
            m_ytdlpStderr = m_ytdlpStderr.right(32768);
        if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
            Logger::instance().writeComponent(QStringLiteral("yt-dlp"),
                                              QString::fromUtf8(chunk));
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, ytdlpLogMode](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        if (m_ytdlpProcess != proc) return;
        if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
        if (m_ytdlpProcess == proc) m_ytdlpProcess = nullptr;
        proc->deleteLater();
        if (ytdlpLogMode != QLatin1String("off"))
            Logger::instance().writeComponent(
                QStringLiteral("yt-dlp"),
                QStringLiteral("failed to start yt-dlp: %1").arg(proc->errorString()));
        setLoading(false);
        emit downloadStatus(tr("download_failed_start"));
        _sendNotification(tr("download_failed_open"), tr("download_ytdlp_missing"), QString());
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, url, keepPlaylist, ytdlpLogMode](int code, QProcess::ExitStatus status) {
        // FailedToStart is handled above and clears m_ytdlpProcess.
        if (m_ytdlpProcess != proc) return;
        m_ytdlpStdoutBuffer += proc->readAllStandardOutput();
        if (m_ytdlpStdoutBuffer.size() > kMaxYtdlpStdout)
            m_ytdlpStdoutBuffer.truncate(kMaxYtdlpStdout);
        m_ytdlpStderr += QString::fromUtf8(proc->readAllStandardError());
        if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
        if (m_ytdlpProcess == proc) m_ytdlpProcess = nullptr;
        proc->deleteLater();

        const bool ok = status == QProcess::NormalExit && code == 0;
        if (ytdlpLogMode == QLatin1String("error") && !ok) {
            QString detail = m_ytdlpStderr.trimmed();
            if (detail.isEmpty())
                detail = QStringLiteral("yt-dlp exited with code %1").arg(code);
            Logger::instance().writeComponent(QStringLiteral("yt-dlp"), detail);
        }
        finishUrlResolution(url, keepPlaylist, ok);
    });

    m_ytdlpTimeout->start();
    // Show the link itself (not the video title): while it is being resolved
    // there is no title yet, and the link makes it obvious which URL is opening.
    emit downloadStatus(tr("stream_opening").arg(url));
    proc->start();
}

void MediaManager::finishUrlResolution(const QString &url, bool keepPlaylist, bool ok) {
    if (ok) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_ytdlpStdoutBuffer, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            ok = false;
        } else {
            const QJsonObject info = document.object();
            m_resolvedTitle = info.value(QStringLiteral("title")).toString();
            m_resolvedIsLive = info.value(QStringLiteral("is_live")).toBool(false);
            if (!m_resolvedIsLive)
                m_resolvedIsLive = info.value(QStringLiteral("live_status")).toString()
                                        == QLatin1String("is_live");

            QString muxedUrl;
            const QJsonArray requested = info.value(QStringLiteral("requested_formats")).toArray();
            for (const QJsonValue &value : requested) {
                const QJsonObject format = value.toObject();
                const QString formatUrl = format.value(QStringLiteral("url")).toString();
                if (formatUrl.isEmpty()) continue;
                // Live HLS formats often report acodec as null/"none" even
                // for audio-only formats. Use yt-dlp's stream type fields as
                // well, otherwise YouTube live is played without its audio URL.
                const QString vcodec = format.value(QStringLiteral("vcodec")).toString();
                const QString acodec = format.value(QStringLiteral("acodec")).toString();
                const QString videoExt = format.value(QStringLiteral("video_ext")).toString();
                const QString audioExt = format.value(QStringLiteral("audio_ext")).toString();
                const QString resolution = format.value(QStringLiteral("resolution")).toString();
                const bool hasVideo = (!vcodec.isEmpty() && vcodec != QLatin1String("none"))
                    || (!videoExt.isEmpty() && videoExt != QLatin1String("none"))
                    || resolution.contains(QLatin1Char('x'));
                const bool hasAudio = (!acodec.isEmpty() && acodec != QLatin1String("none"))
                    || (!audioExt.isEmpty() && audioExt != QLatin1String("none"))
                    || resolution == QLatin1String("audio only");
                if (hasVideo && hasAudio) {
                    if (muxedUrl.isEmpty()) muxedUrl = formatUrl;
                } else if (hasVideo) {
                    if (m_resolvedVideoUrl.isEmpty()) m_resolvedVideoUrl = formatUrl;
                } else if (hasAudio) {
                    if (m_resolvedAudioUrl.isEmpty()) m_resolvedAudioUrl = formatUrl;
                }
            }
            if (m_resolvedVideoUrl.isEmpty())
                m_resolvedVideoUrl = info.value(QStringLiteral("url")).toString();
            if (m_resolvedVideoUrl.isEmpty())
                m_resolvedVideoUrl = muxedUrl;
            if (m_resolvedVideoUrl.isEmpty()) ok = false;

            m_resolvedChapters.clear();
            m_resolvedDuration = info.value(QStringLiteral("duration")).toDouble(0.0);
            const QJsonArray chapters = info.value(QStringLiteral("chapters")).toArray();
            for (const QJsonValue &value : chapters) {
                const QJsonObject chapter = value.toObject();
                const double start = chapter.value(QStringLiteral("start_time")).toDouble(-1.0);
                if (start < 0.0) continue;
                ChapterInfo entry;
                entry.start = start;
                entry.end = chapter.value(QStringLiteral("end_time")).toDouble(0.0);
                entry.title = chapter.value(QStringLiteral("title")).toString().trimmed();
                m_resolvedChapters.append(entry);
            }
            std::sort(m_resolvedChapters.begin(), m_resolvedChapters.end(),
                      [](const ChapterInfo &a, const ChapterInfo &b) {
                          return a.start < b.start;
                      });
            // Drop duplicate timestamps (mpv needs strictly increasing starts)
            // and fill in missing end times from the next chapter / duration.
            QVector<ChapterInfo> uniqueChapters;
            for (ChapterInfo chapter : std::as_const(m_resolvedChapters)) {
                if (!uniqueChapters.isEmpty()
                        && chapter.start - uniqueChapters.last().start < 0.01)
                    continue;
                uniqueChapters.append(chapter);
            }
            for (int i = 0; i < uniqueChapters.size(); ++i) {
                ChapterInfo &chapter = uniqueChapters[i];
                if (chapter.end > chapter.start)
                    continue;
                if (i + 1 < uniqueChapters.size())
                    chapter.end = uniqueChapters[i + 1].start;
                else if (m_resolvedDuration > chapter.start)
                    chapter.end = m_resolvedDuration;
                else
                    chapter.end = chapter.start + 1.0;
            }
            m_resolvedChapters = uniqueChapters;
        }
    }

    if (!ok) {
        QString detail;
        if (m_ytdlpTimedOut) {
            detail = tr("download_timeout");
        } else {
            detail = m_ytdlpStderr.trimmed();
            if (detail.isEmpty()) {
                detail = tr("download_no_result");
            } else {
                detail = detail.section(QLatin1Char('\n'), -1).trimmed();
                if (detail.startsWith(QLatin1String("ERROR:")))
                    detail = detail.mid(6).trimmed();
                if (detail.size() > 300) detail = detail.left(300) + QStringLiteral("…");
            }
        }
        emit downloadStatus(tr("download_failed_open"));
        _sendNotification(tr("download_failed_open"), detail, QString());
        m_resolvedTitle.clear();
        m_resolvedVideoUrl.clear();
        m_resolvedAudioUrl.clear();
        m_resolvedChapters.clear();
        m_resolvedIsLive = false;
        // The link failed to resolve: do not leave the loading entry marked as
        // playing. A playlist that was already playing keeps its marker.
        if (m_pendingUrlModel && !m_pendingUrlWasPlaying)
            m_pendingUrlModel->setPlaying(false);
        clearPendingUrlPlayback();
        setLoading(false);
        return;
    }

    playStream(url, m_resolvedVideoUrl, m_resolvedAudioUrl, m_resolvedTitle, keepPlaylist,
               m_resolvedIsLive);
}

void MediaManager::playStream(const QString &originalUrl, const QString &videoUrl,
                              const QString &audioUrl, const QString &title, bool keepPlaylist,
                              bool isLive) {
    if (!m_mpv || !m_playlists) return;
    m_crossfadeFadeInCurrent = false;
    m_pendingCrossfadeIn = false;
    m_crossfadeOutScheduled = false;
    setCrossfadeFilter(QString());
    resetSilenceState();
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();

    if (!keepPlaylist) {
        PlaylistModel *def = m_playlists->defaultModel();
        if (!def) return;
        const int existingIndex = def->indexOf(originalUrl);
        if (existingIndex < 0) {
            def->setFiles({originalUrl}, originalUrl);
            if (m_playMode == "shuffle") def->setShuffle(true);
        }
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        def->setCurrentIndex(existingIndex >= 0 ? existingIndex : 0);
    }

    // Keep the playing marker on the playlist the URL was started from, not
    // on the tab that happens to be active after yt-dlp finishes (the user
    // may switch playlists while the link is being resolved).
    PlaylistModel *titleModel = keepPlaylist ? m_pendingUrlModel.data()
                                             : m_playlists->defaultModel();
    if (keepPlaylist && !titleModel)
        titleModel = m_playlists->activeModel();
    if (titleModel) {
        m_playlists->setPlayingModel(titleModel);
        // Re-find the row: the playlist could have been reordered while the
        // link was loading.
        const int index = titleModel->indexOf(originalUrl);
        if (index >= 0)
            titleModel->setCurrentIndex(index);
        if (!title.isEmpty())
            titleModel->setVideoTitle(originalUrl, title);
    }
    clearPendingUrlPlayback();

    m_lastPlayedFile = originalUrl;
    m_lastPlayedTimer.start();
    m_lastStreamTitle.clear();
    m_streamFromYtdlp = true;
    // Save the previous position before m_liveStream describes the new file.
    preparePlaybackPosition(originalUrl, isLive);
    setLiveStream(isLive);
    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    if (!isLive && !m_resolvedChapters.isEmpty()) {
        const QString chaptersFile = writeChaptersFile(m_resolvedChapters);
        if (!chaptersFile.isEmpty()) {
            m_mpv->setChaptersFile(chaptersFile);
            m_pendingChaptersFile = chaptersFile;
        }
    }
    m_resolvedChapters.clear();

    m_mpv->setExternalAudio(audioUrl);
    setLoading(true);
    setHasMedia(true);
    m_mpv->setSource(videoUrl, isLive);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != originalUrl) {
        m_history.append(originalUrl);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    // Show the video title in the notification (if yt-dlp provided one).
    const QString notifyTitle = title.isEmpty() ? originalUrl : title;
    QTimer::singleShot(100, this, [this, notifyTitle]() { _sendNotification(notifyTitle); });
    emit fileChanged(originalUrl);
}

void MediaManager::onMpvFileLoaded() {
    if (!m_mpv) return;
    m_silenceLoadTimer.start();
    setLoading(false);
    if (!m_pendingChaptersFile.isEmpty()) {
        QFile::remove(m_pendingChaptersFile);
        m_pendingChaptersFile.clear();
    }

    // Continue where the previous session stopped. Seeking right after
    // fileLoaded is safe: mpv is already playing and the seek is async.
    if (!m_liveStream && m_resumeTarget > 0.0
            && (!m_config || m_config->resumePlayback())) {
        const double target = m_resumeTarget;
        m_resumeTarget = 0.0;
        m_mpv->seekExact(target);
    } else {
        m_resumeTarget = 0.0;
    }

    updateCrossfade();

    // Start the ICY reader only after mpv has opened the stream. Opening a
    // second connection while the radio is still connecting could make some
    // Shoutcast servers stall the player.
    if (!m_icyReader || m_streamFromYtdlp) return;
    const QUrl url(m_mpv->source());
    if (isDirectStreamUrl(url) && isLiveStreamUrl(url))
        m_icyReader->start(url);
}

QString MediaManager::writeChaptersFile(const QVector<ChapterInfo> &chapters)
{
    if (chapters.isEmpty())
        return {};
    if (!m_chaptersDir)
        m_chaptersDir = new QTemporaryDir;
    if (!m_chaptersDir->isValid())
        return {};

    const QString path = m_chaptersDir->path()
                         + QStringLiteral("/chapters-%1.txt").arg(++m_chaptersCounter);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};

    // mpv's chapters-file does not accept OGM directly; ffmetadata is parsed
    // by FFmpeg's ffmetadata demuxer.
    QTextStream out(&file);
    out << QStringLiteral(";FFMETADATA1\n");
    for (const ChapterInfo &chapter : chapters) {
        const int startMs = qMax(0, static_cast<int>(chapter.start * 1000.0));
        const int endMs = qMax(startMs + 1, static_cast<int>(chapter.end * 1000.0));
        QString title = chapter.title;
        title.replace(QLatin1Char('\n'), QLatin1Char(' '));
        title.replace(QLatin1Char('\r'), QLatin1Char(' '));
        title = title.trimmed();
        out << QStringLiteral("[CHAPTER]\n")
            << QStringLiteral("TIMEBASE=1/1000\n")
            << QStringLiteral("START=%1\n").arg(startMs)
            << QStringLiteral("END=%1\n").arg(endMs);
        if (!title.isEmpty())
            out << QStringLiteral("title=%1\n").arg(title);
    }
    file.close();
    return path;
}

QString MediaManager::positionsFilePath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache");
    return base + QStringLiteral("/luma/playback-positions.json");
}

void MediaManager::preparePlaybackPosition(const QString &key, bool live)
{
    // Store where the previous file stopped before switching the key.
    saveCurrentPosition();
    clearPlaybackPosition();

    if (live || key.isEmpty())
        return;
    if (m_config && !m_config->resumePlayback())
        return;

    m_positionKey = key.contains(QStringLiteral("://"))
                        ? key
                        : QFileInfo(key).absoluteFilePath();
    m_resumeTarget = m_positions.positionFor(m_positionKey);
    if (m_positionTimer)
        m_positionTimer->start();
}

void MediaManager::saveCurrentPosition()
{
    if (!m_mpv || m_positionKey.isEmpty() || m_liveStream)
        return;
    if (m_mpv->isCasting())
        return;
    if (m_config && !m_config->resumePlayback())
        return;

    const double duration = m_mpv->duration();
    const double position = m_mpv->position();
    if (duration <= 0.0 || position <= 0.0)
        return;
    if (!m_positions.setPosition(m_positionKey, position, duration))
        return;
    if (m_positionWriter)
        m_positionWriter->write(positionsFilePath(), m_positions.serialize());
}

void MediaManager::clearPlaybackPosition()
{
    if (m_positionTimer)
        m_positionTimer->stop();
    m_positionKey.clear();
    m_resumeTarget = 0.0;
}

void MediaManager::clearPendingUrlPlayback()
{
    m_pendingUrlModel.clear();
    m_pendingUrlWasPlaying = false;
}

void MediaManager::playResolvedUrl(const QString &originalUrl, const QString &playUrl) {
    if (!m_mpv || !m_playlists) return;
    clearPendingUrlPlayback();
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();

    PlaylistModel *def = m_playlists->defaultModel();
    if (!def) return;

    // The playlist keeps the original URL while mpv receives the direct stream.
    // If the URL is already in the list (playlist navigation), don't replace it.
    const int existingIndex = def->indexOf(originalUrl);
    if (existingIndex < 0) {
        def->setFiles({originalUrl}, originalUrl);
        if (m_playMode == "shuffle") def->setShuffle(true);
    }
    if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
        m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    m_lastPlayedFile = originalUrl;
    m_lastPlayedTimer.start();
    m_lastStreamTitle.clear();
    m_streamFromYtdlp = false;
    const bool live = isLiveStreamUrl(QUrl(originalUrl)) && originalUrl == playUrl;
    preparePlaybackPosition(originalUrl, live);
    setLiveStream(live);
    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlists->setPlayingModel(def);
    def->setCurrentIndex(existingIndex >= 0 ? existingIndex : 0);
    setLoading(true);
    setHasMedia(true);
    m_mpv->setSource(playUrl, live);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != originalUrl) {
        m_history.append(originalUrl);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    const QUrl original(originalUrl);
    const QString title = urlTitle(originalUrl);
    // For direct streams the notification arrives with the ICY tag once mpv gets it.
    if (!isDirectStreamUrl(original))
        QTimer::singleShot(100, this, [this, title]() { _sendNotification(title); });
    emit fileChanged(originalUrl);
}

void MediaManager::playIndex(int index) {
    if (!m_playlists) return;
    playIndexInModel(m_playlists->activeModel(), index);
}

void MediaManager::playIndexInModel(PlaylistModel *model, int index) {
    if (!m_mpv || !m_playlists) return;
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();
    if (!model) return;

    QString file = model->fileAt(index);
    if (file.isEmpty()) return;

    // The file may have been deleted or moved while it sat in the playlist.
    // Do not hand it to mpv (which would fail silently): tell the UI and mark
    // the entry, and drop the mark if the file is back.
    if (!file.startsWith(QStringLiteral("http://"))
            && !file.startsWith(QStringLiteral("https://"))) {
        if (!QFileInfo::exists(file)) {
            model->setUnavailable(file, true);
            emit fileUnavailable(file);
            return;
        }
        model->setUnavailable(file, false);
    }

    if (file == m_lastPlayedFile && m_lastPlayedTimer.isValid() && m_lastPlayedTimer.elapsed() < 100) {
        return;
    }
    m_lastPlayedFile = file;
    m_lastPlayedTimer.start();
    resetSilenceState();

    // Resolve playlist URLs through yt-dlp so the proxy works.
    if ((file.startsWith(QStringLiteral("http://")) || file.startsWith(QStringLiteral("https://")))
            && !isDirectStreamUrl(QUrl(file))) {
        model->setCurrentIndex(index);
        // Mark the entry as playing right away: resolving the link can take a
        // while and the playlist should show what is about to play. Other
        // playlists keep their marker until the link actually resolves.
        m_pendingUrlWasPlaying = model->isPlaying();
        m_pendingUrlModel = model;
        model->setPlaying(true);
        openUrlAsync(file, true);
        return;
    }
    clearPendingUrlPlayback();

    if (!file.contains(QStringLiteral("://"))) {
        m_crossfadeFadeInCurrent = m_pendingCrossfadeIn;
        m_pendingCrossfadeIn = false;
    } else {
        m_crossfadeFadeInCurrent = false;
        m_pendingCrossfadeIn = false;
    }
    m_crossfadeOutScheduled = false;
    setCrossfadeFilter(QString());

    m_lastStreamTitle.clear();
    m_streamFromYtdlp = false;
    const bool live = isLiveStreamUrl(QUrl(file));
    preparePlaybackPosition(file, live);
    setLiveStream(live);

    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlists->setPlayingModel(model);
    model->setCurrentIndex(index);
    setHasMedia(true);
    m_mpv->setSource(file, live);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != file) {
        m_history.append(file);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    QTimer::singleShot(100, this, [this, file]() { _notifyForFile(file); });
    emit fileChanged(file);
}

void MediaManager::playNext() {
    if (!m_mpv || !m_playlists) return;
    // Navigation must follow the playlist that is actually playing, not the
    // tab the user happens to be viewing.
    PlaylistModel *model = m_playlists->playingModel();
    if (!model) model = m_playlists->activeModel();
    if (!model) return;

    int current = model->currentIndex();
    int count = model->rowCount();
    if (count == 0) return;

    const QString mode = m_playlists->playlistModeFor(model);
    if (mode == "repeat") {
        if (current >= 0) playIndexInModel(model, current);
    }
    else if (mode == "shuffle") {
        int next = current + 1;
        if (next >= count) {
            model->setShuffle(true);
            next = 0;
        }
        playIndexInModel(model, next);
    }
    else if (mode == "stop") {
        stopPlayback();
    }
    else {
        int next = current + 1;
        if (next < count) {
            playIndexInModel(model, next);
        } else {
            stopPlayback();
        }
    }
}

void MediaManager::stopPlayback() {
    if (!m_mpv) return;
    saveCurrentPosition();
    clearPlaybackPosition();
    clearPendingUrlPlayback();
    // Unload the file so the video area goes black and the control bar returns
    // to 00:00 instead of leaving the last frame of the finished playlist on
    // screen.
    m_mpv->stopPlayback();
    m_crossfadeFadeInCurrent = false;
    m_pendingCrossfadeIn = false;
    m_crossfadeOutScheduled = false;
    setCrossfadeFilter(QString());
    resetSilenceState();
    if (m_playlists)
        m_playlists->setPlayingModel(nullptr);
    setHasMedia(false);
    setLiveStream(false);
    setLoading(false);
}

void MediaManager::nextChapter()
{
    if (!m_mpv || m_mpv->isCasting()) return;
    m_mpv->command(QStringList() << QStringLiteral("add")
                                 << QStringLiteral("chapter") << QStringLiteral("1"));
}

void MediaManager::previousChapter()
{
    if (!m_mpv || m_mpv->isCasting()) return;
    m_mpv->command(QStringList() << QStringLiteral("add")
                                 << QStringLiteral("chapter") << QStringLiteral("-1"));
}

void MediaManager::playPrev() {
    if (!m_mpv || !m_playlists) return;
    PlaylistModel *model = m_playlists->playingModel();
    if (!model) model = m_playlists->activeModel();
    if (!model) return;

    int current = model->currentIndex();
    int count = model->rowCount();
    if (count == 0) return;

    if (current <= 0) {
        if (current == 0) m_mpv->seekExact(0.0);
        return;
    }

    int prev = current - 1;
    if (prev >= 0) playIndexInModel(model, prev);
}

void MediaManager::openFileDialog() {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();

    // Use the shared audio/video filter
    QString path = QFileDialog::getOpenFileName(
        nullptr,
        tr("dialog_open_video"),
                                                defaultDir,
                                                tr("dialog_media_filter") // <-- New localization key
    );
    if (!path.isEmpty()) openFile(path);
}

void MediaManager::openFolderDialog() {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString folder = QFileDialog::getExistingDirectory(nullptr, tr("dialog_open_folder"), defaultDir);
    if (!folder.isEmpty()) openFolder(folder);
}

void MediaManager::openSubtitlesDialog() {
    if (!m_mpv) return;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString path = QFileDialog::getOpenFileName(nullptr, tr("dialog_open_subtitles"), defaultDir, tr("dialog_subtitles_filter"));
    if (!path.isEmpty()) {
        m_mpv->command(QStringList() << "sub-add" << path);
        m_mpv->setProperty("sub", "auto");
    }
}

void MediaManager::openAudioDialog() {
    if (!m_mpv) return;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString path = QFileDialog::getOpenFileName(nullptr, tr("dialog_open_audio"), defaultDir, tr("dialog_audio_filter"));
    if (!path.isEmpty()) m_mpv->command(QStringList() << "audio-add" << path);
}

QVariantList MediaManager::getTracks(const QString &type) const {
    if (!m_mpv) return {};
    QVariantList res;
    // track-list is observed by MpvItem; no blocking call to the mpv thread.
    for (const QVariant &t : m_mpv->observedProperty("track-list").toList()) {
        QVariantMap map = t.toMap();
        if (map["type"].toString() != type) continue;
        QVariantMap item;
        int trackId = map["id"].toInt();
        item["id"] = trackId;
        QString title = map["title"].toString();
        if (title.isEmpty()) title = tr("track_default_name").arg(trackId);
        item["title"] = title;
        item["lang"] = map["lang"].toString();
        res << item;
    }
    return res;
}

void MediaManager::setTrack(const QString &type, int id) { if (m_mpv) m_mpv->setProperty(type, id); }
void MediaManager::setTrackOff(const QString &type) { if (m_mpv) m_mpv->setProperty(type, "no"); }

int MediaManager::currentTrackId(const QString &type) const {
    if (!m_mpv) return 0;
    // aid/sid/vid are observed by MpvItem (see its constructor).
    QVariant val = m_mpv->observedProperty(type);
    if (val.typeId() == QMetaType::QString && val.toString() == "no") return 0;
    return val.toInt();
}
