#include "MediaNotifier.hpp"
#include "ConfigManager.hpp"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QtConcurrent/QtConcurrentRun>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

namespace {

// Audio file metadata for the notification.
struct TrackTags {
    QString title;
    QString artist;
    QString album;
    QString coverPath;
};

// Result of reading tags in a worker thread.
struct TrackTagsResult {
    bool ok = false;
    TrackTags tags;
};

QString dictValue(AVDictionary *meta, const char *key) {
    if (!meta) return QString();
    const AVDictionaryEntry *entry = av_dict_get(meta, key, nullptr, 0);
    return entry ? QString::fromUtf8(entry->value).trimmed() : QString();
}

QMutex &coverMutex()
{
    static QMutex mutex;
    return mutex;
}

QString coverCacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        if (base.isEmpty()) base = QDir::homePath() + "/.cache";
        base += "/luma";
    }
    return base + "/covers";
}

// The notification only needs the cover of the file that is playing now.
// Caller must hold coverMutex().
void removeOtherCoversLocked(const QString &keep)
{
    QDir dir(coverCacheDir());
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    for (const QFileInfo &fi : files) {
        if (fi.absoluteFilePath() != keep)
            QFile::remove(fi.absoluteFilePath());
    }
}

void clearCoverCache()
{
    QMutexLocker locker(&coverMutex());
    removeOtherCoversLocked(QString());
}

QString coverCachePath(const QString &sourcePath) {
    const QFileInfo fi(sourcePath);
    const QByteArray hash = QCryptographicHash::hash(
        (sourcePath + QString::number(fi.lastModified().toSecsSinceEpoch())).toUtf8(),
        QCryptographicHash::Md5).toHex();

    const QString dir = coverCacheDir();
    QDir().mkpath(dir);
    return dir + "/" + QString::fromLatin1(hash) + ".jpg";
}

// Extracts the embedded cover art (ID3 APIC / MP4 covr, etc.). Attached
// pictures are normal JPEG/PNG bytes; letting Qt decode them avoids a separate
// FFmpeg decoder and the fragile sws_scale path for unusual covers.
QImage decodeAttachedCover(AVFormatContext *ctx) {
    int streamIndex = -1;
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        if (ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            streamIndex = static_cast<int>(i);
            break;
        }
    }
    if (streamIndex < 0) return QImage();

    AVStream *stream = ctx->streams[streamIndex];
    if (!stream->attached_pic.data || stream->attached_pic.size <= 0) return QImage();

    const QByteArray data(reinterpret_cast<const char *>(stream->attached_pic.data),
                          stream->attached_pic.size);
    return QImage::fromData(data);
}

// Reads the tags; returns true if anything was found.
bool readTrackTags(const QString &path, TrackTags &tags) {
    AVFormatContext *ctx = nullptr;
    if (avformat_open_input(&ctx, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;

    tags.title = dictValue(ctx->metadata, "title");
    tags.artist = dictValue(ctx->metadata, "artist");
    if (tags.artist.isEmpty()) tags.artist = dictValue(ctx->metadata, "album_artist");
    tags.album = dictValue(ctx->metadata, "album");

    const bool hasTags = !tags.title.isEmpty() || !tags.artist.isEmpty() || !tags.album.isEmpty();
    {
        // Notification tasks can run concurrently for two quick file switches;
        // serialize the cover-cache check/write. Only the current file keeps
        // its cover: everything else in the covers directory is removed.
        QMutexLocker locker(&coverMutex());
        if (hasTags) {
            const QString coverPath = coverCachePath(path);
            if (QFile::exists(coverPath)) {
                tags.coverPath = coverPath;
            } else {
                const QImage cover = decodeAttachedCover(ctx);
                if (!cover.isNull() && cover.save(coverPath, "JPG", 90))
                    tags.coverPath = coverPath;
            }
            removeOtherCoversLocked(tags.coverPath);
        } else {
            removeOtherCoversLocked(QString());
        }
    }

    avformat_close_input(&ctx);
    return hasTags;
}

} // namespace

MediaNotifier::MediaNotifier(QObject *parent)
    : QObject(parent)
{
    // Do not keep cover art from previous sessions; at most the current file's
    // cover is allowed to stay.
    clearCoverCache();

    m_interface = new QDBusInterface(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", QDBusConnection::sessionBus(), this);
}

bool MediaNotifier::enabled() const
{
    if (m_config && !m_config->notifications())
        return false;
    return m_interface && m_interface->isValid();
}

void MediaNotifier::send(const QString &summary, const QString &body, const QString &coverPath)
{
    if (!enabled())
        return;

    QVariantMap hints { { QStringLiteral("desktop-entry"), QStringLiteral("luma") } };
    if (!coverPath.isEmpty())
        hints.insert(QStringLiteral("image-path"), coverPath);

    m_interface->asyncCall("Notify", "Luma", (uint)0, "luma", summary, body, QStringList(),
                           hints, (int)-1);
}

void MediaNotifier::notify(const QString &summary, const QString &body)
{
    send(summary, body, QString());
}

void MediaNotifier::notifyForFile(const QString &path)
{
    if (!enabled())
        return;

    // A new file invalidates a notification that is still being prepared, and
    // the previous file's cover is no longer needed.
    const quint64 generation = ++m_generation;
    clearCoverCache();

    // For URLs, wait for the ICY tag from MediaManager — don't read tags over
    // the network.
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return;

    // Reading tags opens the file with FFmpeg (and may decode cover art), so
    // keep it off the GUI thread. The worker captures only the path, never
    // `this`.
    auto *watcher = new QFutureWatcher<TrackTagsResult>(this);
    connect(watcher, &QFutureWatcher<TrackTagsResult>::finished, this,
            [this, watcher, path, generation]() {
        const TrackTagsResult result = watcher->result();
        watcher->deleteLater();

        if (generation != m_generation)
            return;
        if (!enabled())
            return;

        if (!result.ok) {
            // Not audio with tags — as before, but without the file extension.
            send(QFileInfo(path).completeBaseName(), QString(), QString());
            return;
        }

        const TrackTags &tags = result.tags;
        QString summary = tags.title;
        if (summary.isEmpty())
            summary = QFileInfo(path).completeBaseName();

        // Artist and album go on separate lines.
        QStringList details;
        if (!tags.artist.isEmpty())
            details << tags.artist;
        if (!tags.album.isEmpty())
            details << tags.album;

        send(summary, details.join(QLatin1Char('\n')), tags.coverPath);
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        TrackTagsResult result;
        result.ok = readTrackTags(path, result.tags);
        return result;
    }));
}
