#pragma once
#include <QObject>
#include <QSettings>
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

private:
    QSettings *m_settings;
};
