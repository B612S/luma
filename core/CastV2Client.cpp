#include "CastV2Client.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QTimer>

CastV2Client::CastV2Client(QObject *parent) : QObject(parent) {
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(5000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &CastV2Client::sendHeartbeat);

    m_statusPollTimer = new QTimer(this);
    m_statusPollTimer->setInterval(1000);
    connect(m_statusPollTimer, &QTimer::timeout, this, &CastV2Client::pollStatus);
}

void CastV2Client::connectToDevice(const QString &address, quint16 port) {
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_transportId.clear();
    m_sessionId.clear();
    m_mediaSessionId = -1;
    m_loadSent = false;
    m_launchRequested = false;

    m_socket = new QSslSocket(this);
    connect(m_socket, &QSslSocket::sslErrors, m_socket, [this](const QList<QSslError> &) {
        m_socket->ignoreSslErrors();
    });
    connect(m_socket, &QSslSocket::readyRead, this, &CastV2Client::onReadyRead);
    connect(m_socket, &QSslSocket::encrypted, this, &CastV2Client::onEncrypted);
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(m_socket->errorString());
    });
    connect(m_socket, &QSslSocket::disconnected, this, [this]() {
        m_heartbeatTimer->stop();
        m_statusPollTimer->stop();
        m_transportId.clear();
        m_sessionId.clear();
        m_mediaSessionId = -1;
        m_isPlaying = false;
        emit playbackStatusChanged();
    });

    m_socket->connectToHostEncrypted(address, port);
}

void CastV2Client::startSession(const QString &mediaUrl, const QString &mediaTitle) {
    m_pendingMediaUrl = mediaUrl;
    m_pendingMediaTitle = mediaTitle;
    m_loadSent = false;

    if (m_socket && m_socket->isEncrypted()) {
        sendLaunchReceiver();
    }
}

void CastV2Client::onEncrypted() {
    emit connected();
    m_heartbeatTimer->start();

    sendMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.connection", R"({"type":"CONNECT"})");
    sendLaunchReceiver();
}

void CastV2Client::sendLaunchReceiver() {
    if (m_launchRequested) return;
    m_launchRequested = true;

    QJsonObject obj;
    obj["type"] = "LAUNCH";
    obj["appId"] = "CC1AD845";
    obj["requestId"] = m_requestId++;

    sendMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.receiver",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void CastV2Client::sendLoadMedia() {
    if (m_loadSent || m_transportId.isEmpty() || m_pendingMediaUrl.isEmpty()) return;
    m_loadSent = true;

    QJsonObject media;
    media["contentId"] = m_pendingMediaUrl;
    media["contentType"] = "application/vnd.apple.mpegurl";
    media["streamType"] = "BUFFERED";

    QJsonObject metadata;
    metadata["metadataType"] = 0;
    metadata["title"] = m_pendingMediaTitle;
    media["metadata"] = metadata;

    QJsonObject obj;
    obj["type"] = "LOAD";
    obj["requestId"] = m_requestId++;
    obj["media"] = media;
    obj["autoplay"] = true;
    obj["currentTime"] = 0;

    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.media",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void CastV2Client::play() {
    if (m_transportId.isEmpty()) return;
    QJsonObject obj;
    obj["type"] = "PLAY";
    obj["requestId"] = m_requestId++;
    if (m_mediaSessionId > 0) obj["mediaSessionId"] = m_mediaSessionId;
    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.media",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void CastV2Client::pause() {
    if (m_transportId.isEmpty()) return;
    QJsonObject obj;
    obj["type"] = "PAUSE";
    obj["requestId"] = m_requestId++;
    if (m_mediaSessionId > 0) obj["mediaSessionId"] = m_mediaSessionId;
    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.media",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void CastV2Client::seekTo(double seconds) {
    if (m_transportId.isEmpty()) return;

    QJsonObject obj;
    obj["type"] = "SEEK";
    obj["requestId"] = m_requestId++;
    obj["currentTime"] = seconds;
    obj["mediaSessionId"] = (m_mediaSessionId > 0) ? m_mediaSessionId : 1;
    obj["resumeState"] = "PLAYBACK_START";

    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.media",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));

    // Задержка, чтобы ТВ успел обработать
    QTimer::singleShot(300, this, &CastV2Client::pollStatus);
}

void CastV2Client::setVolume(double level) {
    if (m_transportId.isEmpty()) return;
    QJsonObject volume;
    volume["level"] = qBound(0.0, level, 1.0);
    volume["muted"] = false;

    QJsonObject obj;
    obj["type"] = "SET_VOLUME";
    obj["requestId"] = m_requestId++;
    obj["volume"] = volume;
    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.media",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void CastV2Client::pollStatus() {
    if (m_transportId.isEmpty()) return;
    QJsonObject obj;
    obj["type"] = "GET_STATUS";
    obj["requestId"] = m_requestId++;
    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.media",
                QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void CastV2Client::stop() {
    m_heartbeatTimer->stop();
    m_statusPollTimer->stop();
    if (m_socket) {
        m_socket->disconnectFromHost();
    }
    m_transportId.clear();
    m_sessionId.clear();
    m_mediaSessionId = -1;
    m_pendingMediaUrl.clear();
    m_loadSent = false;
    m_launchRequested = false;
    m_isPlaying = false;
    emit playbackStatusChanged();
}

void CastV2Client::sendHeartbeat() {
    if (m_socket && m_socket->isEncrypted()) {
        sendMessage("sender-0", "receiver-0", "urn:x-cast:com.google.cast.tp.heartbeat", R"({"type":"PING"})");
    }
}

void CastV2Client::onReadyRead() {
    QByteArray data = m_socket->readAll();

    while (data.size() >= 4) {
        quint32 len = (quint8(data[0]) << 24) | (quint8(data[1]) << 16) | (quint8(data[2]) << 8) | quint8(data[3]);
        if (data.size() < 4 + int(len)) break;

        QByteArray proto = data.mid(4, len);
        data.remove(0, 4 + len);

        QString jsonPayload;
        int pos = 0;
        while (pos < proto.size()) {
            quint8 tag = quint8(proto[pos++]);
            int field = tag >> 3;
            int wire = tag & 0x07;

            if (wire == 2) {
                quint64 strLen = 0;
                int shift = 0;
                while (pos < proto.size()) {
                    quint8 b = quint8(proto[pos++]);
                    strLen |= quint64(b & 0x7F) << shift;
                    if (!(b & 0x80)) break;
                    shift += 7;
                }
                QByteArray val = proto.mid(pos, int(strLen));
                pos += int(strLen);
                if (field == 6) jsonPayload = QString::fromUtf8(val);
            } else if (wire == 0) {
                while (pos < proto.size()) { if (!(quint8(proto[pos++]) & 0x80)) break; }
            } else if (wire == 5) { pos += 4; }
            else if (wire == 1) { pos += 8; }
            else { break; }
        }

        if (jsonPayload.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(jsonPayload.toUtf8());
        QJsonObject obj = doc.object();
        QString type = obj["type"].toString();

        if (type == "RECEIVER_STATUS") {
            QJsonObject status = obj["status"].toObject();
            QJsonArray apps = status["applications"].toArray();
            for (const QJsonValue &appVal : apps) {
                QJsonObject app = appVal.toObject();
                if (app["appId"].toString() == "CC1AD845") {
                    m_transportId = app["transportId"].toString();
                    m_sessionId = app["sessionId"].toString();

                    sendMessage("sender-0", m_transportId, "urn:x-cast:com.google.cast.tp.connection", R"({"type":"CONNECT"})");
                    sendLoadMedia();
                    break;
                }
            }
        }
        else if (type == "MEDIA_STATUS") {
            QJsonArray statusArr = obj["status"].toArray();
            if (!statusArr.isEmpty()) {
                QJsonObject st = statusArr[0].toObject();

                if (st.contains("mediaSessionId")) {
                    m_mediaSessionId = st["mediaSessionId"].toInt();
                }

                QString playerState = st["playerState"].toString();
                bool wasPlaying = m_isPlaying;
                m_isPlaying = (playerState == "PLAYING");

                m_currentPosition = st["currentTime"].toDouble();
                m_currentDuration = st["media"].toObject()["duration"].toDouble();

                double newVol = st["volume"].toObject()["level"].toDouble(-1.0);
                bool volChanged = false;
                if (newVol >= 0.0 && qAbs(newVol - m_currentVolume) > 0.001) {
                    m_currentVolume = newVol;
                    volChanged = true;
                }

                if (m_mediaSessionId != -1 && !m_statusPollTimer->isActive()) {
                    m_statusPollTimer->start();
                } else if (m_mediaSessionId == -1) {
                    m_statusPollTimer->stop();
                }

                if (wasPlaying != m_isPlaying) emit playbackStatusChanged();
                emit positionChanged();
                if (volChanged) emit volumeChanged();

                if (playerState == "PLAYING" || playerState == "BUFFERING") {
                    emit mediaLoaded();
                }
            } else {
                m_mediaSessionId = -1;
                m_statusPollTimer->stop();
            }
        }
        else if (type == "LOAD_FAILED" || type == "INVALID_REQUEST" || type == "ERROR") {
            qWarning() << "CastV2Client ERROR:" << jsonPayload;
            emit errorOccurred("Chromecast rejected media: " + jsonPayload);
        }
    }
}

void CastV2Client::sendMessage(const QString &sourceId, const QString &destId,
                               const QString &ns, const QString &jsonPayload) {
    if (!m_socket || !m_socket->isEncrypted()) return;
    m_socket->write(encodeCastMessage(sourceId, destId, ns, jsonPayload));
                               }

                               QByteArray CastV2Client::encodeCastMessage(const QString &sourceId, const QString &destId,
                                                                          const QString &ns, const QString &jsonPayload) {
                                   QByteArray payload;
                                   auto writeVarint = [&payload](quint64 v) {
                                       while (v >= 0x80) { payload.append(char(quint8(v) | 0x80)); v >>= 7; }
                                       payload.append(char(quint8(v)));
                                   };
                                   auto writeStringField = [&payload, &writeVarint](int field, const QByteArray &s) {
                                       payload.append(char(quint8((field << 3) | 2)));
                                       writeVarint(quint64(s.size()));
                                       payload.append(s);
                                   };

                                   writeVarint(0x08); writeVarint(0);
                                   writeStringField(2, sourceId.toUtf8());
                                   writeStringField(3, destId.toUtf8());
                                   writeStringField(4, ns.toUtf8());
                                   writeVarint(0x28); writeVarint(0);
                                   writeStringField(6, jsonPayload.toUtf8());

                                   QByteArray result;
                                   quint32 size = quint32(payload.size());
                                   result.append(char((size >> 24) & 0xFF));
                                   result.append(char((size >> 16) & 0xFF));
                                   result.append(char((size >> 8) & 0xFF));
                                   result.append(char(size & 0xFF));
                                   result.append(payload);
                                   return result;
                                                                          }
