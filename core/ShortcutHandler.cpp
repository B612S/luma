#include "ShortcutHandler.hpp"
#include "CastManager.hpp"
#include <QKeyEvent>
#include <QWheelEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QWidget>
#include <QQuickItem>
#include <QVariantMap>

ShortcutHandler::ShortcutHandler(MpvItem *mpv, OsdManager *osd, MediaManager *media,
                                 QQuickWindow *window, QObject *parent)
: QObject(parent), m_mpv(mpv), m_osd(osd), m_media(media)
{
    m_seekTimer = new QTimer(this);
    m_seekTimer->setInterval(300);
    connect(m_seekTimer, &QTimer::timeout, this, &ShortcutHandler::updateSeekOsd);

    m_volTimer = new QTimer(this);
    m_volTimer->setInterval(100);
    connect(m_volTimer, &QTimer::timeout, this, &ShortcutHandler::updateVolOsd);

    m_volResetTimer = new QTimer(this);
    m_volResetTimer->setSingleShot(true);
    m_volResetTimer->setInterval(1000);
    connect(m_volResetTimer, &QTimer::timeout, this, [this]() {
        m_isInternalVolume = false;
    });

    m_wheelDebounceTimer = new QTimer(this);
    m_wheelDebounceTimer->setSingleShot(true);
    m_wheelDebounceTimer->setInterval(80);
    connect(m_wheelDebounceTimer, &QTimer::timeout, this, [this]() {
        if (qAbs(m_wheelVolumeDelta) > 0.001) {
            if (m_castManager && m_castManager->casting()) {
                double newVol = qBound(0.0, m_castManager->remoteVolume() + m_wheelVolumeDelta, 100.0);
                m_castManager->setVolume(newVol);
                if (m_osd) m_osd->showVolume(newVol);
            } else if (m_mpv) {
                double currentVol = m_mpv->volume();
                double newVol = qBound(0.0, currentVol + m_wheelVolumeDelta, 100.0);
                m_isInternalVolume = true;
                m_mpv->setVolume(newVol);
                if (m_osd) m_osd->showVolume(newVol);
            }
            m_wheelVolumeDelta = 0.0;
        }
    });

    if (m_mpv) {
        connect(m_mpv, &MpvItem::fileEnded, this, [this](const QString &reason) {
            if (reason != "eof") return;
            if (m_ignoreEndFile) return;
            if (m_castManager && m_castManager->casting()) return;

            QTimer::singleShot(100, this, [this]() {
                if (m_media) m_media->playNext();
            });
        });
        connect(m_mpv, &MpvItem::sourceChanged, this, &ShortcutHandler::onFileChanged);
        connect(m_mpv, &MpvItem::positionChanged, this, &ShortcutHandler::onPositionChanged);
        connect(m_mpv, &MpvItem::volumeChanged, this, &ShortcutHandler::onVolumeChanged);
    }

    QTimer::singleShot(1000, this, [this]() { m_isInternalVolume = false; });

    if (window) setWindow(window);
}

ShortcutHandler::~ShortcutHandler() {
    if (m_window) {
        m_window->removeEventFilter(this);
    }
}

void ShortcutHandler::setWindow(QQuickWindow *window) {
    if (m_window == window) return;
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    if (m_window) m_window->installEventFilter(this);
}

void ShortcutHandler::setSliderSeeking(bool seeking) {
    m_isSliderSeeking = seeking;
}

void ShortcutHandler::onFileChanged() {
    m_seekTimer->stop();
    m_volTimer->stop();
    m_volResetTimer->stop();
    m_wheelDebounceTimer->stop();
    m_targetSeekTime = 0.0;
    m_lastPosition = 0.0;
    m_seekStep = 0.0;
    m_volStep = 0.0;
    m_wheelVolumeDelta = 0.0;

    m_cropEnabled = false;
    m_cropPending = false;
    if (m_mpv) m_mpv->setCropEnabled(false);

    if (m_osd) m_osd->hideOsd();
    m_isInternalVolume = true;
    m_volResetTimer->start(100);
}

void ShortcutHandler::onPositionChanged() {
    if (!m_mpv) return;
    if (m_isSliderSeeking) return;
    if (m_isInternalSeeking) return;
    if (m_castManager && m_castManager->casting()) return; // Ignore local changes while casting

    double currentPos = m_mpv->position();
    if (currentPos < 0) return;

    if (qAbs(currentPos - m_lastPosition) > 2.0) {
        if (m_osd && !m_osd->visible()) {
            m_osd->requestTimeOsd();
        }
    }
    m_lastPosition = currentPos;
}

void ShortcutHandler::onVolumeChanged() {
    if (!m_mpv || !m_osd) return;
    if (m_isInternalVolume) return;
    if (m_castManager && m_castManager->casting()) return; // Ignore local volume while casting
    m_osd->showVolume();
}

bool ShortcutHandler::isCursorOverQmlPanel() {
    if (!m_window) return false;
    if (!m_playlistItem) m_playlistItem = m_window->findChild<QQuickItem*>("playlistPanel");
    if (!m_statsItem) m_statsItem = m_window->findChild<QQuickItem*>("statsPanel");
    QPoint localPos = m_window->mapFromGlobal(QCursor::pos());
    if (m_playlistItem && m_playlistItem->isVisible()) {
        if (m_playlistItem->contains(m_playlistItem->mapFromItem(m_window->contentItem(), localPos))) return true;
    }
    if (m_statsItem && m_statsItem->isVisible()) {
        if (m_statsItem->contains(m_statsItem->mapFromItem(m_window->contentItem(), localPos))) return true;
    }
    return false;
}

void ShortcutHandler::updateSeekOsd() {
    if (m_castManager && m_castManager->casting()) {
        double dur = m_castManager->remoteDuration();
        if (dur <= 0) { m_seekTimer->stop(); return; }
        if ((m_seekStep > 0 && m_targetSeekTime >= dur) || (m_seekStep < 0 && m_targetSeekTime <= 0.0)) {
            if (m_osd) m_osd->requestSeekOsd(m_targetSeekTime, dur);
            return;
        }
        m_targetSeekTime += m_seekStep;
        m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
        m_castManager->seekTo(m_targetSeekTime);
        if (m_osd) m_osd->requestSeekOsd(m_targetSeekTime, dur);
        return;
    }

    if (!m_mpv || !m_osd) return;
    double dur = m_mpv->duration();
    if (dur <= 0) { m_seekTimer->stop(); return; }
    if ((m_seekStep > 0 && m_targetSeekTime >= dur) || (m_seekStep < 0 && m_targetSeekTime <= 0.0)) {
        m_osd->requestSeekOsd(m_targetSeekTime, dur);
        return;
    }
    m_targetSeekTime += m_seekStep;
    m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
    m_mpv->setPosition(m_targetSeekTime);
    m_osd->requestSeekOsd(m_targetSeekTime, dur);
}

void ShortcutHandler::updateVolOsd() {
    if (m_castManager && m_castManager->casting()) {
        double currentVol = m_castManager->remoteVolume();
        double newVol = qBound(0.0, currentVol + m_volStep, 100.0);
        if (qFuzzyCompare(newVol, currentVol)) return;
        m_castManager->setVolume(newVol);
        if (m_osd) m_osd->showVolume(newVol);
        return;
    }

    if (!m_mpv || !m_osd) return;
    double currentVol = m_mpv->volume();
    double newVol = qBound(0.0, currentVol + m_volStep, 100.0);
    if (qFuzzyCompare(newVol, currentVol)) return;

    m_isInternalVolume = true;
    m_mpv->setVolume(newVol);
    m_osd->showVolume(newVol);
}

void ShortcutHandler::startSeek(double step) {
    m_isInternalSeeking = true;

    if (m_castManager && m_castManager->casting()) {
        double dur = m_castManager->remoteDuration();
        m_targetSeekTime = m_castManager->remotePosition();
        m_seekStep = step;
        m_targetSeekTime += step;
        if (dur > 0) m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
        else m_targetSeekTime = qMax(0.0, m_targetSeekTime);
        m_castManager->seekTo(m_targetSeekTime);
        if (m_osd) m_osd->requestSeekOsd(m_targetSeekTime, dur);
        m_seekTimer->start();
        return;
    }

    if (!m_mpv || !m_osd) return;
    double pos = m_mpv->position();
    double dur = m_mpv->duration();
    m_targetSeekTime = (pos < 0 || dur <= 0) ? 0.0 : pos;
    m_seekStep = step;
    m_targetSeekTime += step;
    if (dur > 0) m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
    else m_targetSeekTime = qMax(0.0, m_targetSeekTime);
    m_mpv->setPosition(m_targetSeekTime);
    m_osd->requestSeekOsd(m_targetSeekTime, dur);
    m_seekTimer->start();
}

void ShortcutHandler::startVolume(double step) {
    m_isInternalVolume = true;
    m_volStep = step;

    if (m_castManager && m_castManager->casting()) {
        double currentVol = m_castManager->remoteVolume();
        double newVol = qBound(0.0, currentVol + step, 100.0);
        m_castManager->setVolume(newVol);
        if (m_osd) m_osd->showVolume(newVol);
        m_volTimer->start();
        m_volResetTimer->start();
        return;
    }

    if (!m_mpv || !m_osd) return;
    double currentVol = m_mpv->volume();
    double newVol = qBound(0.0, currentVol + step, 100.0);

    m_mpv->setVolume(newVol);
    m_osd->showVolume(newVol);
    m_volTimer->start();
    m_volResetTimer->start();
}

void ShortcutHandler::executeAction(ShortcutAction action, bool autoRepeat) {
    if (m_castManager && m_castManager->casting()) {
        switch (action) {
            case ShortcutAction::TogglePause:
                if (!autoRepeat) {
                    if (m_castManager->isRemotePlaying()) m_castManager->pause();
                    else m_castManager->play();
                }
                return;
            case ShortcutAction::SeekBackward:
                if (!autoRepeat) startSeek(-5.0); else m_seekStep = -5.0;
                return;
            case ShortcutAction::SeekForward:
                if (!autoRepeat) startSeek(5.0); else m_seekStep = 5.0;
                return;
            case ShortcutAction::SeekBackwardLong:
                if (!autoRepeat) startSeek(-60.0); else m_seekStep = -60.0;
                return;
            case ShortcutAction::SeekForwardLong:
                if (!autoRepeat) startSeek(60.0); else m_seekStep = 60.0;
                return;
            case ShortcutAction::VolumeUp:
                if (!autoRepeat) startVolume(5.0); else m_volStep = 5.0;
                return;
            case ShortcutAction::VolumeDown:
                if (!autoRepeat) startVolume(-5.0); else m_volStep = -5.0;
                return;
            case ShortcutAction::ToggleMute:
                if (!autoRepeat) {
                    if (m_castManager->remoteVolume() > 0.0) {
                        m_castManager->setVolume(0.0);
                        if (m_osd) m_osd->showVolume(0.0);
                    } else {
                        m_castManager->setVolume(50.0);
                        if (m_osd) m_osd->showVolume(50.0);
                    }
                }
                return;
            default:
                break;
        }
    }

    switch (action) {
        case ShortcutAction::TogglePause:
            if (!autoRepeat && m_mpv) m_mpv->setPaused(!m_mpv->paused());
            break;
        case ShortcutAction::SeekBackward:
            if (!autoRepeat) startSeek(-5.0); else m_seekStep = -5.0;
            break;
        case ShortcutAction::SeekForward:
            if (!autoRepeat) startSeek(5.0); else m_seekStep = 5.0;
            break;
        case ShortcutAction::SeekBackwardLong:
            if (!autoRepeat) startSeek(-60.0); else m_seekStep = -60.0;
            break;
        case ShortcutAction::SeekForwardLong:
            if (!autoRepeat) startSeek(60.0); else m_seekStep = 60.0;
            break;

        case ShortcutAction::FrameBackward:
            if (!autoRepeat && m_mpv) m_mpv->command(QStringList() << "frame-back-step");
            break;

        case ShortcutAction::FrameForward:
            if (!autoRepeat && m_mpv) {
                bool oldState = m_mpv->blockSignals(true);
                bool wasMuted = m_mpv->muted();
                m_mpv->setMuted(true);
                m_mpv->command(QStringList() << "frame-step");
                QTimer::singleShot(50, this, [this, wasMuted, oldState]() {
                    if (m_mpv) {
                        m_mpv->setMuted(wasMuted);
                        m_mpv->blockSignals(oldState);
                    }
                });
            }
            break;

        case ShortcutAction::VolumeUp:
            if (!autoRepeat) startVolume(5.0); else m_volStep = 5.0;
            break;
        case ShortcutAction::VolumeDown:
            if (!autoRepeat) startVolume(-5.0); else m_volStep = -5.0;
            break;
        case ShortcutAction::ToggleMute:
            if (!autoRepeat && m_mpv) {
                m_isInternalVolume = true;
                // muted() is updated asynchronously by mpv, so decide from the
                // new state; otherwise the OSD showed the inverted status.
                const bool newMuted = !m_mpv->muted();
                m_mpv->setMuted(newMuted);
                if (m_osd) m_osd->showVolume(newMuted ? 0.0 : m_mpv->volume(), newMuted);
                m_volResetTimer->start();
            }
            break;
        case ShortcutAction::NextFile:
            if (!autoRepeat) {
                m_ignoreEndFile = true;
                QTimer::singleShot(1000, this, [this]() { m_ignoreEndFile = false; });
                if (m_media) m_media->playNext();
            }
            break;
        case ShortcutAction::PreviousFile:
            if (!autoRepeat) {
                m_ignoreEndFile = true;
                QTimer::singleShot(1000, this, [this]() { m_ignoreEndFile = false; });
                if (m_media) m_media->playPrev();
            }
            break;
        case ShortcutAction::TogglePlaylist:
            if (!autoRepeat) emit togglePlaylist();
            break;
        case ShortcutAction::ToggleStats:
            if (!autoRepeat) emit toggleStats();
            break;
        case ShortcutAction::ToggleFullscreen:
            if (!autoRepeat && m_window) {
                // Fullscreen is not wanted in picture-in-picture mode.
                if (m_window->property("pipMode").toBool()) break;
                if (m_window->windowStates() & Qt::WindowFullScreen) m_window->showNormal();
                else m_window->showFullScreen();
            }
            break;
        case ShortcutAction::ToggleTimeOsd:
            if (!autoRepeat && m_osd) m_osd->togglePinTimeOsd();
            break;
        case ShortcutAction::TogglePip:
            if (!autoRepeat) emit togglePip();
            break;
        case ShortcutAction::ShowShortcutsHelp:
            if (!autoRepeat) emit shortcutsHelpVisibleChanged(true);
            break;
        case ShortcutAction::CycleSubtitles:
            if (!autoRepeat) cycleSubtitles();
            break;
        case ShortcutAction::CycleAudio:
            if (!autoRepeat) cycleAudio();
            break;
        case ShortcutAction::CycleDeinterlace:
            break;
        case ShortcutAction::CycleAspectRatio:
            if (!autoRepeat) cycleAspectRatio();
            break;
        case ShortcutAction::ToggleCrop:
            if (!autoRepeat) toggleCrop();
            break;
        case ShortcutAction::None:
        default:
            break;
    }
}

bool ShortcutHandler::eventFilter(QObject *obj, QEvent *event) {
    if (!m_enabled) return false;

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent*>(event);
        if (!m_mpv) return false;

        // Ignore when focus is in a text field (TextField, TextInput, etc.)
        QObject *focus = QGuiApplication::focusObject();
        if (focus && (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextField"))) {
            return false;
        }

        if (key->key() == Qt::Key_Escape && !key->isAutoRepeat()) {
            if (m_window && (m_window->windowStates() & Qt::WindowFullScreen)) {
                m_window->showNormal();
                return true;
            }
        }

        ShortcutAction action = actionForEvent(key);
        if (action == ShortcutAction::None) return false;

        executeAction(action, key->isAutoRepeat());
        return true;
    }
    else if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat()) {
            ShortcutAction action = actionForEvent(key);
            switch (action) {
                case ShortcutAction::SeekBackward:
                case ShortcutAction::SeekForward:
                case ShortcutAction::SeekBackwardLong:
                case ShortcutAction::SeekForwardLong:
                    m_seekTimer->stop();
                    m_isInternalSeeking = false;
                    QTimer::singleShot(50, this, [this]() { if (m_osd) m_osd->endTemporaryOsd(); });
                    return true;

                case ShortcutAction::FrameBackward:
                case ShortcutAction::FrameForward:
                    return true;

                case ShortcutAction::ShowShortcutsHelp:
                    emit shortcutsHelpVisibleChanged(false);
                    return true;

                case ShortcutAction::VolumeUp:
                case ShortcutAction::VolumeDown:
                    m_volTimer->stop();
                    m_volResetTimer->start();
                    return true;
                default:
                    break;
            }
        }
    }
    else if (event->type() == QEvent::Wheel) {
        QWidget *focusWidget = QApplication::focusWidget();
        if (focusWidget) {
            QWidget *activeWindow = focusWidget->window();
            if (activeWindow && m_window && activeWindow->winId() != m_window->winId()) {
                return false;
            }
        }

        if (isCursorOverQmlPanel()) return false;
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        if (!m_mpv) return false;

        m_isInternalVolume = true;

        int delta = wheel->angleDelta().y();
        if (delta == 0) {
            delta = wheel->pixelDelta().y() * 15;
        }

        double volStep = (delta / 120.0) * 5.0;

        // While casting, send it straight to Chromecast and consume the event
        if (m_castManager && m_castManager->casting()) {
            double newVol = qBound(0.0, m_castManager->remoteVolume() + volStep, 100.0);
            m_castManager->setVolume(newVol);
            if (m_osd) m_osd->showVolume(newVol);
            m_volResetTimer->start();
            return true;
        }

        m_wheelVolumeDelta += volStep;
        m_wheelDebounceTimer->start();

        m_volResetTimer->start();
        return true;
    }
    return QObject::eventFilter(obj, event);
}

void ShortcutHandler::toggleCrop() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    if (m_cropEnabled) {
        m_cropEnabled = false;
        m_cropPending = false;

        m_mpv->setCropEnabled(false);
        m_mpv->commandBlocking(QStringList() << "set" << "video-crop" << "");
        m_mpv->commandBlocking(QStringList() << "set" << "video-aspect-override" << "no");

        QVariantList vfList = m_mpv->getProperty("vf").toList();
        for (const QVariant &vf : vfList) {
            if (vf.toMap().value("label").toString() == "autocrop-cropdetect") {
                m_mpv->commandBlocking(QStringList() << "vf" << "remove" << "@autocrop-cropdetect");
                break;
            }
        }
        double pos = m_mpv->position();
        if (pos > 0.0) m_mpv->commandBlocking(QStringList() << "seek" << QString::number(pos) << "absolute+exact");
        m_mpv->update();
        if (m_osd) m_osd->showInfo(tr("osd_crop_off"));
        return;
    }

    if (m_cropPending) return;
    m_cropPending = true;
    if (m_osd) m_osd->showInfo(tr("osd_crop_detecting"));

    m_mpv->commandBlocking(QStringList() << "set" << "video-crop" << "");
    QVariantList vfList = m_mpv->getProperty("vf").toList();
    for (const QVariant &vf : vfList) {
        if (vf.toMap().value("label").toString() == "autocrop-cropdetect") {
            m_mpv->commandBlocking(QStringList() << "vf" << "remove" << "@autocrop-cropdetect");
            break;
        }
    }

    const QString savedHwdec = m_mpv->getProperty("hwdec").toString();
    m_mpv->commandBlocking(QStringList() << "set" << "hwdec" << "no");

    QVariant addResult = m_mpv->commandBlocking(QStringList()
    << "vf" << "add"
    << "@autocrop-cropdetect:cropdetect=limit=24/255:round=2:reset=0");

    {
        ErrorReturn err = addResult.value<ErrorReturn>();
        if (err.error != 0) {
            m_mpv->commandBlocking(QStringList() << "set" << "hwdec" << (savedHwdec.isEmpty() ? "auto" : savedHwdec));
            m_cropPending = false;
            if (m_osd) m_osd->showInfo(tr("osd_crop_failed"));
            return;
        }
    }

    const bool wasPaused = m_mpv->paused();
    m_mpv->setPaused(false);

    QTimer::singleShot(1500, this, [this, wasPaused, savedHwdec]() {
        if (!m_mpv) { m_cropPending = false; return; }
        QVariantMap meta = m_mpv->getProperty("vf-metadata/autocrop-cropdetect").toMap();
        const QString w = meta.value("lavfi.cropdetect.w").toString();
        const QString h = meta.value("lavfi.cropdetect.h").toString();
        const QString x = meta.value("lavfi.cropdetect.x").toString();
        const QString y = meta.value("lavfi.cropdetect.y").toString();

        m_mpv->commandBlocking(QStringList() << "vf" << "remove" << "@autocrop-cropdetect");

        if (!w.isEmpty() && !h.isEmpty() && w != "0" && h != "0") {
            const QString cropRect = QString("%1x%2+%3+%4").arg(w, h, x, y);
            m_mpv->commandBlocking(QStringList() << "set" << "video-crop" << cropRect);
            m_mpv->update();
            if (m_osd) m_osd->showInfo(tr("osd_crop_on"));
            m_cropEnabled = true;
            m_mpv->setCropEnabled(true);
        } else {
            if (m_osd) m_osd->showInfo(tr("osd_crop_failed"));
            m_cropEnabled = false;
            m_mpv->setCropEnabled(false);
        }
        m_mpv->setPaused(wasPaused);
        m_mpv->commandBlocking(QStringList() << "set" << "hwdec" << (savedHwdec.isEmpty() ? "auto" : savedHwdec));
        m_cropPending = false;
    });
}

void ShortcutHandler::cycleAspectRatio() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    QStringList aspects = {"original", "16:9", "4:3", "21:9", "1:1", "9:16"};
    QStringList labels  = {"Original", "16:9", "4:3", "21:9", "1:1", "9:16"};
    m_aspectIndex = (m_aspectIndex + 1) % aspects.size();
    QString mode = aspects[m_aspectIndex];
    QString label = labels[m_aspectIndex];
    if (mode == "original") m_mpv->setProperty("video-aspect-override", "no");
    else m_mpv->setProperty("video-aspect-override", mode);
    if (m_osd) m_osd->showInfo(tr("osd_aspect").arg(label));
}

void ShortcutHandler::cycleSubtitles() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    QVariantList tracks = m_mpv->getProperty("track-list").toList();
    QList<int> subIds;
    for (const QVariant &t : tracks) {
        QVariantMap map = t.toMap();
        if (map["type"].toString() == "sub") subIds << map["id"].toInt();
    }
    if (subIds.isEmpty()) {
        if (m_osd) m_osd->showInfo(tr("osd_no_subtitles"));
        return;
    }
    int currentId = m_mpv->getProperty("sid").toInt();
    int nextIndex = 0;
    if (currentId > 0) {
        int idx = subIds.indexOf(currentId);
        if (idx != -1) nextIndex = (idx + 1) % (subIds.size() + 1);
    }
    if (nextIndex == subIds.size()) {
        m_mpv->setProperty("sid", "no");
        if (m_osd) m_osd->showInfo(tr("osd_sub_off"));
    } else {
        m_mpv->setProperty("sid", subIds[nextIndex]);
        if (m_osd) m_osd->showInfo(tr("osd_sub_track").arg(subIds[nextIndex]));
    }
}

void ShortcutHandler::cycleAudio() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    QVariantList tracks = m_mpv->getProperty("track-list").toList();
    QList<int> audioIds;
    for (const QVariant &t : tracks) {
        QVariantMap map = t.toMap();
        if (map["type"].toString() == "audio") audioIds << map["id"].toInt();
    }
    if (audioIds.isEmpty()) {
        if (m_osd) m_osd->showInfo(tr("osd_no_audio"));
        return;
    }
    int currentId = m_mpv->getProperty("aid").toInt();
    int nextIndex = 0;
    if (currentId > 0) {
        int idx = audioIds.indexOf(currentId);
        if (idx != -1) nextIndex = (idx + 1) % audioIds.size();
    }
    m_mpv->setProperty("aid", audioIds[nextIndex]);
    if (m_osd) m_osd->showInfo(tr("osd_audio_track").arg(audioIds[nextIndex]));
}
