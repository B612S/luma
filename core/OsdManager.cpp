#include "OsdManager.hpp"

namespace {
constexpr int kTemporaryOsdMs = 2500;
}

OsdManager::OsdManager(MpvItem *mpv, QObject *parent) : QObject(parent), m_mpv(mpv) {
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kTemporaryOsdMs);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        if (m_state == StateTimeTemporary && m_seekOsdShown.isValid()) {
            const qint64 elapsed = m_seekOsdShown.elapsed();
            if (elapsed < kTemporaryOsdMs) {
                m_hideTimer->start(int(kTemporaryOsdMs - elapsed));
                return;
            }
        }
        if (m_state == StateTimeTemporary || m_state == StateOverlayVolume || m_state == StateOverlayInfo) {
            restoreTimeMode();
        }
    });

    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100);
    connect(m_updateTimer, &QTimer::timeout, this, &OsdManager::tickOsd);
}

void OsdManager::restoreTimeMode() {
    if (m_isPinned) {
        m_state = StateTimePinned;
        m_visible = true;
        m_seekTarget = -1.0;
        // Update the time right away to avoid a delay
        updateTimeInternal(m_mpv->position(), m_mpv->duration());
        emit messageChanged();
        emit visibleChanged();
        if (!m_updateTimer->isActive()) m_updateTimer->start();
    } else {
        m_state = StateHidden;
        m_updateTimer->stop();
        hideOsdInternal();
    }
}

void OsdManager::tickOsd() {
    if (!m_visible || !m_mpv) {
        m_updateTimer->stop();
        return;
    }

    // While an overlay (volume/info) is shown, leave the text alone
    if (m_state == StateOverlayVolume || m_state == StateOverlayInfo) {
        return;
    }

    // While seeking, until mpv reaches the target, show the target
    if (m_state == StateTimeTemporary && m_seekTarget >= 0.0) {
        double realPos = m_mpv->position();
        if (realPos >= 0.0 && qAbs(realPos - m_seekStartPos) > 0.05) {
            m_seekTarget = -1.0; // mpv reached the target, switch to the real time
        } else {
            double dur = m_seekTargetDuration > 0.0 ? m_seekTargetDuration : m_mpv->duration();
            if (dur > 0.0) updateTimeInternal(m_seekTarget, dur);
            return;
        }
    }

    // Otherwise (Pinned, TemporaryTime) show the real time
    double pos = m_mpv->position();
    double dur = m_mpv->duration();
    if (pos >= 0.0 && dur > 0.0) {
        updateTimeInternal(pos, dur);
    }
}

void OsdManager::requestTimeOsd() {
    if (!m_mpv) return;
    if (m_isPinned) return; // If T is pressed, ignore the usual appearance

    m_state = StateTimeTemporary;
    m_seekTarget = -1.0;
    updateTimeInternal(m_mpv->position(), m_mpv->duration());
    m_visible = true;
    emit messageChanged();
    emit visibleChanged();
    m_seekOsdShown.start();
    m_hideTimer->start();
    if (!m_updateTimer->isActive()) m_updateTimer->start();
}

void OsdManager::requestTimeOsd(double pos, double dur) {
    if (!m_mpv || m_isPinned) return;

    m_state = StateTimeTemporary;
    updateTimeInternal(pos, dur);
    m_visible = true;
    emit messageChanged();
    emit visibleChanged();
    m_seekOsdShown.start();
    m_hideTimer->start();
    if (!m_updateTimer->isActive()) m_updateTimer->start();
}

void OsdManager::requestSeekOsd(double pos, double dur) {
    if (!m_mpv) return;

    // Seeking always shows the time temporarily
    m_state = StateTimeTemporary;
    m_seekOsdShown.start();
    m_hideTimer->start();

    if (m_seekTarget < 0.0) m_seekStartPos = m_mpv->position();
    m_seekTarget = pos;
    m_seekTargetDuration = dur;
    updateTimeInternal(pos, dur);
    m_visible = true;
    emit messageChanged();
    emit visibleChanged();
    if (!m_updateTimer->isActive()) m_updateTimer->start();
}

void OsdManager::togglePinTimeOsd() {
    if (!m_mpv) return;

    if (m_isPinned) {
        m_isPinned = false;
        m_hideTimer->stop();
        restoreTimeMode(); // Hide or leave as is
    } else {
        m_isPinned = true;
        m_state = StateTimePinned;
        m_seekTarget = -1.0;
        m_hideTimer->stop(); // Disable auto-hide
        updateTimeInternal(m_mpv->position(), m_mpv->duration());
        m_visible = true;
        emit messageChanged();
        emit visibleChanged();
        if (!m_updateTimer->isActive()) m_updateTimer->start();
    }
}

void OsdManager::endTemporaryOsd() {
    // Called when the user releases the seek key
    if (m_state == StateTimeTemporary) {
        // Restart the full interval from the release; the elapsed-time guard
        // above also prevents an older timer from shortening it.
        m_seekOsdShown.start();
        m_hideTimer->start(kTemporaryOsdMs);
    }
}

void OsdManager::hideOsd() {
    m_isPinned = false;
    m_state = StateHidden;
    m_seekTarget = -1.0;
    m_updateTimer->stop();
    m_hideTimer->stop();
    m_seekOsdShown.invalidate();
    hideOsdInternal();
}

void OsdManager::showVolume(double vol) {
    showVolume(vol, m_mpv ? m_mpv->muted() : false);
}

void OsdManager::showVolume(double vol, bool muted) {
    if (!m_mpv) return;

    // Temporary overlay
    m_state = StateOverlayVolume;

    int volInt = qRound(vol);
    m_message = (muted || volInt <= 0) ? tr("osd_muted") : QString::number(volInt) + "%";

    m_visible = true;
    emit messageChanged();
    emit visibleChanged();
    m_hideTimer->start();
}

void OsdManager::showVolume() {
    if (!m_mpv) return;
    showVolume(m_mpv->volume());
}

void OsdManager::showInfo(const QString &text) {
    if (text.isEmpty()) return;

    m_state = StateOverlayInfo;
    m_message = text;
    m_visible = true;
    emit messageChanged();
    emit visibleChanged();
    m_hideTimer->start();
}

void OsdManager::hideOsdInternal() {
    m_visible = false;
    emit visibleChanged();
}

void OsdManager::updateTimeInternal(double pos, double dur) {
    if (pos < 0 || dur <= 0) return;

    int total = static_cast<int>(pos);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;

    QString timeStr = (h > 0)
    ? QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
    : QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));

    int dt = static_cast<int>(dur);
    int dh = dt / 3600;
    int dm = (dt % 3600) / 60;
    int ds = dt % 60;
    QString durStr = (dh > 0)
    ? QString(" / %1:%2:%3").arg(dh, 2, 10, QChar('0')).arg(dm, 2, 10, QChar('0')).arg(ds, 2, 10, QChar('0'))
    : QString(" / %1:%2").arg(dm, 2, 10, QChar('0')).arg(ds, 2, 10, QChar('0'));

    m_message = timeStr + durStr;
    emit messageChanged();
}
