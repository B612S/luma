#include "TagScanner.hpp"

#include <QMetaObject>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {

struct ScannedTags {
    QString title;
    QString artist;
    QString album;
    int year = 0;
    double duration = 0.0;
    bool ok = false;
};

ScannedTags scanTags(const QString &path)
{
    ScannedTags result;
    AVFormatContext *ctx = nullptr;
    if (avformat_open_input(&ctx, path.toUtf8().constData(), nullptr, nullptr) != 0)
        return result;

    result.ok = true;
    if (avformat_find_stream_info(ctx, nullptr) >= 0) {
        if (AVDictionaryEntry *tag = av_dict_get(ctx->metadata, "title", nullptr, 0))
            result.title = QString::fromUtf8(tag->value);
        if (AVDictionaryEntry *tag = av_dict_get(ctx->metadata, "artist", nullptr, 0))
            result.artist = QString::fromUtf8(tag->value);
        if (AVDictionaryEntry *tag = av_dict_get(ctx->metadata, "album", nullptr, 0))
            result.album = QString::fromUtf8(tag->value);
        for (const char *key : {"date", "year"}) {
            if (AVDictionaryEntry *tag = av_dict_get(ctx->metadata, key, nullptr, 0)) {
                const QString value = QString::fromUtf8(tag->value).trimmed();
                const int year = value.left(4).toInt();
                if (year > 0) {
                    result.year = year;
                    break;
                }
            }
        }
        if (ctx->duration > 0)
            result.duration = double(ctx->duration) / AV_TIME_BASE;
    }
    avformat_close_input(&ctx);
    return result;
}

} // namespace

TagScanner::TagScanner(QObject *parent) : QObject(parent)
{
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("LumaTagScanner"));
    m_thread.start();
}

TagScanner::~TagScanner()
{
    stop();
}

void TagScanner::stop()
{
    if (!m_thread.isRunning()) {
        delete m_worker;
        m_worker = nullptr;
        m_pending.clear();
        return;
    }

    m_thread.quit();
    if (!m_thread.wait(3000)) {
        m_thread.terminate();
        m_thread.wait(1000);
    }
    delete m_worker;
    m_worker = nullptr;
    m_pending.clear();
}

void TagScanner::enqueue(const QString &path)
{
    if (path.isEmpty() || m_pending.contains(path))
        return;
    m_pending.insert(path);

    QMetaObject::invokeMethod(m_worker, [this, path]() {
        const ScannedTags tags = scanTags(path);
        QMetaObject::invokeMethod(this, [this, path, tags]() {
            m_pending.remove(path);
            emit scanned(path, tags.title, tags.artist, tags.album, tags.year,
                         tags.duration, tags.ok);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
