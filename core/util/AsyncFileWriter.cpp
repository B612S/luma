#include "util/AsyncFileWriter.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QDebug>

#include <utility>

// Lives in the writer's thread; no signals or slots are needed because writes
// are queued as functors.
class AsyncFileWriteWorker : public QObject {
public:
    explicit AsyncFileWriteWorker(QObject *parent = nullptr) : QObject(parent) {}

    void write(const QString &filePath, const QByteArray &data)
    {
        const QString dir = QFileInfo(filePath).absolutePath();
        if (!dir.isEmpty() && !QDir().mkpath(dir)) {
            qWarning("AsyncFileWriter: cannot create %s", qPrintable(dir));
            return;
        }
        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning("AsyncFileWriter: cannot open %s", qPrintable(filePath));
            return;
        }
        if (file.write(data) != data.size() || !file.commit())
            qWarning("AsyncFileWriter: cannot write %s", qPrintable(filePath));
    }
};

AsyncFileWriter::AsyncFileWriter(QObject *parent) : QObject(parent)
{
    m_worker = new AsyncFileWriteWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("AsyncFileWriter"));
    m_thread.start();
}

AsyncFileWriter::~AsyncFileWriter()
{
    waitForIdle();
    m_thread.quit();
    m_thread.wait();
}

void AsyncFileWriter::write(const QString &filePath, const QByteArray &data)
{
    QMetaObject::invokeMethod(
        m_worker,
        [worker = m_worker, filePath, data]() { worker->write(filePath, data); },
        Qt::QueuedConnection);
}

void AsyncFileWriter::waitForIdle()
{
    if (!m_thread.isRunning())
        return;
    Q_ASSERT(QThread::currentThread() != &m_thread);
    QMetaObject::invokeMethod(m_worker, []() {}, Qt::BlockingQueuedConnection);
}
