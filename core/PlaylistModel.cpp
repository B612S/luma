#include "PlaylistModel.hpp"
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QUrl>
#include <QRandomGenerator>
#include <algorithm>
#include <utility>

namespace {
QJsonObject entryToJsonObject(const PlaylistModel::Entry &e)
{
    QJsonObject obj;
    obj["path"] = e.path;
    obj["kind"] = e.kind;
    // Don't save the live radio title: it goes stale after a restart.
    // Save the video title from yt-dlp.
    if (!e.title.isEmpty()
            && (!e.path.contains(QStringLiteral("://")) || e.persistTitle))
        obj["title"] = e.title;
    // The station name is stable, unlike the live radio tag: save it, so the
    // playlist shows the station after a restart instead of the URL.
    if (!e.station.isEmpty()) obj["station"] = e.station;
    // Empty metadata is not written: a video entry only needs path/kind/title.
    if (!e.artist.isEmpty()) obj["artist"] = e.artist;
    if (!e.album.isEmpty()) obj["album"] = e.album;
    if (e.year > 0) obj["year"] = e.year;
    // Saved only when set, so ordinary playlists stay unchanged.
    if (e.unavailable) obj["unavailable"] = true;
    return obj;
}
} // namespace

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractListModel(parent) {}

void PlaylistModel::setStreamTitle(const QString &path, const QString &title) {
    if (title.isEmpty()) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path != path) continue;
        if (m_entries[i].title == title) return;
        m_entries[i].title = title;
        // The live tag belongs to this moment only; never write it to disk,
        // even if the entry had a saved title before.
        m_entries[i].persistTitle = false;
        emit dataChanged(index(i), index(i), {Qt::DisplayRole, Qt::UserRole + 6});
    }
}

void PlaylistModel::setStationTitle(const QString &path, const QString &name) {
    if (name.isEmpty()) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &entry = m_entries[i];
        if (entry.path != path) continue;
        if (entry.station == name) return;
        entry.station = name;
        emit dataChanged(index(i), index(i), {Qt::DisplayRole, Qt::UserRole + 8});
        emit modified();
        return;
    }
}

void PlaylistModel::setVideoTitle(const QString &path, const QString &title) {
    if (title.isEmpty()) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path != path) continue;
        if (m_entries[i].title == title && m_entries[i].persistTitle) return;
        m_entries[i].title = title;
        m_entries[i].persistTitle = true;
        emit dataChanged(index(i), index(i), {Qt::DisplayRole, Qt::UserRole + 6});
        emit modified();
    }
}

bool PlaylistModel::updateMetadata(const QString &path, const QString &title,
                                    const QString &artist, const QString &album,
                                    int year)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        if (e.path != path)
            continue;

        const QString newTitle = title.isEmpty() && !e.path.contains(QStringLiteral("://"))
                                 ? QFileInfo(e.path).completeBaseName()
                                 : title;
        if (e.title == newTitle && e.artist == artist && e.album == album
                && e.year == year) {
            return false;
        }

        e.title = newTitle;
        e.artist = artist;
        e.album = album;
        e.year = year;
        emit dataChanged(index(i), index(i),
                         {Qt::DisplayRole, Qt::UserRole + 3, Qt::UserRole + 4,
                          Qt::UserRole + 6});
        emit modified();
        return true;
    }
    return false;
}

void PlaylistModel::setUnavailable(const QString &path, bool unavailable) {
    if (path.isEmpty()) return;
    if (m_unavailable.contains(path) == unavailable) return;

    bool changed = false;
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &entry = m_entries[i];
        if (entry.path != path || entry.unavailable == unavailable) continue;
        entry.unavailable = unavailable;
        changed = true;
        emit dataChanged(index(i), index(i), {Qt::UserRole + 7});
    }

    if (unavailable) {
        m_unavailable.insert(path);
        watchUnavailableDirectory(path);
    } else {
        m_unavailable.remove(path);
        pruneUnavailableWatcher(QFileInfo(path).absolutePath());
    }

    // Named playlists save the mark, so a deleted file stays red after a restart.
    if (changed) emit modified();
}

void PlaylistModel::pruneUnavailableWatcher(const QString &dir) {
    if (!m_watcher || dir.isEmpty()) return;
    for (const QString &missing : m_unavailable) {
        if (QFileInfo(missing).absolutePath() == dir) return;
    }
    m_watcher->removePath(dir);
}

void PlaylistModel::watchUnavailableDirectory(const QString &path) {
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
                &PlaylistModel::onUnavailableDirectoryChanged);
    }

    const QString dir = QFileInfo(path).absolutePath();
    if (dir.isEmpty() || m_watcher->directories().contains(dir)) return;
    if (QFileInfo(dir).isDir()) m_watcher->addPath(dir);
}

// A watched directory changed (for example, the user restored a deleted file):
// drop the mark for every missing entry that exists again and save that.
void PlaylistModel::onUnavailableDirectoryChanged(const QString &dir) {
    bool changed = false;
    for (auto it = m_unavailable.begin(); it != m_unavailable.end(); ) {
        const QString path = *it;
        if (QFileInfo(path).absolutePath() != dir || !QFileInfo::exists(path)) {
            ++it;
            continue;
        }
        it = m_unavailable.erase(it);
        for (int i = 0; i < m_entries.size(); ++i) {
            Entry &entry = m_entries[i];
            if (entry.path != path || !entry.unavailable) continue;
            entry.unavailable = false;
            changed = true;
            emit dataChanged(index(i), index(i), {Qt::UserRole + 7});
        }
    }

    pruneUnavailableWatcher(dir);
    if (changed) emit modified();
}

void PlaylistModel::clearUnavailable() {
    if (m_watcher) {
        const QStringList dirs = m_watcher->directories();
        if (!dirs.isEmpty()) m_watcher->removePaths(dirs);
    }
    m_unavailable.clear();
}

int PlaylistModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size()) return {};

    const Entry &e = m_entries[index.row()];
    switch (role) {
        case Qt::DisplayRole: {
            const bool current = m_isPlaying && index.row() == m_currentIndex;
            QString name;
            if (!e.station.isEmpty()) {
                // Radio: the live tag while this station plays, the station
                // name otherwise (including a stream that was never started).
                const bool hasLiveTitle = !e.title.isEmpty()
                                          && e.title != e.path
                                          && e.title != e.station;
                name = (current && hasLiveTitle) ? e.title : e.station;
            } else {
                name = e.title;
                if (e.path.contains(QStringLiteral("://"))) {
                    // Until yt-dlp returns a title (or if the link is broken),
                    // show the actual URL — including the query. Dropping it
                    // made every broken YouTube link look like a valid
                    // "youtube.com/watch".
                    const QString fileName = QFileInfo(e.path).fileName();
                    if (name.isEmpty() || (!fileName.isEmpty() && name == fileName))
                        name = e.path;
                } else {
                    // Don't show the extension for local files.
                    const QString fileName = QFileInfo(e.path).fileName();
                    if (name.isEmpty() || name == fileName)
                        name = QFileInfo(e.path).completeBaseName();
                }
                if (name.isEmpty()) name = e.path;
            }
            if (current) return "▶ " + name;
            return name;
        }
        case Qt::UserRole:      return e.path;
        case Qt::UserRole + 1:  return m_isPlaying && index.row() == m_currentIndex;
        case Qt::UserRole + 2:  return e.kind;
        case Qt::UserRole + 3:  return e.artist;
        case Qt::UserRole + 4:  return e.album;
        case Qt::UserRole + 6:  return e.title;
        case Qt::UserRole + 7:  return e.unavailable;
        case Qt::UserRole + 8:  return e.station;
        default: return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const {
    return {
        {Qt::DisplayRole,    "display"},
        {Qt::UserRole,       "path"},
        {Qt::UserRole + 1,   "isCurrent"},
        {Qt::UserRole + 2,   "kind"},
        {Qt::UserRole + 3,   "artist"},
        {Qt::UserRole + 4,   "album"},
        {Qt::UserRole + 6,   "title"},
        {Qt::UserRole + 7,   "unavailable"},
        {Qt::UserRole + 8,   "station"}
    };
}

QVariant PlaylistModel::entryToVariant(const Entry &e) const {
    return entryToJsonObject(e).toVariantMap();
}

PlaylistModel::Entry PlaylistModel::makeEntry(const QString &path, const QString &kind,
                                              const QString &title, const QString &station,
                                              const QString &artist, const QString &album,
                                              int year, bool unavailable)
{
    Entry e;
    e.path = path;
    e.kind = kind;
    if (e.kind.isEmpty()) e.kind = "video";
    e.title = title;
    e.station = station;
    // Old playlists stored the file name (with extension) as the title.
    if (!e.path.contains(QStringLiteral("://")) && e.title == QFileInfo(e.path).fileName())
        e.title = QFileInfo(e.path).completeBaseName();
    e.persistTitle = !e.title.isEmpty();
    e.artist = artist;
    e.album = album;
    e.year = year;
    e.unavailable = unavailable;
    if (e.title.isEmpty() && !e.path.contains(QStringLiteral("://")))
        e.title = QFileInfo(e.path).completeBaseName();
    return e;
}

PlaylistModel::Entry PlaylistModel::entryFromJson(const QJsonObject &obj)
{
    return makeEntry(obj.value(QStringLiteral("path")).toString(),
                     obj.value(QStringLiteral("kind")).toString(),
                     obj.value(QStringLiteral("title")).toString(),
                     obj.value(QStringLiteral("station")).toString(),
                     obj.value(QStringLiteral("artist")).toString(),
                     obj.value(QStringLiteral("album")).toString(),
                     obj.value(QStringLiteral("year")).toInt(),
                     obj.value(QStringLiteral("unavailable")).toBool());
}

PlaylistModel::Entry PlaylistModel::variantToEntry(const QVariantMap &m) const {
    return makeEntry(m.value("path").toString(),
                     m.value("kind").toString(),
                     m.value("title").toString(),
                     m.value("station").toString(),
                     m.value("artist").toString(),
                     m.value("album").toString(),
                     m.value("year").toInt(),
                     m.value("unavailable").toBool());
}

// ---------------------------------------------------------------------------
// Loading (does NOT emit modified — so nothing is saved when reading from disk)
// ---------------------------------------------------------------------------

void PlaylistModel::setFiles(const QStringList &files, const QString &current) {
    clearUnavailable();
    beginResetModel();
    m_entries.clear();
    m_originalPaths.clear();
    m_paths.clear();
    for (const QString &f : files) {
        Entry e;
        e.path = f;
        e.kind = "video";
        e.title = f.contains(QStringLiteral("://")) ? QString()
                                                    : QFileInfo(f).completeBaseName();
        m_entries.append(e);
        m_originalPaths << f;
        m_paths.insert(f);
    }
    m_currentPlayingFile = current;
    m_currentIndex = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == current) { m_currentIndex = i; break; }
    }
    endResetModel();
    emit currentIndexChanged();
    emit countChanged();
}

void PlaylistModel::setEntries(const QVariantList &entries, const QString &current) {
    QVector<Entry> list;
    list.reserve(entries.size());
    for (const QVariant &v : entries) {
        Entry e = variantToEntry(v.toMap());
        if (e.path.isEmpty()) continue;
        list.append(e);
    }
    installEntries(std::move(list), current);
}

void PlaylistModel::setEntriesFromJson(const QJsonArray &entries, const QString &current) {
    QVector<Entry> list;
    list.reserve(entries.size());
    for (const QJsonValue &v : entries) {
        if (!v.isObject()) continue;
        Entry e = entryFromJson(v.toObject());
        if (e.path.isEmpty()) continue;
        list.append(e);
    }
    installEntries(std::move(list), current);
}

void PlaylistModel::installEntries(QVector<Entry> &&entries, const QString &current) {
    clearUnavailable();
    m_droppedUnavailableMarks = false;
    beginResetModel();
    m_entries = std::move(entries);
    m_originalPaths.clear();
    m_paths.clear();
    m_originalPaths.reserve(m_entries.size());
    m_paths.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        m_originalPaths << e.path;
        m_paths.insert(e.path);
    }
    m_currentPlayingFile = current;
    m_currentIndex = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == current) { m_currentIndex = i; break; }
    }

    // Restore the saved "unavailable" marks. A file that came back while Luma
    // was closed is available again, so its stale mark is dropped (the next
    // save rewrites the entry).
    for (Entry &entry : m_entries) {
        if (!entry.unavailable) continue;
        if (QFileInfo::exists(entry.path)) {
            entry.unavailable = false;
            m_droppedUnavailableMarks = true;
            continue;
        }
        m_unavailable.insert(entry.path);
    }

    endResetModel();
    emit currentIndexChanged();
    emit countChanged();

    for (const QString &path : m_unavailable)
        watchUnavailableDirectory(path);
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

QString PlaylistModel::fileAt(int index) const {
    if (index >= 0 && index < m_entries.size()) return m_entries[index].path;
    return {};
}

QString PlaylistModel::currentFile() const {
    return fileAt(m_currentIndex);
}

bool PlaylistModel::contains(const QString &path) const {
    return m_paths.contains(path);
}

int PlaylistModel::indexOf(const QString &path) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == path) return i;
    }
    return -1;
}

void PlaylistModel::setPlaying(bool playing)
{
    if (m_isPlaying == playing) return;
    m_isPlaying = playing;
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        emit dataChanged(index(m_currentIndex), index(m_currentIndex),
                         {Qt::DisplayRole, Qt::UserRole + 1});
    }
}

void PlaylistModel::setCurrentIndex(int idx) {
    if (m_currentIndex == idx) return;

    int oldIndex = m_currentIndex;
    m_currentIndex = idx;

    if (idx >= 0 && idx < m_entries.size()) {
        m_currentPlayingFile = m_entries[idx].path;
    }

    emit currentIndexChanged();

    if (oldIndex >= 0 && oldIndex < m_entries.size())
        emit dataChanged(index(oldIndex), index(oldIndex));
    if (idx >= 0 && idx < m_entries.size())
        emit dataChanged(index(idx), index(idx));
}

// ---------------------------------------------------------------------------
// Changes (emit modified)
// ---------------------------------------------------------------------------

void PlaylistModel::sortBy(const QString &key, bool ascending)
{
    if (m_entries.size() < 2)
        return;

    auto keyValue = [&key](const Entry &e) {
        if (key == QLatin1String("artist")) return e.artist;
        if (key == QLatin1String("album")) return e.album;
        QString title = e.title;
        if (title.isEmpty()) title = e.station;
        if (title.isEmpty()) title = QFileInfo(e.path).completeBaseName();
        return title;
    };

    beginResetModel();
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [&](const Entry &a, const Entry &b) {
        if (key == QLatin1String("year")) {
            const bool aEmpty = a.year <= 0;
            const bool bEmpty = b.year <= 0;
            if (aEmpty != bEmpty) return !aEmpty; // entries without a year go last
            if (a.year != b.year)
                return ascending ? a.year < b.year : a.year > b.year;
        } else {
            const QString va = keyValue(a);
            const QString vb = keyValue(b);
            const bool aEmpty = va.isEmpty();
            const bool bEmpty = vb.isEmpty();
            if (aEmpty != bEmpty) return !aEmpty;
            const int cmp = QString::localeAwareCompare(va, vb);
            if (cmp != 0)
                return ascending ? cmp < 0 : cmp > 0;
        }

        const int titleCmp = QString::localeAwareCompare(a.title, b.title);
        if (titleCmp != 0)
            return ascending ? titleCmp < 0 : titleCmp > 0;
        return QString::localeAwareCompare(a.path, b.path) < 0;
    });

    m_originalPaths.clear();
    for (const Entry &e : m_entries)
        m_originalPaths << e.path;
    m_currentIndex = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == m_currentPlayingFile) {
            m_currentIndex = i;
            break;
        }
    }
    endResetModel();
    emit currentIndexChanged();
    emit modified();
}

void PlaylistModel::setShuffle(bool shuffle) {
    if (m_entries.isEmpty()) return;

    beginResetModel();
    if (shuffle) {
        QVector<Entry> others;
        Entry currentEntry;
        bool hasCurrent = false;

        for (const Entry &e : m_entries) {
            if (e.path == m_currentPlayingFile) {
                currentEntry = e;
                hasCurrent = true;
            } else {
                others.append(e);
            }
        }

        int n = others.size();
        for (int i = n - 1; i > 0; --i) {
            int j = QRandomGenerator::global()->bounded(i + 1);
            others.swapItemsAt(i, j);
        }

        m_entries.clear();
        if (hasCurrent) m_entries.append(currentEntry);
        m_entries.append(others);
    } else {
        // Restore the original order from m_originalPaths
        QVector<Entry> restored;
        for (const QString &p : m_originalPaths) {
            for (const Entry &e : m_entries) {
                if (e.path == p) {
                    restored.append(e);
                    break;
                }
            }
        }
        if (restored.size() == m_entries.size()) {
            m_entries = restored;
        }
    }

    m_currentIndex = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == m_currentPlayingFile) {
            m_currentIndex = i;
            break;
        }
    }

    endResetModel();
    emit currentIndexChanged();
    emit modified();
}

void PlaylistModel::moveItem(int from, int to) {
    if (from < 0 || from >= m_entries.size() ||
        to < 0 || to >= m_entries.size() || from == to) return;

    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_entries.move(from, to);

    const int oldCurrentIndex = m_currentIndex;
    if (m_currentIndex == from) {
        m_currentIndex = to;
    } else if (m_currentIndex > from && m_currentIndex <= to) {
        m_currentIndex--;
    } else if (m_currentIndex < from && m_currentIndex >= to) {
        m_currentIndex++;
    }

    endMoveRows();

    // Emit only after endMoveRows(): a QML view reacting to a current-index
    // change in the middle of a row move may scroll or relayout while the move
    // is in progress, which made the playlist jump during drag & drop.
    if (m_currentIndex != oldCurrentIndex)
        emit currentIndexChanged();

    m_originalPaths.clear();
    for (const Entry &e : m_entries) m_originalPaths << e.path;

    emit modified();
}

void PlaylistModel::removeItem(int index) {
    if (index < 0 || index >= m_entries.size()) return;

    bool wasCurrent = (m_currentIndex == index);

    beginRemoveRows(QModelIndex(), index, index);
    QString path = m_entries[index].path;
    m_entries.removeAt(index);
    m_originalPaths.removeAll(path);
    m_paths.remove(path);
    // Playlist files from old versions may contain duplicate paths.
    for (const Entry &e : m_entries) {
        if (e.path == path) {
            m_paths.insert(path);
            break;
        }
    }
    endRemoveRows();

    // No duplicate left: the runtime "unavailable" mark no longer applies.
    if (indexOf(path) < 0) setUnavailable(path, false);

    if (wasCurrent) {
        if (m_entries.isEmpty()) {
            m_currentIndex = -1;
            m_currentPlayingFile.clear();
        } else {
            m_currentIndex = qMin(index, m_entries.size() - 1);
            m_currentPlayingFile = m_entries[m_currentIndex].path;
            emit dataChanged(this->index(m_currentIndex), this->index(m_currentIndex));
        }
        emit currentIndexChanged();
    } else if (m_currentIndex > index) {
        m_currentIndex--;
        emit currentIndexChanged();
        emit dataChanged(this->index(m_currentIndex), this->index(m_currentIndex));
    }

    emit countChanged();
    emit modified();
}

void PlaylistModel::clear() {
    clearUnavailable();
    beginResetModel();
    m_entries.clear();
    m_originalPaths.clear();
    m_paths.clear();
    m_currentIndex = -1;
    m_currentPlayingFile.clear();
    endResetModel();
    emit currentIndexChanged();
    emit countChanged();
    emit modified();
}

void PlaylistModel::appendEntry(const QVariantMap &entry) {
    Entry e = variantToEntry(entry);
    if (e.path.isEmpty()) return;

    // Filter out duplicates by path (O(1) even for large folders).
    if (m_paths.contains(e.path)) return;

    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append(e);
    m_originalPaths << e.path;
    m_paths.insert(e.path);
    endInsertRows();

    emit countChanged();
    emit modified();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

QVariantList PlaylistModel::toVariantList() const {
    QVariantList list;
    list.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        list.append(entryToVariant(e));
    }
    return list;
}

QJsonArray PlaylistModel::entriesToJson(const QVector<Entry> &entries) {
    QJsonArray array;
    for (const Entry &e : entries)
        array.append(entryToJsonObject(e));
    return array;
}

QJsonArray PlaylistModel::toJsonArray() const {
    return entriesToJson(m_entries);
}
