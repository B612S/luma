#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>

// Suffixes Luma treats as music (audio-only) files. Video containers such as
// MP4/MKV are handled by the video path even when they contain only audio.
inline const QStringList &audioFileExtensions()
{
    static const QStringList extensions = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("aac"),
        QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("m4a"),
        QStringLiteral("opus"), QStringLiteral("wma"), QStringLiteral("m4b"),
        QStringLiteral("wv"), QStringLiteral("ape"), QStringLiteral("aiff"),
        QStringLiteral("caf")
    };
    return extensions;
}

inline bool isAudioFilePath(const QString &path)
{
    return audioFileExtensions().contains(QFileInfo(path).suffix().toLower());
}
