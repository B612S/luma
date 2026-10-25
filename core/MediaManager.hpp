#pragma once
#include <QObject>
#include <QStringList>
#include <QElapsedTimer>
#include <QPointer>
#include <QDBusInterface>
#include "mpvqt/MpvItem.hpp"
#include "PlaylistModel.hpp"

class ConfigManager;

class MediaManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(PlaylistModel* playlist READ playlist CONSTANT)
    Q_PROPERTY(QString playMode READ playMode WRITE setPlayMode NOTIFY playModeChanged)
    Q_PROPERTY(bool isManualSwitch READ isManualSwitch NOTIFY isManualSwitchChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY fileChanged)

public:
    explicit MediaManager(MpvItem *mpv = nullptr, QObject *parent = nullptr);
    ~MediaManager() override;

    PlaylistModel* playlist() { return m_playlist; }
    void setMpv(MpvItem *mpv) { m_mpv = mpv; }
    void setConfig(ConfigManager *config) { m_config = config; }

    bool isManualSwitch() const { return m_isManualSwitch; }
    void setManualSwitch(bool val);

    QString playMode() const { return m_playMode; }
    void setPlayMode(const QString &mode);

    QString currentFileName() const;

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

    Q_INVOKABLE void openFileDialog();
    Q_INVOKABLE void openFolderDialog();
    Q_INVOKABLE void openUrlDialog();
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

private:
    void _sendNotification(const QString &body);

    MpvItem *m_mpv;
    PlaylistModel *m_playlist;
    ConfigManager *m_config = nullptr;
    QStringList m_history;
    QString m_playMode = "sequential";
    bool m_isManualSwitch = false;

    QString m_lastPlayedFile;
    QElapsedTimer m_lastPlayedTimer;

    QDBusInterface *m_notifyInterface = nullptr;
};
