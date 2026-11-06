#include "PlaybackPositions.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {
// Shorter positions are not worth resuming, and a file watched to the end
// should start from the beginning next time.
constexpr double kMinPositionSeconds = 5.0;
constexpr double kEndMarginSeconds = 10.0;
constexpr qint64 kMaxAgeSeconds = 90LL * 24 * 60 * 60;
constexpr int kFormatVersion = 1;
} // namespace

PlaybackPositions::PlaybackPositions(int maxEntries)
    : m_maxEntries(qMax(1, maxEntries))
{
}

qint64 PlaybackPositions::now()
{
    return QDateTime::currentSecsSinceEpoch();
}

void PlaybackPositions::setMaxEntries(int maxEntries)
{
    m_maxEntries = qMax(1, maxEntries);
    prune();
}

bool PlaybackPositions::load(const QString &filePath)
{
    m_entries.clear();
    QFile file(filePath);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const bool ok = loadFromData(file.readAll());
    if (!ok)
        m_entries.clear();
    return ok;
}

bool PlaybackPositions::loadFromData(const QByteArray &data)
{
    m_entries.clear();
    if (data.trimmed().isEmpty())
        return true;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() > kFormatVersion)
        return false;

    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    const qint64 current = now();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        const QString key = it.key();
        if (key.isEmpty() || !it.value().isObject())
            continue;
        const QJsonObject obj = it.value().toObject();
        Entry entry;
        entry.position = obj.value(QStringLiteral("pos")).toDouble();
        entry.duration = obj.value(QStringLiteral("dur")).toDouble();
        entry.updated = static_cast<qint64>(obj.value(QStringLiteral("upd")).toDouble());
        if (entry.position <= 0.0 || entry.updated <= 0)
            continue;
        if (current - entry.updated > kMaxAgeSeconds)
            continue;
        m_entries.insert(key, entry);
    }

    prune(current);
    return true;
}

QByteArray PlaybackPositions::serialize() const
{
    QJsonObject entries;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("pos")] = it.value().position;
        if (it.value().duration > 0.0)
            obj[QStringLiteral("dur")] = it.value().duration;
        obj[QStringLiteral("upd")] = static_cast<double>(it.value().updated);
        entries.insert(it.key(), obj);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = kFormatVersion;
    root[QStringLiteral("entries")] = entries;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

double PlaybackPositions::positionFor(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd())
        return 0.0;
    return it.value().position;
}

bool PlaybackPositions::setPosition(const QString &key, double position, double duration,
                                    qint64 nowSecs)
{
    if (key.isEmpty())
        return false;

    const qint64 current = nowSecs >= 0 ? nowSecs : now();
    const bool tooEarly = position < kMinPositionSeconds;
    const bool tooLate = duration > 0.0 && duration - position < kEndMarginSeconds;
    if (tooEarly || tooLate) {
        const bool existed = m_entries.remove(key) > 0;
        if (existed)
            prune(current);
        return existed;
    }

    Entry &entry = m_entries[key];
    const bool changed = entry.position != position || entry.duration != duration;
    entry.position = position;
    entry.duration = duration;
    entry.updated = current;
    prune(current);
    return changed;
}

void PlaybackPositions::remove(const QString &key)
{
    m_entries.remove(key);
}

void PlaybackPositions::prune(qint64 nowSecs)
{
    const qint64 current = nowSecs >= 0 ? nowSecs : now();
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (current - it.value().updated > kMaxAgeSeconds)
            it = m_entries.erase(it);
        else
            ++it;
    }

    // Evict the least recently used entries, keeping the cache size bounded.
    while (m_entries.size() > m_maxEntries) {
        auto oldest = m_entries.begin();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it.value().updated < oldest.value().updated)
                oldest = it;
        }
        m_entries.erase(oldest);
    }
}
