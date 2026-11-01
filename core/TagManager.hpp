#pragma once
#include <QString>
#include <QVariantMap>

// Чтение и запись тегов аудиофайлов через TagLib (mp3, flac, ogg, mp4/m4a, wav…).
namespace TagManager {

// Общие теги, которые можно редактировать в окне «Свойства».
QVariantMap read(const QString &path);

// Записывает переданные теги прямо в файл. false — если формат/файл не поддержан.
bool write(const QString &path, const QVariantMap &tags);

} // namespace TagManager
