#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include "mpvqt/MpvItem.hpp"

class OsdManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
public:
    explicit OsdManager(MpvItem *mpv = nullptr, QObject *parent = nullptr);

    void setMpv(MpvItem *mpv) { m_mpv = mpv; }
    // While casting, local mpv is paused; seek OSD must follow the target
    // supplied by the cast controller instead of local mpv's stale position.
    void setCastMode(bool enabled) {
        if (m_castMode != enabled) {
            m_seekTarget = -1.0;
            m_seekTargetDuration = -1.0;
            m_seekStartPos = -1.0;
        }
        m_castMode = enabled;
        if (!enabled) {
            m_castPosition = -1.0;
            m_castDuration = -1.0;
        }
    }
    // Remote receiver position/duration; used when no seek target is pending.
    void setCastPosition(double pos, double dur, bool playing) {
        if (pos < 0.0 || qAbs(pos - m_castPosition) > 0.25 || playing != m_castPlaying)
            m_castPositionTimer.restart();
        m_castPosition = pos;
        m_castDuration = dur;
        m_castPlaying = playing;
    }

    QString message() const { return m_message; }
    bool visible() const { return m_visible; }
    bool isPinned() const { return m_isPinned; }

    Q_INVOKABLE void requestTimeOsd();
    Q_INVOKABLE void requestTimeOsd(double pos, double dur);
    Q_INVOKABLE void requestSeekOsd(double pos, double dur);
    Q_INVOKABLE void togglePinTimeOsd();
    Q_INVOKABLE void endTemporaryOsd();
    Q_INVOKABLE void hideOsd();

    // A new source was opened: drop seek/volume overlays from the previous
    // file, but keep an info toast and a pinned time counter alive.
    void onFileChanged();

    Q_INVOKABLE void showVolume();
    Q_INVOKABLE void showVolume(double vol);
    void showVolume(double vol, bool muted);
    Q_INVOKABLE void showInfo(const QString &text);

signals:
    void messageChanged();
    void visibleChanged();

private:
    void updateTimeInternal(double pos, double dur);
    void hideOsdInternal();
    void tickOsd();
    void restoreTimeMode(); // Return to time mode after a temporary OSD

    MpvItem *m_mpv;
    QTimer *m_hideTimer;
    QTimer *m_updateTimer;
    QElapsedTimer m_seekOsdShown;

    QString m_message;
    bool m_visible = false;
    bool m_isPinned = false; // T pressed
    bool m_castMode = false;
    double m_castPosition = -1.0;
    double m_castDuration = -1.0;
    bool m_castPlaying = false;
    QElapsedTimer m_castPositionTimer;
    double castPositionNow() const;

    enum OsdState {
        StateHidden,          // OSD fully hidden
        StateTimePinned,      // Time shown permanently (T)
        StateTimeTemporary,   // Time shown temporarily (seeking)
        StateOverlayVolume,   // Temporary volume overlay
        StateOverlayInfo      // Temporary info overlay
    };
    OsdState m_state = StateHidden;

    // For seeking
    double m_seekTarget = -1.0;
    double m_seekTargetDuration = -1.0;
    double m_seekStartPos = -1.0;
};
