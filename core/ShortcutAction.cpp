#include "ShortcutAction.hpp"
#include <QKeyEvent>

ShortcutAction actionForEvent(QKeyEvent *event) {
    // On Wayland, Qt's nativeScanCode() returns the evdev keycode + 8, which is
    // exactly the value stored in the shortcut table — no normalization needed.
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
