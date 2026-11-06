#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>

class QNetworkReply;

// Worker of IcyMetadataReader. It owns the network manager and lives in its own
// thread: the first request can spend seconds on proxy detection, TLS and CA
// initialisation, which must never stall the GUI thread.
class IcyMetadataWorker : public QObject {
    Q_OBJECT

public:
    explicit IcyMetadataWorker(QObject *parent = nullptr);

public slots:
    void start(const QUrl &url);
    void stop();

signals:
    void titleChanged(const QString &title);

private:
    void onReadyRead();
    void parse();
    QNetworkAccessManager *manager();

    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    int m_metaint = 0;
    QString m_lastTitle;
};

// Reads the live StreamTitle from an Icecast/Shoutcast stream. mpv exposes the
// static icy-name but not the per-track StreamTitle for every server, so this
// small reader runs alongside playback for direct radio URLs. The actual
// network work happens on a worker thread; the public API stays asynchronous.
class IcyMetadataReader : public QObject {
    Q_OBJECT

public:
    explicit IcyMetadataReader(QObject *parent = nullptr);
    ~IcyMetadataReader() override;

    void start(const QUrl &url);
    void stop();

signals:
    void titleChanged(const QString &title);

private:
    QThread m_thread;
    IcyMetadataWorker *m_worker = nullptr;
};
