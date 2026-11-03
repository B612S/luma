#include "IcyMetadataReader.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>

IcyMetadataReader::IcyMetadataReader(QObject *parent) : QObject(parent) {}

void IcyMetadataReader::start(const QUrl &url)
{
    stop();
    if (!url.isValid() || (url.scheme() != QLatin1String("http")
                           && url.scheme() != QLatin1String("https")))
        return;

    m_buffer.clear();
    m_metaint = 0;
    m_lastTitle.clear();

    QNetworkRequest request(url);
    request.setRawHeader("Icy-MetaData", "1");
    request.setRawHeader("User-Agent", "Luma");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_manager.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &IcyMetadataReader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply) {
            m_reply->deleteLater();
            m_reply = nullptr;
        }
    });
}

void IcyMetadataReader::stop()
{
    if (!m_reply) return;
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
    m_buffer.clear();
    m_metaint = 0;
}

void IcyMetadataReader::onReadyRead()
{
    if (!m_reply) return;

    if (m_metaint <= 0) {
        bool ok = false;
        const int value = m_reply->rawHeader("icy-metaint").toInt(&ok);
        if (ok && value > 0) {
            m_metaint = value;
        } else {
            // Headers are available with the first payload. Without an ICY
            // interval this is not an Icecast/Shoutcast audio stream; stop
            // instead of downloading a second copy of an HLS/video stream.
            stop();
            return;
        }
    }

    m_buffer.append(m_reply->readAll());
    parse();

    // A server without ICY metadata must not make this reader grow forever.
    if (m_metaint <= 0 && m_buffer.size() > 64 * 1024) {
        m_buffer.clear();
    }
}

void IcyMetadataReader::parse()
{
    if (m_metaint <= 0) return;

    while (m_buffer.size() >= m_metaint + 1) {
        m_buffer.remove(0, m_metaint);
        const int length = static_cast<unsigned char>(m_buffer.at(0)) * 16;
        if (m_buffer.size() < 1 + length)
            return;
        const QByteArray metadata = m_buffer.mid(1, length);
        m_buffer.remove(0, 1 + length);

        const int start = metadata.indexOf("StreamTitle='");
        if (start < 0) continue;
        const int valueStart = start + 13;
        const int end = metadata.indexOf("';", valueStart);
        if (end < 0) continue;
        const QString title = QString::fromUtf8(metadata.mid(valueStart, end - valueStart))
                                  .trimmed();
        if (title.isEmpty() || title == m_lastTitle)
            continue;
        m_lastTitle = title;
        emit titleChanged(title);
    }
}
