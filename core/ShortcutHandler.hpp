#pragma once
#include <QObject>
#include <QMap>
#include <QTimer>
#include <QQuickWindow>
#include <QCursor>
#include <QPointer>
#include <QVariantList>
#include "mpvqt/MpvItem.hpp"
#include "OsdManager.hpp"
#include "MediaManager.hpp"
#include "ShortcutAction.hpp"

class CastManager;
class GlobalShortcuts;
class PluginManager;

class ShortcutHandler : public QObject {
    Q_OBJECT
public:
    explicit ShortcutHandler(MpvItem *mpv = nullptr,
                             OsdManager *osd = nullptr,
                             MediaManager *media = nullptr,
                             QQuickWindow *window = nullptr,
                             QObject *parent = nullptr,
                             bool globalShortcutsAllowed = true);
    ~ShortcutHandler() override;

    void setWindow(QQuickWindow *window);
    void setMpv(MpvItem *mpv);
    void setOsd(OsdManager *osd) { m_osd = osd; }
    void setMedia(MediaManager *media) { m_media = media; }
    void setCastManager(CastManager *cast) { m_castManager = cast; }
    void setPluginManager(PluginManager *plugins) { m_pluginManager = plugins; }
    // Called from QML while the user drags the seek slider.
    Q_INVOKABLE void setSliderSeeking(bool seeking);

    bool eventFilter(QObject *obj, QEvent *event) override;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    // Called from QML when dialogs open: disables hotkeys
    // and wheel scrolling as volume control.
    Q_INVOKABLE void setShortcutsEnabled(bool enabled) { m_enabled = enabled; }
    // The help dialog sets this so its wheel events scroll the list instead of
    // changing the volume.
    Q_INVOKABLE void setHelpVisible(bool visible) { m_helpVisible = visible; }

    // Configurable bindings. Each action can have one local and one global
    // shortcut; the help window itself stays on "?" and is not in this list.
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(bool globalShortcutsAvailable READ globalShortcutsAvailable NOTIFY bindingsChanged)
    Q_PROPERTY(bool globalShortcutsAllowed READ globalShortcutsAllowed NOTIFY bindingsChanged)
    Q_PROPERTY(QString shortcutsPath READ shortcutsPath CONSTANT)
    Q_INVOKABLE void setShortcut(const QString &id, int keycode, int modifiers,
                                 const QString &text, bool global);
    Q_INVOKABLE void clearShortcut(const QString &id, bool global);
    Q_INVOKABLE QString keyText(int keycode, int key, int modifiers) const;
    // Global bindings are disabled in multi-instance mode.
    void setGlobalShortcutsAllowed(bool allowed);

public slots:
    void toggleCrop();
    void executeActionById(const QString &id);

signals:
    void togglePlaylist();
    void toggleStats();
    void statsSelected();
    void togglePip();
    void fitWindowToVideo();
    void toggleEqualizerRequested();
    void shortcutsHelpVisibleChanged(bool visible);
    void bindingsChanged();

private:
    void executeAction(ShortcutAction action, bool autoRepeat);
    ShortcutAction matchAction(QKeyEvent *event) const;
    void loadShortcuts();
    void saveShortcuts();
    void updateGlobalShortcuts();
    QVariantList bindings() const;
    bool globalShortcutsAvailable() const;
    bool globalShortcutsAllowed() const { return m_globalShortcutsAllowed; }
    QString shortcutsPath() const;
    void startSeek(double step);
    void startVolume(double step);
    void updateSeekOsd();
    void updateVolOsd();
    void cycleAspectRatio();
    void cycleSubtitles();
    void cycleAudio();
    bool isCursorOverQmlPanel();
    void onFileChanged();
    void onPositionChanged();
    void onVolumeChanged();
    bool m_enabled = true;
    bool m_helpVisible = false;

    MpvItem *m_mpv = nullptr;
    OsdManager *m_osd = nullptr;
    MediaManager *m_media = nullptr;
    CastManager *m_castManager = nullptr;
    PluginManager *m_pluginManager = nullptr;
    QPointer<QQuickWindow> m_window;

    QTimer *m_seekTimer = nullptr;
    QTimer *m_volTimer = nullptr;
    QTimer *m_volResetTimer = nullptr;
    QTimer *m_wheelTargetTimer = nullptr;

    double m_seekStep = 0.0;
    double m_targetSeekTime = 0.0;
    double m_volStep = 0.0;

    bool m_isInternalSeeking = false;
    bool m_isSliderSeeking = false;
    double m_lastPosition = 0.0;
    bool m_isInternalVolume = true;
    bool m_ignoreEndFile = false;

    int m_aspectIndex = 0;

    bool m_cropEnabled = false;
    bool m_cropPending = false;

    QPointer<QQuickItem> m_playlistItem;
    QPointer<QQuickItem> m_statsItem;

    QMap<ShortcutAction, ShortcutBinding> m_localBindings;
    QMap<ShortcutAction, ShortcutBinding> m_globalBindings;
    GlobalShortcuts *m_globalShortcuts = nullptr;
    QString m_shortcutsPath;

    // Wheel volume changes are applied immediately. The target is kept for a
    // short burst so fast wheel/touchpad events accumulate instead of being
    // lost while mpv has not reported the previous volume yet.
    double m_wheelVolumeTarget = 0.0;
    bool m_wheelTargetValid = false;
    bool m_globalShortcutsAllowed = true;
};
