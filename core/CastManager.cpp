#include "CastManager.hpp"
#include <QFileInfo>
#include <QTimer>

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
        emit castFailed(msg);
        stopCast();
    });
    connect(m_client, &CastV2Client::playbackStatusChanged, this, &CastManager::remoteStatusChanged);
    connect(m_client, &CastV2Client::positionChanged, this, &CastManager::remoteStatusChanged);
    connect(m_client, &CastV2Client::volumeChanged, this, &CastManager::remoteStatusChanged);
}

void CastManager::castTo(const QString &address, quint16 port,
                         const QString &filePath, const QString &title) {
    if (m_casting) stopCast();

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        emit castFailed(QString("File not found: %1").arg(filePath));
        return;
    }

    if (!m_server->start(fi.absoluteFilePath())) {
        emit castFailed("Failed to start HLS stream (check ffmpeg)");
        return;
    }

    const QString url = m_server->url();
    const QString mediaTitle = title.isEmpty() ? fi.fileName() : title;

    QTimer::singleShot(2500, this, [this, address, port, url, mediaTitle]() {
        if (!m_server->isRunning()) {
            emit castFailed("Stream server stopped unexpectedly");
            return;
        }
        m_client->connectToDevice(address, port);
        m_client->startSession(url, mediaTitle);
    });

    emit castStarted(address);
                         }

                         void CastManager::stopCast() {
                             m_client->stop();
                             m_server->stop();
                             if (m_casting) {
                                 m_casting = false;
                                 emit castingChanged();
                             }
                         }
