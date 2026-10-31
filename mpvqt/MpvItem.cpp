#include "MpvItem.hpp"
#include <QDebug>
#include <QVariantMap>
#include <QRectF>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include "mpvcontroller.h"
#include "core/ThumbnailProvider.hpp"
#include "mpvqt/FrameDecoder.hpp"

MpvItem::MpvItem(QQuickItem *parent) : MpvAbstractItem(parent) {
    m_frameDecoder = new FrameDecoder(this);

    m_positionTimer.start();

    connect(mpvController(), &MpvController::propertyChanged, this, &MpvItem::onMpvPropertyChanged);

    connect(mpvController(), &MpvController::fileLoaded, this, [this]() {
        m_source = getProperty("path").toString();
        m_duration = getProperty("duration").toDouble();
        m_volume = getProperty("volume").toDouble();
        m_muted = getProperty("mute").toBool();
        m_paused = getProperty("pause").toBool();
        m_deinterlaceEnabled = (getProperty("deinterlace").toString() != "no");

        emit sourceChanged();
        emit durationChanged();
        emit volumeChanged();
        emit mutedChanged();
        emit pausedChanged();
        emit deinterlaceChanged();
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
}

void MpvItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    MpvAbstractItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void MpvItem::setVolume(double vol) {
    // Если идет трансляция, вообще не трогаем libmpv
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
    command(QStringList() << "loadfile" << src);
    emit sourceChanged();
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
        // ЖЕСТКАЯ ОСТАНОВКА: отключаем рендеринг видео и звук, чтобы не мешать ТВ
        setProperty("vid", "no");
        setProperty("aid", "no");
        setProperty("mute", true);
        setProperty("volume", 0.0);
    } else {
        // Возвращаем на место
        setProperty("vid", "auto");
        setProperty("aid", "auto");
        setProperty("mute", false);
        setProperty("volume", m_volume);
    }

    emit isCastingChanged();
}

void MpvItem::requestThumbnail(double seekTime, const QString &requestId) {
    if (!m_frameDecoder) return;

    m_frameDecoder->cancelCurrentDecode();

    QImage img = m_frameDecoder->decodeFrame(m_source, seekTime);

    if (!img.isNull()) {
        QQmlEngine *engine = qmlEngine(this);
        if (engine) {
            auto *provider = static_cast<ThumbnailProvider*>(engine->imageProvider("thumbnail"));
            if (provider) {
                QString uniqueId = requestId + "_" + QString::number(seekTime);
                provider->insertImage(uniqueId, img);
                emit thumbnailReady(uniqueId);
            }
        }
    }
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
        if (m_isCasting) return; // Игнорируем локальные изменения паузы при касте
        m_paused = value.toBool();
        emit pausedChanged();
    }
    else if (property == "volume") {
        if (m_isCasting) return; // Игнорируем локальные изменения громкости при касте
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
    else if (property == "deinterlace") {
        bool enabled = (value.toString() != "no");
        if (m_deinterlaceEnabled != enabled) {
            m_deinterlaceEnabled = enabled;
            emit deinterlaceChanged();
        }
    }
}
