#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class SocksHttpProxyConnection;

// Local HTTP proxy that tunnels connections through SOCKS5.
// Needed because mpv/FFmpeg cannot use a SOCKS proxy directly: mpv gets
// http-proxy=http://127.0.0.1:<port> and pulls the stream (with Range requests)
// through it, while the bridge proxies the bytes into SOCKS5.
class SocksHttpProxy : public QObject {
    Q_OBJECT
public:
    explicit SocksHttpProxy(QObject *parent = nullptr);
    ~SocksHttpProxy() override;

    // Starts a listener on 127.0.0.1 with a random port.
    bool start(const QUrl &socksProxy);
    void stop();

    bool isRunning() const { return !m_localUrl.isEmpty(); }
    QString localUrl() const { return m_localUrl; }
    QString lastError() const { return m_lastError; }

signals:
    void error(const QString &message);

private:
    friend class SocksHttpProxyConnection;

    void handleNewConnection();
    void removeConnection(SocksHttpProxyConnection *connection);

    QString socksHost() const { return m_socks.host(); }
    quint16 socksPort() const { return static_cast<quint16>(m_socks.port(1080)); }
    QString socksUser() const { return m_socks.userName(); }
    QString socksPassword() const { return m_socks.password(); }

    QTcpServer m_server;
    QUrl m_socks;
    QString m_localUrl;
    QString m_lastError;
    QList<SocksHttpProxyConnection *> m_connections;
};
