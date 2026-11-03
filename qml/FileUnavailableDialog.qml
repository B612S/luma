import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Shown when a playlist entry points to a file that has been deleted or moved,
// styled like the Settings and information dialogs.
Dialog {
    id: dialog
    title: qsTranslate("FileUnavailableDialog", "unavailable_title")
    modal: true
    focus: true
    width: 460
    padding: 0

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string filePath: ""

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }

    onOpened: {
        if (typeof shortcuts !== "undefined" && shortcuts !== null)
            shortcuts.setShortcutsEnabled(false);
    }
    onClosed: {
        if (typeof shortcuts !== "undefined" && shortcuts !== null)
            shortcuts.setShortcutsEnabled(true);
    }

    function openWith(path) {
        dialog.filePath = path;
        dialog.open();
    }

    contentItem: Item {
        implicitHeight: column.implicitHeight + 40

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTranslate("FileUnavailableDialog", "unavailable_text")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Kirigami.Theme.textColor
                font.pixelSize: 14
            }

            Label {
                Layout.fillWidth: true
                text: dialog.filePath
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                font.pixelSize: 13
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.7)
            }
        }
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignRight

        Button {
            text: qsTranslate("FileUnavailableDialog", "unavailable_ok")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.close()
        }
        Item { implicitWidth: 16; implicitHeight: 1 }
    }
}
