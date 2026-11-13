#include "tags/TagManager.hpp"

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tstring.h>

namespace {

QString fromTagLib(const TagLib::String &value) {
    if (value.isEmpty()) return QString();
    return QString::fromUtf8(value.toCString(true));
}

TagLib::String toTagLib(const QString &value) {
    return TagLib::String(value.toUtf8().constData(), TagLib::String::UTF8);
}

} // namespace

QVariantMap TagManager::read(const QString &path)
{
    QVariantMap tags;

    TagLib::FileRef file(path.toUtf8().constData(), true, TagLib::AudioProperties::Fast);
    if (file.isNull() || !file.tag()) return tags;

    TagLib::Tag *tag = file.tag();
    tags["title"] = fromTagLib(tag->title());
    tags["artist"] = fromTagLib(tag->artist());
    tags["album"] = fromTagLib(tag->album());
    tags["genre"] = fromTagLib(tag->genre());
    tags["comment"] = fromTagLib(tag->comment());
    tags["year"] = tag->year() > 0 ? QString::number(tag->year()) : QString();
    tags["track"] = tag->track() > 0 ? QString::number(tag->track()) : QString();

    return tags;
}

bool TagManager::write(const QString &path, const QVariantMap &tags)
{
    TagLib::FileRef file(path.toUtf8().constData());
    if (file.isNull() || !file.file() || !file.tag()) return false;

    TagLib::Tag *tag = file.tag();
    tag->setTitle(toTagLib(tags.value("title").toString()));
    tag->setArtist(toTagLib(tags.value("artist").toString()));
    tag->setAlbum(toTagLib(tags.value("album").toString()));
    tag->setGenre(toTagLib(tags.value("genre").toString()));
    tag->setComment(toTagLib(tags.value("comment").toString()));

    bool ok = false;
    const int year = tags.value("year").toString().toInt(&ok);
    tag->setYear(ok && year > 0 ? year : 0);

    const int track = tags.value("track").toString().toInt(&ok);
    tag->setTrack(ok && track > 0 ? track : 0);

    if (!file.save()) return false;

    return true;
}
