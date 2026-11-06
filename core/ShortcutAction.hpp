#pragma once
#include <QList>
#include <Qt>
#include <QKeyEvent>
#include <QString>

enum class ShortcutAction {
    // Playback
    TogglePause,
    SeekForward,
    SeekBackward,
    SeekForwardLong,
    SeekBackwardLong,
    FrameForward,   // FIX: +1 frame
    FrameBackward,  // FIX: -1 frame
    VolumeUp,
    VolumeDown,
    ToggleMute,
    NextFile,
    PreviousFile,
    PreviousChapter,
    NextChapter,
    // UI
    TogglePlaylist,
    ToggleStats,
    ToggleFullscreen,
    ToggleTimeOsd,
    TogglePip,
    ToggleWindow,
    FitWindowToVideo,
    ShowShortcutsHelp,
    // Tracks
    CycleSubtitles,
    CycleAudio,
    CycleDeinterlace,
    CycleAspectRatio,
    ToggleCrop,
    // Unknown
    None
};

struct ShortcutDef {
    ShortcutAction action;
    const char *keyName;
    quint32 keycode; // Wayland keycode (evdev + 8)
    Qt::KeyboardModifiers modifiers;
};

struct ShortcutActionInfo {
    ShortcutAction action;
    const char *id;
    const char *category; // playback, interface, tracks
    const char *name;     // English name for desktop-portal descriptions
};

inline const QList<ShortcutActionInfo>& shortcutActions() {
    static const QList<ShortcutActionInfo> list = {
        { ShortcutAction::TogglePause,       "toggle_pause",       "playback",  "Play / pause" },
        { ShortcutAction::SeekBackward,      "seek_backward",      "playback",  "Seek backward" },
        { ShortcutAction::SeekForward,       "seek_forward",       "playback",  "Seek forward" },
        { ShortcutAction::SeekBackwardLong,  "seek_backward_long", "playback",  "Seek backward 60 s" },
        { ShortcutAction::SeekForwardLong,   "seek_forward_long",  "playback",  "Seek forward 60 s" },
        { ShortcutAction::FrameBackward,     "frame_backward",     "playback",  "Previous frame" },
        { ShortcutAction::FrameForward,      "frame_forward",      "playback",  "Next frame" },
        { ShortcutAction::VolumeUp,          "volume_up",          "playback",  "Volume up" },
        { ShortcutAction::VolumeDown,        "volume_down",        "playback",  "Volume down" },
        { ShortcutAction::ToggleMute,        "toggle_mute",        "playback",  "Mute" },
        { ShortcutAction::PreviousFile,      "previous_file",      "playback",  "Previous file" },
        { ShortcutAction::NextFile,          "next_file",          "playback",  "Next file" },
        { ShortcutAction::PreviousChapter,   "previous_chapter",   "playback",  "Previous chapter" },
        { ShortcutAction::NextChapter,       "next_chapter",       "playback",  "Next chapter" },
        { ShortcutAction::TogglePlaylist,    "toggle_playlist",    "interface", "Playlist panel" },
        { ShortcutAction::ToggleStats,       "toggle_stats",       "interface", "Statistics panel" },
        { ShortcutAction::ToggleFullscreen,  "toggle_fullscreen",  "interface", "Fullscreen" },
        { ShortcutAction::ToggleTimeOsd,     "toggle_time",        "interface", "Pin the time counter" },
        { ShortcutAction::TogglePip,         "toggle_pip",         "interface", "Picture in picture" },
        { ShortcutAction::ToggleWindow,      "toggle_window",      "interface", "Show / hide window" },
        { ShortcutAction::FitWindowToVideo,  "fit_window",         "interface", "Fit window to video" },
        { ShortcutAction::CycleSubtitles,    "cycle_subtitles",    "tracks",    "Next subtitles track" },
        { ShortcutAction::CycleAudio,        "cycle_audio",        "tracks",    "Next audio track" },
        { ShortcutAction::CycleDeinterlace,  "cycle_deinterlace",  "tracks",    "Switch deinterlacing" },
        { ShortcutAction::CycleAspectRatio,  "cycle_aspect",       "tracks",    "Switch aspect ratio" },
        { ShortcutAction::ToggleCrop,        "toggle_crop",        "tracks",    "Detect and crop black bars" },
    };
    return list;
}

inline const ShortcutActionInfo* shortcutActionInfo(ShortcutAction action) {
    for (const ShortcutActionInfo &info : shortcutActions()) {
        if (info.action == action) return &info;
    }
    return nullptr;
}

inline QString shortcutActionId(ShortcutAction action) {
    if (const ShortcutActionInfo *info = shortcutActionInfo(action))
        return QString::fromLatin1(info->id);
    return QString();
}

inline ShortcutAction shortcutActionFromId(const QString &id) {
    for (const ShortcutActionInfo &info : shortcutActions()) {
        if (id == QLatin1String(info.id)) return info.action;
    }
    return ShortcutAction::None;
}

// Converts a QKeySequence-style trigger to the XDG shortcuts specification
// spelling used by the GlobalShortcuts portal.
inline QString shortcutPortalTrigger(const QString &text) {
    QString trigger = text.trimmed();
    trigger.replace(QStringLiteral("Ctrl"), QStringLiteral("CTRL"), Qt::CaseInsensitive);
    trigger.replace(QStringLiteral("Alt"), QStringLiteral("ALT"), Qt::CaseInsensitive);
    trigger.replace(QStringLiteral("Shift"), QStringLiteral("SHIFT"), Qt::CaseInsensitive);
    trigger.replace(QStringLiteral("Meta"), QStringLiteral("LOGO"), Qt::CaseInsensitive);
    trigger.remove(QLatin1Char(' '));
    return trigger;
}

struct ShortcutBinding {
    ShortcutAction action = ShortcutAction::None;
    quint32 keycode = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QString text;

    bool isValid() const { return action != ShortcutAction::None && keycode > 0 && !text.isEmpty(); }
};

inline const QList<ShortcutDef>& defaultShortcuts() {
    static const QList<ShortcutDef> list = {
        // ---- Playback ----
        { ShortcutAction::TogglePause,       "Space",  65,  Qt::NoModifier },
        { ShortcutAction::SeekBackward,      "Left",  113,  Qt::NoModifier },
        { ShortcutAction::SeekForward,       "Right", 114,  Qt::NoModifier },
        { ShortcutAction::SeekBackwardLong,  "Left",  113,  Qt::ControlModifier },
        { ShortcutAction::SeekForwardLong,   "Right", 114,  Qt::ControlModifier },

        // FIX: Alt+Left / Alt+Right for frame-by-frame seeking
        { ShortcutAction::FrameBackward,     "Left",  113,  Qt::AltModifier },
        { ShortcutAction::FrameForward,      "Right", 114,  Qt::AltModifier },

        { ShortcutAction::VolumeUp,          "Up",    111,  Qt::NoModifier },
        { ShortcutAction::VolumeDown,        "Down",  116,  Qt::NoModifier },
        { ShortcutAction::ToggleMute,        "M",      58,  Qt::NoModifier },
        { ShortcutAction::PreviousFile,      "Q",      24,  Qt::NoModifier },
        { ShortcutAction::NextFile,          "E",      26,  Qt::NoModifier },
        { ShortcutAction::PreviousChapter,   "PageUp", 112, Qt::NoModifier },
        { ShortcutAction::NextChapter,       "PageDown", 117, Qt::NoModifier },

        // ---- UI ----
        { ShortcutAction::TogglePlaylist,    "S",      39,  Qt::NoModifier },
        { ShortcutAction::ToggleStats,       "I",      31,  Qt::NoModifier },
        { ShortcutAction::ToggleFullscreen,  "F",      41,  Qt::NoModifier },
        { ShortcutAction::ToggleTimeOsd,     "T",      28,  Qt::NoModifier },
        { ShortcutAction::TogglePip,         "P",      33,  Qt::NoModifier },
        { ShortcutAction::FitWindowToVideo,  "W",      25,  Qt::NoModifier },

        // Holding "?" (Shift+/) or "." on the Russian layout shows the help.
        { ShortcutAction::ShowShortcutsHelp, "/",      61,  Qt::NoModifier },
        { ShortcutAction::ShowShortcutsHelp, "/",      61,  Qt::ShiftModifier },

        // ---- Tracks ----
        { ShortcutAction::CycleSubtitles,    "C",      54,  Qt::NoModifier },
        { ShortcutAction::CycleAudio,        "A",      38,  Qt::NoModifier },
        { ShortcutAction::CycleDeinterlace,  "D",      40,  Qt::NoModifier },
        { ShortcutAction::CycleAspectRatio,  "R",      27,  Qt::NoModifier },
        { ShortcutAction::ToggleCrop,        "J",      44,  Qt::NoModifier },
    };
    return list;
}

ShortcutAction actionForEvent(QKeyEvent *event);
