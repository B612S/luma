#include "PlaylistModel.hpp"
#include <QFileInfo>
#include <QRandomGenerator>

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractListModel(parent) {}

int PlaylistModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_files.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_files.size()) return {};

    if (role == Qt::DisplayRole) {
        QString name = QFileInfo(m_files[index.row()]).fileName();
        if (index.row() == m_currentIndex) return "▶ " + name;
        return name;
    }
    if (role == Qt::UserRole) return m_files[index.row()];
    if (role == Qt::UserRole + 1) return index.row() == m_currentIndex;
    return {};
}

QHash<int, QByteArray> PlaylistModel::roleNames() const {
    return {
        {Qt::DisplayRole, "display"},
        {Qt::UserRole, "path"},
        {Qt::UserRole + 1, "isCurrent"}
    };
}

void PlaylistModel::setFiles(const QStringList &files, const QString &current) {
    beginResetModel();
    m_originalFiles = files;
    m_files = files;
    m_currentPlayingFile = current;
    m_currentIndex = m_files.indexOf(current);
    endResetModel();
    emit currentIndexChanged();
}

QString PlaylistModel::fileAt(int index) const {
    if (index >= 0 && index < m_files.size()) return m_files[index];
    return {};
}

QString PlaylistModel::currentFile() const {
    return fileAt(m_currentIndex);
}

void PlaylistModel::setCurrentIndex(int idx) {
    if (m_currentIndex == idx) return;

    int oldIndex = m_currentIndex;
    m_currentIndex = idx;

    if (idx >= 0 && idx < m_files.size()) {
        m_currentPlayingFile = m_files[idx];
    }

    emit currentIndexChanged();

    if (oldIndex >= 0 && oldIndex < m_files.size()) emit dataChanged(index(oldIndex), index(oldIndex));
    if (idx >= 0 && idx < m_files.size()) emit dataChanged(index(idx), index(idx));
}

void PlaylistModel::setShuffle(bool shuffle) {
    if (m_files.isEmpty()) return;

    beginResetModel();
    if (shuffle) {
        // Убираем текущий играющий файл из списка
        QStringList otherFiles = m_originalFiles;
        otherFiles.removeAll(m_currentPlayingFile);

        // Перемешиваем остальные
        int n = otherFiles.size();
        for (int i = n - 1; i > 0; --i) {
            int j = QRandomGenerator::global()->bounded(i + 1);
            otherFiles.swapItemsAt(i, j);
        }

        // Ставим текущий файл на первое место, остальные за ним
        m_files.clear();
        if (!m_currentPlayingFile.isEmpty()) {
            m_files.append(m_currentPlayingFile);
        }
        m_files.append(otherFiles);
    } else {
        m_files = m_originalFiles;
    }

    m_currentIndex = m_files.indexOf(m_currentPlayingFile);
    endResetModel();
    emit currentIndexChanged();
}

void PlaylistModel::moveItem(int from, int to) {
    if (from < 0 || from >= m_files.size() || to < 0 || to >= m_files.size() || from == to) return;

    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_files.move(from, to);

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
    m_originalFiles = m_files;
}

void PlaylistModel::removeItem(int index) {
    if (index < 0 || index >= m_files.size()) return;

    // Запоминаем, был ли удален текущий трек
    bool wasCurrent = (m_currentIndex == index);

    beginRemoveRows(QModelIndex(), index, index);
    QString file = m_files.takeAt(index);
    m_originalFiles.removeAll(file);
    endRemoveRows();

    if (wasCurrent) {
        if (m_files.isEmpty()) {
            m_currentIndex = -1;
            m_currentPlayingFile.clear();
        } else {
            // Если удалили текущий, новый текущий — тот, что встал на его место
            // (или последний, если удалили последний)
            m_currentIndex = qMin(index, m_files.size() - 1);
            m_currentPlayingFile = m_files[m_currentIndex];
            // Сообщаем QML, что у нового текущего элемента изменились данные
            emit dataChanged(this->index(m_currentIndex), this->index(m_currentIndex));
        }
        emit currentIndexChanged();
    } else if (m_currentIndex > index) {
        m_currentIndex--;
        emit currentIndexChanged();
        // Если текущий трек сместился, нужно обновить его отображение (жирный шрифт)
        emit dataChanged(this->index(m_currentIndex), this->index(m_currentIndex));
    }
}
