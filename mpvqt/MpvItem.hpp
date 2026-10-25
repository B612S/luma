#pragma once
#include "mpvabstractitem.h"
#include <QVariant>
#include <QRectF>

class FrameDecoder;

class MpvItem : public MpvAbstractItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool videoDisabled READ videoDisabled NOTIFY videoDisabledChanged)
    Q_PROPERTY(bool deinterlaceEnabled READ deinterlaceEnabled NOTIFY deinterlaceChanged)
    Q_PROPERTY(bool cropEnabled READ cropEnabled NOTIFY cropEnabledChanged)

public:
    explicit MpvItem(QQuickItem *parent = nullptr);

    double position() const { return m_position; }
    void setPosition(double pos);
    double duration() const { return m_duration; }
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    double volume() const { return m_volume; }
    void setVolume(double vol);
    bool muted() const { return m_muted; }
    void setMuted(bool m);
    QString source() const { return m_source; }
    void setSource(const QString &src);
    bool videoDisabled() const { return m_videoDisabled; }
    bool deinterlaceEnabled() const { return m_deinterlaceEnabled; }
    bool cropEnabled() const { return m_cropEnabled; }
    void setCropEnabled(bool enabled);

    Q_INVOKABLE void seekFast(double pos);
    Q_INVOKABLE void seekExact(double pos);
    Q_INVOKABLE void requestThumbnail(double seekTime, const QString &requestId);

    Q_INVOKABLE QString getAudioOutput();
    Q_INVOKABLE QVariant property(const QString &name) { return getProperty(name); }

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

signals:
    void positionChanged();
    void durationChanged();
    void pausedChanged();
    void volumeChanged();
    void mutedChanged();
    void sourceChanged();
    void videoDisabledChanged();
    void fileEnded();
    void deinterlaceChanged();
    void cropEnabledChanged();
    void thumbnailReady(const QString &requestId);

private slots:
    void onMpvPropertyChanged(const QString &property, const QVariant &value);

private:
    QString m_source;
    double m_position = 0.0;
    double m_duration = 0.0;
    bool   m_paused   = false;
    double m_volume   = 100.0;
    bool   m_muted    = false;
    bool   m_videoDisabled = false;
    bool   m_deinterlaceEnabled = false;
    bool   m_cropEnabled = false;

    FrameDecoder *m_frameDecoder = nullptr;
};
