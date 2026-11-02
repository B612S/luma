#pragma once

#include <QObject>
#include <QString>

// Keeps the picture-in-picture window above the other windows on KDE Plasma.
//
// Wayland has no client-side request for "keep above" and KWin ignores
// Qt::WindowStaysOnTopHint, so a small KWin script is loaded over KWin's
// scripting D-Bus interface. The script watches Luma's windows and keeps the
// window whose title is "Luma PiP" above the others (and only that window, so
// leaving PiP restores the normal stacking). Nothing is written to the user's
// KWin configuration; the script is unloaded when the application quits.
//
// On other compositors, or when KWin's scripting interface is unavailable, all
// of this is a silent no-op.
class KWinBridge : public QObject
{
    Q_OBJECT

public:
    explicit KWinBridge(QObject *parent = nullptr);
    ~KWinBridge() override;

    // Loads the watcher when PiP starts; unloads it when PiP ends. The actual
    // keep-above state follows the window title, so a delayed window event
    // cannot leave the window stuck above the others.
    Q_INVOKABLE void setPipActive(bool active);

private:
    // True when KWin's scripting interface is reachable.
    bool available() const;
    bool loadWatcher();
    void unloadWatcher();

    // Plugin name of the loaded script (empty when no script is loaded).
    QString m_watcherName;
    // KWin refuses to reload a script under a name that is still registered, so
    // every load gets its own name.
    int m_loadCount = 0;
};
