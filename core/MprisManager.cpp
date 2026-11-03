#include "MprisManager.hpp"
#include "MediaManager.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QUrl>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTimer>
#include <QDBusMetaType>
#include <QDebug>

MprisManager::MprisManager(MpvItem *mpv, MediaManager *media, QObject *parent)
: QObject(parent), m_mpv(mpv), m_media(media)
{
    qDBusRegisterMetaType<QDBusObjectPath>();
    qDBusRegisterMetaType<QVariantMap>();

    m_posTimer = new QTimer(this);
    m_posTimer->setInterval(1000);
    connect(m_posTimer, &QTimer::timeout, this, [this]() {
        if (m_mpv && !m_mpv->source().isEmpty() && !m_mpv->paused()) {
            PropertiesChanged();
        }
    });

    if (m_mpv) {
        connectMpvSignals();
        registerOnBus();
        m_posTimer->start();
        PropertiesChanged();
        sendCanProperties();
    }
}

MprisManager::~MprisManager() {
    if (m_busRegistered) {
        QDBusConnection::sessionBus().unregisterService("org.mpris.MediaPlayer2.luma");
        QDBusConnection::sessionBus().unregisterObject("/org/mpris/MediaPlayer2");
    }
}

void MprisManager::connectMpvSignals() {
    connect(m_mpv, &MpvItem::sourceChanged, this, [this]() {
        PropertiesChanged();
        sendCanProperties();
    });
    connect(m_mpv, &MpvItem::pausedChanged, this, &MprisManager::PropertiesChanged);
    connect(m_mpv, &MpvItem::volumeChanged, this, &MprisManager::PropertiesChanged);
}

void MprisManager::registerOnBus() {
    if (m_busRegistered) return;

    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.registerObject("/org/mpris/MediaPlayer2", this,
        QDBusConnection::ExportAllSlots
        | QDBusConnection::ExportAllProperties
        | QDBusConnection::ExportAllSignals)) {
        qWarning() << "MPRIS: failed to register object";
    return;
        }

        if (!bus.registerService("org.mpris.MediaPlayer2.luma")) {
            qWarning() << "MPRIS: failed to register service (already taken?)";
            bus.unregisterObject("/org/mpris/MediaPlayer2");
            return;
        }

        m_busRegistered = true;
}

QString MprisManager::getPlaybackStatus() const {
    if (!m_mpv) return "Stopped";
    if (m_mpv->source().isEmpty()) return "Stopped";
    return m_mpv->paused() ? "Paused" : "Playing";
}

qlonglong MprisManager::getPosition() const {
    if (!m_mpv) return 0;
    double pos = m_mpv->position();
    if (pos < 0) return 0;
    return static_cast<qlonglong>(pos * 1000000);
}

bool MprisManager::getCanSeek() const {
    return m_mpv && (!m_media || !m_media->liveStream());
}

double MprisManager::getVolume() const {
    if (!m_mpv) return 0.0;
    return m_mpv->volume() / 100.0;
}

QVariantMap MprisManager::getMetadata() const {
    QVariantMap map;
    if (!m_mpv) return map;
    QString file = m_mpv->source();
    if (file.isEmpty()) return map;

    QFileInfo fi(file);
    map["mpris:trackid"] = QVariant::fromValue(
        QDBusObjectPath("/org/mpris/MediaPlayer2/Track/" + QString::number(qHash(file))));
    map["xesam:title"] = fi.fileName();
    map["xesam:url"] = QUrl::fromLocalFile(file).toString();

    double dur = m_mpv->duration();
    if (dur > 0) {
        map["mpris:length"] = static_cast<qlonglong>(dur * 1000000);
    }

    return map;
}

// --- Root Interface ---
void MprisManager::Raise() {}
void MprisManager::Quit() { QCoreApplication::quit(); }

// --- Player Interface ---
void MprisManager::Next() { if (m_media) m_media->playNext(); }
void MprisManager::Previous() { if (m_media) m_media->playPrev(); }
void MprisManager::Pause() { if (m_mpv) m_mpv->setPaused(true); }
void MprisManager::PlayPause() { if (m_mpv) m_mpv->setPaused(!m_mpv->paused()); }
void MprisManager::Play() { if (m_mpv) m_mpv->setPaused(false); }

void MprisManager::Stop() {
    if (!m_mpv || !m_media) return;
    m_media->setManualSwitch(true);
    m_mpv->command(QStringList() << "stop");
    // Capturing this would be unsafe if the manager were ever destroyed before
    // the timer fires; the media manager owns the timer context.
    QTimer::singleShot(1000, m_media, [media = m_media]() {
        if (media) media->setManualSwitch(false);
    });
}

void MprisManager::Seek(qlonglong offset) {
    if (!m_mpv || (m_media && m_media->liveStream())) return;
    double newPos = m_mpv->position() + (offset / 1000000.0);
    m_mpv->setPosition(std::max(0.0, newPos));
}

void MprisManager::SetPosition(const QDBusObjectPath &, qlonglong position) {
    if (!m_mpv || (m_media && m_media->liveStream())) return;
    m_mpv->setPosition(position / 1000000.0);
}

void MprisManager::setVolume(double volume) {
    if (m_mpv) m_mpv->setVolume(volume * 100.0);
}

void MprisManager::OpenUri(const QString &uri) {
    if (m_media) m_media->openFile(uri);
}

void MprisManager::PropertiesChanged() {
    if (!m_busRegistered) return;

    QDBusMessage signal = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged");

    QVariantMap changedProps;
    changedProps["PlaybackStatus"] = getPlaybackStatus();
    changedProps["Metadata"] = getMetadata();
    changedProps["Volume"] = getVolume();
    changedProps["Position"] = getPosition();

    signal << "org.mpris.MediaPlayer2.Player" << changedProps << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

void MprisManager::sendCanProperties() {
    if (!m_busRegistered) return;

    QDBusMessage signal = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged");

    QVariantMap changedProps;
    changedProps["CanGoNext"] = getCanGoNext();
    changedProps["CanGoPrevious"] = getCanGoPrevious();
    changedProps["CanPlay"] = getCanPlay();
    changedProps["CanPause"] = getCanPause();
    changedProps["CanSeek"] = getCanSeek();
    changedProps["CanControl"] = getCanControl();

    signal << "org.mpris.MediaPlayer2.Player" << changedProps << QStringList();
    QDBusConnection::sessionBus().send(signal);
}
