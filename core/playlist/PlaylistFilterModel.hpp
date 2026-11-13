#pragma once

#include <QSortFilterProxyModel>
#include <QPointer>
#include <QQmlEngine>

#include "playlist/PlaylistModel.hpp"

// Filters the active playlist for the search bar at the bottom of the panel.
// Matching is case-insensitive and looks at the displayed title, the artist and
// the path, so both "naruto" and a file name work.
class PlaylistFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PlaylistModel* playlistModel READ playlistModel WRITE setPlaylistModel
               NOTIFY playlistModelChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    explicit PlaylistFilterModel(QObject *parent = nullptr);

    PlaylistModel *playlistModel() const { return m_model; }
    void setPlaylistModel(PlaylistModel *model);

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);

    // Row mapping between the filtered view and the playlist itself. Both
    // return -1 when the row is not visible / no longer exists.
    Q_INVOKABLE int mapToSource(int proxyRow) const;
    Q_INVOKABLE int mapFromSource(int sourceRow) const;

signals:
    void playlistModelChanged();
    void filterChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QPointer<PlaylistModel> m_model;
    QString m_filter;
};
