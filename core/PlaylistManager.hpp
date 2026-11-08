#pragma once
#include <QObject>
#include <QVariantList>
#include <QHash>
#include <QStringList>
#include <QSet>
#include <QTimer>
#include "PlaylistModel.hpp"

class ConfigManager;
class PlaylistSaver;
class QFileSystemWatcher;
class TagScanner;
class RadioNameResolver;

class PlaylistManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(QString activeId READ activeId WRITE setActiveId NOTIFY activeIdChanged)
    // Id of the playlist whose entry is actually playing (may differ from the
    // open tab). The tab bar uses it to emphasise the playing playlist.
    Q_PROPERTY(QString playingId READ playingId NOTIFY playingIdChanged)
    Q_PROPERTY(PlaylistModel* activeModel READ activeModel NOTIFY activeModelChanged)
    Q_PROPERTY(PlaylistModel* defaultModel READ defaultModel CONSTANT)

public:
    explicit PlaylistManager(ConfigManager *config, QObject *parent = nullptr);
    ~PlaylistManager() override;

    static const QString DEFAULT_ID;

    QVariantList playlists() const;
    QString activeId() const { return m_activeId; }
    QString playingId() const;
    void setActiveId(const QString &id);

    PlaylistModel* activeModel() const;
    PlaylistModel* defaultModel() const { return m_defaultModel; }
    // Model that owns the current playback; the active (viewed) model can be
    // a different tab after the user switches playlists.
    PlaylistModel* playingModel() const { return m_playingModel; }
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
    // Folders watched by a playlist: existing media is imported in the
    // background when a folder is added, and new files are appended when they
    // appear (QFileSystemWatcher + debounced background scan, no polling).
    Q_INVOKABLE QStringList watchFolders(const QString &id) const;
    Q_INVOKABLE bool addWatchFolder(const QString &id, const QString &folder);
    Q_INVOKABLE void removeWatchFolder(const QString &id, const QString &folder);
    Q_INVOKABLE void openAddWatchFolderDialog(const QString &id);
    Q_INVOKABLE bool isDefaultPlaylist(const QString &id) const;
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
    // Playback mode of the playlist that owns a model.
    QString playlistModeFor(PlaylistModel *model) const;
    // Re-emits the translated playlist list after a language change.
    void retranslate();
    // Fetches the title of a link via yt-dlp (also used when the URL is edited).
    void fetchUrlTitle(const QString &id, const QString &url);

signals:
    void playlistsChanged();
    void activeIdChanged();
    void playingIdChanged();
    void playlistModeChanged(const QString &id, const QString &mode);
    void activeModelChanged();
    void watchFoldersChanged(const QString &id);

private:
    void loadAll();
    void savePlaylist(const QString &id);
    void loadPlaylist(const QString &id);
    // Serialises and writes in the background (see PlaylistSaver).
    void queuePlaylistSave(const QString &id);
    void scanPlaylistTags(const QString &id);
    // Probes the icy-name of radio links that have no station name yet. Runs
    // in the background; results are saved to the playlist.
    void resolveStationNames(PlaylistModel *model);
    void saveOrder();
    // Changes (drag and drop, additions) arrive very often, so writing to disk
    // and the playlistsChanged signal are coalesced via a timer.
    void scheduleSave(const QString &id);
    void flushPendingSaves();
    // Watch the named playlist files written by other Luma instances. The
    // Default playlist is intentionally not shared between windows.
    void setupPlaylistWatcher();
    void watchPlaylistFiles();
    void reloadExternalPlaylists();
    // Folder watching for playlists.
    void setupFolderWatching();
    void rebuildFolderWatcher();
    void scheduleWatchScan(const QString &id);
    void startWatchScan(const QString &id);
    void invalidateWatchScan(const QString &id);
    void stopWatchingPlaylist(const QString &id);
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
    PlaylistModel *m_playingModel = nullptr;
    bool m_loading = false;
    QTimer m_saveTimer;
    TagScanner *m_tagScanner = nullptr;
    PlaylistSaver *m_saver = nullptr;
    RadioNameResolver *m_radioNames = nullptr;
    // Station names learned in this session, so a link added to another
    // playlist gets its name without probing the stream again.
    QHash<QString, QString> m_stationNames;
    QSet<QString> m_dirtyIds;
    QSet<QString> m_scannedPlaylists;
    bool m_playlistsChangedPending = false;
    QFileSystemWatcher *m_playlistWatcher = nullptr;
    QTimer m_externalReloadTimer;
    bool m_reloadingExternal = false;
    // Playlist writes still queued in PlaylistSaver, so an external reload
    // does not read the previous snapshot from disk while our save is running.
    QHash<QString, int> m_pendingPlaylistSaves;
    // A file can be missing briefly while another instance atomically replaces
    // it; only remove the playlist after it stayed missing for a moment.
    QSet<QString> m_missingPlaylistIds;
    // Watch folders per playlist (including the per-window Default playlist).
    QHash<QString, QStringList> m_watchFolders;
    QHash<QString, QSet<QString>> m_watchedDirs;
    QHash<QString, QSet<QString>> m_folderDirOwners;
    QFileSystemWatcher *m_folderWatcher = nullptr;
    QTimer m_watchScanDebounce;
    QSet<QString> m_watchScanPending;
    QSet<QString> m_watchScanRunning;
    QSet<QString> m_watchScanQueued;
    QHash<QString, quint64> m_watchScanGeneration;
};
