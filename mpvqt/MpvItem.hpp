// Luma-specific file: not part of upstream KDE mpvqt (see mpvqt/README.md).
#pragma once
#include "mpvabstractitem.h"
#include <QVariant>
#include <QRectF>
#include <QRect>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <QHash>
#include <QPointer>

class FrameDecoder;

// Thumbnail frames are decoded on a separate thread: an FFmpeg seek
// takes tens of milliseconds and must not block the UI.
class ThumbnailWorker : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailWorker(QObject *parent = nullptr);
    ~ThumbnailWorker() override;

public slots:
    void decode(const QString &filePath, double time, const QString &requestId);
    void clearCache();

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
    Q_PROPERTY(QRect videoCropRect READ videoCropRect NOTIFY videoCropRectChanged)
    Q_PROPERTY(QVariantList chapterList READ chapterList NOTIFY chapterListChanged)
    Q_PROPERTY(int chapter READ chapter NOTIFY chapterChanged)

    // NEW: Property for passing the cast state from QML to C++
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
    // `live` disables the rewind/DVR cache and keeps only a small forward buffer.
    void setSource(const QString &src, bool live = false);
    // Audio stream to attach to the next source. yt-dlp returns video and
    // audio as separate URLs; this is passed as a file-local option so mpv
    // opens both tracks together instead of adding audio after fileLoaded.
    void setExternalAudio(const QString &url);
    // OGM chapter file for the next source (yt-dlp links have no embedded
    // chapters; mpv loads this file as if they were part of the stream).
    void setChaptersFile(const QString &path);
    // Current stream title (for radio — the ICY StreamTitle).
    QString mediaTitle() const { return m_mediaTitle; }
    bool videoDisabled() const { return m_videoDisabled; }
    bool deinterlaceEnabled() const { return m_deinterlaceEnabled; }
    bool cropEnabled() const { return m_cropEnabled; }
    void setCropEnabled(bool enabled);
    // Rectangle reported by mpv's video-params/crop-* properties, in source
    // frame pixels. Empty when the file has no crop information.
    QRect videoCropRect() const;
    QVariantList chapterList() const { return m_chapterList; }
    int chapter() const { return m_chapter; }

    // NEW:
    bool isCasting() const { return m_isCasting; }
    void setCastingState(bool casting);

    Q_INVOKABLE void seekFast(double pos);
    Q_INVOKABLE void seekExact(double pos);
    Q_INVOKABLE void requestThumbnail(double seekTime, const QString &requestId);
    // Unloads the file and returns to the "nothing opened" state: the renderer
    // paints black and the control bar no longer shows the finished file.
    void stopPlayback();

    Q_INVOKABLE QString getAudioOutput();
    Q_INVOKABLE QString getAudioDevice();
    // Enables mpv's own log messages at "off"/"error"/"info"/"debug".
    void setLogLevel(const QString &mode);
    Q_INVOKABLE QVariant property(const QString &name) { return getProperty(name); }
    // Last value received from mpv's observeProperty; never blocks the UI.
    Q_INVOKABLE QVariant observedProperty(const QString &name) const { return m_observedValues.value(name); }

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
    void fileLoaded();
    void deinterlaceChanged();
    void cropEnabledChanged();
    void videoCropRectChanged();
    // The file's display aspect or source dimensions changed (also emitted for
    // the new file after switching), so aspect-dependent previews can follow.
    void videoParamsChanged();
    void chapterListChanged();
    void chapterChanged();
    void thumbnailReady(const QString &requestId);
    // Emitted from the mpv worker thread; connected via a queued connection.
    void mpvLogMessage(const QString &prefix, const QString &level, const QString &message);

    // NEW:
    void isCastingChanged();

private slots:
    void onMpvPropertyChanged(const QString &property, const QVariant &value);
    void onThumbnailDecoded(const QString &requestId, double time, const QImage &image);

private:
    void dispatchThumbnail();

    QString m_source;
    QString m_externalAudio;
    QString m_chaptersFile;
    bool m_chaptersFileSet = false;
    QString m_mediaTitle;
    QHash<QString, QVariant> m_observedValues;
    double m_position = 0.0;
    double m_duration = 0.0;
    bool   m_paused   = false;
    double m_volume   = 100.0;
    bool   m_muted    = false;
    bool   m_videoDisabled = false;
    bool   m_deinterlaceEnabled = false;
    bool   m_cropEnabled = false;
    QVariantList m_chapterList;
    int    m_chapter = -1;

    // NEW:
    bool   m_isCasting = false;

    QThread *m_thumbThread = nullptr;
    QPointer<ThumbnailWorker> m_thumbWorker;
    bool m_thumbBusy = false;
    bool m_thumbPending = false;
    double m_thumbPendingTime = 0.0;
    QString m_thumbPendingId;

    QElapsedTimer m_positionTimer;
};
