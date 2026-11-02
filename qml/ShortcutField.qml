import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

// A click-to-capture key field used by the Shortcuts settings page.
Rectangle {
    id: root

    property string text: ""
    property bool capturing: false
    property bool isGlobal: false

    signal captured(int keycode, int key, int modifiers, string text)
    signal cleared()

    width: 160
    height: 32
    radius: 4
    color: capturing
           ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                     Kirigami.Theme.highlightColor.b, 0.15)
           : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                     Kirigami.Theme.textColor.b, 0.06)
    border.width: 1
    border.color: capturing
                  ? Kirigami.Theme.highlightColor
                  : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                            Kirigami.Theme.textColor.b, 0.25)
    focus: false
    activeFocusOnTab: true

    onActiveFocusChanged: {
        if (!activeFocus) capturing = false;
    }

    Text {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.capturing
              ? qsTranslate("SettingsDialog", "shortcuts_press")
              : (root.text !== "" ? root.text : qsTranslate("SettingsDialog", "shortcuts_unassigned"))
        color: root.text !== "" || root.capturing
               ? Kirigami.Theme.textColor
               : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                         Kirigami.Theme.textColor.b, 0.55)
        font.pixelSize: 13
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus();
            root.capturing = true;
        }
    }

    Keys.onPressed: (event) => {
        if (!root.capturing) return;
        if (event.isAutoRepeat) { event.accepted = true; return; }

        if (event.key === Qt.Key_Escape) {
            root.capturing = false;
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            root.capturing = false;
            root.cleared();
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta) {
            event.accepted = true;
            return;
        }

        const sequence = shortcuts.keyText(event.nativeScanCode, event.key, event.modifiers);
        if (sequence === "") { event.accepted = true; return; }

        root.capturing = false;
        root.captured(event.nativeScanCode, event.key, event.modifiers, sequence);
        event.accepted = true;
    }
}
