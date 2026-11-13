#include "platform/SleepInhibitor.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QLoggingCategory>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lumaSleep, "luma.sleep")

namespace {

constexpr auto kService = "org.freedesktop.portal.Desktop";
constexpr auto kPath = "/org/freedesktop/portal/desktop";
constexpr auto kInhibitInterface = "org.freedesktop.portal.Inhibit";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

// Inhibit flags from the portal specification. Logout and user switch are
// deliberately excluded: a video player must not prevent locking the session.
constexpr uint kInhibitSuspend = 4;
constexpr uint kInhibitIdle = 8;

// Only used to close the handle while the event loop is already shutting down.
// A portal that does not answer within this time is left to release the
// inhibition when Luma's D-Bus connection goes away.
constexpr int kShutdownCloseTimeoutMs = 250;

QString inhibitReason()
{
    return QCoreApplication::translate("SleepInhibitor", "Playing media");
}

} // namespace

SleepInhibitor::SleepInhibitor(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_availability = Availability::Unavailable;
        qCDebug(lumaSleep) << "D-Bus is unavailable; sleep inhibition is off";
        return;
    }

    m_serviceWatcher = new QDBusServiceWatcher(
        QString::fromLatin1(kService), bus,
        QDBusServiceWatcher::WatchForRegistration
            | QDBusServiceWatcher::WatchForUnregistration,
        this);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &SleepInhibitor::onPortalRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &SleepInhibitor::onPortalUnregistered);

    // Close before the session bus is torn down. The portal would release the
    // inhibition on disconnect anyway, but waiting for that can keep the screen
    // awake for a moment after the window is gone.
    if (qApp) {
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            m_active = false;
            closeHandle(true);
        });
    }
}

SleepInhibitor::~SleepInhibitor()
{
    closeHandle(true);
}

void SleepInhibitor::setActive(bool active)
{
    if (m_active == active)
        return;

    m_active = active;
    if (active)
        requestInhibit();
    else
        releaseInhibit();
}

void SleepInhibitor::requestInhibit()
{
    if (!m_active || m_pending || !m_handle.isEmpty()
            || m_availability == Availability::Unavailable) {
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_availability = Availability::Unavailable;
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("reason"), inhibitReason());

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInhibitInterface), QStringLiteral("Inhibit"));
    // An empty window identifier is explicitly allowed by the portal; the
    // inhibition is not tied to a dialog, so no xdg-foreign handle is needed.
    message << QString()
            << static_cast<uint>(kInhibitSuspend | kInhibitIdle)
            << options;

    m_pending = true;
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        if (generation != m_generation) {
            // The portal was replaced while this call was in flight.
            finished->deleteLater();
            return;
        }
        handleInhibitReply(finished);
    });
}

void SleepInhibitor::releaseInhibit()
{
    // A pending Inhibit is closed by its reply handler: m_active is already
    // false, so the new handle is not kept.
    if (m_pending)
        return;
    closeHandle(false);
}

void SleepInhibitor::closeHandle(bool blocking)
{
    if (m_handle.isEmpty())
        return;

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), m_handle,
        QString::fromLatin1(kRequestInterface), QStringLiteral("Close"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (blocking)
        bus.call(message, QDBus::Block, kShutdownCloseTimeoutMs);
    else
        bus.send(message);

    m_handle.clear();
    qCDebug(lumaSleep) << "inhibition released";
}

void SleepInhibitor::handleInhibitReply(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    m_pending = false;

    if (watcher->isError()) {
        const QDBusError error = watcher->error();
        const QDBusError::ErrorType type = error.type();

        // A missing service or object can be transient (the portal is starting
        // or was just replaced), so keep the availability unknown and let the
        // next play/pause or the service watcher try again.
        if (type == QDBusError::ServiceUnknown || type == QDBusError::UnknownObject) {
            qCDebug(lumaSleep) << "desktop portal is not ready:" << error.name();
            return;
        }

        // These will not fix themselves without a new portal implementation:
        // remember the failure instead of retrying on every play/pause.
        const bool unsupported = type == QDBusError::UnknownMethod
            || type == QDBusError::UnknownInterface
            || type == QDBusError::NotSupported
            || type == QDBusError::AccessDenied;

        if (unsupported) {
            m_availability = Availability::Unavailable;
            qCInfo(lumaSleep) << "portal does not support inhibition:" << error.name()
                              << error.message();
        } else {
            qCWarning(lumaSleep) << "cannot inhibit sleep:" << error.name()
                                 << error.message();
        }
        return;
    }

    QString handle;
    const QList<QVariant> arguments = watcher->reply().arguments();
    if (!arguments.isEmpty()) {
        const QVariant value = arguments.constFirst();
        if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
            handle = value.value<QDBusObjectPath>().path();
        else
            handle = value.toString();
    }

    if (handle.isEmpty()) {
        m_availability = Availability::Unavailable;
        qCWarning(lumaSleep) << "the portal returned no inhibit handle";
        return;
    }

    m_availability = Availability::Available;
    m_handle = handle;
    if (!m_active) {
        // Playback was paused before the portal answered.
        closeHandle(false);
        return;
    }

    qCDebug(lumaSleep) << "inhibiting idle and suspend";
}

void SleepInhibitor::onPortalRegistered()
{
    m_availability = Availability::Unknown;
    if (m_active)
        requestInhibit();
}

void SleepInhibitor::onPortalUnregistered()
{
    // The Request object was owned by the old portal instance and is gone.
    ++m_generation;
    m_pending = false;
    m_handle.clear();
    m_availability = Availability::Unknown;
    qCDebug(lumaSleep) << "desktop portal disappeared";
}
