#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>

class QFileSystemWatcher;
class QTimer;

class ConfigManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool rememberVolume READ rememberVolume WRITE setRememberVolume NOTIFY rememberVolumeChanged)
    Q_PROPERTY(int defaultVolume READ defaultVolume WRITE setDefaultVolume NOTIFY defaultVolumeChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString playlistMode READ playlistMode WRITE setPlaylistMode NOTIFY playlistModeChanged)
    Q_PROPERTY(bool appendToPlaylist READ appendToPlaylist WRITE setAppendToPlaylist NOTIFY appendToPlaylistChanged)
    Q_PROPERTY(bool singleInstance READ singleInstance WRITE setSingleInstance NOTIFY singleInstanceChanged)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY notificationsChanged)
    Q_PROPERTY(bool resumePlayback READ resumePlayback WRITE setResumePlayback NOTIFY resumePlaybackChanged)
    Q_PROPERTY(bool inhibitSleep READ inhibitSleep WRITE setInhibitSleep NOTIFY inhibitSleepChanged)
    Q_PROPERTY(bool showChapterMarkers READ showChapterMarkers WRITE setShowChapterMarkers NOTIFY showChapterMarkersChanged)
    Q_PROPERTY(QString controlBarMode READ controlBarMode WRITE setControlBarMode NOTIFY controlBarModeChanged)
    Q_PROPERTY(QString subtitleFont READ subtitleFont WRITE setSubtitleFont NOTIFY subtitlesChanged)
    Q_PROPERTY(int subtitleFontSize READ subtitleFontSize WRITE setSubtitleFontSize NOTIFY subtitlesChanged)
    Q_PROPERTY(bool subtitleBold READ subtitleBold WRITE setSubtitleBold NOTIFY subtitlesChanged)
    Q_PROPERTY(QString subtitleColor READ subtitleColor WRITE setSubtitleColor NOTIFY subtitlesChanged)
    Q_PROPERTY(int subtitleBorderSize READ subtitleBorderSize WRITE setSubtitleBorderSize NOTIFY subtitlesChanged)
    Q_PROPERTY(QString subtitleBorderColor READ subtitleBorderColor WRITE setSubtitleBorderColor NOTIFY subtitlesChanged)
    Q_PROPERTY(bool subtitleBackground READ subtitleBackground WRITE setSubtitleBackground NOTIFY subtitlesChanged)
    Q_PROPERTY(int subtitlePosition READ subtitlePosition WRITE setSubtitlePosition NOTIFY subtitlesChanged)
    Q_PROPERTY(bool subtitleIgnoreFileStyle READ subtitleIgnoreFileStyle WRITE setSubtitleIgnoreFileStyle NOTIFY subtitlesChanged)
    Q_PROPERTY(bool showPanelsFullscreen READ showPanelsFullscreen WRITE setShowPanelsFullscreen NOTIFY showPanelsFullscreenChanged)
    Q_PROPERTY(QString audioOutput READ audioOutput WRITE setAudioOutput NOTIFY audioOutputChanged)
    Q_PROPERTY(int musicCrossfade READ musicCrossfade WRITE setMusicCrossfade NOTIFY musicCrossfadeChanged)
    Q_PROPERTY(QString hwdec READ hwdec WRITE setHwdec NOTIFY hwdecChanged)
    Q_PROPERTY(QString ytdlpQuality READ ytdlpQuality WRITE setYtdlpQuality NOTIFY ytdlpSettingsChanged)
    Q_PROPERTY(QString ytdlpProxy READ ytdlpProxy WRITE setYtdlpProxy NOTIFY ytdlpSettingsChanged)
    Q_PROPERTY(QString ytdlpProxyType READ ytdlpProxyType WRITE setYtdlpProxyType NOTIFY ytdlpSettingsChanged)
    Q_PROPERTY(QString ytdlpProxyAddress READ ytdlpProxyAddress WRITE setYtdlpProxyAddress NOTIFY ytdlpSettingsChanged)
    Q_PROPERTY(QString lumaLogMode READ lumaLogMode WRITE setLumaLogMode NOTIFY loggingChanged)
    Q_PROPERTY(QString mpvLogMode READ mpvLogMode WRITE setMpvLogMode NOTIFY loggingChanged)
    Q_PROPERTY(QString ytdlpLogMode READ ytdlpLogMode WRITE setYtdlpLogMode NOTIFY loggingChanged)

public:
    explicit ConfigManager(QObject *parent = nullptr);

    bool rememberVolume() const;
    void setRememberVolume(bool v);

    int defaultVolume() const;
    void setDefaultVolume(int v);

    int volume() const;
    void setVolume(int v);

    QString language() const;
    void setLanguage(const QString &lang);
    // Resolves a stored value ("auto"/"en"/"ru") to a compiled translation.
    // "auto" follows the first two letters of systemLocale; unsupported
    // languages and locales fall back to English.
    static QString resolveLanguage(const QString &requested, const QString &systemLocale);

    QString playlistMode() const;
    void setPlaylistMode(const QString &mode);

    bool appendToPlaylist() const;
    void setAppendToPlaylist(bool v);

    bool singleInstance() const;
    void setSingleInstance(bool enabled);

    bool notifications() const;
    void setNotifications(bool enabled);

    bool resumePlayback() const;
    void setResumePlayback(bool enabled);

    // Keep the screen and the system awake while media is playing.
    bool inhibitSleep() const;
    void setInhibitSleep(bool enabled);

    bool showChapterMarkers() const;
    void setShowChapterMarkers(bool enabled);

    // "hidden" | "float" | "pinned"
    QString controlBarMode() const;
    void setControlBarMode(const QString &mode);

    QString subtitleFont() const;
    void setSubtitleFont(const QString &font);
    int subtitleFontSize() const;
    void setSubtitleFontSize(int size);
    bool subtitleBold() const;
    void setSubtitleBold(bool bold);
    QString subtitleColor() const;
    void setSubtitleColor(const QString &color);
    int subtitleBorderSize() const;
    void setSubtitleBorderSize(int size);
    QString subtitleBorderColor() const;
    void setSubtitleBorderColor(const QString &color);
    bool subtitleBackground() const;
    void setSubtitleBackground(bool enabled);
    int subtitlePosition() const;
    void setSubtitlePosition(int position);
    bool subtitleIgnoreFileStyle() const;
    void setSubtitleIgnoreFileStyle(bool enabled);

    bool showPanelsFullscreen() const;
    void setShowPanelsFullscreen(bool enabled);

    // Audio output: "auto" | "pipewire" | "pulse" | "alsa".
    QString audioOutput() const;
    void setAudioOutput(const QString &output);

    // Crossfade duration for local music, in seconds; 0 disables it.
    int musicCrossfade() const;
    void setMusicCrossfade(int seconds);

    // mpv hardware decoding: "auto", "auto-copy", "vaapi", "nvdec",
    // "no", etc. The value is written to luma.conf and applied to mpv.
    QString hwdec() const;
    void setHwdec(const QString &mode);

    // yt-dlp settings are stored in ~/.config/luma/yt-dlp.conf.
    QString ytdlpQuality() const;
    void setYtdlpQuality(const QString &quality);
    QString ytdlpProxy() const;
    void setYtdlpProxy(const QString &proxy);
    // Proxy type ("none", "socks5", "socks5h", "http", "https") and address
    // ("host:port") are edited in the settings as separate fields.
    QString ytdlpProxyType() const;
    void setYtdlpProxyType(const QString &type);
    QString ytdlpProxyAddress() const;
    void setYtdlpProxyAddress(const QString &address);
    // Ready-to-use -f expression for yt-dlp.
    QString ytdlpFormatExpression() const;

    // Path to the yt-dlp config (created on first launch).
    QString ytdlpConfigPath() const;
    QStringList ytdlpConfigArgs() const;

    // Log collection modes: "off", "error", "info", "debug". Stored in
    // luma.conf under [debug]; files go to ~/.cache/luma/{luma,mpv,yt-dlp}.log.
    QString lumaLogMode() const;
    void setLumaLogMode(const QString &mode);
    QString mpvLogMode() const;
    void setMpvLogMode(const QString &mode);
    QString ytdlpLogMode() const;
    void setYtdlpLogMode(const QString &mode);

    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);

    void saveWindowSize(int width, int height);

signals:
    void rememberVolumeChanged();
    void defaultVolumeChanged();
    void volumeChanged();
    void languageChanged();
    void playlistModeChanged();
    void appendToPlaylistChanged();
    void singleInstanceChanged();
    void notificationsChanged();
    void resumePlaybackChanged();
    void inhibitSleepChanged();
    void showChapterMarkersChanged();
    void controlBarModeChanged();
    void subtitlesChanged();
    void showPanelsFullscreenChanged();
    void audioOutputChanged();
    void musicCrossfadeChanged();
    void hwdecChanged();
    void ytdlpSettingsChanged();
    void loggingChanged();
    // luma.conf (or mpv.conf) changed on disk and was reloaded. Objects that
    // keep their own copy of settings (the equalizer) can pick up the values
    // written by another Luma instance.
    void settingsReloaded();

private:
    void loadPlaybackSettingsFromMpvConf();
    void savePlaybackToMpvConf();
    void updateProxyParts();
    void loadYtdlpConfig();
    void writeYtdlpConfig();
    // Another instance may rewrite the config files while this one is running;
    // watch them and reload, so the UI shows the values that are on disk.
    void setupFileWatcher();
    void reloadFromDisk();

    QSettings *m_settings;
    QString m_configDir;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_reloadTimer = nullptr;
    bool m_reloading = false;
    QString m_audioOutput = QStringLiteral("auto");
    QString m_hwdec = QStringLiteral("auto");
    QStringList m_ytdlpExtra;
    QString m_ytdlpQuality = "best";
    QString m_ytdlpProxy;
    QString m_ytdlpProxyType = "none";
    QString m_ytdlpProxyAddress;
};
