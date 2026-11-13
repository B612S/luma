#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

class AsyncFileWriteWorker;

// Writes small cache files in a background thread. QSaveFile's fsync on the
// GUI thread can stall for tens or hundreds of milliseconds on slow storage;
// position/state caches are not worth a dropped video frame.
class AsyncFileWriter : public QObject {
    Q_OBJECT

public:
    explicit AsyncFileWriter(QObject *parent = nullptr);
    ~AsyncFileWriter() override;

    // Queues an atomic replace of filePath. The data is copied (implicitly
    // shared), the write happens in the worker thread.
    void write(const QString &filePath, const QByteArray &data);

    // Blocks until every queued write has been completed.
    void waitForIdle();

private:
    QThread m_thread;
    AsyncFileWriteWorker *m_worker = nullptr;
};
