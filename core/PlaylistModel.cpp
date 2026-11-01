#include "PlaylistModel.hpp"
#include <QFileInfo>
#include <QUrl>
#include <QRandomGenerator>

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractListModel(parent) {}

void PlaylistModel::setStreamTitle(const QString &path, const QString &title) {
    if (title.isEmpty()) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path != path) continue;
        if (m_entries[i].title == title) return;
        m_entries[i].title = title;
        emit dataChanged(index(i), index(i), {Qt::DisplayRole, Qt::UserRole + 6});
    }
}

int PlaylistModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size()) return {};

    const Entry &e = m_entries[index.row()];
    switch (role) {
        case Qt::DisplayRole: {
            QString name = e.title.isEmpty() ? QFileInfo(e.path).fileName() : e.title;
            if (name.isEmpty() && e.path.contains(QStringLiteral("://"))) {
                const QUrl url(e.path);
                name = url.host();
                if (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
                    name += url.path();
            }
            if (index.row() == m_currentIndex) return "▶ " + name;
            return name;
        }
        case Qt::UserRole:      return e.path;
        case Qt::UserRole + 1:  return index.row() == m_currentIndex;
        case Qt::UserRole + 2:  return e.kind;
        case Qt::UserRole + 3:  return e.artist;
        case Qt::UserRole + 4:  return e.album;
        case Qt::UserRole + 5:  return e.duration;
        case Qt::UserRole + 6:  return e.title;
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
        {Qt::UserRole + 5,   "duration"},
        {Qt::UserRole + 6,   "title"}
    };
}

QVariant PlaylistModel::entryToVariant(const Entry &e) const {
    QVariantMap m;
    m["path"] = e.path;
    m["kind"] = e.kind;
    // Живой заголовок потока не сохраняем: после перезапуска он устареет.
    if (!e.path.contains(QStringLiteral("://")))
        m["title"] = e.title;
    m["artist"] = e.artist;
    m["album"] = e.album;
    m["duration"] = e.duration;
    return m;
}

PlaylistModel::Entry PlaylistModel::variantToEntry(const QVariantMap &m) const {
    Entry e;
    e.path = m.value("path").toString();
    e.kind = m.value("kind").toString();
    if (e.kind.isEmpty()) e.kind = "video";
    e.title = m.value("title").toString();
    e.artist = m.value("artist").toString();
    e.album = m.value("album").toString();
    e.duration = m.value("duration").toDouble();
    if (e.title.isEmpty()) e.title = QFileInfo(e.path).fileName();
    return e;
}

// ---------------------------------------------------------------------------
// Загрузка (НЕ эмитит modified — чтобы не сохранять при чтении с диска)
// ---------------------------------------------------------------------------

void PlaylistModel::setFiles(const QStringList &files, const QString &current) {
    beginResetModel();
    m_entries.clear();
    m_originalPaths.clear();
    for (const QString &f : files) {
        Entry e;
        e.path = f;
        e.kind = "video";
        e.title = QFileInfo(f).fileName();
        m_entries.append(e);
        m_originalPaths << f;
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
    beginResetModel();
    m_entries.clear();
    m_originalPaths.clear();
    for (const QVariant &v : entries) {
        Entry e = variantToEntry(v.toMap());
        if (e.path.isEmpty()) continue;
        m_entries.append(e);
        m_originalPaths << e.path;
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

// ---------------------------------------------------------------------------
// Чтение
// ---------------------------------------------------------------------------

QString PlaylistModel::fileAt(int index) const {
    if (index >= 0 && index < m_entries.size()) return m_entries[index].path;
    return {};
}

QString PlaylistModel::currentFile() const {
    return fileAt(m_currentIndex);
}

bool PlaylistModel::contains(const QString &path) const {
    for (const Entry &e : m_entries) {
        if (e.path == path) return true;
    }
    return false;
}

int PlaylistModel::indexOf(const QString &path) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == path) return i;
    }
    return -1;
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
// Изменения (эмитят modified)
// ---------------------------------------------------------------------------

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
        // Восстанавливаем исходный порядок по m_originalPaths
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

    if (m_currentIndex == from) {
        m_currentIndex = to;
        emit currentIndexChanged();
    } else if (m_currentIndex > from && m_currentIndex <= to) {
        m_currentIndex--;
        emit currentIndexChanged();
    } else if (m_currentIndex < from && m_currentIndex >= to) {
        m_currentIndex++;
        emit currentIndexChanged();
    }

    endMoveRows();

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
    endRemoveRows();

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
    beginResetModel();
    m_entries.clear();
    m_originalPaths.clear();
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

    // Отсев дубликатов по path
    for (const Entry &existing : m_entries) {
        if (existing.path == e.path) return;
    }

    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append(e);
    m_originalPaths << e.path;
    endInsertRows();

    emit countChanged();
    emit modified();
}

// ---------------------------------------------------------------------------
// Сериализация
// ---------------------------------------------------------------------------

QVariantList PlaylistModel::toVariantList() const {
    QVariantList list;
    list.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        list.append(entryToVariant(e));
    }
    return list;
}
