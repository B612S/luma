#pragma once
#include <QObject>
#include <QSslSocket>
#include <QTimer>

class CastV2Client : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStatusChanged)
    Q_PROPERTY(double currentPosition READ currentPosition NOTIFY positionChanged)
    Q_PROPERTY(double currentDuration READ currentDuration NOTIFY positionChanged)
    Q_PROPERTY(double currentVolume READ currentVolume NOTIFY volumeChanged)

public:
    explicit CastV2Client(QObject *parent = nullptr);

    void connectToDevice(const QString &address, quint16 port);
    void startSession(const QString &mediaUrl, const QString &mediaTitle);
    void stop();

    bool isPlaying() const { return m_isPlaying; }
    double currentPosition() const { return m_currentPosition; }
    double currentDuration() const { return m_currentDuration; }
    double currentVolume() const { return m_currentVolume; }

    // Команды управления
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seekTo(double seconds);
    Q_INVOKABLE void setVolume(double level); // 0.0 - 1.0

signals:
    void connected();
    void mediaLoaded();
    void playbackStatusChanged();
    void positionChanged();
    void volumeChanged();
    void errorOccurred(const QString &message);

private slots:
    void onReadyRead();
    void onEncrypted();
    void sendHeartbeat();
    void pollStatus();

private:
    void sendMessage(const QString &sourceId, const QString &destId,
                     const QString &ns, const QString &jsonPayload);
    QByteArray encodeCastMessage(const QString &sourceId, const QString &destId,
                                 const QString &ns, const QString &jsonPayload);
    void sendLaunchReceiver();
    void sendLoadMedia();

    QSslSocket *m_socket = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_statusPollTimer = nullptr;

    QString m_transportId;
    QString m_sessionId;
    int m_requestId = 1;

    QString m_pendingMediaUrl;
    QString m_pendingMediaTitle;
    bool m_launchRequested = false;
    bool m_loadSent = false;

    bool m_isPlaying = false;
    double m_currentPosition = 0.0;
    double m_currentDuration = 0.0;
    double m_currentVolume = 0.5;
};
