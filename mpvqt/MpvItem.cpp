// Luma-specific file: not part of upstream KDE mpvqt (see mpvqt/README.md).
#include "MpvItem.hpp"
#include <QDebug>
#include <QVariantMap>
#include <QRectF>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include "mpvcontroller.h"
#include "core/ThumbnailProvider.hpp"
#include "mpvqt/FrameDecoder.hpp"

ThumbnailWorker::ThumbnailWorker(QObject *parent) : QObject(parent) {}

ThumbnailWorker::~ThumbnailWorker() {
    delete m_decoder;
}

void ThumbnailWorker::clearCache() {
    if (m_decoder) m_decoder->clearCache();
}

void ThumbnailWorker::decode(const QString &filePath, double time, const QString &requestId) {
    if (!m_decoder) m_decoder = new FrameDecoder();
    const QImage image = m_decoder->decodeFrame(filePath, time);
    emit decoded(requestId, time, image);
}

MpvItem::MpvItem(QQuickItem *parent) : MpvAbstractItem(parent) {
    m_thumbThread = new QThread(this);
    m_thumbWorker = new ThumbnailWorker;
    m_thumbWorker->moveToThread(m_thumbThread);
    connect(m_thumbThread, &QThread::finished, m_thumbWorker.data(), &QObject::deleteLater);
    connect(m_thumbWorker.data(), &ThumbnailWorker::decoded, this, &MpvItem::onThumbnailDecoded);
    m_thumbThread->start();

    m_positionTimer.start();

    connect(mpvController(), &MpvController::propertyChanged, this, &MpvItem::onMpvPropertyChanged);
    connect(mpvController(), &MpvController::logMessage, this, &MpvItem::mpvLogMessage,
            Qt::QueuedConnection);

    connect(mpvController(), &MpvController::fileLoaded, this, [this]() {
        m_source = getProperty("path").toString();
        m_mediaTitle = getProperty("media-title").toString();
        m_duration = getProperty("duration").toDouble();
        m_volume = getProperty("volume").toDouble();
        m_muted = getProperty("mute").toBool();
        m_paused = getProperty("pause").toBool();
        m_deinterlaceEnabled = (getProperty("deinterlace").toString() != "no");

        emit sourceChanged();
        emit mediaTitleChanged();
        emit durationChanged();
        emit volumeChanged();
        emit mutedChanged();
        emit pausedChanged();
        emit deinterlaceChanged();
        emit fileLoaded();
    });

    connect(mpvController(), &MpvController::endFile, this, [this](QString reason) {
        emit fileEnded(reason);
    });

    setProperty("keepaspect", "yes");
    setProperty("panscan", 0.0);
    setProperty("video-unscaled", "no");

    observeProperty("time-pos", MPV_FORMAT_DOUBLE);
    // End of the demuxer cache: the live edge of a live stream.
    observeProperty("demuxer-cache-time", MPV_FORMAT_DOUBLE);
    observeProperty("seekable", MPV_FORMAT_FLAG);
    observeProperty("duration", MPV_FORMAT_DOUBLE);
    observeProperty("pause", MPV_FORMAT_FLAG);
    observeProperty("volume", MPV_FORMAT_DOUBLE);
    observeProperty("mute", MPV_FORMAT_FLAG);
    observeProperty("vid", MPV_FORMAT_NODE);
    observeProperty("aid", MPV_FORMAT_INT64);
    observeProperty("sid", MPV_FORMAT_INT64);
    observeProperty("track-list", MPV_FORMAT_NODE);
    observeProperty("video-aspect-override", MPV_FORMAT_NODE);
    observeProperty("deinterlace", MPV_FORMAT_STRING);
    observeProperty("media-title", MPV_FORMAT_STRING);
    observeProperty("metadata", MPV_FORMAT_NODE);

    // Used by the statistics panel. Reading these through getProperty() from
    // QML would block the GUI thread while mpv is opening or buffering a file.
    const QStringList statsProperties = {
        QStringLiteral("filename"), QStringLiteral("file-size"),
        QStringLiteral("file-format"), QStringLiteral("video-codec"),
        QStringLiteral("audio-codec"), QStringLiteral("current-ao"),
        QStringLiteral("width"),
        QStringLiteral("height"), QStringLiteral("container-fps"),
        QStringLiteral("video-bitrate"), QStringLiteral("hwdec-current"),
        QStringLiteral("frame-drop-count"), QStringLiteral("audio-bitrate"),
        QStringLiteral("audio-params/channel-count"),
        QStringLiteral("audio-params/samplerate"),
        QStringLiteral("audio-out-params"), QStringLiteral("audio-device"),
        QStringLiteral("avsync"), QStringLiteral("demuxer-cache-duration"),
        QStringLiteral("current-tracks/video/image"),
        QStringLiteral("current-tracks/video/albumart")
    };
    for (const QString &property : statsProperties)
        observeProperty(property, MPV_FORMAT_NODE);
}

MpvItem::~MpvItem() {
    if (m_thumbThread) {
        m_thumbThread->quit();
        // A corrupt file can block FFmpeg for a long time; do not hang the
        // shutdown indefinitely.
        if (!m_thumbThread->wait(2000)) {
            m_thumbThread->terminate();
            m_thumbThread->wait(1000);
        }
    }
    // Normally deleted through QThread::finished -> deleteLater; delete it
    // explicitly if the deferred delete did not run (e.g. after terminate()).
    if (m_thumbWorker)
        delete m_thumbWorker.data();
}

void MpvItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    MpvAbstractItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void MpvItem::setVolume(double vol) {
    // While casting, leave libmpv alone entirely
    if (m_isCasting) return;
    setProperty("volume", vol);
}

void MpvItem::setPosition(double pos) {
    if (m_isCasting) return;
    command(QStringList() << "seek" << QString::number(pos) << "absolute");
}

void MpvItem::seekFast(double pos) {
    if (m_isCasting) return;
    command(QStringList() << "seek" << QString::number(pos) << "absolute");
}

void MpvItem::seekExact(double pos) {
    if (m_isCasting) return;
    command(QStringList() << "seek" << QString::number(pos) << "absolute+exact");
}

void MpvItem::setPaused(bool p) {
    if (m_isCasting) return;
    setProperty("pause", p);
}

void MpvItem::stopPlayback() {
    if (m_isCasting) return;

    // Unload the file instead of pausing at the end: mpv drops the last frame
    // and the renderer clears its framebuffer to black on the next paint.
    command(QStringList() << QStringLiteral("stop"));

    // mpv stops reporting time-pos/duration while idle and the property-change
    // handler keeps the last value, so the control bar would still show the
    // finished file. Reset them to the "nothing opened" state instead.
    m_position = 0.0;
    m_duration = 0.0;
    m_paused = true;
    // Keep mpv's own state in sync so MPRIS reports "Paused", not "Playing".
    setProperty("pause", true);
    emit positionChanged();
    emit durationChanged();
    emit pausedChanged();

    // The file end is not followed by a redraw on its own: force one so the
    // last frame is replaced by the cleared framebuffer. The stop command is
    // queued to mpv's thread, so repaint again shortly after in case this first
    // repaint happens before the file is actually unloaded.
    update();
    QTimer::singleShot(100, this, [this]() { update(); });
}

void MpvItem::setMuted(bool m) {
    if (m_isCasting) return;
    setProperty("mute", m);
}

void MpvItem::setExternalAudio(const QString &url) {
    m_externalAudio = url;
}

void MpvItem::setSource(const QString &src, bool live) {
    m_source = src;
    // Frames decoded for the previous file are never requested again, and the
    // QML preview still holds the last one. Drop both so switching files does
    // not keep old thumbnails (and their textures) alive.
    if (QQmlEngine *engine = qmlEngine(this)) {
        if (auto *provider = dynamic_cast<ThumbnailProvider *>(
                engine->imageProvider(QStringLiteral("thumbnail")))) {
            provider->clear();
        }
    }
    if (m_thumbWorker)
        QMetaObject::invokeMethod(m_thumbWorker.data(), "clearCache", Qt::QueuedConnection);

    // Keep the 5-minute rewind cache for network streams only. For a local
    // file "auto" means no demuxer cache (mpv's default): seeking is direct,
    // and forcing the stream cache made RSS grow by hundreds of megabytes.
    const QUrl url(src);
    const bool network = !url.scheme().isEmpty() && !url.isLocalFile();
    if (live) {
        // Live TV/radio: no rewind/DVR, but a forward buffer is still needed
        // to absorb HLS segment and network jitter. HLS needs a larger window;
        // Icecast/Shoutcast radio does not, and a long radio buffer made the
        // ICY title run ~15 s ahead of the audio.
        const bool hls = url.path().endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)
                         || url.path().endsWith(QStringLiteral(".mpd"), Qt::CaseInsensitive);
        setProperty("cache", QStringLiteral("yes"));
        setProperty("cache-secs", hls ? 30.0 : 4.0);
        setProperty("demuxer-max-bytes", hls ? QStringLiteral("64MiB") : QStringLiteral("16MiB"));
        setProperty("demuxer-max-back-bytes", QStringLiteral("0"));

        // Twitch puts a PREROLL stitched ad at the beginning of the live
        // playlist; starting at segment 0 shows "Commercial break in progress"
        // for ~30 s. Start a few segments before the live edge instead. This
        // only affects the HLS demuxer and is ignored by other streams.
        if (url.host().contains(QStringLiteral("twitch"), Qt::CaseInsensitive)
                || url.host().contains(QStringLiteral("ttvnw.net"), Qt::CaseInsensitive)) {
            setProperty("demuxer-lavf-o", QStringLiteral("live_start_index=-3"));
        }
    } else {
        setProperty("cache", network ? QStringLiteral("yes") : QStringLiteral("auto"));
        if (network) {
            setProperty("cache-secs", 300.0);
            setProperty("demuxer-max-bytes", QStringLiteral("256MiB"));
        }
    }

    // yt-dlp returns video and audio as separate URLs. Pass the audio as a
    // file-local loadfile option: both tracks are then opened together and
    // playback starts with sound. Adding it in fileLoaded() instead made the
    // first seconds of audio play before the external track was attached.
    QStringList loadCommand{QStringLiteral("loadfile"), src};
    if (!m_externalAudio.isEmpty()) {
        // loadfile() option values are comma-separated, so quote the URL:
        // googlevideo URLs contain commas (e.g. in sparams) and would
        // otherwise be cut into several unknown options.
        QString audio = m_externalAudio;
        audio.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        audio.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        loadCommand << QStringLiteral("replace") << QStringLiteral("-1")
                    << QStringLiteral("audio-file=\"%1\"").arg(audio);
    }
    m_externalAudio.clear();

    emit sourceChanged();
    // Start loading after QML closes the menu and repaints the frame:
    // mpv briefly stops video output when switching files.
    QTimer::singleShot(0, this, [this, loadCommand]() { command(loadCommand); });
}

void MpvItem::setCropEnabled(bool enabled) {
    if (m_cropEnabled != enabled) {
        m_cropEnabled = enabled;
        emit cropEnabledChanged();
    }
}

void MpvItem::setCastingState(bool casting) {
    if (m_isCasting == casting) return;
    m_isCasting = casting;

    if (m_isCasting) {
        // HARD STOP: disable video rendering and audio so the TV is not disturbed
        setProperty("vid", "no");
        setProperty("aid", "no");
        setProperty("mute", true);
        setProperty("volume", 0.0);
    } else {
        // Put everything back
        setProperty("vid", "auto");
        setProperty("aid", "auto");
        setProperty("mute", false);
        setProperty("volume", m_volume);
    }

    emit isCastingChanged();
}

void MpvItem::requestThumbnail(double seekTime, const QString &requestId) {
    if (!m_thumbWorker) return;

    // While a frame is being decoded, keep only the latest request:
    // intermediate positions are not needed while dragging the slider.
    m_thumbPending = true;
    m_thumbPendingTime = seekTime;
    m_thumbPendingId = requestId;
    dispatchThumbnail();
}

void MpvItem::dispatchThumbnail() {
    if (m_thumbBusy || !m_thumbPending || !m_thumbWorker) return;

    m_thumbBusy = true;
    m_thumbPending = false;
    QMetaObject::invokeMethod(m_thumbWorker.data(), "decode", Qt::QueuedConnection,
                              Q_ARG(QString, m_source),
                              Q_ARG(double, m_thumbPendingTime),
                              Q_ARG(QString, m_thumbPendingId));
}

void MpvItem::onThumbnailDecoded(const QString &requestId, double time, const QImage &image) {
    m_thumbBusy = false;

    if (!image.isNull()) {
        QQmlEngine *engine = qmlEngine(this);
        if (engine) {
            auto *provider = static_cast<ThumbnailProvider*>(engine->imageProvider("thumbnail"));
            if (provider) {
                const QString uniqueId = requestId + "_" + QString::number(time);
                provider->insertImage(uniqueId, image);
                emit thumbnailReady(uniqueId);
            }
        }
    }

    dispatchThumbnail();
}

QString MpvItem::getAudioOutput() {
    QVariant currentAo = getProperty("current-ao");

    if (currentAo.isValid() && !currentAo.toString().isEmpty()) {
        return currentAo.toString();
    }

    return "N/A";
}

QString MpvItem::getAudioDevice() {
    QVariant device = getProperty("audio-device");
    if (device.isValid() && !device.toString().isEmpty()) {
        return device.toString();
    }
    return "N/A";
}

void MpvItem::setLogLevel(const QString &mode) {
    if (!mpvController()) return;
    QMetaObject::invokeMethod(mpvController(), "setLogLevel", Qt::QueuedConnection,
                              Q_ARG(QString, mode));
}

void MpvItem::onMpvPropertyChanged(const QString &property, const QVariant &value) {
    m_observedValues.insert(property, value);

    if (property == "time-pos") {
        if (m_positionTimer.elapsed() > 200) {
            m_position = value.toDouble();
            emit positionChanged();
            m_positionTimer.restart();
        } else {
            m_position = value.toDouble();
        }
    }
    else if (property == "duration") { m_duration = value.toDouble(); emit durationChanged(); }
    else if (property == "pause") {
        if (m_isCasting) return; // Ignore local pause changes while casting
        m_paused = value.toBool();
        emit pausedChanged();
    }
    else if (property == "volume") {
        if (m_isCasting) return; // Ignore local volume changes while casting
        m_volume = value.toDouble();
        emit volumeChanged();
    }
    else if (property == "mute") {
        if (m_isCasting) return;
        m_muted = value.toBool();
        emit mutedChanged();
    }
    else if (property == "vid") {
        bool disabled = false;
        if (value.typeId() == QMetaType::QString) disabled = (value.toString() == "no");
        else if (value.canConvert<int>()) disabled = (value.toInt() == 0);
        else if (value.canConvert<QVariantMap>()) disabled = value.toMap().isEmpty();
        m_videoDisabled = disabled;
        emit videoDisabledChanged();
        update();
    }
    else if (property == "media-title") {
        // For radio mpv updates this title when the track changes (ICY).
        const QString title = value.toString();
        // Do not replace a real ICY title with the stream URL that mpv falls
        // back to when the metadata map arrives first.
        if (title.startsWith(QStringLiteral("http://"))
                || title.startsWith(QStringLiteral("https://"))) {
            if (!m_mediaTitle.isEmpty()
                    && !m_mediaTitle.startsWith(QStringLiteral("http://"))
                    && !m_mediaTitle.startsWith(QStringLiteral("https://")))
                return;
        }
        if (m_mediaTitle != title) {
            m_mediaTitle = title;
            emit mediaTitleChanged();
        }
    }
    else if (property == "deinterlace") {
        bool enabled = (value.toString() != "no");
        if (m_deinterlaceEnabled != enabled) {
            m_deinterlaceEnabled = enabled;
            emit deinterlaceChanged();
        }
    }
}
