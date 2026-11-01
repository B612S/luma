#pragma once
#include "mpvabstractitem.h"
#include <QVariant>
#include <QRectF>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>

class FrameDecoder;

// Кадры для превью декодируются в отдельном потоке: FFmpeg-перемотка
// занимает десятки миллисекунд и не должна подвешивать интерфейс.
class ThumbnailWorker : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailWorker(QObject *parent = nullptr);
    ~ThumbnailWorker() override;

public slots:
    void decode(const QString &filePath, double time, const QString &requestId);

signals:
    void decoded(const QString &requestId, double time, const QImage &image);

private:
    FrameDecoder *m_decoder = nullptr;
};

class MpvItem : public MpvAbstractItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaTitleChanged)
    Q_PROPERTY(bool videoDisabled READ videoDisabled NOTIFY videoDisabledChanged)
    Q_PROPERTY(bool deinterlaceEnabled READ deinterlaceEnabled NOTIFY deinterlaceChanged)
    Q_PROPERTY(bool cropEnabled READ cropEnabled NOTIFY cropEnabledChanged)

    // НОВОЕ: Свойство для передачи состояния каста из QML в C++
    Q_PROPERTY(bool isCasting READ isCasting WRITE setCastingState NOTIFY isCastingChanged)

public:
    explicit MpvItem(QQuickItem *parent = nullptr);
    ~MpvItem() override;

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
    // Актуальный заголовок потока (для радио — ICY StreamTitle).
    QString mediaTitle() const { return m_mediaTitle; }
    bool videoDisabled() const { return m_videoDisabled; }
    bool deinterlaceEnabled() const { return m_deinterlaceEnabled; }
    bool cropEnabled() const { return m_cropEnabled; }
    void setCropEnabled(bool enabled);

    // НОВОЕ:
    bool isCasting() const { return m_isCasting; }
    void setCastingState(bool casting);

    Q_INVOKABLE void seekFast(double pos);
    Q_INVOKABLE void seekExact(double pos);
    Q_INVOKABLE void requestThumbnail(double seekTime, const QString &requestId);

    Q_INVOKABLE QString getAudioOutput();
    Q_INVOKABLE QString getAudioDevice();
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
    void mediaTitleChanged();
    void videoDisabledChanged();
    void fileEnded(const QString &reason);
    void deinterlaceChanged();
    void cropEnabledChanged();
    void thumbnailReady(const QString &requestId);

    // НОВОЕ:
    void isCastingChanged();

private slots:
    void onMpvPropertyChanged(const QString &property, const QVariant &value);
    void onThumbnailDecoded(const QString &requestId, double time, const QImage &image);

private:
    void dispatchThumbnail();

    QString m_source;
    QString m_mediaTitle;
    double m_position = 0.0;
    double m_duration = 0.0;
    bool   m_paused   = false;
    double m_volume   = 100.0;
    bool   m_muted    = false;
    bool   m_videoDisabled = false;
    bool   m_deinterlaceEnabled = false;
    bool   m_cropEnabled = false;

    // НОВОЕ:
    bool   m_isCasting = false;

    QThread *m_thumbThread = nullptr;
    ThumbnailWorker *m_thumbWorker = nullptr;
    bool m_thumbBusy = false;
    bool m_thumbPending = false;
    double m_thumbPendingTime = 0.0;
    QString m_thumbPendingId;

    QElapsedTimer m_positionTimer;
};
