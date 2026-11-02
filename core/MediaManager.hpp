#pragma once
#include <QObject>
#include <QStringList>
#include <QByteArray>
#include <QElapsedTimer>
#include <QPointer>
#include <QDBusInterface>
#include "mpvqt/MpvItem.hpp"
#include "PlaylistModel.hpp"

class ConfigManager;
class PlaylistManager;
class QProcess;
class QTimer;

class MediaManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(PlaylistModel* playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(QString playMode READ playMode WRITE setPlayMode NOTIFY playModeChanged)
    Q_PROPERTY(bool isManualSwitch READ isManualSwitch NOTIFY isManualSwitchChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY fileChanged)
    // A direct stream (radio) is playing: seeking works only within the cache.
    Q_PROPERTY(bool liveStream READ liveStream NOTIFY liveStreamChanged)
    // A new source is being resolved/opened; stats must not read mpv synchronously.
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)

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
    // True only for real video; embedded audio cover art is not video.
    Q_INVOKABLE bool hasVideo() const;
    bool liveStream() const { return m_liveStream; }
    bool isLoading() const { return m_loading; }

    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openFolder(const QString &folder);
    Q_INVOKABLE void addToPlaylist(const QString &path);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrev();
    Q_INVOKABLE void playIndex(int index);

    Q_INVOKABLE void openInFileManager(const QString &path);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void removeFromPlaylist(int index);
    Q_INVOKABLE QString getDefaultFileManagerName();
    // Blocking variant (kept for API compatibility); the UI uses the async one.
    Q_INVOKABLE QVariantMap getFileInfo(const QString &path);
    Q_INVOKABLE void requestFileInfo(const QString &path);
    // Audio file tags: read and written in the file itself (via TagLib).
    Q_INVOKABLE QVariantMap readTags(const QString &path);
    Q_INVOKABLE bool saveTags(const QString &path, const QVariantMap &tags);
    // Edit the URL of a streaming playlist entry.
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
    void loadingChanged();
    // Text for the OSD while a URL is being opened via yt-dlp.
    void downloadStatus(const QString &message);
    // Result of the asynchronous file-properties request.
    void fileInfoReady(const QString &path, const QVariantMap &info);

private:
    void _sendNotification(const QString &body);
    void _sendNotification(const QString &summary, const QString &body, const QString &coverPath);
    // Notification with track metadata (artist, album, cover art).
    void _notifyForFile(const QString &path);
    // Live stream title (ICY for radio).
    void onMediaTitleChanged();
    // yt-dlp returns direct URLs; mpv plays them through the local SOCKS bridge.
    void openUrlAsync(const QString &url, bool keepPlaylist = false);
    void finishUrlResolution(const QString &url, bool keepPlaylist, bool ok);
    void playStream(const QString &originalUrl, const QString &videoUrl,
                    const QString &audioUrl, const QString &title, bool keepPlaylist);
    void playResolvedUrl(const QString &originalUrl, const QString &playUrl);
    // The external audio track (video-only + audio) is attached after fileLoaded.
    void onMpvFileLoaded();

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
    bool m_loading = false;
    void setLoading(bool loading);
    void setLiveStream(bool live);

    QDBusInterface *m_notifyInterface = nullptr;
    QProcess *m_ytdlpProcess = nullptr;
    QTimer *m_ytdlpTimeout = nullptr;
    QByteArray m_ytdlpStdoutBuffer;
    QString m_ytdlpStderr;
    QString m_defaultFileManagerName;
    // Discards notifications from a file that was already replaced.
    quint64 m_notifyGeneration = 0;
    QString m_resolvedTitle;
    QString m_resolvedVideoUrl;
    QString m_resolvedAudioUrl;
    QString m_pendingAudioUrl;
    bool m_ytdlpTimedOut = false;
    // The source came from yt-dlp: its media-title is a fragment of a googlevideo URL,
    // so no notification is needed for it (the original URL is sent to playStream).
    bool m_streamFromYtdlp = false;
};
