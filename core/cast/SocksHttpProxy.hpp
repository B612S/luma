#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class SocksHttpProxyConnection;

// Local HTTP proxy that tunnels connections through SOCKS5, used only by the
// cast encoder. FFmpeg has no SOCKS client of its own, so it reads the remote
// source from http://127.0.0.1:<port> while the bytes are proxied into SOCKS5.
// The player itself (libmpv/libcurl) uses the SOCKS URL directly. The listener
// is bound to loopback and is never exposed to the network or to the TV.
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

    QTcpServer *m_server = nullptr;
    QUrl m_socks;
    QString m_localUrl;
    QString m_lastError;
    QList<SocksHttpProxyConnection *> m_connections;
};
