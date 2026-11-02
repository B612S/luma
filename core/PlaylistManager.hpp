#pragma once
#include <QObject>
#include <QVariantList>
#include <QHash>
#include <QStringList>
#include <QSet>
#include <QTimer>
#include "PlaylistModel.hpp"

class ConfigManager;
class TagScanner;

class PlaylistManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(QString activeId READ activeId WRITE setActiveId NOTIFY activeIdChanged)
    Q_PROPERTY(PlaylistModel* activeModel READ activeModel NOTIFY activeModelChanged)
    Q_PROPERTY(PlaylistModel* defaultModel READ defaultModel CONSTANT)

public:
    explicit PlaylistManager(ConfigManager *config, QObject *parent = nullptr);
    ~PlaylistManager() override;

    static const QString DEFAULT_ID;

    QVariantList playlists() const;
    QString activeId() const { return m_activeId; }
    void setActiveId(const QString &id);

    PlaylistModel* activeModel() const;
    PlaylistModel* defaultModel() const { return m_defaultModel; }
    Q_INVOKABLE PlaylistModel* modelById(const QString &id) const;

    Q_INVOKABLE QString createPlaylist(const QString &name);
    Q_INVOKABLE void deletePlaylist(const QString &id);
    Q_INVOKABLE void movePlaylist(int from, int to);
    Q_INVOKABLE void sortPlaylist(const QString &id, const QString &key, bool ascending);
    Q_INVOKABLE void shufflePlaylist(const QString &id);
    Q_INVOKABLE bool playlistHasAudio(const QString &id) const;
    Q_INVOKABLE void renamePlaylist(const QString &id, const QString &newName);
    Q_INVOKABLE void addFilesToPlaylist(const QString &id, const QStringList &paths);
    Q_INVOKABLE void addFolderToPlaylist(const QString &id, const QString &folder);
    Q_INVOKABLE void openAddFilesDialog(const QString &id);
    Q_INVOKABLE void openAddFolderDialog(const QString &id);
    Q_INVOKABLE void addUrlToPlaylist(const QString &id, const QString &url);
    void scanAudioPaths(const QStringList &paths);
    void setPlayingModel(PlaylistModel *model);
    Q_INVOKABLE QString playlistName(const QString &id) const;
    Q_INVOKABLE void saveActive();

    // Playback mode ("sequential" | "repeat" | "shuffle" | "stop") is stored
    // per playlist. The Default playlist takes its mode from the settings;
    // named playlists keep their own mode in the playlist file.
    Q_INVOKABLE QString playlistMode(const QString &id) const;
    Q_INVOKABLE void setPlaylistMode(const QString &id, const QString &mode);
    // Re-emits the translated playlist list after a language change.
    void retranslate();
    // Fetches the title of a link via yt-dlp (also used when the URL is edited).
    void fetchUrlTitle(const QString &id, const QString &url);

signals:
    void playlistsChanged();
    void activeIdChanged();
    void activeModelChanged();

private:
    void loadAll();
    void savePlaylist(const QString &id);
    void loadPlaylist(const QString &id);
    void saveOrder();
    // Changes (drag and drop, additions) arrive very often, so writing to disk
    // and the playlistsChanged signal are coalesced via a timer.
    void scheduleSave(const QString &id);
    void flushPendingSaves();
    QString playlistFilePath(const QString &id) const;
    QString sanitizeName(const QString &name) const;
    QStringList playlistOrder() const;
    QString playlistsDir() const;
    QVariantMap buildEntry(const QString &path) const;

    ConfigManager *m_config;
    PlaylistModel *m_defaultModel;
    QHash<QString, PlaylistModel*> m_models;
    QHash<QString, QString> m_names;
    QHash<QString, QString> m_modes;
    QStringList m_order;
    QString m_activeId = DEFAULT_ID;
    bool m_loading = false;
    QTimer m_saveTimer;
    TagScanner *m_tagScanner = nullptr;
    QSet<QString> m_dirtyIds;
    bool m_playlistsChangedPending = false;
};
