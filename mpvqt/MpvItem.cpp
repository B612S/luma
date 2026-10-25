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

    // Запускаем таймер троттлинга
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
        emit fileEnded();
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

void MpvItem::setPosition(double pos) { command(QStringList() << "seek" << QString::number(pos) << "absolute"); }
void MpvItem::seekFast(double pos) { command(QStringList() << "seek" << QString::number(pos) << "absolute"); }
void MpvItem::seekExact(double pos) { command(QStringList() << "seek" << QString::number(pos) << "absolute+exact"); }
void MpvItem::setPaused(bool p) { setProperty("pause", p); }
void MpvItem::setVolume(double vol) { setProperty("volume", vol); }
void MpvItem::setMuted(bool m) { setProperty("mute", m); }

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

void MpvItem::requestThumbnail(double seekTime, const QString &requestId) {
    if (!m_frameDecoder) return;

    // Отменяем предыдущий запрос, если он еще выполняется
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
    QVariant params = getProperty("audio-out-params");
    if (params.canConvert<QVariantMap>()) {
        QVariantMap map = params.toMap();
        if (map.contains("driver")) return map["driver"].toString();
    }
    return "N/A";
}

void MpvItem::onMpvPropertyChanged(const QString &property, const QVariant &value) {
    if (property == "time-pos") {
        // ТРОТТЛИНГ: Обновляем QML не чаще, чем раз в 200 мс
        if (m_positionTimer.elapsed() > 200) {
            m_position = value.toDouble();
            emit positionChanged();
            m_positionTimer.restart();
        } else {
            // Все равно обновляем внутреннюю переменную,
            // чтобы при следующем "тике" отправить актуальное значение
            m_position = value.toDouble();
        }
    }
    else if (property == "duration") { m_duration = value.toDouble(); emit durationChanged(); }
    else if (property == "pause") { m_paused = value.toBool(); emit pausedChanged(); }
    else if (property == "volume") { m_volume = value.toDouble(); emit volumeChanged(); }
    else if (property == "mute") { m_muted = value.toBool(); emit mutedChanged(); }
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
