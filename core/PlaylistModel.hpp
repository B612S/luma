#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    struct Entry {
        QString path;
        QString kind;      // "video" | "audio"
        QString title;
        QString artist;
        QString album;
        int     year = 0;
        double  duration = 0.0;
        // The video title from yt-dlp is saved in the playlist; the live ICY title
        // of radio is not (it goes stale after a restart).
        bool persistTitle = false;
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Loading (does not emit modified, so nothing is saved when reading from disk) ---
    Q_INVOKABLE void setFiles(const QStringList &files, const QString &current);
    Q_INVOKABLE void setEntries(const QVariantList &entries, const QString &current);

    // --- Reading ---
    Q_INVOKABLE QString fileAt(int index) const;
    Q_INVOKABLE QString currentFile() const;
    Q_INVOKABLE int count() const { return m_entries.size(); }

    bool contains(const QString &path) const;
    int indexOf(const QString &path) const;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

    // Live stream title (radio): shown instead of the URL, not saved.
    void setStreamTitle(const QString &path, const QString &title);
    // Only the model that owns the active playback shows the ▶ marker.
    void setPlaying(bool playing);
    bool isPlaying() const { return m_isPlaying; }

    // Video title from yt-dlp: shown and saved in the playlist.
    void setVideoTitle(const QString &path, const QString &title);
    // Updates local audio metadata read in the background (tags/duration).
    bool updateMetadata(const QString &path, const QString &title, const QString &artist,
                        const QString &album, int year, double duration);
    Q_INVOKABLE void sortBy(const QString &key, bool ascending = true);

    // --- Changes (emit modified) ---
    Q_INVOKABLE void setShuffle(bool shuffle);
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE void removeItem(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void appendEntry(const QVariantMap &entry);

    // --- Serialization ---
    QVariantList toVariantList() const;
    const QVector<Entry> &entries() const { return m_entries; }

signals:
    void currentIndexChanged();
    void countChanged();
    void modified();

private:
    QVariant entryToVariant(const Entry &e) const;
    Entry variantToEntry(const QVariantMap &m) const;

    QStringList m_originalPaths;   // for disabling shuffle
    // Fast duplicate check when a whole folder is appended (appendEntry used
    // to scan every existing entry for every new file).
    QSet<QString> m_paths;
    QVector<Entry> m_entries;
    int m_currentIndex = -1;
    QString m_currentPlayingFile;
    bool m_isPlaying = true;
};
