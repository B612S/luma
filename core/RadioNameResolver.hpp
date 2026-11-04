#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QThread>

class QNetworkAccessManager;
class QNetworkReply;

// Internal worker of RadioNameResolver. It owns the network manager and lives
// in its own thread: the first HTTPS request can spend seconds on proxy
// detection, TLS and CA initialisation, and that must never stall the GUI or
// the video output. The manager is created lazily inside resolve(), i.e. in
// the worker thread, so its thread affinity is correct.
class RadioProbeWorker : public QObject {
    Q_OBJECT

public:
    explicit RadioProbeWorker(QObject *parent = nullptr);

public slots:
    void resolve(const QString &url);

signals:
    void nameResolved(const QString &url, const QString &name);

private:
    QNetworkAccessManager *manager();
    void startNext();
    void finish(const QString &url, QNetworkReply *reply, const QString &name);
    static QString decodeIcyName(const QByteArray &raw);

    QNetworkAccessManager *m_manager = nullptr;
    QQueue<QString> m_queue;
    QSet<QString> m_active;
    QSet<QString> m_tried;
    int m_running = 0;
};

// Fetches the station name of a radio stream from its ICY "icy-name" header
// without playing it, so the playlist can show the station instead of the URL.
// Only the response headers are read: the request is aborted before the audio
// body arrives. Probes run in a background thread, a couple at a time.
class RadioNameResolver : public QObject {
    Q_OBJECT

public:
    explicit RadioNameResolver(QObject *parent = nullptr);
    ~RadioNameResolver() override;

    // Queues a probe for an http(s) URL. Each URL is probed at most once per
    // session, regardless of how many playlists contain it.
    void resolve(const QString &url);

signals:
    // Emitted only when the stream actually reports a name.
    void nameResolved(const QString &url, const QString &name);

private:
    QThread m_thread;
    RadioProbeWorker *m_worker = nullptr;
};
