#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QProcess>
#include <QPointer>
#include <QFile>
#include <QList>
#include <QHash>
#include <memory>

class QTimer;
class SocksHttpProxy;

// Serves a complete VOD HLS playlist for a local file. The playlist lists all
// segments up front (which makes Chromecast's native HLS seek work); segment
// chunks are generated on demand by FFmpeg into /dev/shm and old chunks are
// evicted, so nothing large is kept on disk or in RAM indefinitely.
class CastMemoryHlsServer : public QObject {
    Q_OBJECT
public:
    explicit CastMemoryHlsServer(QObject *parent = nullptr);
    ~CastMemoryHlsServer();

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
    // The receiver can start as soon as the first complete segment exists; the
    // rest of the first chunk keeps encoding while the TV fills its buffer.
    bool isPlaylistReady() const { return isRunning() && !m_playlist.isEmpty()
                                     && m_tempDir && segmentReady(0, 0); }
    double duration() const { return m_duration; }
    double streamStart() const { return 0.0; }
    // The playlist is VOD; seeking is handled natively by the receiver.
    bool seekTo(double) { return isRunning(); }

signals:
    void startFailed(const QString &reason);

private:
    struct PendingRequest {
        QPointer<QTcpSocket> socket;
        QString fileName;
        qint64 start = 0;
        qint64 end = 0;
        bool partial = false;
        bool head = false;
        QByteArray contentType;
    };

    void handleClient(QTcpSocket *socket);
    void applyProbe(quint64 generation, const QByteArray &data);
    void failStart(const QString &reason);
    void cleanupStaleRamDirs();
    bool prepareProxy(const QString &inputFile);
    bool startEncoding(QString *error);
    bool segmentReady(int chunk, int segment) const;
    void serveReadyPending();
    void maybePrefetch();
    void startChunk(int chunk);
    void finishChunk(int chunk, bool ok);
    void servePending(int chunk);
    void serveFile(QTcpSocket *socket, const QString &path, qint64 start, qint64 end,
                   bool partial, bool head, const QByteArray &contentType);
    void servePlaylist(QTcpSocket *socket, bool head);
    void evictChunks();
    QString chunkDir(int chunk) const;
    QString chunkMarker(int chunk) const;
    QStringList chunkArgs(int chunk) const;
    int chunkForSegment(int segment) const;
    int segmentCount() const;
    bool parseSegmentPath(const QString &name, int *chunk, int *segment) const;
    QString detectLocalIP(const QString &targetIP) const;

    QTcpServer *m_server = nullptr;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QProcess *m_probe = nullptr;
    QTimer *m_probeTimer = nullptr;
    QProcess *m_chunkProcess = nullptr;
    QTimer *m_segmentTimer = nullptr;
    int m_generatingChunk = -1;
    QList<PendingRequest> m_pending;
    QList<int> m_recentChunks;
    // Chunk the receiver last asked for; prefetch stays one chunk ahead of it
    // so a long cast never encodes the whole file in the background.
    int m_lastRequestedChunk = -1;
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

    static constexpr int kChunkSeconds = 20;
    static constexpr int kSegmentSeconds = 2;
};
