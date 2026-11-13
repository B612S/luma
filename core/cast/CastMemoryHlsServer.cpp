#include "cast/CastMemoryHlsServer.hpp"
#include "cast/SocksHttpProxy.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QDebug>
#include <QtConcurrent>
#include <cmath>
#include <expected>
#include <functional>
#include <cstring>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sched.h>
#include <csignal>
#endif

namespace {

constexpr int kMaxRequestBytes = 16 * 1024;

struct ProbeStream {
    QString type;
    QString codec;
    QString pixelFormat;
};

struct ProbeResult {
    QList<ProbeStream> streams;
    QString formatName;
    double duration = 0.0;
};

std::expected<ProbeResult, QString> parseProbe(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError)
        return std::unexpected(QStringLiteral("ffprobe returned invalid JSON: %1")
                                   .arg(error.errorString()));
    const QJsonObject root = document.object();
    const QJsonArray streamArray = root.value(QStringLiteral("streams")).toArray();
    if (streamArray.isEmpty())
        return std::unexpected(QStringLiteral("No media streams found"));

    ProbeResult result;
    result.formatName = root.value(QStringLiteral("format")).toObject()
                            .value(QStringLiteral("format_name")).toString();
    result.duration = root.value(QStringLiteral("format")).toObject()
                          .value(QStringLiteral("duration")).toString().toDouble();
    for (const QJsonValue &value : streamArray) {
        const QJsonObject stream = value.toObject();
        result.streams.append({stream.value(QStringLiteral("codec_type")).toString(),
                               stream.value(QStringLiteral("codec_name")).toString(),
                               stream.value(QStringLiteral("pix_fmt")).toString()});
    }
    return result;
}

void removeRamPathAsync(const QString &path)
{
    if (path.isEmpty()) return;
    (void)QtConcurrent::run([path]() { QDir(path).removeRecursively(); });
}

void setParentDeathSignal(QProcess *process)
{
#ifdef Q_OS_LINUX
    process->setChildProcessModifier([]() {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        // Cast encoding must not make the GUI unresponsive while a chunk is
        // generated. Lower priority and cap the thread count below.
        ::setpriority(PRIO_PROCESS, 0, 19);
        // SCHED_IDLE is allowed for an unprivileged process lowering its own
        // priority and guarantees the GUI/compositor preempts chunk encoding.
        struct sched_param param {};
        ::sched_setscheduler(0, SCHED_IDLE, &param);
    });
#else
    Q_UNUSED(process)
#endif
}

} // namespace

CastMemoryHlsServer::CastMemoryHlsServer(QObject *parent) : QObject(parent)
{
    // ffmpeg finalizes a segment before creating the next file; poll while a
    // chunk is being written so the receiver can start with the first segment
    // instead of waiting for the whole 20-second chunk.
    m_segmentTimer = new QTimer(this);
    m_segmentTimer->setInterval(150);
    connect(m_segmentTimer, &QTimer::timeout, this, &CastMemoryHlsServer::serveReadyPending);
}
CastMemoryHlsServer::~CastMemoryHlsServer() { stop(); }

QString CastMemoryHlsServer::chunkDir(int chunk) const
{
    return m_tempDir ? m_tempDir->path() + QStringLiteral("/c%1").arg(chunk) : QString();
}

QString CastMemoryHlsServer::chunkMarker(int chunk) const
{
    return chunkDir(chunk) + QStringLiteral("/.ready");
}

int CastMemoryHlsServer::segmentCount() const
{
    if (!(m_duration > 0.0)) return 0;
    return qMax(1, int(std::ceil(m_duration / double(kSegmentSeconds))));
}

int CastMemoryHlsServer::chunkForSegment(int segment) const
{
    return segment / qMax(1, kChunkSeconds / kSegmentSeconds);
}

QString CastMemoryHlsServer::detectLocalIP(const QString &targetIP) const
{
    const QStringList targetParts = targetIP.split('.');
    if (targetParts.size() != 4) return QStringLiteral("127.0.0.1");
    const QString targetPrefix = targetParts[0] + "." + targetParts[1] + "."
                                 + targetParts[2] + ".";
    QString fallbackIP;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol
                    || entry.ip().isLoopback()) continue;
            const QString ip = entry.ip().toString();
            if (ip.startsWith(targetPrefix)) return ip;
            if (fallbackIP.isEmpty()) fallbackIP = ip;
        }
    }
    return fallbackIP.isEmpty() ? QStringLiteral("127.0.0.1") : fallbackIP;
}

void CastMemoryHlsServer::cleanupStaleRamDirs()
{
    const QString root = QDir(QStringLiteral("/dev/shm")).exists()
                             ? QStringLiteral("/dev/shm") : QDir::tempPath();
    QDir dir(root);
    const qint64 currentPid = QCoreApplication::applicationPid();
    const QStringList entries = dir.entryList({QStringLiteral("luma-cast-*")},
                                              QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const int separator = entry.indexOf(QLatin1Char('-'), 10);
        qint64 pid = -1;
        if (separator > 10)
            pid = entry.mid(10, separator - 10).toLongLong();
        // Legacy temporary directories used a random six-character suffix.
        // Remove those too; the current naming starts with the PID.
        if (pid > 0 && (pid == currentPid
                || QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid))))
            continue;
        removeRamPathAsync(dir.filePath(entry));
    }
}

bool CastMemoryHlsServer::start(const QString &inputFile, const QString &targetIP,
                                const QString &audioFilter, const QString &audioInput,
                                double knownDuration)
{
    stop();
    cleanupStaleRamDirs();
    m_inputFile = inputFile;
    m_audioInput = audioInput;
    m_audioFilter = audioFilter;
    m_targetIP = targetIP;
    m_lastRequestedChunk = -1;
    if (!prepareProxy(inputFile))
        return false;
    const quint64 generation = ++m_startGeneration;

    // yt-dlp already knows the duration: skip the network ffprobe and start
    // encoding immediately. Optional -map arguments in chunkArgs tolerate a
    // source that has only video or only audio.
    if (knownDuration > 0.0) {
        m_duration = knownDuration;
        m_hasVideo = true;
        m_hasAudio = true;
        QString error;
        if (!startEncoding(&error)) {
            qWarning() << "CastMemoryHlsServer:" << error;
            return false;
        }
        return true;
    }

    m_probe = new QProcess(this);
    m_probe->setProcessChannelMode(QProcess::SeparateChannels);
    setParentDeathSignal(m_probe);
    connect(m_probe, &QProcess::errorOccurred, this,
            [this, generation](QProcess::ProcessError) {
        if (generation != m_startGeneration || !m_probe) return;
        failStart(QStringLiteral("ffprobe failed: %1").arg(m_probe->errorString()));
    });
    connect(m_probe, &QProcess::finished, this,
            [this, generation](int code, QProcess::ExitStatus status) {
        if (generation != m_startGeneration || !m_probe) return;
        const QByteArray data = m_probe->readAllStandardOutput();
        QProcess *probe = m_probe;
        m_probe = nullptr;
        probe->deleteLater();
        if (m_probeTimer) {
            m_probeTimer->stop();
            m_probeTimer->deleteLater();
            m_probeTimer = nullptr;
        }
        if (status != QProcess::NormalExit || code != 0) {
            failStart(QStringLiteral("ffprobe could not read the input"));
            return;
        }
        applyProbe(generation, data);
    });
    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    m_probeTimer->setInterval(6000);
    connect(m_probeTimer, &QTimer::timeout, this, [this, generation]() {
        if (generation != m_startGeneration) return;
        if (m_probe) {
            m_probe->disconnect(this);
            m_probe->kill();
            m_probe->deleteLater();
            m_probe = nullptr;
        }
        failStart(QStringLiteral("ffprobe timed out"));
    });
    m_probeTimer->start();
    QStringList probeArgs{QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-show_entries"),
                          QStringLiteral("stream=codec_type,codec_name,pix_fmt:format=format_name,duration"),
                          QStringLiteral("-of"), QStringLiteral("json")};
    if (!m_ffmpegProxy.isEmpty())
        probeArgs << QStringLiteral("-http_proxy") << m_ffmpegProxy;
    probeArgs << inputFile;
    m_probe->start(QStringLiteral("ffprobe"), probeArgs);
    return true;
}

void CastMemoryHlsServer::applyProbe(quint64 generation, const QByteArray &data)
{
    if (generation != m_startGeneration) return;
    const std::expected<ProbeResult, QString> parsed = parseProbe(data);
    if (!parsed) {
        failStart(parsed.error());
        return;
    }
    m_hasVideo = false;
    m_hasAudio = false;
    for (const ProbeStream &stream : parsed->streams) {
        if (stream.type == QLatin1String("video")) m_hasVideo = true;
        else if (stream.type == QLatin1String("audio")) m_hasAudio = true;
    }
    m_duration = parsed->duration > 0.0 ? parsed->duration : 0.0;
    if (!(m_duration > 0.0)
            || (!m_hasVideo && !m_hasAudio && m_audioInput.isEmpty())) {
        failStart(QStringLiteral("The input has no playable duration/streams"));
        return;
    }

    QString error;
    if (!startEncoding(&error))
        failStart(error);
}

bool CastMemoryHlsServer::startEncoding(QString *error)
{
    // The full VOD playlist is known once the duration is known. Segments are
    // produced in 20-second chunks on demand; the receiver can seek anywhere
    // immediately because every segment is listed up front.
    const int segments = segmentCount();
    QString playlist = QStringLiteral(
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:%1\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n").arg(kSegmentSeconds);
    for (int i = 0; i < segments; ++i) {
        const double remaining = m_duration - i * kSegmentSeconds;
        const double duration = qMin(double(kSegmentSeconds), qMax(0.001, remaining));
        playlist += QStringLiteral("#EXTINF:%1,\nc%2/seg%3.ts\n")
                        .arg(duration, 0, 'f', 6)
                        .arg(chunkForSegment(i))
                        .arg(i % (kChunkSeconds / kSegmentSeconds), 5, 10, QLatin1Char('0'));
    }
    playlist += QStringLiteral("#EXT-X-ENDLIST\n");
    m_playlist = playlist;

    const QString ramRoot = QDir(QStringLiteral("/dev/shm")).exists()
                                ? QStringLiteral("/dev/shm") : QDir::tempPath();
    m_tempDir = std::make_unique<QTemporaryDir>(
        ramRoot + QStringLiteral("/luma-cast-%1-XXXXXX")
                     .arg(QCoreApplication::applicationPid()));
    if (!m_tempDir->isValid()) {
        m_tempDir.reset();
        *error = QStringLiteral("Could not create the RAM segment directory");
        return false;
    }

    m_localIP = detectLocalIP(m_targetIP);
    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections())
            handleClient(m_server->nextPendingConnection());
    });
    if (!m_server->listen(QHostAddress(m_localIP), 0)) {
        *error = QStringLiteral("Could not listen on the local interface");
        return false;
    }
    m_port = m_server->serverPort();
    const QUrl inputUrl(m_inputFile);
    // Never log a resolved googlevideo URL: it carries session tokens.
    const QString inputDescription = inputUrl.scheme().isEmpty()
                                         ? m_inputFile : inputUrl.host();
    qInfo() << "CastMemoryHlsServer: VOD" << m_localIP << m_port
            << "duration" << m_duration << "segments" << segments
            << "chunks" << qCeil(m_duration / kChunkSeconds)
            << "input" << inputDescription
            << "audio" << (m_audioInput.isEmpty() ? QStringLiteral("muxed")
                                                  : QStringLiteral("separate"));
    qInfo() << "CastMemoryHlsServer: URL" << url();

    // Warm the first chunk; the receiver will request it immediately.
    startChunk(0);
    return true;
}

void CastMemoryHlsServer::failStart(const QString &reason)
{
    stop();
    emit startFailed(reason);
}

void CastMemoryHlsServer::stop()
{
    ++m_startGeneration;
    if (m_probeTimer) {
        m_probeTimer->stop();
        m_probeTimer->deleteLater();
        m_probeTimer = nullptr;
    }
    if (m_probe) {
        m_probe->disconnect(this);
        if (m_probe->state() != QProcess::NotRunning) {
            m_probe->kill();
            m_probe->waitForFinished(100);
        }
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    if (m_chunkProcess) {
        m_chunkProcess->disconnect(this);
        if (m_chunkProcess->state() != QProcess::NotRunning) {
            m_chunkProcess->kill();
            m_chunkProcess->waitForFinished(100);
        }
        m_chunkProcess->deleteLater();
        m_chunkProcess = nullptr;
    }
    m_generatingChunk = -1;
    m_lastRequestedChunk = -1;
    if (m_segmentTimer) m_segmentTimer->stop();
    for (const PendingRequest &request : m_pending) {
        if (request.socket) request.socket->disconnectFromHost();
    }
    m_pending.clear();
    m_recentChunks.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_tempDir) {
        m_tempDir->setAutoRemove(false);
        const QString path = m_tempDir->path();
        m_tempDir.reset();
        removeRamPathAsync(path);
    }
    m_inputFile.clear();
    m_audioInput.clear();
    m_targetIP.clear();
    m_audioFilter.clear();
    if (m_socksBridge)
        m_socksBridge->stop();
    m_ffmpegProxy.clear();
    m_localIP.clear();
    m_token.clear();
    m_playlist.clear();
    m_port = 0;
    m_duration = 0.0;
    m_hasVideo = m_hasAudio = true;
}

bool CastMemoryHlsServer::prepareProxy(const QString &inputFile)
{
    m_ffmpegProxy.clear();
    const QUrl input(inputFile);
    const bool network = input.isValid()
        && (input.scheme() == QLatin1String("http")
            || input.scheme() == QLatin1String("https"));
    if (!network || m_proxyUrl.isEmpty())
        return true;

    const QUrl proxy(m_proxyUrl);
    const QString scheme = proxy.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        m_ffmpegProxy = m_proxyUrl;
        return true;
    }
    if (scheme != QLatin1String("socks5") && scheme != QLatin1String("socks5h")) {
        qWarning() << "CastMemoryHlsServer: unsupported proxy scheme" << scheme;
        return false;
    }

    // FFmpeg has no SOCKS client: hand it a loopback HTTP proxy that tunnels
    // the bytes through SOCKS. The listener is local to this machine.
    if (!m_socksBridge)
        m_socksBridge = new SocksHttpProxy(this);
    if (!m_socksBridge->isRunning() && !m_socksBridge->start(proxy)) {
        qWarning() << "CastMemoryHlsServer: SOCKS bridge failed:"
                   << m_socksBridge->lastError();
        return false;
    }
    m_ffmpegProxy = m_socksBridge->localUrl();
    qInfo() << "CastMemoryHlsServer: using local SOCKS bridge" << m_ffmpegProxy;
    return true;
}

QString CastMemoryHlsServer::url() const
{
    if (!isRunning() || m_token.isEmpty()) return {};
    return QStringLiteral("http://%1:%2/stream/%3/playlist.m3u8")
        .arg(m_localIP).arg(m_port).arg(m_token);
}

QStringList CastMemoryHlsServer::chunkArgs(int chunk) const
{
    const double start = chunk * double(kChunkSeconds);
    const double duration = qBound(0.1, m_duration - start, double(kChunkSeconds));
    const bool hasAudio = m_hasAudio || !m_audioInput.isEmpty();

    QStringList args;
    // A local file is encoded fast with one thread, which keeps the GUI
    // responsive while the cast is prepared. A remote yt-dlp source is
    // decoded (often AV1/VP9) and encoded in real time, so it needs more
    // threads while the local player is paused anyway.
    const bool networkInput = m_inputFile.contains(QStringLiteral("://"));
    const int encoderThreads = networkInput ? 4 : 1;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel") << QStringLiteral("warning");
    if (!m_ffmpegProxy.isEmpty())
        args << QStringLiteral("-http_proxy") << m_ffmpegProxy;
    args << QStringLiteral("-threads") << QString::number(encoderThreads)
         << QStringLiteral("-ss") << QString::number(start, 'f', 3)
         << QStringLiteral("-i") << m_inputFile;
    if (!m_audioInput.isEmpty()) {
        // The same -ss on both inputs keeps the separately resolved audio
        // aligned with the video when a chunk starts mid-stream.
        if (!m_ffmpegProxy.isEmpty())
            args << QStringLiteral("-http_proxy") << m_ffmpegProxy;
        args << QStringLiteral("-ss") << QString::number(start, 'f', 3)
             << QStringLiteral("-i") << m_audioInput;
    }
    args << QStringLiteral("-t") << QString::number(duration, 'f', 3)
         << QStringLiteral("-threads") << QString::number(encoderThreads);

    if (m_hasVideo) {
        args << QStringLiteral("-map") << QStringLiteral("0:v:0?")
             << QStringLiteral("-threads") << QString::number(encoderThreads)
             << QStringLiteral("-filter_threads") << QString::number(encoderThreads)
             << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-tune") << QStringLiteral("zerolatency")
             << QStringLiteral("-profile:v") << QStringLiteral("main")
             << QStringLiteral("-level") << QStringLiteral("4.0")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-b:v") << QStringLiteral("4M")
             << QStringLiteral("-maxrate") << QStringLiteral("5M")
             << QStringLiteral("-bufsize") << QStringLiteral("8M")
             << QStringLiteral("-g") << QString::number(kSegmentSeconds * 25)
             << QStringLiteral("-keyint_min") << QString::number(kSegmentSeconds * 25)
             << QStringLiteral("-sc_threshold") << QStringLiteral("0")
             << QStringLiteral("-force_key_frames")
             << QStringLiteral("expr:gte(t,n_forced*%1)").arg(kSegmentSeconds);
    }
    if (!m_audioInput.isEmpty())
        args << QStringLiteral("-map") << QStringLiteral("1:a:0?");
    else if (m_hasAudio)
        args << QStringLiteral("-map") << QStringLiteral("0:a:0?");
    if (hasAudio) {
        args << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("128k")
             << QStringLiteral("-ac") << QStringLiteral("2")
             << QStringLiteral("-ar") << QStringLiteral("48000");
        if (!m_audioFilter.isEmpty())
            args << QStringLiteral("-af") << m_audioFilter;
    }
    args << QStringLiteral("-output_ts_offset") << QString::number(start, 'f', 3)
         << QStringLiteral("-f") << QStringLiteral("hls")
         << QStringLiteral("-hls_time") << QString::number(kSegmentSeconds)
         << QStringLiteral("-hls_list_size") << QStringLiteral("0")
         << QStringLiteral("-hls_playlist_type") << QStringLiteral("vod")
         << QStringLiteral("-hls_segment_filename")
         << chunkDir(chunk) + QStringLiteral("/seg%05d.ts")
         << chunkDir(chunk) + QStringLiteral("/playlist.m3u8");
    return args;
}

void CastMemoryHlsServer::startChunk(int chunk)
{
    if (!m_tempDir || chunk < 0 || chunk >= qCeil(m_duration / kChunkSeconds)) return;
    if (m_chunkProcess) {
        if (m_generatingChunk == chunk) return;
        m_chunkProcess->disconnect(this);
        if (m_chunkProcess->state() != QProcess::NotRunning) {
            m_chunkProcess->kill();
            m_chunkProcess->waitForFinished(100);
        }
        m_chunkProcess->deleteLater();
        m_chunkProcess = nullptr;
        QDir(chunkDir(m_generatingChunk)).removeRecursively();
        m_generatingChunk = -1;
    }
    if (QFile::exists(chunkMarker(chunk))) return;

    QDir().mkpath(chunkDir(chunk));
    qInfo() << "CastMemoryHlsServer: generating chunk" << chunk;
    m_generatingChunk = chunk;
    m_chunkProcess = new QProcess(this);
    m_chunkProcess->setProcessChannelMode(QProcess::SeparateChannels);
    setParentDeathSignal(m_chunkProcess);
    connect(m_chunkProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray err = m_chunkProcess->readAllStandardError().trimmed();
        if (!err.isEmpty()) qWarning().noquote() << "[cast ffmpeg]" << err;
    });
    connect(m_chunkProcess, &QProcess::errorOccurred, this,
            [this, chunk](QProcess::ProcessError error) {
        if (m_generatingChunk != chunk || error != QProcess::FailedToStart)
            return;
        if (m_chunkProcess) {
            m_chunkProcess->disconnect(this);
            m_chunkProcess->deleteLater();
            m_chunkProcess = nullptr;
        }
        m_generatingChunk = -1;
        finishChunk(chunk, false);
    });
    connect(m_chunkProcess, &QProcess::finished, this,
            [this, chunk](int code, QProcess::ExitStatus status) {
        if (m_generatingChunk != chunk) return;
        const bool ok = status == QProcess::NormalExit && code == 0
            && QFile::exists(chunkDir(chunk) + QStringLiteral("/seg00000.ts"));
        QProcess *process = m_chunkProcess;
        m_chunkProcess = nullptr;
        m_generatingChunk = -1;
        if (process) process->deleteLater();
        finishChunk(chunk, ok);
    });
    m_chunkProcess->start(QStringLiteral("ffmpeg"), chunkArgs(chunk));
    if (m_segmentTimer) m_segmentTimer->start();
}

void CastMemoryHlsServer::finishChunk(int chunk, bool ok)
{
    if (m_segmentTimer) m_segmentTimer->stop();
    if (!ok) {
        // Without a probe in the fast path, an expired/blocked source is only
        // discovered here. Fail the cast immediately instead of waiting for
        // the preparation timeout.
        if (chunk == 0 && !segmentReady(0, 0)) {
            qWarning() << "CastMemoryHlsServer: ffmpeg could not read the cast source";
            emit startFailed(QStringLiteral("FFmpeg could not read the cast stream"));
            return;
        }
        qWarning() << "CastMemoryHlsServer: chunk generation failed" << chunk;
        QDir(chunkDir(chunk)).removeRecursively();
        for (int i = m_pending.size() - 1; i >= 0; --i) {
            int c = -1, seg = -1;
            if (parseSegmentPath(m_pending[i].fileName, &c, &seg) && c == chunk
                    && m_pending[i].socket) {
                m_pending[i].socket->write(
                    "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                m_pending[i].socket->disconnectFromHost();
                m_pending.removeAt(i);
            }
        }
        // Retry any request that was waiting for a different chunk.
        for (const PendingRequest &request : m_pending) {
            if (request.socket && request.fileName.startsWith(QStringLiteral("c"))) {
                int c = -1, s = -1;
                if (parseSegmentPath(request.fileName, &c, &s) && c != chunk) {
                    startChunk(c);
                    break;
                }
            }
        }
        return;
    }
    QFile marker(chunkMarker(chunk));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        marker.close();
    qInfo() << "CastMemoryHlsServer: chunk ready" << chunk;
    m_recentChunks.removeAll(chunk);
    m_recentChunks.prepend(chunk);
    evictChunks();
    servePending(chunk);
    bool startedPending = false;
    for (const PendingRequest &request : m_pending) {
        if (!request.socket) continue;
        int c = -1, s = -1;
        if (parseSegmentPath(request.fileName, &c, &s) && c != chunk) {
            startChunk(c);
            startedPending = true;
            break;
        }
    }
    // Warm the next chunk while the receiver plays this one. Encoding a remote
    // source takes many seconds, so waiting for the receiver to request the
    // next chunk would stall playback at every chunk boundary.
    if (!startedPending)
        maybePrefetch();
}

void CastMemoryHlsServer::maybePrefetch()
{
    if (!m_tempDir || m_chunkProcess || m_lastRequestedChunk < 0)
        return;
    const int target = m_lastRequestedChunk + 1;
    if (target >= qCeil(m_duration / kChunkSeconds))
        return;
    if (QFile::exists(chunkMarker(target)))
        return;
    startChunk(target);
}

bool CastMemoryHlsServer::segmentReady(int chunk, int segment) const
{
    if (!m_tempDir || segment < 0 || segment >= kChunkSeconds / kSegmentSeconds)
        return false;
    const QString dir = chunkDir(chunk);
    const QString path = dir + QStringLiteral("/seg%1.ts")
                                  .arg(segment, 5, 10, QLatin1Char('0'));
    const QFileInfo file(path);
    if (!file.isFile() || file.size() <= 0)
        return false;
    if (QFile::exists(chunkMarker(chunk)))
        return true;
    // ffmpeg closes a segment before it creates the next one. The last
    // segment of a chunk becomes ready with the chunk marker.
    const int next = segment + 1;
    if (next >= kChunkSeconds / kSegmentSeconds)
        return false;
    const QFileInfo nextFile(dir + QStringLiteral("/seg%1.ts")
                                     .arg(next, 5, 10, QLatin1Char('0')));
    return nextFile.isFile() && nextFile.size() > 0;
}

void CastMemoryHlsServer::serveReadyPending()
{
    if (!m_tempDir || m_generatingChunk < 0) return;
    const int chunk = m_generatingChunk;
    const int perChunk = kChunkSeconds / kSegmentSeconds;
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        PendingRequest &request = m_pending[i];
        int c = -1, segment = -1;
        if (!parseSegmentPath(request.fileName, &c, &segment) || c != chunk)
            continue;
        if (!request.socket) {
            m_pending.removeAt(i);
            continue;
        }
        const int local = segment % perChunk;
        if (!segmentReady(chunk, local))
            continue;
        const QString path = chunkDir(chunk) + QStringLiteral("/seg%1.ts")
                                  .arg(local, 5, 10, QLatin1Char('0'));
        const qint64 size = QFileInfo(path).size();
        const qint64 start = request.start;
        const qint64 end = request.end < 0 ? size - 1 : qMin(request.end, size - 1);
        m_pending.removeAt(i);
        serveFile(request.socket, path, start, end, request.partial,
                  request.head, request.contentType);
    }
}

void CastMemoryHlsServer::evictChunks()
{
    while (m_recentChunks.size() > 2) {
        const int old = m_recentChunks.takeLast();
        if (old == m_generatingChunk) continue;
        bool needed = false;
        for (const PendingRequest &request : m_pending) {
            int c = -1, s = -1;
            if (parseSegmentPath(request.fileName, &c, &s) && c == old) {
                needed = true;
                break;
            }
        }
        if (needed) continue;
        QDir(chunkDir(old)).removeRecursively();
        qInfo() << "CastMemoryHlsServer: evicted chunk" << old;
    }
}

bool CastMemoryHlsServer::parseSegmentPath(const QString &name, int *chunk, int *segment) const
{
    if (!name.startsWith(QLatin1Char('c'))) return false;
    const int slash = name.indexOf(QLatin1Char('/'));
    if (slash < 2) return false;
    const int chunkValue = name.mid(1, slash - 1).toInt();
    const QString leaf = name.mid(slash + 1);
    if (!leaf.startsWith(QStringLiteral("seg")) || !leaf.endsWith(QStringLiteral(".ts")))
        return false;
    const int segmentValue = leaf.mid(3, leaf.size() - 6).toInt();
    if (chunkValue < 0 || segmentValue < 0) return false;
    *chunk = chunkValue;
    *segment = segmentValue;
    return true;
}

void CastMemoryHlsServer::handleClient(QTcpSocket *socket)
{
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    auto buffer = QSharedPointer<QByteArray>::create();
    auto handled = QSharedPointer<bool>::create(false);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled]() {
        if (*handled) return;
        if (!m_server) { socket->close(); return; }
        buffer->append(socket->readAll());
        if (buffer->size() > kMaxRequestBytes) {
            *handled = true;
            socket->write("HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const int headerEnd = buffer->indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        *handled = true;
        const QByteArray request = buffer->left(headerEnd + 2);
        const QByteArray firstLine = request.left(request.indexOf("\r\n"));
        const int firstSpace = firstLine.indexOf(' ');
        const int secondSpace = firstLine.indexOf(' ', firstSpace + 1);
        if (firstSpace <= 0 || secondSpace <= firstSpace) {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QByteArray method = firstLine.left(firstSpace).toUpper();
        const QString target = QString::fromUtf8(firstLine.mid(firstSpace + 1, secondSpace - firstSpace - 1));
        const QString path = QUrl(target).path();
        qDebug() << "CastMemoryHlsServer: request" << method << path;
        if (method == "OPTIONS") {
            socket->write("HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\n"
                          "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Range, Origin, Accept, Content-Type\r\n"
                          "Content-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        if (method != "GET" && method != "HEAD") {
            socket->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QString prefix = QStringLiteral("/stream/%1/").arg(m_token);
        if (!path.startsWith(prefix)) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        const QString fileName = path.mid(prefix.size());
        if (fileName.isEmpty() || fileName.contains("..")) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        if (fileName == QLatin1String("playlist.m3u8")) {
            servePlaylist(socket, method == "HEAD");
            return;
        }
        int chunk = -1, segment = -1;
        if (!parseSegmentPath(fileName, &chunk, &segment)
                || chunk >= qCeil(m_duration / kChunkSeconds)
                || segment >= segmentCount()) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        qint64 fileSize = 0;
        const QString filePath = chunkDir(chunk) + QStringLiteral("/seg%1.ts")
                                     .arg(segment % (kChunkSeconds / kSegmentSeconds), 5, 10, QLatin1Char('0'));
        qint64 start = 0, end = -1;
        bool partial = false;
        const int rangePos = request.toLower().indexOf("\r\nrange:");
        if (rangePos >= 0) {
            const int le = request.indexOf("\r\n", rangePos + 2);
            const QByteArray line = request.mid(rangePos + 8,
                (le < 0 ? request.size() : le) - rangePos - 8);
            const int eq = line.indexOf('=');
            const int dash = line.indexOf('-', eq + 1);
            if (eq >= 0 && dash > eq) {
                const QByteArray a = line.mid(eq + 1, dash - eq - 1).trimmed();
                const QByteArray b = line.mid(dash + 1).trimmed();
                if (!a.isEmpty()) {
                    start = a.toLongLong();
                    end = b.isEmpty() ? -1 : b.toLongLong();
                    partial = start >= 0;
                }
            }
        }

        m_lastRequestedChunk = chunk;
        const int localSegment = segment % (kChunkSeconds / kSegmentSeconds);
        if (segmentReady(chunk, localSegment)) {
            // Only a finished chunk may be evicted; an incomplete one is still
            // being written and is not in m_recentChunks yet.
            if (QFile::exists(chunkMarker(chunk))) {
                m_recentChunks.removeAll(chunk);
                m_recentChunks.prepend(chunk);
                evictChunks();
            }
            fileSize = QFileInfo(filePath).size();
            if (end < 0 || end >= fileSize) end = fileSize - 1;
            serveFile(socket, filePath, start, end, partial,
                      method == "HEAD", QByteArray("video/mp2t"));
            maybePrefetch();
            return;
        }

        PendingRequest pending;
        pending.socket = socket;
        pending.fileName = fileName;
        pending.start = start;
        pending.end = end;
        pending.partial = partial;
        pending.head = method == "HEAD";
        pending.contentType = QByteArray("video/mp2t");
        m_pending.append(pending);
        // The receiver may prefetch the next chunk while the current one is
        // still being encoded. Do not let that request kill the encoder: wait
        // for it to finish. A real jump to another chunk still preempts.
        if (!(m_chunkProcess && m_generatingChunk >= 0
              && chunk == m_generatingChunk + 1))
            startChunk(chunk);
        maybePrefetch();
    });
}

void CastMemoryHlsServer::servePlaylist(QTcpSocket *socket, bool head)
{
    const QByteArray data = m_playlist.toUtf8();
    const QByteArray header = "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/vnd.apple.mpegurl\r\n"
        "Content-Length: " + QByteArray::number(data.size())
        + "\r\nAccept-Ranges: none\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    socket->write(header);
    if (!head) socket->write(data);
    socket->disconnectFromHost();
}

void CastMemoryHlsServer::serveFile(QTcpSocket *socket, const QString &path,
                                    qint64 start, qint64 end, bool partial,
                                    bool head, const QByteArray &contentType)
{
    QFile infoFile(path);
    const qint64 fileSize = infoFile.size();
    if (end < 0 || end >= fileSize) end = fileSize - 1;
    if (start < 0 || start >= fileSize || end < start) {
        socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    const qint64 contentLength = end - start + 1;
    QByteArray header = "HTTP/1.1 " + QByteArray(partial ? "206 Partial Content" : "200 OK")
        + "\r\nContent-Type: " + contentType
        + "\r\nContent-Length: " + QByteArray::number(contentLength)
        + "\r\nAccept-Ranges: bytes\r\n"
          "Access-Control-Allow-Origin: *\r\n"
          "Access-Control-Allow-Headers: Range\r\n"
          "Access-Control-Expose-Headers: Content-Length, Content-Range\r\n"
          "Cache-Control: no-store\r\nConnection: close\r\n";
    if (partial)
        header += "Content-Range: bytes " + QByteArray::number(start) + "-"
                  + QByteArray::number(end) + "/" + QByteArray::number(fileSize) + "\r\n";
    header += "\r\n";
    socket->write(header);
    if (head || contentLength <= 0) {
        socket->disconnectFromHost();
        return;
    }

    auto *source = new QFile(path, socket);
    if (!source->open(QIODevice::ReadOnly) || !source->seek(start)) {
        delete source;
        socket->disconnectFromHost();
        return;
    }
    auto remaining = QSharedPointer<qint64>::create(contentLength);
    auto pump = QSharedPointer<std::function<void()>>::create();
    *pump = [socket, source, remaining, pump]() {
        while (*remaining > 0 && socket->bytesToWrite() < 1024 * 1024) {
            const qint64 chunkSize = qMin<qint64>(*remaining, 1024 * 1024);
            const QByteArray chunk = source->read(chunkSize);
            if (chunk.isEmpty()) {
                socket->disconnectFromHost();
                return;
            }
            socket->write(chunk);
            *remaining -= chunk.size();
        }
        if (*remaining <= 0)
            socket->disconnectFromHost();
    };
    connect(socket, &QTcpSocket::bytesWritten, socket, [pump]() { (*pump)(); });
    (*pump)();
}

void CastMemoryHlsServer::servePending(int chunk)
{
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        PendingRequest &request = m_pending[i];
        int c = -1, s = -1;
        if (!parseSegmentPath(request.fileName, &c, &s) || c != chunk)
            continue;
        if (!request.socket) {
            m_pending.removeAt(i);
            continue;
        }
        const QString path = chunkDir(chunk) + QStringLiteral("/seg%1.ts")
                                 .arg(s % (kChunkSeconds / kSegmentSeconds), 5, 10, QLatin1Char('0'));
        if (!QFileInfo(path).isFile()) {
            m_pending.removeAt(i);
            request.socket->write("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            request.socket->disconnectFromHost();
            continue;
        }
        m_pending.removeAt(i);
        const qint64 size = QFileInfo(path).size();
        qint64 start = request.start;
        qint64 end = request.end < 0 ? size - 1 : qMin(request.end, size - 1);
        serveFile(request.socket, path, start, end, request.partial,
                  request.head, request.contentType);
    }
}
