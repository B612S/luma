#pragma once
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>

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
    Q_PROPERTY(QString ytdlpQuality READ ytdlpQuality WRITE setYtdlpQuality NOTIFY ytdlpSettingsChanged)
    Q_PROPERTY(QString ytdlpProxy READ ytdlpProxy WRITE setYtdlpProxy NOTIFY ytdlpSettingsChanged)

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

    QString playlistMode() const;
    void setPlaylistMode(const QString &mode);

    bool appendToPlaylist() const;
    void setAppendToPlaylist(bool v);

    bool singleInstance() const;
    void setSingleInstance(bool enabled);

    bool notifications() const;
    void setNotifications(bool enabled);

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

    // Параметры yt-dlp хранятся в ~/.config/luma/yt-dlp.conf.
    QString ytdlpQuality() const;
    void setYtdlpQuality(const QString &quality);
    QString ytdlpProxy() const;
    void setYtdlpProxy(const QString &proxy);
    // Готовое выражение -f для yt-dlp.
    QString ytdlpFormatExpression() const;

    // Путь к конфигурации yt-dlp (создаётся при первом запуске).
    QString ytdlpConfigPath() const;

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
    void controlBarModeChanged();
    void subtitlesChanged();
    void showPanelsFullscreenChanged();
    void ytdlpSettingsChanged();

private:
    void loadYtdlpConfig();
    void writeYtdlpConfig();

    QSettings *m_settings;
    QString m_configDir;
    QStringList m_ytdlpExtra;
    QString m_ytdlpQuality = "best";
    QString m_ytdlpProxy;
};
