#include "CastManager.hpp"
#include <QFileInfo>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>

CastManager::CastManager(QObject *parent) : QObject(parent) {
    m_discovery = new CastDiscovery(this);
    m_server = new CastStreamServer(this);
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
    connect(m_client, &CastV2Client::playbackStatusChanged, this, &CastManager::remoteStatusChanged);
    connect(m_client, &CastV2Client::positionChanged, this, &CastManager::remoteStatusChanged);
    connect(m_client, &CastV2Client::volumeChanged, this, &CastManager::remoteStatusChanged);
}

void CastManager::castTo(const QString &address, quint16 port,
                         const QString &filePath, const QString &title) {
    // Cancel a pending wait from a previous (possibly still encoding) cast.
    stopEncodingWait();
    if (m_casting) stopCast();

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        emit castFailed(QString("File not found: %1").arg(filePath));
        return;
    }

    if (!m_server->start(fi.absoluteFilePath(), address)) {
        emit castFailed("Failed to start HLS stream (FFmpeg error)");
        return;
    }

    const QString url = m_server->url();
    const QString mediaTitle = title.isEmpty() ? fi.fileName() : title;

    m_castElapsed.start();
    m_castWaitTimer = new QTimer(this);
    m_castWaitTimer->setInterval(200);

    connect(m_castWaitTimer, &QTimer::timeout, this, [this, address, port, url, mediaTitle]() {
        if (!m_server->isRunning()) {
            stopEncodingWait();
            emit castFailed("Stream server stopped unexpectedly");
            return;
        }

        if (m_server->isFullyEncoded()) {
            stopEncodingWait();

            qDebug() << "CastManager: Encoding finished. Sending LOAD to Chromecast...";

            m_client->connectToDevice(address, port);
            m_client->startSession(url, mediaTitle);

            emit castStarted(address);
            return;
        }

        if (m_castElapsed.elapsed() > 30000) {
            stopEncodingWait();
            qWarning() << "CastManager: Timeout waiting for encoding.";
            // Stop the server before emitting: a handler may start a new cast
            // synchronously and must not be stopped by this old timeout.
            m_server->stop();
            emit castFailed("FFmpeg did not finish encoding in time");
        }
    });

    m_castWaitTimer->start();
}

void CastManager::stopEncodingWait() {
    if (!m_castWaitTimer) return;
    m_castWaitTimer->stop();
    m_castWaitTimer->deleteLater();
    m_castWaitTimer = nullptr;
}

void CastManager::stopCast() {
    stopEncodingWait();

    m_client->stop();
    m_server->stop();

    if (m_casting) {
        m_casting = false;
        emit castingChanged();
    }
}
