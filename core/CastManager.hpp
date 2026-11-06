#pragma once
#include <QObject>
#include <QElapsedTimer>
#include "CastDiscovery.hpp"
#include "CastStreamServer.hpp"
#include "CastV2Client.hpp"

class Equalizer;
class QTimer;

class CastManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(CastDiscovery* discovery READ discovery CONSTANT)
    Q_PROPERTY(bool casting READ casting NOTIFY castingChanged)
    Q_PROPERTY(bool isRemotePlaying READ isRemotePlaying NOTIFY remoteStatusChanged)
    Q_PROPERTY(double remotePosition READ remotePosition NOTIFY remoteStatusChanged)
    Q_PROPERTY(double remoteDuration READ remoteDuration NOTIFY remoteStatusChanged)
    Q_PROPERTY(double remoteVolume READ remoteVolume NOTIFY remoteStatusChanged)

public:
    explicit CastManager(QObject *parent = nullptr);

    CastDiscovery* discovery() { return m_discovery; }
    bool casting() const { return m_casting; }

    bool isRemotePlaying() const { return m_client->isPlaying(); }
    double remotePosition() const { return m_client->currentPosition(); }
    double remoteDuration() const { return m_client->currentDuration(); }
    double remoteVolume() const { return m_client->currentVolume() * 100.0; }

    Q_INVOKABLE void castTo(const QString &address, quint16 port, const QString &filePath, const QString &title = {});
    Q_INVOKABLE void stopCast();

    // The current equalizer curve is baked into the HLS re-encode.
    void setEqualizer(Equalizer *equalizer) { m_equalizer = equalizer; }

    // Remote playback control
    Q_INVOKABLE void play() { m_client->play(); }
    Q_INVOKABLE void pause() { m_client->pause(); }
    Q_INVOKABLE void seekTo(double pos) { m_client->seekTo(pos); }
    Q_INVOKABLE void setVolume(double volPercent) { m_client->setVolume(volPercent / 100.0); }

signals:
    void castingChanged();
    void castStarted(const QString &deviceName);
    void castFailed(const QString &error);
    void remoteStatusChanged();

private:
    void stopEncodingWait();

    CastDiscovery *m_discovery;
    CastStreamServer *m_server;
    CastV2Client *m_client;
    Equalizer *m_equalizer = nullptr;
    bool m_casting = false;
    // Only the current "wait for FFmpeg to finish encoding" timer; stopped and
    // deleted when the cast is cancelled or replaced.
    QTimer *m_castWaitTimer = nullptr;
    QElapsedTimer m_castElapsed;
};
