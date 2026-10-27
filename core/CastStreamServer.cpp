#include "CastStreamServer.hpp"
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

CastStreamServer::CastStreamServer(QObject *parent) : QObject(parent) {
    m_localIP = detectLocalIP();
}

CastStreamServer::~CastStreamServer() { stop(); }

QString CastStreamServer::detectLocalIP() const {
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                && !entry.ip().isLoopback()) {
                return entry.ip().toString();
                }
        }
    }
    return "127.0.0.1";
}

bool CastStreamServer::start(const QString &inputFile) {
    stop();

    m_tempDir = new QTemporaryDir();
    if (!m_tempDir->isValid()) {
        delete m_tempDir;
        m_tempDir = nullptr;
        return false;
    }

    QString playlistPath = m_tempDir->path() + "/playlist.m3u8";

    m_ffmpeg = new QProcess(this);
    QStringList args;
    args << "-y" << "-i" << inputFile
    << "-c:v" << "libx264" << "-preset" << "veryfast" << "-tune" << "zerolatency"
    << "-profile:v" << "main" << "-level" << "4.0"
    << "-c:a" << "aac" << "-b:a" << "128k" << "-ac" << "2"
    << "-f" << "hls"
    << "-hls_time" << "4"
    << "-hls_list_size" << "6"
    << "-hls_flags" << "delete_segments+append_list+omit_endlist"
    << playlistPath;

    m_ffmpeg->start("ffmpeg", args);

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server->hasPendingConnections()) {
            handleClient(m_server->nextPendingConnection());
        }
    });

    // Порт 0 — ОС выберет свободный
    if (!m_server->listen(QHostAddress::Any, 0)) {
        qWarning() << "CastStreamServer: listen failed:" << m_server->errorString();
        stop();
        return false;
    }
    m_port = m_server->serverPort();
    return true;
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
        if (!request.startsWith("GET ")) {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QString path = QString::fromUtf8(request).section(' ', 1, 1);
        QString fileName = path.section('/', -1).split('?').first();

        if (fileName.isEmpty() || fileName.contains("..")) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QString filePath = m_tempDir->path() + "/" + fileName;
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }

        QByteArray data = file.readAll();
        QString contentType = fileName.endsWith(".m3u8")
        ? "application/vnd.apple.mpegurl"
        : "video/mp2t";

        QByteArray header = QString(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %1\r\n"
            "Content-Length: %2\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "\r\n").arg(contentType).arg(data.size()).toUtf8();

            socket->write(header);
            socket->write(data);
            socket->disconnectFromHost();
    });
}
