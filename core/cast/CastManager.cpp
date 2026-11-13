#include "cast/CastManager.hpp"
#include "media/Equalizer.hpp"
#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>

CastManager::CastManager(QObject *parent) : QObject(parent) {
    m_discovery = new CastDiscovery(this);
    m_server = new CastMemoryHlsServer(this);
    m_client = new CastV2Client(this);

    connect(m_client, &CastV2Client::mediaLoaded, this, [this]() {
        if (!m_casting) {
            m_casting = true;
            emit castingChanged();
        }
    });
    connect(m_client, &CastV2Client::errorOccurred, this, [this](const QString &msg) {
        qWarning() << "CastManager: Client error:" << msg;
        emit castFailed(msg);
        stopCast();
    });
    connect(m_server, &CastMemoryHlsServer::startFailed, this,
            [this](const QString &reason) {
        emit castFailed(reason);
        stopCast();
    });
    connect(m_client, &CastV2Client::playbackStatusChanged, this, &CastManager::remoteStatusChanged);
    connect(m_client, &CastV2Client::positionChanged, this, &CastManager::remoteStatusChanged);
    connect(m_client, &CastV2Client::volumeChanged, this, &CastManager::remoteStatusChanged);
}

void CastManager::castTo(const QString &address, quint16 port,
                         const QString &filePath, const QString &title,
                         const QString &deviceName) {
    const bool isUrl = filePath.contains(QStringLiteral("://"));
    QFileInfo fi(filePath);
    if (!isUrl && !fi.exists()) {
        emit castFailed(QString("File not found: %1").arg(filePath));
        return;
    }
    const QString inputPath = isUrl ? filePath : fi.absoluteFilePath();
    const QString displayName = deviceName.isEmpty() ? address : deviceName;

    // The playlist keeps the original page URL (a YouTube watch link, for
    // example) while the media is actually played from direct URLs resolved by
    // yt-dlp. Ask MediaManager for those inputs so FFmpeg does not receive an
    // HTML page. Local files and direct streams pass through unchanged.
    CastSource source;
    if (m_sourceResolver)
        source = m_sourceResolver(inputPath);
    const QString videoUrl = source.videoUrl.isEmpty() ? inputPath : source.videoUrl;
    const QString audioUrl = source.audioUrl;

    // Claim the TV before touching the current cast. In multi-instance mode
    // another Luma window may already be casting there: refuse the request
    // without disturbing any running cast (including this instance's).
    if (!m_deviceLock.acquire(address, port)) {
        qInfo() << "CastManager: TV" << displayName << "is already casting in another instance";
        emit castBusy(displayName);
        return;
    }

    // When switching files on the same TV, keep m_casting true so QML keeps
    // the cast overlay and the hidden local player disabled while the new
    // stream is prepared.
    const bool wasCasting = m_casting;
    stopEncodingWait();
    stopSeekWait();
    if (wasCasting)
        m_client->stop();
    m_server->stop();

    qInfo() << "CastManager: starting cast" << inputPath << "to" << address << port;
    if (videoUrl != inputPath)
        qInfo() << "CastManager: using resolved stream"
                << (audioUrl.isEmpty() ? QStringLiteral("muxed")
                                       : QStringLiteral("separate audio"));
    if (!m_castRequested) {
        m_castRequested = true;
        emit castRequestedChanged();
    }
    m_deviceAddress = address;
    m_devicePort = port;
    if (m_deviceName != displayName) {
        m_deviceName = displayName;
        emit deviceNameChanged();
    }
    m_currentFile = inputPath;

    const QString audioFilter = m_equalizer ? m_equalizer->ffmpegFilter() : QString();
    if (!m_server->start(videoUrl, address, audioFilter, audioUrl, source.duration)) {
        emit castFailed("Failed to start the cast stream");
        stopCast();
        return;
    }

    const QString mediaTitle = !title.isEmpty() ? title
        : (!source.title.isEmpty() ? source.title
           : (isUrl ? QUrl(filePath).fileName() : fi.fileName()));
    if (m_mediaTitle != mediaTitle) {
        m_mediaTitle = mediaTitle;
        emit mediaTitleChanged();
    }

    // FFmpeg's HLS muxer has to write the first segment into the RAM window
    // before the TV may load the playlist. A yt-dlp source first has to be
    // downloaded through the proxy, so 1080p can take much longer than a
    // local file (the local timeout stays short).
    const int startTimeoutMs = videoUrl != inputPath ? 30000 : 15000;
    stopEncodingWait();
    m_castElapsed.start();
    m_castWaitTimer = new QTimer(this);
    m_castWaitTimer->setInterval(200);
    connect(m_castWaitTimer, &QTimer::timeout, this,
            [this, address, port, mediaTitle, displayName, startTimeoutMs]() {
        if (!m_server->isRunning() && !m_server->isStarting()) {
            stopEncodingWait();
            emit castFailed("Stream server stopped unexpectedly");
            stopCast();
            return;
        }
        if (m_server->isPlaylistReady()) {
            stopEncodingWait();
            qInfo() << "CastManager: stream ready" << m_server->url()
                    << "duration" << m_server->duration();
            m_client->connectToDevice(address, port);
            m_client->startSession(m_server->url(), mediaTitle,
                                   QStringLiteral("application/vnd.apple.mpegurl"),
                                   QStringLiteral("BUFFERED"), m_server->duration());
            emit castStarted(displayName);
            return;
        }
        if (m_castElapsed.elapsed() > startTimeoutMs) {
            stopEncodingWait();
            emit castFailed("FFmpeg did not produce an HLS playlist in time");
            stopCast();
        }
    });
    m_castWaitTimer->start();
}

void CastManager::onMediaFileChanged(const QString &filePath) {
    if (!m_castRequested || filePath.isEmpty() || filePath == m_currentFile)
        return;

    const bool isUrl = filePath.contains(QStringLiteral("://"));
    if (!isUrl && !QFileInfo::exists(filePath))
        return;

    const QFileInfo fi(filePath);
    // For URLs castTo() uses the title resolved by yt-dlp; a file name derived
    // from a watch link ("watch") would replace it with noise.
    const QString title = isUrl ? QString() : fi.completeBaseName();
    // castTo() calls stopCast(), which clears these members; pass copies.
    const QString address = m_deviceAddress;
    const quint16 port = m_devicePort;
    const QString deviceName = m_deviceName;
    castTo(address, port, filePath, title, deviceName);
}

void CastManager::seekTo(double pos) {
    if (!m_casting || !m_server) return;
    // The server exposes a complete VOD playlist; seeking is native on the
    // receiver. Segments around the new position are generated on demand.
    qInfo() << "CastManager: native seek" << pos;
    m_client->seekTo(pos);
    emit remoteStatusChanged();
}

void CastManager::stopSeekWait() {
    if (!m_seekWaitTimer) return;
    m_seekWaitTimer->stop();
    m_seekWaitTimer->deleteLater();
    m_seekWaitTimer = nullptr;
}

void CastManager::stopEncodingWait() {
    if (!m_castWaitTimer) return;
    m_castWaitTimer->stop();
    m_castWaitTimer->deleteLater();
    m_castWaitTimer = nullptr;
}

void CastManager::setVolume(double volPercent) {
    // Local volume controls are disabled while casting; never let a stray
    // slider/MPRIS/key event tear down the receiver session.
    if (m_castRequested) {
        qInfo() << "CastManager: ignored local volume change while casting";
        return;
    }
    m_client->setVolume(volPercent / 100.0);
}

void CastManager::stopCast() {
    stopEncodingWait();
    stopSeekWait();
    if (!m_mediaTitle.isEmpty()) {
        m_mediaTitle.clear();
        emit mediaTitleChanged();
    }
    m_deviceAddress.clear();
    m_devicePort = 0;
    if (!m_deviceName.isEmpty()) {
        m_deviceName.clear();
        emit deviceNameChanged();
    }
    m_currentFile.clear();
    if (m_castRequested) {
        m_castRequested = false;
        emit castRequestedChanged();
    }
    qInfo() << "CastManager: stopping cast";
    m_client->stop();
    m_server->stop();
    // The receiver session is gone, so another window may claim the TV now.
    m_deviceLock.release();

    if (m_casting) {
        m_casting = false;
        emit castingChanged();
    }
}
