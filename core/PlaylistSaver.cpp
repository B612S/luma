#include "PlaylistSaver.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>

#include <utility>

// Lives in the saver's thread. It has no signals or slots: saves are queued
// with a functor, so Q_OBJECT is not needed.
class PlaylistWriter : public QObject {
public:
    explicit PlaylistWriter(QObject *parent = nullptr) : QObject(parent) {}

    void write(const QString &filePath, const QString &id, const QString &name,
               const QString &mode, const QVector<PlaylistModel::Entry> &entries,
               const QStringList &watchFolders)
    {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["mode"] = mode;
        obj["entries"] = PlaylistModel::entriesToJson(entries);
        if (!watchFolders.isEmpty())
            obj["watchFolders"] = QJsonArray::fromStringList(watchFolders);

        const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning("PlaylistSaver: cannot open %s", qPrintable(filePath));
            return;
        }
        if (file.write(data) != data.size() || !file.commit())
            qWarning("PlaylistSaver: cannot write %s", qPrintable(filePath));
    }
};

PlaylistSaver::PlaylistSaver(QObject *parent) : QObject(parent)
{
    m_writer = new PlaylistWriter;
    m_writer->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_writer, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PlaylistSaver"));
    m_thread.start();
}

PlaylistSaver::~PlaylistSaver()
{
    waitForIdle();
    m_thread.quit();
    m_thread.wait();
}

void PlaylistSaver::save(const QString &filePath, const QString &id, const QString &name,
                         const QString &mode, const QVector<PlaylistModel::Entry> &entries,
                         const QStringList &watchFolders)
{
    QVector<PlaylistModel::Entry> snapshot = entries;
    const QStringList folders = watchFolders;
    QMetaObject::invokeMethod(
        m_writer,
        [saver = this, writer = m_writer, filePath, id, name, mode,
         snapshot = std::move(snapshot), folders]() {
            writer->write(filePath, id, name, mode, snapshot, folders);
            QMetaObject::invokeMethod(
                saver, [saver, filePath]() { emit saver->saved(filePath); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void PlaylistSaver::waitForIdle()
{
    if (!m_thread.isRunning())
        return;
    Q_ASSERT(QThread::currentThread() != &m_thread);
    // Queued events are processed in order, so this runs after every save that
    // was queued before it. The connection is blocking: it returns only when
    // those writes are done.
    QMetaObject::invokeMethod(m_writer, []() {}, Qt::BlockingQueuedConnection);
}
