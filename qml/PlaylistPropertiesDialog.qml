import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Per-playlist properties, using the same layout as the Settings dialog:
// categories on the left, their contents on the right, Close in the footer.
// Currently a playlist can watch folders: existing media is imported in the
// background when a folder is added and new files are appended when they
// appear.
Dialog {
    id: dialog
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property real maxDialogWidth: {
        const w = Overlay.overlay ? Overlay.overlay.width : 0;
        return Math.max(360, (w > 0 ? w : 800) - 32);
    }
    readonly property real maxDialogHeight: {
        const h = Overlay.overlay ? Overlay.overlay.height : 0;
        return Math.max(280, (h > 0 ? h : 600) - 32);
    }
    width: Math.min(800, maxDialogWidth)
    height: Math.min(600, maxDialogHeight)

    property string playlistId: ""
    property string playlistName: ""
    property var folders: []

    // Same selection style as Settings: 0.35 body, opaque side bar.
    readonly property color selectionBackgroundColor: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                                              Kirigami.Theme.highlightColor.g,
                                                              Kirigami.Theme.highlightColor.b, 0.35)

    title: playlistName !== ""
           ? qsTranslate("PlaylistPanel", "playlist_properties_title") + " — " + playlistName
           : qsTranslate("PlaylistPanel", "playlist_properties_title")

    function refresh() {
        if (typeof Playlists === "undefined" || !Playlists)
            return;
        folders = Playlists.watchFolders(playlistId);
        playlistName = Playlists.playlistName(playlistId);
    }

    function openFor(id) {
        playlistId = id;
        playlistName = (typeof Playlists !== "undefined" && Playlists)
                       ? Playlists.playlistName(id) : "";
        refresh();
        open();
    }

    onOpened: {
        if (typeof shortcuts !== "undefined" && shortcuts !== null)
            shortcuts.setShortcutsEnabled(false);
        content.forceActiveFocus();
    }
    onClosed: {
        if (typeof shortcuts !== "undefined" && shortcuts !== null)
            shortcuts.setShortcutsEnabled(true);
    }

    background: Rectangle {
        // Same translucency as the Settings dialog.
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }

    Connections {
        target: typeof Playlists !== "undefined" ? Playlists : null

        function onWatchFoldersChanged(id) {
            if (id === dialog.playlistId)
                dialog.refresh();
        }

        function onPlaylistsChanged() {
            if (typeof Playlists === "undefined" || !Playlists)
                return;
            if (dialog.playlistId === "")
                return;
            if (Playlists.modelById(dialog.playlistId) === null) {
                dialog.close();
                return;
            }
            dialog.refresh();
        }
    }

    contentItem: RowLayout {
        id: content
        spacing: 0
        focus: true

        Keys.onEscapePressed: (event) => {
            dialog.close();
            event.accepted = true;
        }

        // ---- Categories (left) ----
        Rectangle {
            Layout.preferredWidth: dialog.width < 700 ? 160 : 200
            Layout.fillHeight: true
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.5)
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                Item { Layout.preferredHeight: 8 }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    radius: 4
                    color: dialog.selectionBackgroundColor

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 4
                        anchors.bottomMargin: 4
                        width: 3
                        radius: 1.5
                        color: Kirigami.Theme.highlightColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        text: qsTranslate("PlaylistPanel", "playlist_watch_folders")
                        color: Kirigami.Theme.textColor
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ---- Watched folders (right) ----
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            clip: true
            contentWidth: width
            contentHeight: contentLayout.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            Item {
                width: parent.width - 22
                implicitHeight: contentLayout.implicitHeight

                ColumnLayout {
                    id: contentLayout
                    width: parent.width
                    spacing: 22

                    Label {
                        Layout.fillWidth: true
                        text: qsTranslate("PlaylistPanel", "playlist_watch_folders")
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: Kirigami.Theme.textColor
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Qt.rgba(Kirigami.Theme.textColor.r,
                                       Kirigami.Theme.textColor.g,
                                       Kirigami.Theme.textColor.b, 0.2)
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTranslate("PlaylistPanel", "playlist_watch_hint")
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: Qt.rgba(Kirigami.Theme.textColor.r,
                                       Kirigami.Theme.textColor.g,
                                       Kirigami.Theme.textColor.b, 0.65)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTranslate("PlaylistPanel", "playlist_add_watch_folder")
                            onClicked: Playlists.openAddWatchFolderDialog(dialog.playlistId)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Label {
                            Layout.fillWidth: true
                            visible: dialog.folders.length === 0
                            text: qsTranslate("PlaylistPanel", "playlist_no_watch_folders")
                            font.pixelSize: 12
                            color: Qt.rgba(Kirigami.Theme.textColor.r,
                                           Kirigami.Theme.textColor.g,
                                           Kirigami.Theme.textColor.b, 0.6)
                        }

                        Repeater {
                            model: dialog.folders

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData
                                    elide: Text.ElideMiddle
                                    font.pixelSize: 14
                                    color: Kirigami.Theme.textColor
                                    ToolTip.visible: folderHover.containsMouse
                                    ToolTip.text: modelData

                                    MouseArea {
                                        id: folderHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }
                                }

                                ToolButton {
                                    text: "×"
                                    font.pixelSize: 16
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTranslate("PlaylistPanel", "playlist_remove_watch_folder")
                                    onClicked: Playlists.removeWatchFolder(dialog.playlistId, modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
