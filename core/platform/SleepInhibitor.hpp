#pragma once

#include <QObject>
#include <QString>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;

// Keeps the session from idling or suspending while media is playing.
//
// The XDG Desktop Portal Inhibit interface is used instead of a compositor
// specific protocol: it needs no window handle, works on KDE and GNOME, and
// fits the Wayland-only design (mpv itself cannot inhibit the screensaver when
// it renders through vo=libmpv). The inhibition is released as soon as playback
// is paused or stopped; on compositors without the portal the calls fail
// quietly and playback is unaffected.
class SleepInhibitor : public QObject
{
    Q_OBJECT

public:
    explicit SleepInhibitor(QObject *parent = nullptr);
    ~SleepInhibitor() override;

    // Inhibits while `active` is true and releases the inhibition otherwise.
    // Repeating the current value does nothing.
    void setActive(bool active);

private:
    enum class Availability {
        Unknown,
        Available,
        Unavailable,
    };

    void requestInhibit();
    void releaseInhibit();
    void closeHandle(bool blocking);
    void handleInhibitReply(QDBusPendingCallWatcher *watcher);
    void onPortalRegistered();
    void onPortalUnregistered();

    bool m_active = false;
    bool m_pending = false;
    Availability m_availability = Availability::Unknown;
    QString m_handle;
    // Incremented when the portal goes away, so a late reply from the old
    // instance cannot install a handle that no longer exists.
    quint64 m_generation = 0;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
};
