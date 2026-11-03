#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkReply;

// Reads the live StreamTitle from an Icecast/Shoutcast stream. mpv exposes the
// static icy-name but not the per-track StreamTitle for every server, so this
// small reader runs alongside playback for direct radio URLs.
class IcyMetadataReader : public QObject {
    Q_OBJECT

public:
    explicit IcyMetadataReader(QObject *parent = nullptr);

    void start(const QUrl &url);
    void stop();

signals:
    void titleChanged(const QString &title);

private:
    void onReadyRead();
    void parse();

    QNetworkAccessManager m_manager;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    int m_metaint = 0;
    QString m_lastTitle;
};
