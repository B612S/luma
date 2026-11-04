#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

#include "PlaylistModel.hpp"

class PlaylistWriter;

// Writes playlist files in the background. Serialising a large playlist and
// waiting for QSaveFile's fsync takes tens of milliseconds (hundreds on slow
// storage); doing that on the GUI thread made the window stutter on every
// autosave. The playlist is snapshotted in save(), the JSON is built and
// written in a worker thread.
class PlaylistSaver : public QObject {
    Q_OBJECT

public:
    explicit PlaylistSaver(QObject *parent = nullptr);
    ~PlaylistSaver() override;

    // Queues a write. The entries are copied (cheap: QString is implicitly
    // shared); the returned call does not wait for the disk.
    void save(const QString &filePath, const QString &id, const QString &name,
              const QString &mode, const QVector<PlaylistModel::Entry> &entries);

    // Blocks until every queued save has been written. Used before a
    // synchronous save (which must win over queued snapshots), before a
    // playlist is deleted, and on shutdown.
    void waitForIdle();

private:
    QThread m_thread;
    PlaylistWriter *m_writer = nullptr;
};
