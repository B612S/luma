#pragma once
#include <QString>
#include <QVariantMap>

// Reading and writing audio file tags via TagLib (mp3, flac, ogg, mp4/m4a, wav…).
namespace TagManager {

// Common tags that can be edited in the "Properties" window.
QVariantMap read(const QString &path);

// Writes the given tags straight into the file. false if the format/file is unsupported.
bool write(const QString &path, const QVariantMap &tags);

} // namespace TagManager
