// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

import QtQuick
import org.kde.kirigami as Kirigami

// Divider between entries of the in-app context menus (LumaMenu).
Rectangle {
    width: parent ? parent.width : 0
    height: 1
    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                   Kirigami.Theme.textColor.b, 0.2)
}
