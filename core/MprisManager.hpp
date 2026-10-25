#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusObjectPath>
#include "mpvqt/MpvItem.hpp"

class MediaManager;

class MprisManager : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    // Player interface
    Q_PROPERTY(QString PlaybackStatus READ getPlaybackStatus)
    Q_PROPERTY(QVariantMap Metadata READ getMetadata)
    Q_PROPERTY(qlonglong Position READ getPosition)
    Q_PROPERTY(double Volume READ getVolume WRITE setVolume)
    Q_PROPERTY(double Rate READ getRate)
    // FIX: Типы изменены на double согласно спецификации MPRIS
    Q_PROPERTY(double MinimumRate READ getMinimumRate)
    Q_PROPERTY(double MaximumRate READ getMaximumRate)
    Q_PROPERTY(bool CanGoNext READ getCanGoNext)
    Q_PROPERTY(bool CanGoPrevious READ getCanGoPrevious)
    Q_PROPERTY(bool CanPlay READ getCanPlay)
    Q_PROPERTY(bool CanPause READ getCanPause)
    Q_PROPERTY(bool CanSeek READ getCanSeek)
    Q_PROPERTY(bool CanControl READ getCanControl)
    Q_PROPERTY(QString LoopStatus READ getLoopStatus)
    Q_PROPERTY(bool Shuffle READ getShuffle)

public:
    explicit MprisManager(MpvItem *mpv, MediaManager *media, QObject *parent = nullptr);

    QString getPlaybackStatus() const;
    QVariantMap getMetadata() const;
    qlonglong getPosition() const;
    double getVolume() const;

    double getRate() const { return 1.0; }
    // FIX: Возвращаем double
    double getMinimumRate() const { return 1.0; }
    double getMaximumRate() const { return 1.0; }
    bool getCanGoNext() const { return true; }
    bool getCanGoPrevious() const { return true; }
    bool getCanPlay() const { return true; }
    bool getCanPause() const { return true; }
    bool getCanSeek() const { return true; }
    bool getCanControl() const { return true; }
    QString getLoopStatus() const { return "None"; }
    bool getShuffle() const { return false; }

public slots:
    // Root interface (org.mpris.MediaPlayer2)
    void Raise();
    void Quit();

    // Player interface (org.mpris.MediaPlayer2.Player)
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void setVolume(double volume);

private slots:
    void PropertiesChanged();

private:
    MpvItem *m_mpv;
    MediaManager *m_media;
};
