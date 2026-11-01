#include "MediaManager.hpp"
#include "PlaylistManager.hpp"
#include "ConfigManager.hpp"
#include "TagManager.hpp"
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
#include <QProcess>
#include <QSettings>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>

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

QString dictValue(AVDictionary *meta, const char *key) {
    if (!meta) return QString();
    const AVDictionaryEntry *entry = av_dict_get(meta, key, nullptr, 0);
    return entry ? QString::fromUtf8(entry->value).trimmed() : QString();
}

QString coverCachePath(const QString &sourcePath) {
    const QFileInfo fi(sourcePath);
    const QByteArray hash = QCryptographicHash::hash(
        (sourcePath + QString::number(fi.lastModified().toSecsSinceEpoch())).toUtf8(),
        QCryptographicHash::Md5).toHex();

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers";
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
    if (hasTags) {
        const QString coverPath = coverCachePath(path);
        if (QFile::exists(coverPath)) {
            tags.coverPath = coverPath;
        } else {
            const QImage cover = decodeAttachedCover(ctx);
            if (!cover.isNull() && cover.save(coverPath, "JPG", 90))
                tags.coverPath = coverPath;
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

} // namespace

MediaManager::MediaManager(MpvItem *mpv, QObject *parent)
: QObject(parent), m_mpv(mpv), m_playlists(nullptr)
{
    m_notifyInterface = new QDBusInterface(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", QDBusConnection::sessionBus(), this
    );

    // Resolving a URL via yt-dlp must not hang forever.
    m_ytdlpTimeout = new QTimer(this);
    m_ytdlpTimeout->setSingleShot(true);
    m_ytdlpTimeout->setInterval(30000);
    connect(m_ytdlpTimeout, &QTimer::timeout, this, [this]() {
        if (!m_ytdlpProcess || m_ytdlpProcess->state() == QProcess::NotRunning) return;
        m_ytdlpTimedOut = true;
        m_ytdlpProcess->kill();
    });
}

MediaManager::~MediaManager() {
    if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
    if (m_ytdlpProcess) {
        m_ytdlpProcess->disconnect(this);
        m_ytdlpProcess->kill();
        m_ytdlpProcess->waitForFinished(1000);
    }
}

void MediaManager::setMpv(MpvItem *mpv) {
    if (m_mpv == mpv) return;
    if (m_mpv) disconnect(m_mpv, nullptr, this, nullptr);
    m_mpv = mpv;
    if (m_mpv) {
        connect(m_mpv, &MpvItem::mediaTitleChanged, this, &MediaManager::onMediaTitleChanged);
        connect(m_mpv, &MpvItem::sourceChanged, this, [this]() { setLoading(true); });
        connect(m_mpv, &MpvItem::fileLoaded, this, &MediaManager::onMpvFileLoaded);
        connect(m_mpv, &MpvItem::fileEnded, this, [this](const QString &) { setLoading(false); });
    }
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
    // For direct streams mpv often reports the URL itself as the title —
    // don't show it in the playlist or notifications.
    if (title.startsWith(QStringLiteral("http://")) || title.startsWith(QStringLiteral("https://")))
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

PlaylistModel* MediaManager::playlist() const {
    return m_playlists ? m_playlists->activeModel() : nullptr;
}

void MediaManager::setPlaylistManager(PlaylistManager *pm) {
    if (m_playlists == pm) return;
    m_playlists = pm;
    emit playlistChanged();
}

void MediaManager::setManualSwitch(bool val) {
    if (m_isManualSwitch != val) { m_isManualSwitch = val; emit isManualSwitchChanged(); }
}

void MediaManager::setPlayMode(const QString &mode) {
    if (m_playMode == mode) return;
    m_playMode = mode;

    if (m_playlists) {
        PlaylistModel *model = m_playlists->activeModel();
        if (model) model->setShuffle(mode == "shuffle");
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
    QVariant val = m_mpv->getProperty("video-aspect-override");
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

    const QString codec = m_mpv->property("video-codec").toString().trimmed();
    if (codec.isEmpty() || codec.compare(QStringLiteral("no"), Qt::CaseInsensitive) == 0)
        return false;

    auto flag = [this](const char *name) {
        const QVariant value = m_mpv->property(name);
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

    // For URLs, wait for the ICY tag from onMediaTitleChanged — don't read tags over the network.
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return;

    TrackTags tags;
    if (!readTrackTags(path, tags)) {
        // Not audio with tags — as before, but without the file extension.
        _sendNotification(QFileInfo(path).completeBaseName());
        return;
    }

    QString summary = tags.title;
    if (summary.isEmpty()) summary = QFileInfo(path).completeBaseName();

    // Artist and album go on separate lines.
    QStringList details;
    if (!tags.artist.isEmpty()) details << tags.artist;
    if (!tags.album.isEmpty()) details << tags.album;

    _sendNotification(summary, details.join(QLatin1Char('\n')), tags.coverPath);
}

QString MediaManager::getDefaultFileManagerName() {
    QProcess process;
    process.start("xdg-mime", QStringList() << "query" << "default" << "inode/directory");
    process.waitForFinished(1000);

    QString desktopFile = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (desktopFile.isEmpty()) return "File Manager";

    QStringList searchPaths = {
        QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
        "/usr/share/applications",
        "/usr/local/share/applications"
    };

    for (const QString &path : searchPaths) {
        QFileInfo fi(path + "/" + desktopFile);
        if (fi.exists()) {
            QSettings desktop(fi.absoluteFilePath(), QSettings::IniFormat);
            desktop.beginGroup("Desktop Entry");
            QString name = desktop.value("Name[" + QLocale::system().name() + "]",
                                         desktop.value("Name")).toString();
                                         if (!name.isEmpty()) return name;
        }
    }
    return desktopFile.remove(".desktop");
}

QVariantMap MediaManager::getFileInfo(const QString &path) {
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

            // Tags for the "Properties" window.
            // Cover art (attached_pic) is not video, so the file is still audio.
            bool hasRealVideo = false;
            if (video_stream_idx >= 0) {
                const AVStream *videoStream = fmt_ctx->streams[video_stream_idx];
                hasRealVideo = (videoStream->disposition & AV_DISPOSITION_ATTACHED_PIC) == 0;
            }
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

QVariantMap MediaManager::readTags(const QString &path) {
    return TagManager::read(path);
}

bool MediaManager::saveTags(const QString &path, const QVariantMap &tags) {
    if (path.isEmpty()) return false;
    return TagManager::write(path, tags);
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

    if (wasCurrent && m_mpv) m_mpv->setSource(url);
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

    // Save if this is a named playlist
    if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
        m_playlists->saveActive();
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
        if (isDirectStreamUrl(QUrl(path))) playResolvedUrl(path, path);
        else openUrlAsync(path);
        return;
    }

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) return;

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

    QStringList files;
    QDir dir(fi.absolutePath());
    for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"}, QDir::Files, QDir::Name)) {
        files << dir.absoluteFilePath(f);
    }
    if (!files.contains(cleanPath)) files.prepend(cleanPath);

    def->setFiles(files, cleanPath);
    if (m_playlists) m_playlists->scanAudioPaths(files);
    if (m_playMode == "shuffle") def->setShuffle(true);

    if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
        m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    playIndex(def->currentIndex());
}

void MediaManager::openFolder(const QString &folder) {
    if (!m_mpv || !m_playlists) return;

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
    setLoading(true);

    const QString exe = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (exe.isEmpty()) {
        // yt-dlp is not installed — hand the URL to mpv (it has its own ytdl).
        m_pendingAudioUrl.clear();
        playStream(url, url, QString(), QString(), keepPlaylist);
        return;
    }

    if (m_ytdlpProcess) {
        m_ytdlpProcess->disconnect(this);
        m_ytdlpProcess->kill();
        m_ytdlpProcess->waitForFinished(1000);
        m_ytdlpProcess->deleteLater();
        m_ytdlpProcess = nullptr;
    }
    if (m_ytdlpTimeout) m_ytdlpTimeout->stop();

    m_pendingAudioUrl.clear();
    m_resolvedTitle.clear();
    m_resolvedVideoUrl.clear();
    m_resolvedAudioUrl.clear();
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

    QStringList args = m_config ? m_config->ytdlpConfigArgs() : QStringList();
    args << QStringLiteral("--no-playlist")
         << QStringLiteral("-j")
         << QStringLiteral("-f") << formatExpr
         << url;

    proc->setProgram(exe);
    proc->setArguments(args);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        m_ytdlpStdoutBuffer += proc->readAllStandardOutput();
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        m_ytdlpStderr += QString::fromUtf8(proc->readAllStandardError());
        if (m_ytdlpStderr.size() > 65536)
            m_ytdlpStderr = m_ytdlpStderr.right(32768);
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        if (m_ytdlpProcess != proc) return;
        if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
        if (m_ytdlpProcess == proc) m_ytdlpProcess = nullptr;
        proc->deleteLater();
        setLoading(false);
        emit downloadStatus(tr("download_failed_start"));
        _sendNotification(tr("download_failed_open"), tr("download_ytdlp_missing"), QString());
    });
    connect(proc, &QProcess::finished, this, [this, proc, url, keepPlaylist](int code, QProcess::ExitStatus status) {
        // FailedToStart is handled above and clears m_ytdlpProcess.
        if (m_ytdlpProcess != proc) return;
        m_ytdlpStdoutBuffer += proc->readAllStandardOutput();
        m_ytdlpStderr += QString::fromUtf8(proc->readAllStandardError());
        if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
        if (m_ytdlpProcess == proc) m_ytdlpProcess = nullptr;
        proc->deleteLater();

        finishUrlResolution(url, keepPlaylist,
                            status == QProcess::NormalExit && code == 0);
    });

    m_ytdlpTimeout->start();
    emit downloadStatus(tr("stream_opening"));
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

            QString muxedUrl;
            const QJsonArray requested = info.value(QStringLiteral("requested_formats")).toArray();
            for (const QJsonValue &value : requested) {
                const QJsonObject format = value.toObject();
                const QString formatUrl = format.value(QStringLiteral("url")).toString();
                if (formatUrl.isEmpty()) continue;
                const QString vcodec = format.value(QStringLiteral("vcodec")).toString();
                const QString acodec = format.value(QStringLiteral("acodec")).toString();
                const bool hasVideo = !vcodec.isEmpty() && vcodec != QLatin1String("none");
                const bool hasAudio = !acodec.isEmpty() && acodec != QLatin1String("none");
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
        setLoading(false);
        return;
    }

    playStream(url, m_resolvedVideoUrl, m_resolvedAudioUrl, m_resolvedTitle, keepPlaylist);
}

void MediaManager::playStream(const QString &originalUrl, const QString &videoUrl,
                              const QString &audioUrl, const QString &title, bool keepPlaylist) {
    if (!m_mpv || !m_playlists) return;

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

    PlaylistModel *titleModel = keepPlaylist ? m_playlists->activeModel()
                                             : m_playlists->defaultModel();
    if (titleModel) {
        m_playlists->setPlayingModel(titleModel);
        if (!title.isEmpty())
            titleModel->setVideoTitle(originalUrl, title);
    }

    m_lastPlayedFile = originalUrl;
    m_lastPlayedTimer.start();
    m_lastStreamTitle.clear();
    m_streamFromYtdlp = true;
    setLiveStream(false);
    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_pendingAudioUrl = audioUrl;
    setLoading(true);
    m_mpv->setSource(videoUrl);
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

    if (!m_pendingAudioUrl.isEmpty()) {
        // A local file may be opened while the previous network stream is still
        // loading. Do not attach the old external audio track to it.
        const QString source = m_mpv->source();
        if (source.startsWith(QStringLiteral("http://"))
                || source.startsWith(QStringLiteral("https://"))) {
            const QString audio = m_pendingAudioUrl;
            m_pendingAudioUrl.clear();
            m_mpv->command(QStringList() << QStringLiteral("audio-add")
                                         << audio << QStringLiteral("select"));
        } else {
            m_pendingAudioUrl.clear();
        }
    }

    setLoading(false);
}

void MediaManager::playResolvedUrl(const QString &originalUrl, const QString &playUrl) {
    if (!m_mpv || !m_playlists) return;

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
    m_pendingAudioUrl.clear();
    m_streamFromYtdlp = false;
    setLiveStream(isDirectStreamUrl(QUrl(originalUrl)) && originalUrl == playUrl);
    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlists->setPlayingModel(def);
    def->setCurrentIndex(existingIndex >= 0 ? existingIndex : 0);
    setLoading(true);
    m_mpv->setSource(playUrl);
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
    if (!m_mpv || !m_playlists) return;
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    QString file = model->fileAt(index);
    if (file.isEmpty()) return;

    if (file == m_lastPlayedFile && m_lastPlayedTimer.isValid() && m_lastPlayedTimer.elapsed() < 100) {
        return;
    }
    m_lastPlayedFile = file;
    m_lastPlayedTimer.start();

    // Resolve playlist URLs through yt-dlp so the proxy works.
    if ((file.startsWith(QStringLiteral("http://")) || file.startsWith(QStringLiteral("https://")))
            && !isDirectStreamUrl(QUrl(file))) {
        model->setCurrentIndex(index);
        openUrlAsync(file, true);
        return;
    }

    m_lastStreamTitle.clear();
    m_pendingAudioUrl.clear();
    m_streamFromYtdlp = false;
    setLiveStream(isDirectStreamUrl(QUrl(file)));

    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlists->setPlayingModel(model);
    model->setCurrentIndex(index);
    m_mpv->setSource(file);
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
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    int current = model->currentIndex();
    int count = model->rowCount();
    if (count == 0) return;

    if (m_playMode == "repeat") {
        if (current >= 0) playIndex(current);
    }
    else if (m_playMode == "shuffle") {
        int next = current + 1;
        if (next >= count) {
            model->setShuffle(true);
            next = 0;
        }
        playIndex(next);
    }
    else if (m_playMode == "stop") {
        if (m_mpv) m_mpv->setPaused(true);
    }
    else {
        int next = current + 1;
        if (next < count) {
            playIndex(next);
        } else {
            if (m_mpv) m_mpv->setPaused(true);
        }
    }
}

void MediaManager::playPrev() {
    if (!m_mpv || !m_playlists) return;
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    int current = model->currentIndex();
    int count = model->rowCount();
    if (count == 0) return;

    if (current <= 0) {
        if (current == 0) m_mpv->seekExact(0.0);
        return;
    }

    int prev = current - 1;
    if (prev >= 0) playIndex(prev);
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
    for (const QVariant &t : m_mpv->getProperty("track-list").toList()) {
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
    QVariant val = m_mpv->getProperty(type);
    if (val.typeId() == QMetaType::QString && val.toString() == "no") return 0;
    return val.toInt();
}
