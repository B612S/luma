#include "ShortcutAction.hpp"
#include <QKeyEvent>

ShortcutAction actionForEvent(QKeyEvent *event) {
    // Under X11 and Wayland, Qt's nativeScanCode() returns the same value
    // (X11 keycode = evdev + 8), so no normalization is needed.
    const quint32 keycode = static_cast<quint32>(event->nativeScanCode());
    const Qt::KeyboardModifiers mods = event->modifiers()
    & ~Qt::GroupSwitchModifier
    & ~Qt::KeypadModifier;

    for (const ShortcutDef &def : defaultShortcuts()) {
        if (def.keycode == keycode && def.modifiers == mods)
            return def.action;
    }
    return ShortcutAction::None;
}
