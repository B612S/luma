#include "MprisManager.hpp"
#include "MediaManager.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QUrl>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

MprisManager::MprisManager(MpvItem *mpv, MediaManager *media, QObject *parent)
: QObject(parent), m_mpv(mpv), m_media(media)
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Регистрируем объект с интерфейсом Player
    if (!bus.registerObject("/org/mpris/MediaPlayer2", this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals)) {
        qWarning() << "Failed to register MPRIS object";
    }

    // Регистрируем корневой интерфейс (Root) для управления плеером
    if (!bus.registerService("org.mpris.MediaPlayer2.luma")) {
        qWarning() << "Failed to register MPRIS service";
    }

    connect(m_mpv, &MpvItem::pausedChanged, this, &MprisManager::PropertiesChanged);
    connect(m_mpv, &MpvItem::sourceChanged, this, &MprisManager::PropertiesChanged);
    connect(m_mpv, &MpvItem::volumeChanged, this, &MprisManager::PropertiesChanged);
}

QString MprisManager::getPlaybackStatus() const {
    if (!m_mpv->source().isEmpty() && m_mpv->paused()) return "Paused";
    if (!m_mpv->source().isEmpty() && !m_mpv->paused()) return "Playing";
    return "Stopped";
}

qlonglong MprisManager::getPosition() const {
    double pos = m_mpv->position();
    if (pos < 0) return 0;
    return static_cast<qlonglong>(pos * 1000000);
}

double MprisManager::getVolume() const {
    return m_mpv->volume() / 100.0;
}

QVariantMap MprisManager::getMetadata() const {
    QVariantMap map;
    QString file = m_mpv->source();
    if (file.isEmpty()) return map;

    QFileInfo fi(file);

    // Обязательные поля MPRIS
    map["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/Track/" + QString::number(qHash(file))));
    map["xesam:title"] = fi.fileName();
    map["xesam:url"] = QUrl::fromLocalFile(file).toString();

    double dur = m_mpv->duration();
    if (dur > 0) {
        map["mpris:length"] = static_cast<qlonglong>(dur * 1000000);
    }

    return map;
}

void MprisManager::Raise() {
    // Поднять окно на передний план (требуется интерфейс QWindow)
}

void MprisManager::Quit() {
    QCoreApplication::quit();
}

void MprisManager::Next() {
    if (m_media) m_media->playNext();
}

void MprisManager::Previous() {
    if (m_media) m_media->playPrev();
}

void MprisManager::Pause() {
    m_mpv->setPaused(true);
}

void MprisManager::PlayPause() {
    m_mpv->setPaused(!m_mpv->paused());
}

void MprisManager::Stop() {
    if (!m_mpv || !m_media) return;

    // Блокируем автопереход на следующий трек
    m_media->setManualSwitch(true);

    // Останавливаем mpv (выгружаем файл)
    m_mpv->command(QStringList() << "stop");

    // Сбрасываем флаг через 1 секунду, чтобы пользователь мог снова запускать треки
    QTimer::singleShot(1000, m_media, [this]() {
        if (m_media) m_media->setManualSwitch(false);
    });
}

void MprisManager::Play() {
    m_mpv->setPaused(false);
}

void MprisManager::Seek(qlonglong offset) {
    double newPos = m_mpv->position() + (offset / 1000000.0);
    m_mpv->setPosition(std::max(0.0, newPos));
}

void MprisManager::SetPosition(const QDBusObjectPath &, qlonglong position) {
    m_mpv->setPosition(position / 1000000.0);
}

void MprisManager::setVolume(double volume) {
    m_mpv->setVolume(volume * 100.0);
}

void MprisManager::PropertiesChanged() {
    QDBusMessage signal = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged"
    );
    QVariantMap changedProps;
    changedProps["PlaybackStatus"] = getPlaybackStatus();
    changedProps["Metadata"] = getMetadata();
    changedProps["Volume"] = getVolume();

    signal << "org.mpris.MediaPlayer2.Player" << changedProps << QStringList();
    QDBusConnection::sessionBus().send(signal);
}
