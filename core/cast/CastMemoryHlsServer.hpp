// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QProcess>
#include <QPointer>
#include <QFile>
#include <QList>
#include <memory>

class QTimer;
class SocksHttpProxy;

// Serves a complete VOD HLS playlist for a local file or a remote stream. The
// playlist lists all segments up front (which makes Chromecast's native HLS
// seek work). One FFmpeg run encodes the whole cast sequentially into
// two-second segments in /dev/shm, so audio and video share a single encoder
// and one continuous MPEG-TS timeline: restarting the encoder every few
// seconds would reset the transport-stream continuity counters and add a new
// AAC priming delay, which makes the TV's audio drift away from the video over
// a long cast. The run starts at the requested segment after a seek, is
// throttled a little ahead of the receiver, and old segments are evicted, so a
// long cast needs only a bounded amount of memory.
class CastMemoryHlsServer : public QObject {
    Q_OBJECT
public:
    explicit CastMemoryHlsServer(QObject *parent = nullptr);
    ~CastMemoryHlsServer() override;

    // audioInput is the separate audio-only URL of a yt-dlp DASH stream; it is
    // empty for local files and muxed streams.
    // knownDuration is the duration yt-dlp already reported for the source; it
    // lets the cast start without a network ffprobe. Zero means "probe it".
    bool start(const QString &inputFile, const QString &targetIP,
               const QString &audioFilter = {}, const QString &audioInput = {},
               double knownDuration = 0.0);
    void stop();

    // SOCKS5/HTTP proxy used by FFmpeg for network inputs. HTTP(S) proxies are
    // passed through; a SOCKS proxy is reached through a loopback bridge
    // because FFmpeg cannot speak SOCKS itself.
    void setProxyUrl(const QString &proxyUrl) { m_proxyUrl = proxyUrl; }

    QString url() const;
    bool isRunning() const { return m_server && m_server->isListening(); }
    bool isStarting() const { return m_probe != nullptr; }
    // The receiver can start as soon as the first complete segment exists.
    bool isPlaylistReady() const { return isRunning() && m_playlistReady; }
    double duration() const { return m_duration; }
    double streamStart() const { return 0.0; }
    // The playlist is VOD; seeking is handled natively by the receiver, which
    // makes the server restart its encoder at the requested segment.
    bool seekTo(double) { return isRunning(); }

signals:
    void startFailed(const QString &reason);

private:
    struct PendingRequest {
        QPointer<QTcpSocket> socket;
        int segment = 0;
        qint64 start = 0;
        qint64 end = 0;
        bool partial = false;
        bool head = false;
    };

    void handleClient(QTcpSocket *socket);
    void applyProbe(quint64 generation, const QByteArray &data);
    void failStart(const QString &reason);
    void cleanupStaleRamDirs();
    bool prepareProxy(const QString &inputFile);
    bool startEncoding(QString *error);
    void startEncoderAt(int segment);
    void stopEncoder();
    void onEncoderFinished(int exitCode, QProcess::ExitStatus status);
    void pollSegments();
    void throttleEncoder();
    void evictSegments();
    void serveReadyPending();
    bool segmentComplete(int segment) const;
    QString segmentPath(int segment) const;
    QString segmentFilePattern() const;
    QStringList encoderArgs(int firstSegment) const;
    int segmentCount() const;
    bool parseSegmentPath(const QString &name, int *segment) const;
    void serveFile(QTcpSocket *socket, const QString &path, qint64 start, qint64 end,
                   bool partial, bool head);
    void servePlaylist(QTcpSocket *socket, bool head);
    void failPending(const QString &reason);
    QString detectLocalIP(const QString &targetIP) const;

    QTcpServer *m_server = nullptr;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QProcess *m_probe = nullptr;
    QTimer *m_probeTimer = nullptr;
    // The single encoder run. It is restarted only for a seek, so within a run
    // the transport stream, its continuity counters and the AAC encoder are
    // continuous.
    QProcess *m_encoder = nullptr;
    QTimer *m_segmentTimer = nullptr;
    QList<PendingRequest> m_pending;
    int m_encoderFirstSegment = 0;
    int m_highestComplete = -1;
    // Highest segment the receiver requested; throttling and eviction follow it.
    int m_lastRequestedSegment = -1;
    int m_lowestKeptSegment = 0;
    bool m_encoderDone = false;
    bool m_encoderFailed = false;
    bool m_encoderPaused = false;
    bool m_playlistReady = false;
    quint64 m_startGeneration = 0;

    QString m_inputFile;
    QString m_audioInput;
    QString m_proxyUrl;
    QString m_ffmpegProxy;
    SocksHttpProxy *m_socksBridge = nullptr;
    QString m_audioFilter;
    QString m_targetIP;
    QString m_localIP;
    QString m_token;
    QString m_playlist;
    quint16 m_port = 0;
    double m_duration = 0.0;
    bool m_hasVideo = true;
    bool m_hasAudio = true;

    static constexpr int kSegmentSeconds = 2;
    // Keep the encoder at most this many segments ahead of the receiver's last
    // request. A receiver normally buffers about ten seconds ahead, so this is
    // roughly half a minute of extra runway.
    static constexpr int kMaxAheadSegments = 15;
    // Keep this many segments behind the last request so a small seek back does
    // not need a new encoder run. Together with the value above this bounds the
    // RAM window to about a minute of video.
    static constexpr int kKeepBehindSegments = 20;
    // A request this far past the last one is a seek, not receiver prefetch.
    static constexpr int kSeekJumpSegments = 15;
};
