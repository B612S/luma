#pragma once
#include <QObject>
#include <QStringList>
#include <QByteArray>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QPair>
#include <QPointer>
#include "mpvqt/MpvItem.hpp"
#include "PlaybackPositions.hpp"
#include "PlaylistModel.hpp"

class AsyncFileWriter;
class ConfigManager;
class IcyMetadataReader;
class PlaylistManager;
class QProcess;
class YtDlpRuntime;
class MediaInfoService;
class MediaNotifier;
class TrailingSilenceDetector;
class MediaFileActions;
class QTemporaryDir;
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
    // Resolved in the background; the playlist menu shows the fallback until
    // the first xdg-mime query returns.
    Q_PROPERTY(QString defaultFileManagerName READ defaultFileManagerName
               NOTIFY defaultFileManagerNameChanged)
    // Something is loaded or playing. False before the first file and after the
    // playlist stopped, so the UI can hide the media information panel.
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY hasMediaChanged)

public:
    explicit MediaManager(MpvItem *mpv = nullptr, QObject *parent = nullptr);
    ~MediaManager() override;

    PlaylistModel* playlist() const;
    void setPlaylistManager(PlaylistManager *pm);
    PlaylistManager* playlistManager() const { return m_playlists; }
    void setMpv(MpvItem *mpv);
    void setConfig(ConfigManager *config);

    // Extra mpv audio filter for the music crossfade (empty when inactive).
    // The equalizer and this filter are combined by main.cpp before setting af.
    QString crossfadeFilter() const { return m_crossfadeFilter; }

    bool isManualSwitch() const { return m_isManualSwitch; }
    void setManualSwitch(bool val);

    QString playMode() const { return m_playMode; }
    void setPlayMode(const QString &mode);

    QString currentFileName() const;
    // True only for real video; embedded audio cover art is not video.
    Q_INVOKABLE bool hasVideo() const;
    bool liveStream() const { return m_liveStream; }
    bool isLoading() const { return m_loading; }
    bool hasMedia() const { return m_hasMedia; }
    QString defaultFileManagerName() const;


    // Runtime requirements for YouTube's JavaScript challenge. The ejs
    // package can be installed into ~/.local/share/luma without touching the
    // system Python installation. The Python probe runs in the background:
    // opening the settings dialog must not block playback.
    Q_INVOKABLE void refreshYtdlpRuntime();
    Q_INVOKABLE void installYtdlp();
    Q_INVOKABLE void installYtdlpEjs();
    Q_INVOKABLE void installDeno();
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openFolder(const QString &folder);
    Q_INVOKABLE void addToPlaylist(const QString &path);
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrev();
    // mpv's chapter navigation works for embedded chapters and for the chapter
    // file Luma attaches to yt-dlp streams.
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void previousChapter();
    Q_INVOKABLE void playIndex(int index);

    Q_INVOKABLE void openInFileManager(const QString &path);
    // Starts a second Luma process playing path. Only meaningful when
    // single-instance mode is off; the playlist menu hides it otherwise.
    Q_INVOKABLE bool openInNewWindow(const QString &path);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void removeFromPlaylist(int index);
    // Used by the playlist context menu: file-only actions (Information) are
    // hidden for entries whose file has been deleted or moved.
    Q_INVOKABLE bool isFileAvailable(const QString &path) const;
    Q_INVOKABLE void requestFileInfo(const QString &path);
    // Bitrate of a short window around the playback position. The statistics
    // panel uses it for video, where mpv's value is a last-GOP average that
    // only changes at keyframes.
    Q_INVOKABLE void requestLiveBitrate(const QString &path, double position);
    // Audio file tags: read and written in the file itself (via TagLib).
    Q_INVOKABLE QVariantMap readTags(const QString &path);
    Q_INVOKABLE bool saveTags(const QString &path, const QVariantMap &tags);
    // Non-blocking tag write for the info dialog: TagLib works on a worker.
    Q_INVOKABLE void requestSaveTags(const QString &path, const QVariantMap &tags);
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

    // Narrow API used by the JavaScript plugin runtime. Keeping these on
    // MediaManager means plugins do not need access to MpvItem or QML.
    Q_INVOKABLE QVariantMap pluginState() const;
    Q_INVOKABLE QVariantMap pluginStats() const;
    Q_INVOKABLE void pluginTogglePause();
    Q_INVOKABLE void pluginSetPaused(bool paused);
    Q_INVOKABLE void pluginSeek(double position);
    Q_INVOKABLE void pluginSetVolume(int volume);
    Q_INVOKABLE void pluginStop();
    Q_INVOKABLE bool pluginOpen(const QString &path);
    // Raw mpv access for privileged plugins. getProperty may block briefly on
    // the mpv thread; the media API above is preferred for normal use.
    Q_INVOKABLE QVariant pluginMpvGetProperty(const QString &name) const;
    Q_INVOKABLE void pluginMpvSetProperty(const QString &name, const QVariant &value);
    // Returns false when mpv rejects the command (unknown filter, bad
    // argument, unavailable property) so plugins can react instead of
    // assuming success.
    Q_INVOKABLE bool pluginMpvCommand(const QStringList &args);

signals:
    void fileChanged(const QString &path);
    void playModeChanged();
    void isManualSwitchChanged();
    void crossfadeFilterChanged();
    void playlistChanged();
    void liveStreamChanged();
    void hasMediaChanged();
    void defaultFileManagerNameChanged();
    void ytdlpInstallFinished(bool success, const QString &message);
    void ytdlpEjsInstallFinished(bool success, const QString &message);
    void denoInstallFinished(bool success, const QString &message);
    // Result of refreshYtdlpRuntime(), including the asynchronous ejs probe.
    void ytdlpRuntimeReady(const QVariantMap &info);
    void loadingChanged();
    // Plugin-facing playback state changes. Position is throttled to about
    // once per second by MediaManager.
    void pluginPositionChanged(double position, double duration);
    void pluginPausedChanged(bool paused);
    void pluginVolumeChanged(double volume);
    // Text for the OSD while a URL is being opened via yt-dlp.
    void downloadStatus(const QString &message);
    // Result of the asynchronous file-properties request.
    void fileInfoReady(const QString &path, const QVariantMap &info);
    void tagsSaved(const QString &path, bool success);
    // Result of requestLiveBitrate(): average over the sampled window.
    void liveBitrateReady(const QString &path, double position,
                          qint64 videoBps, qint64 audioBps);
    // A local file could not be opened because it no longer exists.
    void fileUnavailable(const QString &path);

private:
    // Nothing is playing any more (end of the playlist or "stop" mode): unload
    // the file so the video area goes black and the player looks idle again.
    void stopPlayback();
    // Live stream title (ICY for radio).
    void onMediaTitleChanged();
    void onIcyTitleChanged(const QString &title);
    void applyIcyTitle();
    // yt-dlp returns direct URLs; mpv plays them through the local SOCKS bridge.
    void openUrlAsync(const QString &url, bool keepPlaylist = false);
    void finishUrlResolution(const QString &url, bool keepPlaylist, bool ok);
    void playIndexInModel(PlaylistModel *model, int index);
    void playStream(const QString &originalUrl, const QString &videoUrl,
                    const QString &audioUrl, const QString &title, bool keepPlaylist,
                    bool isLive);
    void playResolvedUrl(const QString &originalUrl, const QString &playUrl);
    void clearPendingUrlPlayback();
    void onMpvFileLoaded();
    // Fills the Default playlist with the rest of the folder after the file
    // has already started playing. The generation discards stale results.
    void scanFolderForPlaylist(const QString &filePath, quint64 generation);
    // Resume support: save the current position and prepare the next key.
    void preparePlaybackPosition(const QString &key, bool live);
    void saveCurrentPosition();
    void clearPlaybackPosition();
    QString positionsFilePath() const;
    // yt-dlp chapters are passed to mpv through an ffmetadata file loaded with
    // the stream, so chapter navigation and markers work for links as well.
    struct ChapterInfo {
        double start = 0.0;
        double end = 0.0;
        QString title;
    };
    QString writeChaptersFile(const QVector<ChapterInfo> &chapters);

    MpvItem *m_mpv;
    PlaylistManager *m_playlists = nullptr;
    ConfigManager *m_config = nullptr;
    QStringList m_history;
    // The playlist a URL was started from. yt-dlp resolves the link
    // asynchronously; the user may switch tabs before it finishes, and the
    // playing marker must stay on the original playlist, not on the tab that
    // happens to be active when playback starts.
    QPointer<PlaylistModel> m_pendingUrlModel;
    QVector<ChapterInfo> m_resolvedChapters;
    double m_resolvedDuration = 0.0;
    QTemporaryDir *m_chaptersDir = nullptr;
    QString m_pendingChaptersFile;
    int m_chaptersCounter = 0;
    // Resume position of the current (non-live) media, cached on disk.
    PlaybackPositions m_positions;
    AsyncFileWriter *m_positionWriter = nullptr;
    QTimer *m_positionTimer = nullptr;
    QString m_positionKey;
    double m_resumeTarget = 0.0;
    // Whether that playlist was already marked as playing before the link
    // started loading (then a failure must not clear its marker).
    bool m_pendingUrlWasPlaying = false;
    QString m_playMode = "sequential";
    bool m_isManualSwitch = false;
    // Crossfade only applies to local music. File-ended state carries the
    // fade-in flag over to the next track.
    QString m_crossfadeFilter;
    bool m_crossfadeFadeInCurrent = false;
    bool m_pendingCrossfadeIn = false;
    bool m_crossfadeOutScheduled = false;
    // Trailing-silence detection runs on a worker. The effective end is the
    // position where the next track should start (0/-1 means "use duration").
    quint64 m_silenceGeneration = 0;
    QString m_silenceSource;
    double m_silenceEffectiveEnd = -1.0;
    bool m_silenceTransitionTriggered = false;
    QElapsedTimer m_silenceLoadTimer;
    void resetSilenceState();
    void startSilenceScan(const QString &path);
    void maybeAdvancePastSilence(double position);

    QString m_lastPlayedFile;
    QElapsedTimer m_lastPlayedTimer;
    QElapsedTimer m_pluginPositionTimer;
    QString m_lastStreamTitle;
    bool m_liveStream = false;
    bool m_loading = false;
    bool m_hasMedia = false;
    void setLoading(bool loading);
    void setLiveStream(bool live);
    void setHasMedia(bool hasMedia);
    void updateCrossfade();
    void setCrossfadeFilter(const QString &filter);

    QProcess *m_ytdlpProcess = nullptr;
    QTimer *m_ytdlpTimeout = nullptr;
    IcyMetadataReader *m_icyReader = nullptr;
    QTimer *m_icyTitleTimer = nullptr;
    QString m_pendingIcyTitle;
    QByteArray m_ytdlpStdoutBuffer;
    QString m_ytdlpStderr;
    YtDlpRuntime *m_ytDlpRuntime = nullptr;
    MediaInfoService *m_mediaInfo = nullptr;
    MediaNotifier *m_notifier = nullptr;
    TrailingSilenceDetector *m_silenceDetector = nullptr;
    MediaFileActions *m_fileActions = nullptr;
    quint64 m_folderScanGeneration = 0;
    QString m_resolvedTitle;
    QString m_resolvedVideoUrl;
    QString m_resolvedAudioUrl;
    bool m_resolvedIsLive = false;
    bool m_ytdlpTimedOut = false;
    // The source came from yt-dlp: its media-title is a fragment of a googlevideo URL,
    // so no notification is needed for it (the original URL is sent to playStream).
    bool m_streamFromYtdlp = false;
};
