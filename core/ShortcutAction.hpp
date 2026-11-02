#pragma once
#include <QList>
#include <Qt>
#include <QKeyEvent>

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
    // UI
    TogglePlaylist,
    ToggleStats,
    ToggleFullscreen,
    ToggleTimeOsd,
    TogglePip,
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

        // ---- UI ----
        { ShortcutAction::TogglePlaylist,    "S",      39,  Qt::NoModifier },
        { ShortcutAction::ToggleStats,       "I",      31,  Qt::NoModifier },
        { ShortcutAction::ToggleFullscreen,  "F",      41,  Qt::NoModifier },
        { ShortcutAction::ToggleTimeOsd,     "T",      28,  Qt::NoModifier },
        { ShortcutAction::TogglePip,         "P",      33,  Qt::NoModifier },

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
