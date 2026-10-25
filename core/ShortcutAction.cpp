#include "ShortcutAction.hpp"
#include <QKeyEvent>

ShortcutAction actionForEvent(QKeyEvent *event) {
    // nativeScanCode() под X11 и Wayland в Qt возвращает одно и то же
    // (X11 keycode = evdev + 8). Поэтому никакой нормализации не нужно.
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
