#include "cast/SocksHttpProxy.hpp"

#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace {
const int kMaxHttpHeader = 64 * 1024;
const int kHandshakeTimeoutMs = 20000;
}

class SocksHttpProxyConnection : public QObject {
public:
    SocksHttpProxyConnection(SocksHttpProxy *manager, QTcpSocket *client)
        : QObject(manager), m_manager(manager), m_client(client)
    {
        m_client->setParent(this);
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        m_timeout->setInterval(kHandshakeTimeoutMs);
        connect(m_timeout, &QTimer::timeout, this, [this]() {
            fail(504, QStringLiteral("SOCKS handshake timeout"));
        });
    }

    void start()
    {
        connect(m_client, &QTcpSocket::readyRead, this, &SocksHttpProxyConnection::onClientReadyRead);
        connect(m_client, &QTcpSocket::disconnected, this, &SocksHttpProxyConnection::finish);
        connect(m_client, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) { finish(); });
        m_timeout->start();
        if (m_client->bytesAvailable() > 0)
            onClientReadyRead();
    }

    void abort() { finish(); }

private:
    enum class State {
        ReadingRequest,
        ConnectingProxy,
        SocksGreeting,
        SocksAuth,
        SocksConnect,
        Tunneling,
        Closed
    };

    void onClientReadyRead()
    {
        if (m_state == State::Tunneling) {
            if (m_proxy) m_proxy->write(m_client->readAll());
            return;
        }
        if (m_state != State::ReadingRequest) return;

        m_clientBuffer += m_client->readAll();
        if (m_clientBuffer.size() > kMaxHttpHeader) {
            fail(431, QStringLiteral("HTTP request header is too large"));
            return;
        }

        const int end = m_clientBuffer.indexOf("\r\n\r\n");
        if (end < 0) return;

        m_clientAfterHeaders = m_clientBuffer.mid(end + 4);
        m_clientBuffer.truncate(end + 4);
        parseRequest();
    }

    void parseRequest()
    {
        const int lineEnd = m_clientBuffer.indexOf("\r\n");
        if (lineEnd < 0) {
            fail(400, QStringLiteral("Bad HTTP request"));
            return;
        }

        const QByteArray requestLine = m_clientBuffer.left(lineEnd);
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() != 3) {
            fail(400, QStringLiteral("Bad HTTP request line"));
            return;
        }

        const QByteArray method = parts[0].toUpper();
        const QString target = QString::fromUtf8(parts[1]);

        if (method == "CONNECT") {
            m_isConnect = true;
            if (!splitHostPort(target, m_targetHost, m_targetPort, 443)) {
                fail(400, QStringLiteral("Bad CONNECT target"));
                return;
            }
            m_forwardRequest.clear();
        } else {
            // Plain HTTP through the proxy: the client sends an absolute URL.
            const QUrl url(target);
            if (!url.isValid() || url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0
                    || url.host().isEmpty()) {
                fail(400, QStringLiteral("Only HTTP proxy requests are supported"));
                return;
            }
            m_targetHost = url.host();
            m_targetPort = static_cast<quint16>(url.port(80));

            QString path = url.path(QUrl::FullyEncoded);
            if (path.isEmpty()) path = QStringLiteral("/");
            const QString query = url.query(QUrl::FullyEncoded);
            if (!query.isEmpty()) path += QLatin1Char('?') + query;

            QByteArray rewritten = method + " " + path.toUtf8() + " " + parts[2] + "\r\n";
            const QList<QByteArray> headers = m_clientBuffer.mid(lineEnd + 2).split('\n');
            for (QByteArray header : headers) {
                if (header.endsWith('\r')) header.chop(1);
                if (header.isEmpty()) continue;
                if (header.toLower().startsWith("proxy-connection:")) continue;
                rewritten += header + "\r\n";
            }
            rewritten += "\r\n";
            m_forwardRequest = rewritten;
        }

        beginProxy();
    }

    void beginProxy()
    {
        m_state = State::ConnectingProxy;
        m_proxy = new QTcpSocket(this);
        connect(m_proxy, &QTcpSocket::connected, this, &SocksHttpProxyConnection::onProxyConnected);
        connect(m_proxy, &QTcpSocket::readyRead, this, &SocksHttpProxyConnection::onProxyReadyRead);
        connect(m_proxy, &QTcpSocket::disconnected, this, &SocksHttpProxyConnection::finish);
        connect(m_proxy, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            // A local SOCKS client may briefly refuse a connection while it
            // opens its own upstream connection (HLS opens many parallel
            // segment requests). Retry once before failing the request: a
            // 502 here reaches mpv as a dropped segment and looks like a
            // stutter.
            if (!m_proxyRetried) {
                m_proxyRetried = true;
                m_proxy->disconnect(this);
                m_proxy->deleteLater();
                m_proxy = nullptr;
                QTimer::singleShot(100, this, [this]() {
                    if (m_state != State::Closed) beginProxy();
                });
                return;
            }
            fail(502, QStringLiteral("SOCKS proxy connection failed"));
        });
        m_proxy->connectToHost(m_manager->socksHost(), m_manager->socksPort());
    }

    void onProxyConnected()
    {
        const bool hasAuth = !m_manager->socksUser().isEmpty()
                          || !m_manager->socksPassword().isEmpty();
        QByteArray greeting;
        greeting.append(char(0x05));
        if (hasAuth) {
            greeting.append(char(0x02)); // no auth / user-pass
            greeting.append(char(0x00));
            greeting.append(char(0x02));
        } else {
            greeting.append(char(0x01));
            greeting.append(char(0x00));
        }
        m_proxy->write(greeting);
        m_state = State::SocksGreeting;
    }

    void onProxyReadyRead()
    {
        if (m_state == State::Tunneling) {
            m_client->write(m_proxy->readAll());
            return;
        }
        m_proxyBuffer += m_proxy->readAll();
        // The SOCKS handshake messages are tiny; never let a misbehaving proxy
        // grow this buffer without bounds.
        if (m_proxyBuffer.size() > kMaxHttpHeader) {
            fail(502, QStringLiteral("SOCKS response is too large"));
            return;
        }
        processSocks();
    }

    void processSocks()
    {
        for (;;) {
            switch (m_state) {
            case State::SocksGreeting: {
                if (m_proxyBuffer.size() < 2) return;
                const int version = uchar(m_proxyBuffer[0]);
                const int method = uchar(m_proxyBuffer[1]);
                m_proxyBuffer.remove(0, 2);
                if (version != 5) {
                    fail(502, QStringLiteral("Not a SOCKS5 proxy"));
                    return;
                }
                if (method == 0x00) {
                    sendConnect();
                    return;
                }
                if (method == 0x02) {
                    const QString user = m_manager->socksUser();
                    const QString pass = m_manager->socksPassword();
                    if (user.isEmpty() && pass.isEmpty()) {
                        fail(502, QStringLiteral("SOCKS proxy requires authentication"));
                        return;
                    }
                    const QByteArray u = user.toUtf8();
                    const QByteArray p = pass.toUtf8();
                    if (u.size() > 255 || p.size() > 255) {
                        fail(502, QStringLiteral("SOCKS credentials are too long"));
                        return;
                    }
                    QByteArray auth;
                    auth.append(char(0x01));
                    auth.append(char(u.size()));
                    auth.append(u);
                    auth.append(char(p.size()));
                    auth.append(p);
                    m_proxy->write(auth);
                    m_state = State::SocksAuth;
                    return;
                }
                fail(502, QStringLiteral("No acceptable SOCKS authentication method"));
                return;
            }
            case State::SocksAuth: {
                if (m_proxyBuffer.size() < 2) return;
                const int version = uchar(m_proxyBuffer[0]);
                const int status = uchar(m_proxyBuffer[1]);
                m_proxyBuffer.remove(0, 2);
                if (version != 1 || status != 0) {
                    fail(502, QStringLiteral("SOCKS authentication failed"));
                    return;
                }
                sendConnect();
                return;
            }
            case State::SocksConnect: {
                if (m_proxyBuffer.size() < 4) return;
                const int version = uchar(m_proxyBuffer[0]);
                const int reply = uchar(m_proxyBuffer[1]);
                const int atyp = uchar(m_proxyBuffer[3]);

                int addrLen = 0;
                if (atyp == 0x01) {
                    addrLen = 4;
                } else if (atyp == 0x04) {
                    addrLen = 16;
                } else if (atyp == 0x03) {
                    if (m_proxyBuffer.size() < 5) return;
                    addrLen = 1 + uchar(m_proxyBuffer[4]);
                } else {
                    fail(502, QStringLiteral("Bad SOCKS address type"));
                    return;
                }

                const int total = 4 + addrLen + 2;
                if (m_proxyBuffer.size() < total) return;
                m_proxyBuffer.remove(0, total);

                if (version != 5 || reply != 0) {
                    fail(502, QStringLiteral("SOCKS connection failed"));
                    return;
                }
                startTunnel();
                return;
            }
            default:
                return;
            }
        }
    }

    void sendConnect()
    {
        QByteArray request;
        request.append(char(0x05));
        request.append(char(0x01));
        request.append(char(0x00));

        const QHostAddress address(m_targetHost);
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            request.append(char(0x01));
            const quint32 ipv4 = address.toIPv4Address();
            request.append(char((ipv4 >> 24) & 0xFF));
            request.append(char((ipv4 >> 16) & 0xFF));
            request.append(char((ipv4 >> 8) & 0xFF));
            request.append(char(ipv4 & 0xFF));
        } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            request.append(char(0x04));
            const Q_IPV6ADDR ipv6 = address.toIPv6Address();
            request.append(reinterpret_cast<const char *>(ipv6.c), 16);
        } else {
            // Pass the domain name to the SOCKS server: DNS goes through the proxy too.
            QByteArray host = QUrl::toAce(m_targetHost);
            if (host.isEmpty()) host = m_targetHost.toUtf8();
            if (host.size() > 255) {
                fail(400, QStringLiteral("Host name is too long"));
                return;
            }
            request.append(char(0x03));
            request.append(char(host.size()));
            request.append(host);
        }

        request.append(char((m_targetPort >> 8) & 0xFF));
        request.append(char(m_targetPort & 0xFF));
        m_proxy->write(request);
        m_state = State::SocksConnect;
    }

    void startTunnel()
    {
        m_state = State::Tunneling;
        m_timeout->stop();

        if (m_isConnect) {
            m_client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
            if (!m_clientAfterHeaders.isEmpty()) {
                m_proxy->write(m_clientAfterHeaders);
                m_clientAfterHeaders.clear();
            }
        } else {
            m_proxy->write(m_forwardRequest + m_clientAfterHeaders);
            m_forwardRequest.clear();
            m_clientAfterHeaders.clear();
        }

        if (!m_proxyBuffer.isEmpty()) {
            m_client->write(m_proxyBuffer);
            m_proxyBuffer.clear();
        }
        if (m_client->bytesAvailable() > 0)
            m_proxy->write(m_client->readAll());
    }

    void fail(int code, const QString &reason)
    {
        if (m_state == State::Closed) return;

        if (m_client && m_client->state() == QAbstractSocket::ConnectedState) {
            const QByteArray body = reason.toUtf8();
            QByteArray status;
            switch (code) {
            case 400: status = "400 Bad Request"; break;
            case 431: status = "431 Request Header Fields Too Large"; break;
            case 504: status = "504 Gateway Timeout"; break;
            default:  status = "502 Bad Gateway"; break;
            }
            QByteArray response = "HTTP/1.1 " + status + "\r\n";
            response += "Content-Type: text/plain; charset=utf-8\r\n";
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
            response += "Connection: close\r\n\r\n";
            response += body;
            m_client->write(response);
            m_client->disconnectFromHost();
        }

        qWarning() << "SocksHttpProxy:" << reason;
        finish();
    }

    void finish()
    {
        if (m_state == State::Closed) return;
        m_state = State::Closed;

        if (m_timeout) m_timeout->stop();
        if (m_client) {
            m_client->disconnect(this);
            m_client->close();
            m_client->deleteLater();
            m_client = nullptr;
        }
        if (m_proxy) {
            m_proxy->disconnect(this);
            m_proxy->close();
            m_proxy->deleteLater();
            m_proxy = nullptr;
        }
        m_manager->removeConnection(this);
        deleteLater();
    }

    static bool splitHostPort(const QString &target, QString &host, quint16 &port, quint16 defaultPort)
    {
        QString value = target;
        if (value.startsWith(QLatin1Char('['))) {
            const int close = value.indexOf(QLatin1Char(']'));
            if (close < 0) return false;
            host = value.mid(1, close - 1);
            const QString rest = value.mid(close + 1);
            if (rest.startsWith(QLatin1Char(':'))) {
                bool ok = false;
                port = rest.mid(1).toUShort(&ok);
                if (!ok) return false;
            } else {
                port = defaultPort;
            }
        } else {
            const int colon = value.lastIndexOf(QLatin1Char(':'));
            if (colon > 0) {
                host = value.left(colon);
                bool ok = false;
                port = value.mid(colon + 1).toUShort(&ok);
                if (!ok) return false;
            } else {
                host = value;
                port = defaultPort;
            }
        }
        return !host.isEmpty() && port != 0;
    }

    SocksHttpProxy *m_manager;
    QTcpSocket *m_client = nullptr;
    QTcpSocket *m_proxy = nullptr;
    QTimer *m_timeout = nullptr;
    State m_state = State::ReadingRequest;
    QByteArray m_clientBuffer;
    QByteArray m_proxyBuffer;
    QByteArray m_clientAfterHeaders;
    QByteArray m_forwardRequest;
    QString m_targetHost;
    quint16 m_targetPort = 0;
    bool m_isConnect = false;
    bool m_proxyRetried = false;
};

SocksHttpProxy::SocksHttpProxy(QObject *parent)
    : QObject(parent)
{
}

SocksHttpProxy::~SocksHttpProxy()
{
    stop();
}

bool SocksHttpProxy::start(const QUrl &socksProxy)
{
    const QString scheme = socksProxy.scheme().toLower();
    if (!socksProxy.isValid() || socksProxy.host().isEmpty()
            || (scheme != QLatin1String("socks5") && scheme != QLatin1String("socks5h"))) {
        m_lastError = QStringLiteral("Invalid SOCKS5 proxy URL");
        return false;
    }

    if (m_server && m_server->isListening() && m_socks == socksProxy)
        return true;

    stop();

    m_socks = socksProxy;
    m_lastError.clear();
    // Created in the thread that owns this object (the proxy runs on its own
    // thread, so the sockets never compete with video rendering).
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &SocksHttpProxy::handleNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        m_lastError = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        m_socks = QUrl();
        emit error(m_lastError);
        return false;
    }

    m_localUrl = QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());
    return true;
}

void SocksHttpProxy::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }

    const QList<SocksHttpProxyConnection *> connections = m_connections;
    m_connections.clear();
    for (SocksHttpProxyConnection *connection : connections)
        connection->abort();

    m_localUrl.clear();
    m_socks = QUrl();
}

void SocksHttpProxy::handleNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        if (!client) return;
        if (m_connections.size() >= 128) {
            // Do not let a local process exhaust Luma's memory/socket limit.
            client->disconnectFromHost();
            client->deleteLater();
            continue;
        }
        auto *connection = new SocksHttpProxyConnection(this, client);
        m_connections.append(connection);
        connection->start();
    }
}

void SocksHttpProxy::removeConnection(SocksHttpProxyConnection *connection)
{
    m_connections.removeAll(connection);
}
