#include "MediaManager.hpp"
#include "ConfigManager.hpp"
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
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

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

MediaManager::MediaManager(MpvItem *mpv, QObject *parent)
: QObject(parent), m_mpv(mpv), m_playlist(new PlaylistModel(this))
{
    m_notifyInterface = new QDBusInterface(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", QDBusConnection::sessionBus(), this
    );
}

MediaManager::~MediaManager() = default;

void MediaManager::setManualSwitch(bool val) {
    if (m_isManualSwitch != val) { m_isManualSwitch = val; emit isManualSwitchChanged(); }
}

void MediaManager::setPlayMode(const QString &mode) {
    if (m_playMode == mode) return;
    m_playMode = mode;

    if (m_playlist) {
        m_playlist->setShuffle(mode == "shuffle");
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

void MediaManager::_sendNotification(const QString &body) {
    if (m_config && !m_config->notifications()) return;

    if (!m_notifyInterface || !m_notifyInterface->isValid()) return;
    m_notifyInterface->asyncCall("Notify", "Luma", (uint)0, "luma", "", body, QStringList(),
                                 QVariantMap({{"desktop-entry", "luma"}}), (int)-1);
}

QString MediaManager::getDefaultFileManagerName() {
    QProcess process;
    process.start("xdg-mime", QStringList() << "query" << "default" << "inode/directory");
    process.waitForFinished(1000);

    QString desktopFile = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (desktopFile.isEmpty()) {
        return "File Manager";
    }

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
            QString name = desktop.value("Name[" + QLocale::system().name() + "]", desktop.value("Name")).toString();
            if (!name.isEmpty()) return name;
        }
    }

    return desktopFile.remove(".desktop");
}

QVariantMap MediaManager::getFileInfo(const QString &path) {
    QVariantMap info;
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
                if (codec) {
                    info["video_codec"] = QString::fromUtf8(codec->long_name);
                } else {
                    info["video_codec"] = "Unknown";
                }
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
                if (codec) {
                    info["audio_codec"] = QString::fromUtf8(codec->long_name);
                } else {
                    info["audio_codec"] = "Unknown";
                }
            } else {
                info["audio_bitrate"] = 0;
                info["sample_rate"] = 0;
                info["channels"] = 0;
                info["audio_codec"] = "N/A";
            }
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

void MediaManager::openInFileManager(const QString &path) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;

    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

void MediaManager::removeFromPlaylist(int index) {
    if (!m_playlist) return;

    if (index == m_playlist->currentIndex()) {
        int nextIndex = m_playlist->currentIndex();
        m_playlist->removeItem(index);

        if (nextIndex >= 0 && nextIndex < m_playlist->rowCount()) {
            playIndex(nextIndex);
        } else {
            if (m_mpv) m_mpv->command(QStringList() << "stop");
        }
    } else {
        m_playlist->removeItem(index);
    }
}

void MediaManager::addToPlaylist(const QString &path) {
    if (!m_mpv) return;

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) return;

    int existingIndex = m_playlist->indexOf(cleanPath);
    if (existingIndex >= 0) {
        playIndex(existingIndex);
        return;
    }

    QStringList newFiles;
    if (fi.isDir()) {
        QDir dir(cleanPath);
        for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"}, QDir::Files, QDir::Name)) {
            newFiles << dir.absoluteFilePath(f);
        }
    } else {
        QDir dir(fi.absolutePath());
        for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"}, QDir::Files, QDir::Name)) {
            newFiles << dir.absoluteFilePath(f);
        }
    }

    if (newFiles.isEmpty()) return;

    QStringList currentFiles;
    for (int i = 0; i < m_playlist->rowCount(); ++i) {
        currentFiles << m_playlist->fileAt(i);
    }

    for (const QString &f : newFiles) {
        if (!currentFiles.contains(f)) {
            currentFiles << f;
        }
    }

    m_playlist->setFiles(currentFiles, cleanPath);

    if (m_playMode == "shuffle") {
        m_playlist->setShuffle(true);
    }

    int newIdx = m_playlist->indexOf(cleanPath);
    if (newIdx >= 0) {
        playIndex(newIdx);
    }
}

void MediaManager::openFile(const QString &path) {
    if (!m_mpv) return;
    if (path.startsWith("http://") || path.startsWith("https://")) {
        m_playlist->setFiles({path}, path);
        if (m_playMode == "shuffle") m_playlist->setShuffle(true);
        playIndex(0);
        return;
    }

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) return;

    if (m_config && m_config->appendToPlaylist() && m_playlist->rowCount() > 0) {
        addToPlaylist(cleanPath);
        return;
    }

    if (m_playlist->currentFile() == cleanPath) return;

    int existingIndex = -1;
    for (int i = 0; i < m_playlist->rowCount(); ++i) {
        if (m_playlist->fileAt(i) == cleanPath) { existingIndex = i; break; }
    }
    if (existingIndex >= 0) {
        playIndex(existingIndex);
        return;
    }

    QStringList files;
    QDir dir(fi.absolutePath());
    for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"}, QDir::Files, QDir::Name)) {
        files << dir.absoluteFilePath(f);
    }
    if (!files.contains(cleanPath)) files.prepend(cleanPath);

    m_playlist->setFiles(files, cleanPath);

    if (m_playMode == "shuffle") {
        m_playlist->setShuffle(true);
    }

    playIndex(m_playlist->currentIndex());
}

void MediaManager::openFolder(const QString &folder) {
    if (!m_mpv) return;
    QDir dir(folder);
    QStringList files;
    for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"}, QDir::Files, QDir::Name)) {
        files << dir.absoluteFilePath(f);
    }
    if (!files.isEmpty()) {
        if (m_config && m_config->appendToPlaylist() && m_playlist->rowCount() > 0) {
            addToPlaylist(folder);
        } else {
            m_playlist->setFiles(files, files.first());
            if (m_playMode == "shuffle") m_playlist->setShuffle(true);
            playIndex(m_playlist->currentIndex());
        }
    }
}

void MediaManager::playIndex(int index) {
    if (!m_mpv) return;
    QString file = m_playlist->fileAt(index);
    if (file.isEmpty()) return;

    // Защита от запуска того же файла (если он уже играет)
    if (file == m_lastPlayedFile && m_lastPlayedTimer.isValid() && m_lastPlayedTimer.elapsed() < 100) {
        return;
    }
    m_lastPlayedFile = file;
    m_lastPlayedTimer.start();

    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlist->setCurrentIndex(index);
    m_mpv->setSource(file);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != file) {
        m_history.append(file);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    QString fileName = QFileInfo(file).fileName();
    QTimer::singleShot(100, this, [this, fileName]() { _sendNotification(fileName); });
    emit fileChanged(file);
}

void MediaManager::playNext() {
    if (!m_mpv) return;
    int current = m_playlist->currentIndex();
    int count = m_playlist->rowCount();
    if (count == 0) return;

    if (m_playMode == "repeat") {
        if (current >= 0) playIndex(current);
    }
    else if (m_playMode == "shuffle") {
        int next = current + 1;
        if (next >= count) {
            m_playlist->setShuffle(true);
            next = 0;
        }
        playIndex(next);
    }
    else if (m_playMode == "stop") {
        if (m_mpv) m_mpv->setPaused(true);
    }
    else {
        // Sequential (по порядку)
        int next = current + 1;
        if (next < count) {
            playIndex(next);
        } else {
            if (m_mpv) m_mpv->setPaused(true);
        }
    }
}

void MediaManager::playPrev() {
    if (!m_mpv) return;

    int current = m_playlist->currentIndex();
    int count = m_playlist->rowCount();

    if (count == 0) return;

    if (current <= 0) {
        if (current == 0) {
            m_mpv->seekExact(0.0);
        }
        return;
    }

    int prev = current - 1;
    if (prev >= 0) {
        playIndex(prev);
    }
}

void MediaManager::openFileDialog() {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString path = QFileDialog::getOpenFileName(nullptr, tr("dialog_open_video"), defaultDir, tr("dialog_video_filter"));
    if (!path.isEmpty()) openFile(path);
}

void MediaManager::openFolderDialog() {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString folder = QFileDialog::getExistingDirectory(nullptr, tr("dialog_open_folder"), defaultDir);
    if (!folder.isEmpty()) openFolder(folder);
}

void MediaManager::openUrlDialog() {
    bool ok;
    QString url = QInputDialog::getText(nullptr, tr("dialog_open_url"), tr("dialog_enter_url"), QLineEdit::Normal, "", &ok);
    if (ok && !url.isEmpty()) openFile(url);
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
