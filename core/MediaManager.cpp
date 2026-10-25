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
        // При переключении на shuffle перемешиваем текущий список.
        // При переключении на другой режим возвращаем оригинальный порядок.
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

void MediaManager::addToPlaylist(const QString &path) {
    if (!m_mpv) return;

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) return;

    // Если файл уже есть в плейлисте, просто играем его
    int existingIndex = m_playlist->indexOf(cleanPath);
    if (existingIndex >= 0) {
        playIndex(existingIndex);
        return;
    }

    // Собираем файлы (из папки или один файл)
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

    // Получаем текущий список
    QStringList currentFiles;
    for (int i = 0; i < m_playlist->rowCount(); ++i) {
        currentFiles << m_playlist->fileAt(i);
    }

    // Добавляем только те, которых еще нет
    for (const QString &f : newFiles) {
        if (!currentFiles.contains(f)) {
            currentFiles << f;
        }
    }

    // ВАЖНО: Устанавливаем в качестве текущего файла именно тот, который открыли,
    // чтобы в режиме shuffle он встал на первое место.
    m_playlist->setFiles(currentFiles, cleanPath);

    // Если режим shuffle, перемешиваем.
    // Так как m_currentPlayingFile теперь cleanPath, он встанет первым, а остальное перемешается.
    if (m_playMode == "shuffle") {
        m_playlist->setShuffle(true);
    }

    // Играем этот файл (он теперь на индексе 0 в режиме shuffle)
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

    // Если включена настройка добавления в существующий плейлист и там уже есть файлы
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

    if (file == m_lastPlayedFile && m_lastPlayedTimer.isValid() && m_lastPlayedTimer.elapsed() < 200) return;
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
    } else if (m_playMode == "shuffle") {
        // В shuffle режиме мы просто идем по уже перемешанному списку
        int next = current + 1;
        if (next < count) {
            playIndex(next);
        } else {
            // Если дошли до конца перемешанного списка, можно либо остановиться,
            // либо перемешать заново. Давайте просто остановимся.
            if (m_mpv) m_mpv->setPaused(true);
        }
    } else if (m_playMode == "stop") {
        if (m_mpv) m_mpv->setPaused(true);
    } else {
        // sequential
        int next = current + 1;
        if (next < count) playIndex(next);
    }
}

void MediaManager::playPrev() {
    if (!m_mpv) return;
    QString currentFile = m_playlist->currentFile();
    if (!m_history.isEmpty() && m_history.last() == currentFile) m_history.removeLast();

    if (!m_history.isEmpty()) {
        QString prevFile = m_history.last();
        m_history.removeLast();
        if (QFileInfo::exists(prevFile)) {
            for (int i = 0; i < m_playlist->rowCount(); ++i) {
                if (QFileInfo(m_playlist->fileAt(i)).absoluteFilePath() == QFileInfo(prevFile).absoluteFilePath()) {
                    playIndex(i); return;
                }
            }
        }
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
