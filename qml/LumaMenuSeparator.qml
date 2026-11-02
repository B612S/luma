import QtQuick
import org.kde.kirigami as Kirigami

// Divider between entries of the in-app context menus (LumaMenu).
Rectangle {
    width: parent ? parent.width : 0
    height: 1
    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                   Kirigami.Theme.textColor.b, 0.2)
}
