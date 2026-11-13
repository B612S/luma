#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <functional>
#include <utility>
#include "cast/CastDeviceLock.hpp"
#include "cast/CastSource.hpp"
#include "cast/CastDiscovery.hpp"
#include "cast/CastMemoryHlsServer.hpp"
#include "cast/CastV2Client.hpp"

class Equalizer;
class QTimer;

class CastManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(CastDiscovery* discovery READ discovery CONSTANT)
    Q_PROPERTY(bool casting READ casting NOTIFY castingChanged)
    Q_PROPERTY(bool castRequested READ castRequested NOTIFY castRequestedChanged)
    // Device name (or address) of the TV this instance is casting to.
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    // Title shown on the cast overlay (resolved yt-dlp title or the file name).
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(bool isRemotePlaying READ isRemotePlaying NOTIFY remoteStatusChanged)
    Q_PROPERTY(double remotePosition READ remotePosition NOTIFY remoteStatusChanged)
    Q_PROPERTY(double remoteDuration READ remoteDuration NOTIFY remoteStatusChanged)
    Q_PROPERTY(double remoteVolume READ remoteVolume NOTIFY remoteStatusChanged)

public:
    explicit CastManager(QObject *parent = nullptr);

    CastDiscovery* discovery() { return m_discovery; }
    bool casting() const { return m_casting; }
    // True from the moment casting is requested (including preparation) until
    // stopCast(); used to disable local controls that do not apply to the TV.
    bool castRequested() const { return m_castRequested; }
    QString deviceName() const { return m_deviceName; }
    QString mediaTitle() const { return m_mediaTitle; }

    bool isRemotePlaying() const { return m_client->isPlaying(); }
    double remotePosition() const {
        return m_casting ? m_server->streamStart() + m_client->currentPosition() : 0.0;
    }
    double remoteDuration() const { return m_casting ? m_server->duration() : 0.0; }
    double remoteVolume() const { return m_client->currentVolume() * 100.0; }

    Q_INVOKABLE void castTo(const QString &address, quint16 port, const QString &filePath,
                            const QString &title = {}, const QString &deviceName = {});
    Q_INVOKABLE void stopCast();

    // Keeps the active cast in sync with playlist/file changes: while casting,
    // every newly opened or navigated file is sent to the same TV.
    void onMediaFileChanged(const QString &filePath);

    // The current equalizer curve is baked into the HLS re-encode.
    void setEqualizer(Equalizer *equalizer) { m_equalizer = equalizer; }

    // Supplies the direct video/audio URLs that yt-dlp resolved for the media
    // that is playing. The playlist keeps the original page URL, so casting a
    // YouTube link would otherwise hand FFmpeg the HTML page.
    using CastSourceResolver = std::function<CastSource(const QString &filePath)>;
    void setSourceResolver(CastSourceResolver resolver)
    {
        m_sourceResolver = std::move(resolver);
    }

    // Proxy used only by the cast encoder for remote sources (the player's
    // own proxy handling is untouched).
    void setProxyUrl(const QString &proxyUrl) { m_server->setProxyUrl(proxyUrl); }

    // Remote playback control
    Q_INVOKABLE void play() { m_client->play(); }
    Q_INVOKABLE void pause() { m_client->pause(); }
    Q_INVOKABLE void seekTo(double pos);
    Q_INVOKABLE void setVolume(double volPercent);

signals:
    void castingChanged();
    void castRequestedChanged();
    void deviceNameChanged();
    void mediaTitleChanged();
    void castStarted(const QString &deviceName);
    void castFailed(const QString &error);
    // Another Luma instance already owns the TV; the cast was not started and
    // the current state (including an existing cast) is unchanged.
    void castBusy(const QString &deviceName);
    void remoteStatusChanged();

private:
    void stopEncodingWait();
    void stopSeekWait();

    CastDiscovery *m_discovery;
    CastMemoryHlsServer *m_server;
    CastV2Client *m_client;
    CastDeviceLock m_deviceLock;
    QTimer *m_castWaitTimer = nullptr;
    QElapsedTimer m_castElapsed;
    QTimer *m_seekWaitTimer = nullptr;
    QElapsedTimer m_seekElapsed;
    QString m_mediaTitle;
    QString m_deviceAddress;
    quint16 m_devicePort = 0;
    QString m_deviceName;
    QString m_currentFile;
    Equalizer *m_equalizer = nullptr;
    CastSourceResolver m_sourceResolver;
    bool m_casting = false;
    bool m_castRequested = false;
};
