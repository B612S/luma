#pragma once
#include <QObject>
#include <QTimer>
#include "mpvqt/MpvItem.hpp"

class OsdManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
public:
    explicit OsdManager(MpvItem *mpv = nullptr, QObject *parent = nullptr);

    void setMpv(MpvItem *mpv) { m_mpv = mpv; }

    QString message() const { return m_message; }
    bool visible() const { return m_visible; }
    bool isPinned() const { return m_isPinned; }

    Q_INVOKABLE void requestTimeOsd();
    Q_INVOKABLE void requestTimeOsd(double pos, double dur);
    Q_INVOKABLE void requestSeekOsd(double pos, double dur);
    Q_INVOKABLE void togglePinTimeOsd();
    Q_INVOKABLE void endTemporaryOsd();
    Q_INVOKABLE void hideOsd();

    Q_INVOKABLE void showVolume();
    Q_INVOKABLE void showVolume(double vol);
    Q_INVOKABLE void showInfo(const QString &text);

signals:
    void messageChanged();
    void visibleChanged();

private:
    void updateTimeInternal(double pos, double dur);
    void hideOsdInternal();
    void tickOsd();
    void restoreTimeMode(); // Возврат к режиму времени после временного OSD

    MpvItem *m_mpv;
    QTimer *m_hideTimer;
    QTimer *m_updateTimer;

    QString m_message;
    bool m_visible = false;
    bool m_isPinned = false; // T нажата

    enum OsdState {
        StateHidden,          // OSD полностью скрыт
        StateTimePinned,      // Время показывается постоянно (T)
        StateTimeTemporary,   // Время показывается временно (перемотка)
        StateOverlayVolume,   // Временное перекрытие громкости
        StateOverlayInfo      // Временное перекрытие инфо
    };
    OsdState m_state = StateHidden;

    // Для перемотки
    double m_seekTarget = -1.0;
    double m_seekTargetDuration = -1.0;
    double m_seekStartPos = -1.0;
};
