// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

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
#include <utility>
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

double jsonNumber(const QJsonValue &value)
{
    // ffprobe prints these as strings by default but may emit plain numbers.
    return value.isString() ? value.toString().toDouble() : value.toDouble();
}

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
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    result.formatName = format.value(QStringLiteral("format_name")).toString();
    result.duration = jsonNumber(format.value(QStringLiteral("duration")));
    for (const auto &value : streamArray) {
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
        // SIGKILL, not SIGTERM: the encoder is throttled with SIGSTOP, and a
        // stopped process keeps a SIGTERM pending instead of exiting, which
        // would leave it behind if the player crashes.
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        // Cast encoding must not make the GUI unresponsive while the encoder
        // runs. Lower priority and cap the thread count below.
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
    // ffmpeg closes a segment before it starts writing the next one, so a
    // segment is complete as soon as the following file appears. Poll while the
    // encoder runs so the receiver can start with the first segment instead of
    // waiting for the whole cast.
    m_segmentTimer = new QTimer(this);
    m_segmentTimer->setInterval(100);
    connect(m_segmentTimer, &QTimer::timeout, this, &CastMemoryHlsServer::pollSegments);
}
CastMemoryHlsServer::~CastMemoryHlsServer() { stop(); }

QString CastMemoryHlsServer::segmentPath(int segment) const
{
    if (!m_tempDir || segment < 0)
        return QString();
    return m_tempDir->path()
        + QStringLiteral("/seg%1.ts").arg(segment, 5, 10, QLatin1Char('0'));
}

QString CastMemoryHlsServer::segmentFilePattern() const
{
    if (!m_tempDir) return QString();
    return m_tempDir->path() + QStringLiteral("/seg%05d.ts");
}

int CastMemoryHlsServer::segmentCount() const
{
    if (!(m_duration > 0.0)) return 0;
    return qMax(1, int(std::ceil(m_duration / double(kSegmentSeconds))));
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
    if (!prepareProxy(inputFile))
        return false;
    const quint64 generation = ++m_startGeneration;

    // yt-dlp already knows the duration: skip the network ffprobe and start
    // encoding immediately. Optional -map arguments in encoderArgs tolerate a
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
    // The full VOD playlist is known once the duration is known, so the
    // receiver can seek anywhere immediately.
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
        playlist += QStringLiteral("#EXTINF:%1,\nseg%2.ts\n")
                        .arg(duration, 0, 'f', 6)
                        .arg(i, 5, 10, QLatin1Char('0'));
    }
    playlist += QStringLiteral("#EXT-X-ENDLIST\n");
    m_playlist = std::move(playlist);

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
            << "input" << inputDescription
            << "audio" << (m_audioInput.isEmpty() ? QStringLiteral("muxed")
                                                  : QStringLiteral("separate"));
    qInfo() << "CastMemoryHlsServer: URL" << url();

    // One continuous encoder run for the whole cast (restarted only on a seek)
    // keeps the transport stream and the AAC encoder state continuous.
    startEncoderAt(0);
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
    stopEncoder();
    if (m_segmentTimer) m_segmentTimer->stop();
    for (const PendingRequest &request : m_pending) {
        if (request.socket) request.socket->disconnectFromHost();
    }
    m_pending.clear();
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
    m_encoderFirstSegment = 0;
    m_highestComplete = -1;
    m_lastRequestedSegment = -1;
    m_lowestKeptSegment = 0;
    m_encoderDone = false;
    m_encoderFailed = false;
    m_encoderPaused = false;
    m_playlistReady = false;
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

QStringList CastMemoryHlsServer::encoderArgs(int firstSegment) const
{
    const double position = firstSegment * double(kSegmentSeconds);
    const double remaining = qMax(0.1, m_duration - position);
    // FFmpeg's -ss counts from the start of the media, which is exactly the
    // position the playlist names, regardless of the input's first timestamp.
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
         << QStringLiteral("-ss") << QString::number(position, 'f', 3)
         << QStringLiteral("-i") << m_inputFile;
    if (!m_audioInput.isEmpty()) {
        // The directly resolved audio stream is normally zero-based, so it is
        // seeked by the playlist position. Both inputs land on the same
        // presentation time.
        if (!m_ffmpegProxy.isEmpty())
            args << QStringLiteral("-http_proxy") << m_ffmpegProxy;
        args << QStringLiteral("-ss") << QString::number(position, 'f', 3)
             << QStringLiteral("-i") << m_audioInput;
    }
    args << QStringLiteral("-t") << QString::number(remaining, 'f', 3)
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
    // The MPEG-TS muxer otherwise adds a default 1.4 s mux delay, which makes
    // the transport-stream timestamps disagree with the playlist positions.
    // Every segment must start exactly where the playlist says it does.
    args << QStringLiteral("-muxdelay") << QStringLiteral("0")
         << QStringLiteral("-muxpreload") << QStringLiteral("0")
         << QStringLiteral("-output_ts_offset") << QString::number(position, 'f', 3)
         << QStringLiteral("-f") << QStringLiteral("hls")
         << QStringLiteral("-hls_time") << QString::number(kSegmentSeconds)
         << QStringLiteral("-hls_list_size") << QStringLiteral("0")
         << QStringLiteral("-hls_playlist_type") << QStringLiteral("vod")
         << QStringLiteral("-start_number") << QString::number(firstSegment)
         << QStringLiteral("-hls_segment_filename") << segmentFilePattern()
         << m_tempDir->path() + QStringLiteral("/ffmpeg.m3u8");
    return args;
}

void CastMemoryHlsServer::startEncoderAt(int segment)
{
    if (!m_tempDir || segment < 0 || segment >= segmentCount()) return;
    stopEncoder();

    // A new encoder run has its own transport-stream continuity counters, AAC
    // encoder priming and timestamp base. Drop the segments of the previous run
    // from here on so the receiver never mixes two runs in one playback.
    const int lastStale = qMin(qMax(m_highestComplete, segment), segmentCount() - 1);
    for (int i = segment; i <= lastStale; ++i) {
        const QString path = segmentPath(i);
        if (QFile::exists(path)) QFile::remove(path);
    }

    // Requests for the old position cannot be served from the new run; the
    // requests at and after the new first segment stay queued and are filled as
    // the encoder reaches them. This matters for the burst of segment requests
    // a receiver sends around a seek.
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        if (m_pending.at(i).segment >= segment) continue;
        PendingRequest &request = m_pending[i];
        if (request.socket) {
            request.socket->write(
                "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            request.socket->disconnectFromHost();
        }
        m_pending.removeAt(i);
    }

    m_encoderFirstSegment = segment;
    m_highestComplete = segment - 1;
    m_lowestKeptSegment = qMin(m_lowestKeptSegment, segment);
    m_encoderDone = false;
    m_encoderFailed = false;
    m_encoderPaused = false;

    m_encoder = new QProcess(this);
    m_encoder->setProcessChannelMode(QProcess::SeparateChannels);
    setParentDeathSignal(m_encoder);
    QProcess *process = m_encoder;
    connect(m_encoder, &QProcess::readyReadStandardError, this, [this, process]() {
        if (m_encoder != process) return;
        const QByteArray err = process->readAllStandardError().trimmed();
        if (!err.isEmpty()) qWarning().noquote() << "[cast ffmpeg]" << err;
    });
    connect(m_encoder, &QProcess::errorOccurred, this,
            [this, process, segment](QProcess::ProcessError error) {
        if (m_encoder != process || error != QProcess::FailedToStart) return;
        m_encoder = nullptr;
        m_encoderDone = true;
        m_encoderFailed = true;
        m_encoderPaused = false;
        process->disconnect(this);
        process->deleteLater();
        // start() may report this synchronously; never tear the server down
        // from inside it.
        QTimer::singleShot(0, this, [this, segment]() {
            if (segment == 0 && !m_playlistReady) {
                emit startFailed(QStringLiteral("FFmpeg could not start"));
                return;
            }
            failPending(QStringLiteral("FFmpeg could not start"));
        });
    });
    connect(m_encoder, &QProcess::finished, this,
            [this, process](int code, QProcess::ExitStatus status) {
        if (m_encoder != process) return;
        onEncoderFinished(code, status);
    });
    qInfo() << "CastMemoryHlsServer: encoding from segment" << segment
            << "at" << (segment * kSegmentSeconds) << "seconds";
    m_encoder->start(QStringLiteral("ffmpeg"), encoderArgs(segment));
    if (m_segmentTimer) m_segmentTimer->start();
}

void CastMemoryHlsServer::stopEncoder()
{
    if (!m_encoder) return;
    QProcess *process = m_encoder;
    m_encoder = nullptr;
    m_encoderPaused = false;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
#ifdef Q_OS_LINUX
        // A throttled (SIGSTOPped) process needs to run again to notice the
        // termination signal.
        if (process->processId() > 0)
            ::kill(pid_t(process->processId()), SIGCONT);
#endif
        process->kill();
        process->waitForFinished(100);
    }
    process->deleteLater();
}

void CastMemoryHlsServer::onEncoderFinished(int exitCode, QProcess::ExitStatus status)
{
    QProcess *process = m_encoder;
    m_encoder = nullptr;
    m_encoderPaused = false;
    if (process) process->deleteLater();
    m_encoderDone = true;
    if (status != QProcess::NormalExit || exitCode != 0) {
        m_encoderFailed = true;
        qWarning() << "CastMemoryHlsServer: encoder stopped with" << exitCode << status;
    } else {
        qInfo() << "CastMemoryHlsServer: encoder finished";
    }
    // Everything the run wrote is final now.
    pollSegments();
    serveReadyPending();
    if (m_encoderFailed && !m_playlistReady) {
        // Nothing usable was produced (the yt-dlp path skips ffprobe).
        emit startFailed(QStringLiteral("FFmpeg could not read the cast stream"));
        return;
    }
    failPending(m_encoderFailed ? QStringLiteral("cast encoder failed")
                                : QStringLiteral("cast stream ended"));
}

bool CastMemoryHlsServer::segmentComplete(int segment) const
{
    if (!m_tempDir || segment < 0 || segment >= segmentCount()) return false;
    const QFileInfo file(segmentPath(segment));
    if (!file.isFile() || file.size() <= 0) return false;
    // A finished encoder run has closed every file it wrote.
    if (m_encoderDone || segment < m_encoderFirstSegment) return true;
    // ffmpeg closes a segment before it creates the next one; the last segment
    // of the file only becomes ready when the encoder exits.
    return QFileInfo::exists(segmentPath(segment + 1));
}

void CastMemoryHlsServer::pollSegments()
{
    if (!m_tempDir) return;
    bool changed = false;
    for (;;) {
        const int next = m_highestComplete + 1;
        if (next < m_encoderFirstSegment || next >= segmentCount()) break;
        if (!segmentComplete(next)) break;
        m_highestComplete = next;
        changed = true;
    }
    if (m_encoderDone) {
        while (m_highestComplete + 1 < segmentCount()
               && QFileInfo::exists(segmentPath(m_highestComplete + 1))) {
            ++m_highestComplete;
            changed = true;
        }
    }
    if (!m_playlistReady && m_highestComplete >= 0) {
        m_playlistReady = true;
        qInfo() << "CastMemoryHlsServer: playlist ready";
    }
    if (changed)
        serveReadyPending();
    throttleEncoder();
    evictSegments();
}

void CastMemoryHlsServer::throttleEncoder()
{
    if (!m_encoder || m_encoderDone || m_encoderFailed) return;
    const int reference = m_lastRequestedSegment < 0
                              ? m_encoderFirstSegment : m_lastRequestedSegment;
    const bool pause = m_highestComplete - reference > kMaxAheadSegments;
    if (pause == m_encoderPaused) return;
#ifdef Q_OS_LINUX
    const qint64 pid = m_encoder->processId();
    if (pid <= 0) return;
    m_encoderPaused = pause;
    ::kill(pid_t(pid), pause ? SIGSTOP : SIGCONT);
    qInfo() << "CastMemoryHlsServer:" << (pause ? "paused" : "resumed")
            << "encoder at segment" << m_highestComplete;
#else
    Q_UNUSED(pause)
#endif
}

void CastMemoryHlsServer::evictSegments()
{
    if (!m_tempDir || m_lastRequestedSegment < 0) return;
    const int cutoff = m_lastRequestedSegment - kKeepBehindSegments;
    while (m_lowestKeptSegment < cutoff && m_lowestKeptSegment < segmentCount()) {
        // The receiver can run ahead of the encoder after a seek. Never skip a
        // segment that has not been written yet: the encoder would create it
        // later and it would never be evicted, leaking the RAM window.
        const QString path = segmentPath(m_lowestKeptSegment);
        if (!QFileInfo::exists(path)) break;
        if (!QFile::remove(path)) break;
        ++m_lowestKeptSegment;
    }
}

void CastMemoryHlsServer::serveReadyPending()
{
    if (!m_tempDir) return;
    for (int i = m_pending.size() - 1; i >= 0; --i) {
        PendingRequest &request = m_pending[i];
        if (!request.socket) {
            m_pending.removeAt(i);
            continue;
        }
        if (!segmentComplete(request.segment)) continue;
        const QString path = segmentPath(request.segment);
        const qint64 size = QFileInfo(path).size();
        // removeAt() shifts the list, so copy everything out of the entry
        // before it goes away. Reading request.socket after removeAt() would
        // send this segment on the next pending request's socket.
        const QPointer<QTcpSocket> socketPtr = request.socket;
        const qint64 start = request.start;
        const qint64 end = request.end < 0 ? size - 1 : qMin(request.end, size - 1);
        const bool partial = request.partial;
        const bool head = request.head;
        m_pending.removeAt(i);
        serveFile(socketPtr, path, start, end, partial, head);
    }
}

void CastMemoryHlsServer::failPending(const QString &reason)
{
    if (m_pending.isEmpty()) return;
    if (!reason.isEmpty())
        qInfo() << "CastMemoryHlsServer: dropping pending requests:" << reason;
    for (const PendingRequest &request : m_pending) {
        if (!request.socket) continue;
        request.socket->write(
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        request.socket->disconnectFromHost();
    }
    m_pending.clear();
}

bool CastMemoryHlsServer::parseSegmentPath(const QString &name, int *segment) const
{
    if (!name.startsWith(QStringLiteral("seg")) || !name.endsWith(QStringLiteral(".ts")))
        return false;
    const QString number = name.mid(3, name.size() - 6);
    if (number.isEmpty()) return false;
    bool ok = false;
    const int value = number.toInt(&ok);
    if (!ok || value < 0) return false;
    *segment = value;
    return true;
}

void CastMemoryHlsServer::handleClient(QTcpSocket *socket)
{
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    auto buffer = QSharedPointer<QByteArray>::create();
    auto handled = QSharedPointer<bool>::create(false);
    auto process = QSharedPointer<std::function<void()>>::create();
    *process = [this, socket, buffer, handled]() -> void
    {
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
        int segment = -1;
        if (!parseSegmentPath(fileName, &segment) || segment >= segmentCount()) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

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

        const int previousRequested = m_lastRequestedSegment;
        if (segment > m_lastRequestedSegment)
            m_lastRequestedSegment = segment;
        const bool fileExists = QFileInfo::exists(segmentPath(segment));
        // The running encoder covers this segment if it starts at or before it
        // and has not finished (a finished run never writes anything again).
        const bool covered = m_encoder && !m_encoderFailed
                             && segment >= m_encoderFirstSegment
                             && segment >= m_lowestKeptSegment
                             && (!m_encoderDone || fileExists);
        // A request far past the last one is a seek, not receiver prefetch.
        const bool forwardSeek = previousRequested >= 0 && !fileExists
                                 && segment > previousRequested + kSeekJumpSegments;
        if (!fileExists && (!covered || forwardSeek)) {
            qInfo() << "CastMemoryHlsServer: restarting encoder at segment" << segment;
            startEncoderAt(segment);
        }
        throttleEncoder();
        if (segmentComplete(segment)) {
            const QString path = segmentPath(segment);
            const qint64 size = QFileInfo(path).size();
            const qint64 serveEnd = end < 0 ? size - 1 : qMin(end, size - 1);
            serveFile(socket, path, start, serveEnd, partial, method == "HEAD");
            evictSegments();
            return;
        }

        PendingRequest pending;
        pending.socket = socket;
        pending.segment = segment;
        pending.start = start;
        pending.end = end;
        pending.partial = partial;
        pending.head = method == "HEAD";
        m_pending.append(pending);
        serveReadyPending();
    };
    connect(socket, &QTcpSocket::readyRead, this, [process]() { (*process)(); });
    // The receiver sends a request immediately after connecting. That data can
    // already be buffered here, and readyRead is only emitted for data that
    // arrives after the connection above; handle the buffered request now
    // instead of waiting for a signal that will never come.
    if (socket->bytesAvailable() > 0)
        (*process)();
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
                                    qint64 start, qint64 end, bool partial, bool head)
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
        + "\r\nContent-Type: video/mp2t"
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
