#pragma once
#include <QObject>
#include <QStringList>
#include <QElapsedTimer>
#include <QPointer>
#include <QDBusInterface>
#include "mpvqt/MpvItem.hpp"
#include "PlaylistModel.hpp"

class ConfigManager;
class PlaylistManager;
class QProcess;

class MediaManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(PlaylistModel* playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(QString playMode READ playMode WRITE setPlayMode NOTIFY playModeChanged)
    Q_PROPERTY(bool isManualSwitch READ isManualSwitch NOTIFY isManualSwitchChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY fileChanged)
    // Идёт прямой поток (радио): можно мотать только внутри кэша.
    Q_PROPERTY(bool liveStream READ liveStream NOTIFY liveStreamChanged)

public:
    explicit MediaManager(MpvItem *mpv = nullptr, QObject *parent = nullptr);
    ~MediaManager() override;

    PlaylistModel* playlist() const;
    void setPlaylistManager(PlaylistManager *pm);
    PlaylistManager* playlistManager() const { return m_playlists; }
    void setMpv(MpvItem *mpv);
    void setConfig(ConfigManager *config) { m_config = config; }

    bool isManualSwitch() const { return m_isManualSwitch; }
    void setManualSwitch(bool val);

    QString playMode() const { return m_playMode; }
    void setPlayMode(const QString &mode);

    QString currentFileName() const;
    bool liveStream() const { return m_liveStream; }

    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openFolder(const QString &folder);
    Q_INVOKABLE void addToPlaylist(const QString &path);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrev();
    Q_INVOKABLE void playIndex(int index);

    Q_INVOKABLE void openInFileManager(const QString &path);
    Q_INVOKABLE void removeFromPlaylist(int index);
    Q_INVOKABLE QString getDefaultFileManagerName();
    Q_INVOKABLE QVariantMap getFileInfo(const QString &path);
    // Теги аудиофайла: чтение и запись в сам файл (через TagLib).
    Q_INVOKABLE QVariantMap readTags(const QString &path);
    Q_INVOKABLE bool saveTags(const QString &path, const QVariantMap &tags);
    // Правка ссылки у потоковой записи плейлиста.
    Q_INVOKABLE bool updateStreamUrl(const QString &oldUrl, const QString &newUrl);

    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void openFolderDialog();
    Q_INVOKABLE void openSubtitlesDialog();
    Q_INVOKABLE void openAudioDialog();

    Q_INVOKABLE QVariantList getTracks(const QString &type) const;
    Q_INVOKABLE void setTrack(const QString &type, int id);
    Q_INVOKABLE void setTrackOff(const QString &type);
    Q_INVOKABLE int currentTrackId(const QString &type) const;

    Q_INVOKABLE void setAspectRatio(const QString &mode);
    Q_INVOKABLE QString getAspectRatio() const;

signals:
    void fileChanged(const QString &path);
    void playModeChanged();
    void isManualSwitchChanged();
    void playlistChanged();
    void liveStreamChanged();

private:
    void _sendNotification(const QString &body);
    void _sendNotification(const QString &summary, const QString &body, const QString &coverPath);
    // Уведомление с метаданными трека (исполнитель, альбом, обложка).
    void _notifyForFile(const QString &path);
    // Живой заголовок потока (ICY у радио).
    void onMediaTitleChanged();
    // yt-dlp: сначала получаем прямой поток, файлы не скачиваются.
    void openUrlAsync(const QString &url);
    void playResolvedUrl(const QString &originalUrl, const QString &playUrl);
    QStringList ytdlpConfigArgs() const;

    MpvItem *m_mpv;
    PlaylistManager *m_playlists = nullptr;
    ConfigManager *m_config = nullptr;
    QStringList m_history;
    QString m_playMode = "sequential";
    bool m_isManualSwitch = false;

    QString m_lastPlayedFile;
    QElapsedTimer m_lastPlayedTimer;
    QString m_lastStreamTitle;
    bool m_liveStream = false;
    void setLiveStream(bool live);

    QDBusInterface *m_notifyInterface = nullptr;
    QProcess *m_ytdlpProcess = nullptr;
};
