#include "playlist/PlaylistFilterModel.hpp"

PlaylistFilterModel::PlaylistFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // Keep the view in sync when tags arrive or entries are renamed/moved.
    setDynamicSortFilter(true);
}

void PlaylistFilterModel::setPlaylistModel(PlaylistModel *model) {
    if (m_model == model) return;
    m_model = model;
    QSortFilterProxyModel::setSourceModel(model);
    emit playlistModelChanged();
}

void PlaylistFilterModel::setFilter(const QString &filter) {
    if (m_filter == filter) return;
    m_filter = filter;
    // invalidateFilter() is deprecated since Qt 6.13; the replacement exists
    // since Qt 6.9, which is Luma's minimum Qt version.
    beginFilterChange();
    endFilterChange();
    emit filterChanged();
}

int PlaylistFilterModel::mapToSource(int proxyRow) const {
    if (proxyRow < 0 || proxyRow >= rowCount()) return -1;
    const QModelIndex source = QSortFilterProxyModel::mapToSource(index(proxyRow, 0));
    return source.isValid() ? source.row() : -1;
}

int PlaylistFilterModel::mapFromSource(int sourceRow) const {
    QAbstractItemModel *source = sourceModel();
    if (!source || sourceRow < 0 || sourceRow >= source->rowCount()) return -1;
    const QModelIndex proxy = QSortFilterProxyModel::mapFromSource(
        source->index(sourceRow, 0));
    return proxy.isValid() ? proxy.row() : -1;
}

bool PlaylistFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    const QString needle = m_filter.trimmed();
    if (needle.isEmpty()) return true;
    if (!m_model) return true;

    const QModelIndex idx = m_model->index(sourceRow, 0, sourceParent);
    const QString display = m_model->data(idx, Qt::DisplayRole).toString();
    const QString title = m_model->data(idx, Qt::UserRole + 6).toString();
    const QString station = m_model->data(idx, Qt::UserRole + 8).toString();
    const QString artist = m_model->data(idx, Qt::UserRole + 3).toString();
    const QString path = m_model->data(idx, Qt::UserRole).toString();

    return display.contains(needle, Qt::CaseInsensitive)
        || title.contains(needle, Qt::CaseInsensitive)
        || station.contains(needle, Qt::CaseInsensitive)
        || artist.contains(needle, Qt::CaseInsensitive)
        || path.contains(needle, Qt::CaseInsensitive);
}
