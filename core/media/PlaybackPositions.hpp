#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

// Cache of the last playback position for non-live media. It is a single JSON
// file in the cache directory: entries are keyed by the original file path or
// URL, and the cache is bounded by both an age limit and a maximum number of
// entries, so it cannot grow forever.
class PlaybackPositions {
public:
    explicit PlaybackPositions(int maxEntries = 500);

    // Missing file is not an error (starts empty); a corrupt file is ignored.
    bool load(const QString &filePath);
    bool loadFromData(const QByteArray &data);
    QByteArray serialize() const;

    // Position in seconds, or 0 when there is no resumable position.
    double positionFor(const QString &key) const;

    // Stores the position, or removes the entry when it is too close to the
    // start or to the end (a finished file must not resume at its last
    // second). Returns true when the cache changed.
    bool setPosition(const QString &key, double position, double duration,
                     qint64 nowSecs = -1);

    void remove(const QString &key);
    void prune(qint64 nowSecs = -1);

    int count() const { return m_entries.size(); }
    int maxEntries() const { return m_maxEntries; }
    void setMaxEntries(int maxEntries);

private:
    struct Entry {
        double position = 0.0;
        double duration = 0.0;
        qint64 updated = 0; // unix time, for the least-recently-used eviction
    };

    static qint64 now();

    QHash<QString, Entry> m_entries;
    int m_maxEntries;
};
