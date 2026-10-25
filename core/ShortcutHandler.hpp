#pragma once
#include <QObject>
#include <QTimer>
#include <QQuickWindow>
#include <QCursor>
#include <QPointer>
#include "mpvqt/MpvItem.hpp"
#include "OsdManager.hpp"
#include "MediaManager.hpp"
#include "ShortcutAction.hpp"

class ShortcutHandler : public QObject {
    Q_OBJECT
public:
    explicit ShortcutHandler(MpvItem *mpv, OsdManager *osd, MediaManager *media,
                             QQuickWindow *window, QObject *parent = nullptr);
    bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
    void toggleCrop();
    void setSliderSeeking(bool seeking);

signals:
    void togglePlaylist();
    void toggleStats();
    void toggleCsd();

private:
    void executeAction(ShortcutAction action, bool autoRepeat);
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

    MpvItem *m_mpv;
    OsdManager *m_osd;
    MediaManager *m_media;
    QQuickWindow *m_window;

    QTimer *m_seekTimer;
    QTimer *m_volTimer;
    QTimer *m_volResetTimer;

    double m_seekStep = 0.0;
    double m_targetSeekTime = 0.0;
    double m_volStep = 0.0;

    bool m_isInternalSeeking = false;
    bool m_isSliderSeeking = false;
    double m_lastPosition = 0.0;
    bool m_isInternalVolume = true;
    bool m_ignoreEndFile = false;

    int m_aspectIndex = 0;

    // --- Auto Crop state ---
    bool m_cropEnabled = false;
    bool m_cropPending = false;

    QPointer<QQuickItem> m_playlistItem;
    QPointer<QQuickItem> m_statsItem;

    double m_wheelVolumeDelta = 0.0;
    QTimer *m_wheelDebounceTimer;
};
