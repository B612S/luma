#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QProcess>

class CastStreamServer : public QObject {
    Q_OBJECT
public:
    explicit CastStreamServer(QObject *parent = nullptr);
    ~CastStreamServer();

    bool start(const QString &inputFile, const QString &targetIP,
               const QString &audioFilter = {});
    void stop();

    QString url() const;
    bool isRunning() const { return m_server && m_server->isListening(); }

    bool isPlaylistReady() const;
    bool isFullyEncoded() const;

private:
    void handleClient(QTcpSocket *socket);
    QString detectLocalIP(const QString &targetIP) const;
    QString playlistPath() const;

    QTcpServer *m_server = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QProcess *m_ffmpeg = nullptr;
    QString m_localIP;
    // Random path prefix: only the Cast device receives this URL, so other
    // devices on the LAN cannot fetch the transcoded stream blindly.
    QString m_token;
    quint16 m_port = 0;
};
