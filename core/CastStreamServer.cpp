#include "CastStreamServer.hpp"
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

CastStreamServer::CastStreamServer(QObject *parent) : QObject(parent) {}

CastStreamServer::~CastStreamServer() { stop(); }

QString CastStreamServer::detectLocalIP(const QString &targetIP) const {
    QStringList targetParts = targetIP.split('.');
    if (targetParts.size() != 4) return "127.0.0.1";

    QString targetPrefix = targetParts[0] + "." + targetParts[1] + "." + targetParts[2] + ".";
    QString fallbackIP;

    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback()) {
                QString ipStr = entry.ip().toString();
                if (ipStr.startsWith(targetPrefix)) {
                    return ipStr;
                }
                if (fallbackIP.isEmpty()) {
                    fallbackIP = ipStr;
                }
            }
        }
    }
    return fallbackIP.isEmpty() ? "127.0.0.1" : fallbackIP;
}

QString CastStreamServer::playlistPath() const {
    if (!m_tempDir) return {};
    return m_tempDir->path() + "/playlist.m3u8";
}

bool CastStreamServer::start(const QString &inputFile, const QString &targetIP) {
    stop();

    m_localIP = detectLocalIP(targetIP);
    qDebug() << "CastStreamServer: Using local IP" << m_localIP << "for target" << targetIP;

    m_tempDir = new QTemporaryDir();
    if (!m_tempDir->isValid()) {
        delete m_tempDir;
        m_tempDir = nullptr;
        return false;
    }

    const QString playlist = playlistPath();

    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_ffmpeg, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray err = m_ffmpeg->readAllStandardError();
        if (!err.isEmpty()) {
            qWarning().noquote() << "[ffmpeg]" << QString::fromUtf8(err).trimmed();
        }
    });

    QStringList args;
    args << "-y" << "-i" << inputFile
    << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-tune" << "zerolatency"
    << "-profile:v" << "main" << "-pix_fmt" << "yuv420p"
    << "-g" << "48" << "-keyint_min" << "48" << "-sc_threshold" << "0"
    << "-c:a" << "aac" << "-b:a" << "128k" << "-ac" << "2" << "-ar" << "48000"
    << "-f" << "hls"
    << "-hls_time" << "2"
    << "-hls_list_size" << "0"
    << "-hls_playlist_type" << "vod"
    << "-hls_segment_type" << "mpegts"
    << playlist;

    m_ffmpeg->start("ffmpeg", args);

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections()) {
            handleClient(m_server->nextPendingConnection());
        }
    });

    if (!m_server->listen(QHostAddress::Any, 0)) {
        qWarning() << "CastStreamServer: listen failed:" << m_server->errorString();
        stop();
        return false;
    }
    m_port = m_server->serverPort();
    return true;
}

bool CastStreamServer::isPlaylistReady() const {
    if (!m_tempDir) return false;
    return QFile::exists(playlistPath());
}

bool CastStreamServer::isFullyEncoded() const {
    if (!m_ffmpeg) return false;
    if (m_ffmpeg->state() == QProcess::NotRunning) {
        if (QFile::exists(playlistPath())) {
            return true;
        }
    }
    return false;
}

void CastStreamServer::stop() {
    if (m_ffmpeg) {
        m_ffmpeg->disconnect(this);
        m_ffmpeg->kill();
        m_ffmpeg->waitForFinished(1000);
        m_ffmpeg->deleteLater();
        m_ffmpeg = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_tempDir) {
        delete m_tempDir;
        m_tempDir = nullptr;
    }
    m_port = 0;
}

QString CastStreamServer::url() const {
    if (!isRunning()) return {};
    return QString("http://%1:%2/stream/playlist.m3u8").arg(m_localIP).arg(m_port);
}

void CastStreamServer::handleClient(QTcpSocket *socket) {
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray request = socket->readAll();
        if (request.isEmpty()) return;

        const QByteArray method = request.left(request.indexOf(' ')).toUpper();

        if (method == "OPTIONS") {
            QByteArray resp =
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Range, Origin, Accept, Content-Type\r\n"
            "Access-Control-Max-Age: 86400\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
            socket->write(resp);
            socket->disconnectFromHost();
            return;
        }

        if (method != "GET") {
            socket->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QString path = QString::fromUtf8(request).section(' ', 1, 1);
        QString fileName = path.section('/', -1).split('?').first();

        if (fileName.isEmpty() || fileName.contains("..") || fileName.contains('/')) {
            socket->write("HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QString filePath = m_tempDir->path() + "/" + fileName;
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            socket->write("HTTP/1.1 404 Not Found\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QByteArray data = file.readAll();
        file.close();

        QString contentType;
        if (fileName.endsWith(".m3u8")) {
            contentType = "application/vnd.apple.mpegurl";
        } else if (fileName.endsWith(".ts")) {
            contentType = "video/mp2t";
        } else {
            contentType = "application/octet-stream";
        }

        QByteArray header = QString(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %1\r\n"
            "Content-Length: %2\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Headers: Range\r\n"
            "Access-Control-Expose-Headers: Content-Length, Content-Range\r\n"
            "Accept-Ranges: none\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "\r\n").arg(contentType).arg(data.size()).toUtf8();

            socket->write(header);
            socket->write(data);
            socket->disconnectFromHost();
    });
}
