#pragma once

#include <QObject>
#include <QString>
#include <QRect>

// Keeps the picture-in-picture window above the other windows on KDE Plasma and
// remembers normal/PiP geometry for the current application run.
//
// Wayland has no client-side request for "keep above" or absolute window
// positioning, so a small KWin script is loaded over KWin's scripting D-Bus
// interface. The script watches Luma's windows, keeps the PiP window above the
// others, and reports its geometry to this object. The geometry is kept in
// memory only: nothing is written to the settings and nothing survives a
// restart. The script and cached source are removed when the application quits.
//
// On other compositors, or when KWin's scripting interface is unavailable, all
// of this is a silent no-op.
class KWinBridge : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.luma.KWin")

public:
    explicit KWinBridge(QObject *parent = nullptr);
    ~KWinBridge() override;

    // Loads the watcher when PiP starts; it stays loaded for the rest of the
    // run so normal-window show/hide also keeps its runtime geometry.
    Q_INVOKABLE void setPipActive(bool active);
    // Applies the PiP geometry remembered during this run.
    Q_INVOKABLE void restorePipGeometry();

public slots:
    // Called by the KWin watcher through D-Bus; kept in memory only.
    void savePipGeometry(int x, int y, int width, int height);

private:
    bool available() const;
    bool loadWatcher();
    void unloadWatcher();
    void registerDbus();
    void cleanupStaleArtifacts();

    QString m_watcherName;
    QString m_watcherPath;
    QString m_dbusService;
    QRect m_runtimePipGeometry;
};
