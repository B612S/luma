#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusObjectPath>
#include "mpvqt/MpvItem.hpp"

class MediaManager;

class MprisManager : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    // Root Interface (org.mpris.MediaPlayer2)
    Q_PROPERTY(QString Identity READ getIdentity CONSTANT)
    Q_PROPERTY(bool CanQuit READ getCanQuit)
    Q_PROPERTY(bool CanRaise READ getCanRaise)
    Q_PROPERTY(bool HasTrackList READ getHasTrackList)
    Q_PROPERTY(QString DesktopEntry READ getDesktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ getSupportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ getSupportedMimeTypes)

    // Player Interface (org.mpris.MediaPlayer2.Player)
    Q_PROPERTY(QString PlaybackStatus READ getPlaybackStatus)
    Q_PROPERTY(QVariantMap Metadata READ getMetadata)
    Q_PROPERTY(qlonglong Position READ getPosition)
    Q_PROPERTY(double Volume READ getVolume WRITE setVolume)
    Q_PROPERTY(double Rate READ getRate)
    Q_PROPERTY(double MinimumRate READ getMinimumRate)
    Q_PROPERTY(double MaximumRate READ getMaximumRate)

    Q_PROPERTY(bool CanGoNext READ getCanGoNext)
    Q_PROPERTY(bool CanGoPrevious READ getCanGoPrevious)
    Q_PROPERTY(bool CanPlay READ getCanPlay)
    Q_PROPERTY(bool CanPause READ getCanPause)
    Q_PROPERTY(bool CanSeek READ getCanSeek)
    Q_PROPERTY(bool CanControl READ getCanControl)

    Q_PROPERTY(bool Fullscreen READ getFullscreen WRITE setFullscreen)
    Q_PROPERTY(bool CanSetFullscreen READ getCanSetFullscreen)
    Q_PROPERTY(QString LoopStatus READ getLoopStatus WRITE setLoopStatus)
    Q_PROPERTY(bool Shuffle READ getShuffle WRITE setShuffle)

public:
    explicit MprisManager(MpvItem *mpv, MediaManager *media, QObject *parent = nullptr);

    // Root Interface getters
    QString getIdentity() const { return "Luma"; }
    bool getCanQuit() const { return true; }
    bool getCanRaise() const { return false; }
    bool getHasTrackList() const { return false; }
    QString getDesktopEntry() const { return "luma"; }
    QStringList getSupportedUriSchemes() const { return {"file", "http", "https"}; }
    QStringList getSupportedMimeTypes() const { return {"video/mp4", "video/x-matroska"}; }

    // Player Interface getters
    QString getPlaybackStatus() const;
    QVariantMap getMetadata() const;
    qlonglong getPosition() const;
    double getVolume() const;

    double getRate() const { return 1.0; }
    double getMinimumRate() const { return 1.0; }
    double getMaximumRate() const { return 1.0; }

    bool getCanGoNext() const { return true; }
    bool getCanGoPrevious() const { return true; }
    bool getCanPlay() const { return true; }
    bool getCanPause() const { return true; }
    bool getCanSeek() const { return true; }
    bool getCanControl() const { return true; }

    bool getFullscreen() const { return false; }
    void setFullscreen(bool) {}
    bool getCanSetFullscreen() const { return false; }

    QString getLoopStatus() const { return "None"; }
    void setLoopStatus(const QString &) {}
    bool getShuffle() const { return false; }
    void setShuffle(bool) {}

public slots:
    // Root Interface slots
    void Raise();
    void Quit();

    // Player Interface slots
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void setVolume(double volume);
    void OpenUri(const QString &uri);

private slots:
    void PropertiesChanged();
    void sendCanProperties();

private:
    MpvItem *m_mpv;
    MediaManager *m_media;
};
