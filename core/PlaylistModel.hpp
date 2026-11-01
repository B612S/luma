#pragma once
#include <QAbstractListModel>
#include <QVector>
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
        double  duration = 0.0;
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Загрузка (не эмитит modified, чтобы не сохранять при чтении с диска) ---
    Q_INVOKABLE void setFiles(const QStringList &files, const QString &current);
    Q_INVOKABLE void setEntries(const QVariantList &entries, const QString &current);

    // --- Чтение ---
    Q_INVOKABLE QString fileAt(int index) const;
    Q_INVOKABLE QString currentFile() const;
    Q_INVOKABLE int count() const { return m_entries.size(); }

    bool contains(const QString &path) const;
    int indexOf(const QString &path) const;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int idx);

    // Живой заголовок потока (радио): показываем вместо ссылки, не сохраняя.
    void setStreamTitle(const QString &path, const QString &title);

    // --- Изменения (эмитят modified) ---
    Q_INVOKABLE void setShuffle(bool shuffle);
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE void removeItem(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void appendEntry(const QVariantMap &entry);

    // --- Сериализация ---
    QVariantList toVariantList() const;
    QVector<Entry> entries() const { return m_entries; }

signals:
    void currentIndexChanged();
    void countChanged();
    void modified();

private:
    QVariant entryToVariant(const Entry &e) const;
    Entry variantToEntry(const QVariantMap &m) const;

    QStringList m_originalPaths;   // для отключения shuffle
    QVector<Entry> m_entries;
    int m_currentIndex = -1;
    QString m_currentPlayingFile;
};
