import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Keyboard shortcuts help. Styled like SettingsDialog/MediaInfoDialog and
// shown while "?" is held. The list is generated from the configured bindings
// (local and global), so changing a shortcut updates it immediately.
Dialog {
    id: root

    property bool helpVisible: false
    visible: helpVisible
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, Overlay.overlay ? Overlay.overlay.width - 64 : 620)
    height: Math.min(600, Overlay.overlay ? Overlay.overlay.height - 64 : 600)
    padding: 0
    title: qsTr("shortcuts_title")

    signal closeRequested()

    onHelpVisibleChanged: {
        if (typeof shortcuts !== "undefined" && shortcuts)
            shortcuts.setHelpVisible(helpVisible);
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }

    onClosed: {
        if (typeof shortcuts !== "undefined" && shortcuts)
            shortcuts.setHelpVisible(false);
        if (helpVisible) closeRequested();
    }

    contentItem: Item {
        Flickable {
            anchors.fill: parent
            anchors.margins: 20
            clip: true
            interactive: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            Column {
                id: contentColumn
                width: parent.width - 16
                spacing: 10

                Repeater {
                    model: root.rows
                    delegate: Row {
                        width: contentColumn.width
                        spacing: 12

                        Label {
                            width: 210
                            text: root.keysFor(modelData)
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Label {
                            width: parent.width - 222
                            text: modelData.help
                                  ? qsTr("sc_help")
                                  : qsTranslate("Shortcuts", "shortcut_" + modelData.id)
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                           Kirigami.Theme.textColor.b, 0.8)
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            text: qsTranslate("SettingsDialog", "about_close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: root.closeRequested()
        }
        Item { implicitWidth: 16; implicitHeight: 1 }
    }

    property var rows: []

    function refreshRows() {
        const result = [];
        if (typeof shortcuts !== "undefined" && shortcuts && shortcuts.bindings) {
            const list = shortcuts.bindings;
            const globalAllowed = shortcuts.globalShortcutsAllowed === true;
            for (let i = 0; i < list.length; ++i) {
                const binding = list[i];
                const globalText = globalAllowed ? (binding.globalText || "") : "";
                if (!binding.localText && !globalText) continue;
                result.push({
                    id: binding.id,
                    localText: binding.localText || "",
                    globalText: globalText,
                    help: false
                });
            }
        }
        result.push({ help: true });
        rows = result;
    }

    function keysFor(row) {
        if (row.help) return "?";
        let keys = row.localText || "";
        if (row.globalText) {
            if (keys !== "") keys += " / ";
            keys += row.globalText + " (" + qsTr("shortcuts_global") + ")";
        }
        return keys;
    }

    Component.onCompleted: {
        if (typeof shortcuts !== "undefined" && shortcuts)
            shortcuts.setHelpVisible(helpVisible);
        refreshRows();
    }

    Connections {
        target: shortcuts
        function onBindingsChanged() { root.refreshRows(); }
    }
}
