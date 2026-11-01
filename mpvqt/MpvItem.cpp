#include "MpvItem.hpp"
#include <QDebug>
#include <QVariantMap>
#include <QRectF>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include <QTimer>
#include "mpvcontroller.h"
#include "core/ThumbnailProvider.hpp"
#include "mpvqt/FrameDecoder.hpp"

ThumbnailWorker::ThumbnailWorker(QObject *parent) : QObject(parent) {}

ThumbnailWorker::~ThumbnailWorker() {
    delete m_decoder;
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
    connect(m_thumbThread, &QThread::finished, m_thumbWorker, &QObject::deleteLater);
    connect(m_thumbWorker, &ThumbnailWorker::decoded, this, &MpvItem::onThumbnailDecoded);
    m_thumbThread->start();

    m_positionTimer.start();

    connect(mpvController(), &MpvController::propertyChanged, this, &MpvItem::onMpvPropertyChanged);

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
    observeProperty("duration", MPV_FORMAT_DOUBLE);
    observeProperty("pause", MPV_FORMAT_FLAG);
    observeProperty("volume", MPV_FORMAT_DOUBLE);
    observeProperty("mute", MPV_FORMAT_FLAG);
    observeProperty("vid", MPV_FORMAT_NODE);
    observeProperty("deinterlace", MPV_FORMAT_STRING);
    observeProperty("media-title", MPV_FORMAT_STRING);
}

MpvItem::~MpvItem() {
    if (m_thumbThread) {
        m_thumbThread->quit();
        m_thumbThread->wait();
    }
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

void MpvItem::setMuted(bool m) {
    if (m_isCasting) return;
    setProperty("mute", m);
}

void MpvItem::setSource(const QString &src) {
    m_source = src;
    emit sourceChanged();
    // Start loading after QML closes the menu and repaints the frame:
    // mpv briefly stops video output when switching files.
    QTimer::singleShot(0, this, [this, src]() {
        command(QStringList() << "loadfile" << src);
    });
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
    QMetaObject::invokeMethod(m_thumbWorker, "decode", Qt::QueuedConnection,
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

void MpvItem::onMpvPropertyChanged(const QString &property, const QVariant &value) {
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
