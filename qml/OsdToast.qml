import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: osd
    // Long messages (e.g. the link yt-dlp is opening) must not overflow the
    // window; such text is elided in the middle instead.
    readonly property real maxWidth: Window.window ? Math.max(200, Window.window.width - 80) : 900
    width: Math.min(label.implicitWidth + 32, maxWidth)
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

    // Show at once (a toast that only fades in stays invisible for the first
    // frames, which looks like a missing OSD) and fade out when hiding.
    visible: isOpen || opacity > 0
    opacity: 0.0

    Behavior on opacity {
        id: opacityBehavior
        NumberAnimation {
            duration: 150
            easing.type: Easing.InOutQuad
        }
    }

    onIsOpenChanged: {
        if (isOpen) {
            // Appear at once: waiting for a fade-in animation can leave the
            // toast invisible for the first frames, which looks like a missing
            // OSD when the player is busy.
            opacityBehavior.enabled = false
            osd.opacity = 1.0
            opacityBehavior.enabled = true
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
        width: osd.width - 32
        horizontalAlignment: Text.AlignHCenter
        color: Kirigami.Theme.textColor
        textFormat: Text.PlainText
        font.pixelSize: 16
        wrapMode: Text.NoWrap
        elide: Text.ElideMiddle
    }
}
