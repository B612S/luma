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

    // Запускает FFmpeg и HTTP-сервер. Возвращает true, если сервер поднялся.
    // URL доступен через url().
    bool start(const QString &inputFile);
    void stop();

    QString url() const;
    bool isRunning() const { return m_server && m_server->isListening(); }

private:
    void handleClient(QTcpSocket *socket);
    QString detectLocalIP() const;

    QTcpServer *m_server = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
    QProcess *m_ffmpeg = nullptr;
    QString m_localIP;
    quint16 m_port = 0;
};
