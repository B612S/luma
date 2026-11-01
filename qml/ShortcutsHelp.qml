import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

// Keyboard shortcuts help: shown while "?" (Shift+/) is held.
// Styling matches the other panels of the app (see SettingsDialog).
Item {
    id: root
    z: 20

    property bool opened: false
    visible: opacity > 0
    opacity: opened ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.InOutQuad } }

    // Label keys in the currently active layout: Keyboard reads
    // the keymap straight from Wayland (wl_keyboard.keymap) and updates on layout switch.
    function keyText(keys) {
        return keys.split("/").map(function (part) {
            const token = part.trim();
            if (token === "Space") return qsTr("sc_space_key");
            if (/^[A-Za-z]$/.test(token)) return Keyboard.keyName(token);
            return token;
        }).join(" / ");
    }

    readonly property var shortcuts: [
        { keys: "Space", text: qsTr("sc_play_pause") },
        { keys: "← / →", text: qsTr("sc_seek") },
        { keys: "Ctrl + ← / →", text: qsTr("sc_seek_long") },
        { keys: "Alt + ← / →", text: qsTr("sc_frame") },
        { keys: "↑ / ↓", text: qsTr("sc_volume") },
        { keys: "M", text: qsTr("sc_mute") },
        { keys: "Q / E", text: qsTr("sc_prev_next") },
        { keys: "S", text: qsTr("sc_playlist") },
        { keys: "I", text: qsTr("sc_stats") },
        { keys: "F", text: qsTr("sc_fullscreen") },
        { keys: "P", text: qsTr("sc_pip") },
        { keys: "T", text: qsTr("sc_time") },
        { keys: "C", text: qsTr("sc_subtitles") },
        { keys: "A", text: qsTr("sc_audio") },
        { keys: "D", text: qsTr("sc_deinterlace") },
        { keys: "R", text: qsTr("sc_aspect") },
        { keys: "J", text: qsTr("sc_crop") },
        { keys: "?", text: qsTr("sc_help") }
    ]

    // While the help is on screen, clicks must not reach the player.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: (mouse) => { mouse.accepted = true; }
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.45)
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        height: Math.min(parent.height - 64, contentColumn.implicitHeight + 48)
        radius: 8
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.85)
        border.color: "transparent"

        Flickable {
            anchors.fill: parent
            anchors.margins: 20
            clip: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            Column {
                id: contentColumn
                width: parent.width - 16
                spacing: 10

                Label {
                    text: qsTr("shortcuts_title")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: Kirigami.Theme.textColor
                }

                Rectangle {
                    width: parent.width; height: 1
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                }

                Repeater {
                    model: root.shortcuts
                    delegate: Row {
                        width: contentColumn.width
                        spacing: 12

                        Label {
                            width: 190
                            text: root.keyText(modelData.keys)
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Label {
                            width: parent.width - 202
                            text: modelData.text
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.8)
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
