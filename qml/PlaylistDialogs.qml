import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    // Строки плейлиста лежат в locales/*.ts под контекстом "PlaylistPanel",
    // поэтому здесь нужен qsTranslate: qsTr() взял бы контекст "PlaylistDialogs"
    // и перевод не нашёлся бы (в интерфейсе остались бы ключи вида playlist_create).

    property alias nameDialog: nameDialog
    property alias deleteDialog: deleteDialog
    property alias clearDialog: clearDialog

    Dialog {
        id: nameDialog
        title: isRename ? qsTranslate("PlaylistPanel", "playlist_rename_title") : qsTranslate("PlaylistPanel", "playlist_create_title")
        modal: true; focus: true; width: 400; padding: 0
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property bool isRename: false
        property string targetId: ""

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
            radius: 8; border.color: "transparent"
        }

        onOpened: {
            if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(false);
            nameField.forceActiveFocus();
            Qt.callLater(() => { nameField.forceActiveFocus(); nameField.selectAll(); });
        }
        onClosed: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(true); }

        contentItem: Item {
            implicitHeight: layout.implicitHeight + 40
            ColumnLayout {
                id: layout; anchors.fill: parent; anchors.margins: 20; spacing: 16
                TextField {
                    id: nameField; Layout.fillWidth: true; placeholderText: qsTranslate("PlaylistPanel", "playlist_name_placeholder")
                    font.pixelSize: 14; color: Kirigami.Theme.textColor; selectByMouse: true; focus: true
                    background: Rectangle {
                        implicitHeight: 36; radius: 4
                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05)
                        border.color: nameField.activeFocus ? Kirigami.Theme.highlightColor : "transparent"; border.width: 1
                    }
                    // Enter/Return обрабатываются штатно (onAccepted), а пробел
                    // должен попадать в текст — перехват здесь не нужен:
                    // ShortcutHandler и так игнорирует клавиши при фокусе в поле ввода.
                    onAccepted: {
                        let name = nameField.text.trim();
                        if (name.length > 0) {
                            if (nameDialog.isRename) Playlists.renamePlaylist(nameDialog.targetId, name);
                            else { let newId = Playlists.createPlaylist(name); if (newId) Playlists.activeId = newId; }
                            nameDialog.close();
                        }
                    }
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight; spacing: 8
                    Button { text: qsTranslate("PlaylistPanel", "playlist_cancel"); onClicked: nameDialog.close() }
                    Button {
                        text: nameDialog.isRename ? qsTranslate("PlaylistPanel", "playlist_rename") : qsTranslate("PlaylistPanel", "playlist_create")
                        enabled: nameField.text.trim().length > 0
                        onClicked: {
                            let name = nameField.text.trim();
                            if (name.length === 0) return;
                            if (nameDialog.isRename) Playlists.renamePlaylist(nameDialog.targetId, name);
                            else { let newId = Playlists.createPlaylist(name); if (newId) Playlists.activeId = newId; }
                            nameDialog.close();
                        }
                    }
                }
            }
        }
        function openForCreate() { isRename = false; targetId = ""; nameField.text = ""; open(); }
        function openForRename(id, currentName) { isRename = true; targetId = id; nameField.text = currentName; open(); }
    }

    Dialog {
        id: deleteDialog
        title: qsTranslate("PlaylistPanel", "playlist_delete_title")
        modal: true; focus: true; width: 400; padding: 0
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string targetId: ""
        property string targetName: ""

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
            radius: 8; border.color: "transparent"
        }

        onOpened: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(false); }
        onClosed: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(true); }

        contentItem: Item {
            implicitHeight: layoutDelete.implicitHeight + 40
            ColumnLayout {
                id: layoutDelete; anchors.fill: parent; anchors.margins: 20; spacing: 16
                Label {
                    Layout.fillWidth: true; text: qsTranslate("PlaylistPanel", "playlist_delete_confirm").arg(deleteDialog.targetName)
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.8); wrapMode: Text.WrapAnywhere
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight; spacing: 8
                    Button { text: qsTranslate("PlaylistPanel", "playlist_cancel"); onClicked: deleteDialog.close() }
                    Button { text: qsTranslate("PlaylistPanel", "playlist_delete"); onClicked: { Playlists.deletePlaylist(deleteDialog.targetId); deleteDialog.close(); } }
                }
            }
        }
        function openFor(id, name) { targetId = id; targetName = name; open(); }
    }

    Dialog {
        id: clearDialog
        title: qsTranslate("PlaylistPanel", "playlist_clear_title")
        modal: true; focus: true; width: 400; padding: 0
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string targetId: ""
        property string targetName: ""

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
            radius: 8; border.color: "transparent"
        }

        onOpened: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(false); }
        onClosed: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(true); }

        contentItem: Item {
            implicitHeight: layoutClear.implicitHeight + 40
            ColumnLayout {
                id: layoutClear; anchors.fill: parent; anchors.margins: 20; spacing: 16
                Label {
                    Layout.fillWidth: true; text: qsTranslate("PlaylistPanel", "playlist_clear_confirm").arg(clearDialog.targetName)
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.8); wrapMode: Text.WrapAnywhere
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight; spacing: 8
                    Button { text: qsTranslate("PlaylistPanel", "playlist_cancel"); onClicked: clearDialog.close() }
                    Button {
                        text: qsTranslate("PlaylistPanel", "playlist_clear")
                        onClicked: {
                            let model = Playlists.modelById(clearDialog.targetId);
                            if (model) model.clear();
                            clearDialog.close();
                        }
                    }
                }
            }
        }
        function openFor(id, name) { targetId = id; targetName = name; open(); }
    }
}
