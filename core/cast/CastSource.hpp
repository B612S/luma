#pragma once
#include <QString>

// Inputs the cast encoder must read for one cast. A local file or a direct
// stream uses videoUrl only. A yt-dlp-resolved DASH stream (YouTube and
// similar) has a video-only URL and, usually, a separate audio-only URL, which
// FFmpeg has to read as two inputs.
struct CastSource {
    QString videoUrl;
    QString audioUrl;
    // Title resolved by yt-dlp, used for the receiver metadata when the caller
    // did not provide one.
    QString title;
    // Duration reported by yt-dlp (seconds, 0 when unknown). A known duration
    // lets the cast encoder skip the network ffprobe and start immediately.
    double duration = 0.0;
};
