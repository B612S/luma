#pragma once
#include <QAbstractListModel>
#include <QStringList>

class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
public:
    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void setFiles(const QStringList &files, const QString &current);
    Q_INVOKABLE QString fileAt(int index) const;
    Q_INVOKABLE QString currentFile() const;
    Q_INVOKABLE void setShuffle(bool shuffle);
    Q_INVOKABLE void moveItem(int from, int to);

    bool contains(const QString &path) const { return m_files.contains(path); }
    int indexOf(const QString &path) const { return m_files.indexOf(path); }

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

signals:
    void currentIndexChanged();

private:
    QStringList m_originalFiles;
    QStringList m_files;
    int m_currentIndex = -1;
    QString m_currentPlayingFile;
};
