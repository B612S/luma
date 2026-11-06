#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

class QFileSystemWatcher;

class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    struct Entry {
        QString path;
        QString kind;      // "video" | "audio"
        QString title;
        // Radio station name (the ICY "icy-name" header). Unlike the live
        // stream title it is stable, so it is saved and shown while the
        // station is not playing.
        QString station;
        QString artist;
        QString album;
        int     year = 0;
        // The video title from yt-dlp is saved in the playlist; the live ICY title
        // of radio is not (it goes stale after a restart).
        bool persistTitle = false;
        // The file was missing when it was last opened; saved so the red mark
        // survives a restart and cleared when the file comes back.
        bool unavailable = false;
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Loading (does not emit modified, so nothing is saved when reading from disk) ---
    Q_INVOKABLE void setFiles(const QStringList &files, const QString &current);
    Q_INVOKABLE void setEntries(const QVariantList &entries, const QString &current);
    // Installs entries prepared on a worker thread. Same as setFiles(), but
    // titles and file information are already resolved off the GUI thread.
    void setPreparedEntries(QVector<Entry> entries, const QString &current)
    {
        installEntries(std::move(entries), current);
    }
    // Same as setEntries(), but reads the objects directly from the playlist
    // file: the QVariantMap round trip was a large part of the load time.
    void setEntriesFromJson(const QJsonArray &entries, const QString &current);

    // --- Reading ---
    Q_INVOKABLE QString fileAt(int index) const;
    Q_INVOKABLE QString currentFile() const;
    Q_INVOKABLE int count() const { return m_entries.size(); }

    bool contains(const QString &path) const;
    int indexOf(const QString &path) const;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

    // Live stream title (radio): shown instead of the URL while it plays, not
    // saved (it goes stale).
    void setStreamTitle(const QString &path, const QString &title);
    // Radio station name from the stream's ICY headers: saved in the playlist
    // and shown when the station is not playing.
    void setStationTitle(const QString &path, const QString &name);
    // Only the model that owns the active playback shows the ▶ marker.
    void setPlaying(bool playing);
    bool isPlaying() const { return m_isPlaying; }

    // Video title from yt-dlp: shown and saved in the playlist.
    void setVideoTitle(const QString &path, const QString &title);
    // Deleted or moved files: the delegate draws such entries in the negative
    // color. The mark is saved in named playlists and the parent directory is
    // watched so it disappears when the file comes back.
    void setUnavailable(const QString &path, bool unavailable);
    bool isUnavailable(const QString &path) const { return m_unavailable.contains(path); }
    // setEntries() dropped a saved mark because the file exists again. The
    // owner should save the playlist so the stale field leaves the file.
    bool takeDroppedUnavailableMarks()
    {
        const bool dropped = m_droppedUnavailableMarks;
        m_droppedUnavailableMarks = false;
        return dropped;
    }
    // Updates local audio metadata read in the background (tags).
    bool updateMetadata(const QString &path, const QString &title, const QString &artist,
                        const QString &album, int year);
    Q_INVOKABLE void sortBy(const QString &key, bool ascending = true);

    // --- Changes (emit modified) ---
    Q_INVOKABLE void setShuffle(bool shuffle);
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE void removeItem(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void appendEntry(const QVariantMap &entry);

    // --- Serialization ---
    QVariantList toVariantList() const;
    // Playlist entries as the JSON objects written to disk. Building the file
    // directly from Entry skips the QVariantMap conversion on save.
    QJsonArray toJsonArray() const;
    static QJsonArray entriesToJson(const QVector<Entry> &entries);
    const QVector<Entry> &entries() const { return m_entries; }

signals:
    void currentIndexChanged();
    void countChanged();
    void modified();

private:
    void installEntries(QVector<Entry> &&entries, const QString &current);
    static Entry makeEntry(const QString &path, const QString &kind, const QString &title,
                           const QString &station, const QString &artist, const QString &album,
                           int year, bool unavailable);
    static Entry entryFromJson(const QJsonObject &obj);
    QVariant entryToVariant(const Entry &e) const;
    Entry variantToEntry(const QVariantMap &m) const;

    void clearUnavailable();
    void watchUnavailableDirectory(const QString &path);
    void pruneUnavailableWatcher(const QString &dir);
    void onUnavailableDirectoryChanged(const QString &dir);

    QStringList m_originalPaths;   // for disabling shuffle
    // Fast duplicate check when a whole folder is appended (appendEntry used
    // to scan every existing entry for every new file).
    QSet<QString> m_paths;
    QVector<Entry> m_entries;
    QSet<QString> m_unavailable;   // mirrors Entry::unavailable for the watcher
    bool m_droppedUnavailableMarks = false;
    QFileSystemWatcher *m_watcher = nullptr;
    int m_currentIndex = -1;
    QString m_currentPlayingFile;
    bool m_isPlaying = true;
};
