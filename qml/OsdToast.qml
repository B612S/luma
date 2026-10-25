import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: osd
    width: label.implicitWidth + 32
    height: 40
    radius: 8
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                   Kirigami.Theme.backgroundColor.g,
                   Kirigami.Theme.backgroundColor.b, 0.85)
    border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                          Kirigami.Theme.textColor.g,
                          Kirigami.Theme.textColor.b, 0.2)

    property alias text: label.text
    property bool isOpen: false

    visible: opacity > 0
    opacity: 0.0

    Behavior on opacity {
        NumberAnimation {
            duration: 150
            easing.type: Easing.InOutQuad
        }
    }

    onIsOpenChanged: {
        if (isOpen) {
            osd.opacity = 1.0
        } else {
            osd.opacity = 0.0
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: false
        propagateComposedEvents: true
        onClicked: (mouse) => { mouse.accepted = false }
    }

    Label {
        id: label
        anchors.centerIn: parent
        color: Kirigami.Theme.textColor
        font.pixelSize: 16
        wrapMode: Text.NoWrap
    }
}
