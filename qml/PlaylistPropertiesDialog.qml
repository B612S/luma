import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Per-playlist properties, using the same layout as the Settings dialog:
// categories on the left, their contents on the right.
// The Default playlist gets the append switch and its playback mode; named
// playlists get their playback mode and watched folders.
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
    property string playlistMode: "sequential"
    property string currentCategory: "playback"

    readonly property bool isDefault: typeof Playlists !== "undefined" && Playlists
                                      && Playlists.isDefaultPlaylist(playlistId)

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
        playlistMode = Playlists.playlistMode(playlistId);
        if (isDefault && currentCategory === "watched")
            currentCategory = "playback";
    }

    function openFor(id) {
        playlistId = id;
        playlistName = (typeof Playlists !== "undefined" && Playlists)
                       ? Playlists.playlistName(id) : "";
        const defaultPlaylist = typeof Playlists !== "undefined" && Playlists
                                && Playlists.isDefaultPlaylist(id);
        currentCategory = defaultPlaylist ? "playback" : "watched";
        refresh();
        open();
    }

    onOpened: content.forceActiveFocus()

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
            if (!dialog.isDefault && Playlists.modelById(dialog.playlistId) === null) {
                dialog.close();
                return;
            }
            dialog.refresh();
        }
    }

    Connections {
        target: typeof Config !== "undefined" ? Config : null
        function onPlaylistModeChanged() {
            if (dialog.isDefault)
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

                component CategoryButton: Rectangle {
                    id: btn
                    property string text: ""
                    property bool isActive: false
                    signal clicked()

                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    activeFocusOnTab: true
                    border.width: activeFocus ? 1 : 0
                    border.color: Kirigami.Theme.highlightColor
                    radius: 4
                    color: isActive ? dialog.selectionBackgroundColor :
                    (catHover.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r,
                                                      Kirigami.Theme.textColor.g,
                                                      Kirigami.Theme.textColor.b, 0.08)
                                            : "transparent")

                    Rectangle {
                        visible: btn.isActive
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
                        text: btn.text
                        color: Kirigami.Theme.textColor
                        font.pixelSize: 14
                        font.weight: btn.isActive ? Font.DemiBold : Font.Normal
                    }

                    Keys.onSpacePressed: (event) => {
                        btn.clicked();
                        event.accepted = true;
                    }
                    Keys.onReturnPressed: (event) => {
                        btn.clicked();
                        event.accepted = true;
                    }
                    Keys.onEnterPressed: (event) => {
                        btn.clicked();
                        event.accepted = true;
                    }

                    MouseArea {
                        id: catHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            btn.forceActiveFocus();
                            btn.clicked();
                        }
                    }
                }

                CategoryButton {
                    text: qsTranslate("PlaylistPanel", "playlist_watch_folders")
                    isActive: dialog.currentCategory === "watched"
                    visible: !dialog.isDefault
                    onClicked: dialog.currentCategory = "watched"
                }
                CategoryButton {
                    text: qsTranslate("SettingsDialog", "settings_playback")
                    isActive: dialog.currentCategory === "playback"
                    onClicked: dialog.currentCategory = "playback"
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ---- Watched folders (named playlists) ----
        Flickable {
            id: foldersFlickable
            visible: dialog.currentCategory === "watched" && !dialog.isDefault
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            clip: true
            contentWidth: width
            contentHeight: foldersColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            FocusScroller { flickable: foldersFlickable }

            Item {
                width: parent.width - 22
                implicitHeight: foldersColumn.implicitHeight

                ColumnLayout {
                    id: foldersColumn
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

                    Button {
                        Layout.alignment: Qt.AlignLeft
                        text: qsTranslate("PlaylistPanel", "playlist_add_watch_folder")
                        onClicked: Playlists.openAddWatchFolderDialog(dialog.playlistId)
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

        // ---- Playback ----
        Flickable {
            id: playbackFlickable
            visible: dialog.currentCategory === "playback"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            clip: true
            contentWidth: width
            contentHeight: playbackColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            FocusScroller { flickable: playbackFlickable }

            Item {
                width: parent.width - 22
                implicitHeight: playbackColumn.implicitHeight

                ColumnLayout {
                    id: playbackColumn
                    width: parent.width
                    spacing: 22

                    Label {
                        Layout.fillWidth: true
                        text: qsTranslate("SettingsDialog", "settings_playback")
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

                    GridLayout {
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 16
                        Layout.fillWidth: true

                        Label {
                            visible: dialog.isDefault
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            text: qsTranslate("SettingsDialog", "settings_append_playlist")
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                        Item {
                            visible: dialog.isDefault
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: 40
                            Switch {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                checked: Config.appendToPlaylist
                                onToggled: Config.appendToPlaylist = checked
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            text: qsTranslate("PlaylistPanel", "playlist_playback_mode")
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                        Item {
                            Layout.preferredWidth: 200
                            Layout.preferredHeight: 40
                            ComboBox {
                                anchors.fill: parent
                                font.pixelSize: 14
                                model: [
                                    { text: qsTranslate("ContextMenu", "playback_sequential"), value: "sequential" },
                                    { text: qsTranslate("ContextMenu", "playback_repeat"), value: "repeat" },
                                    { text: qsTranslate("ContextMenu", "playback_shuffle"), value: "shuffle" },
                                    { text: qsTranslate("ContextMenu", "playback_stop"), value: "stop" }
                                ]
                                textRole: "text"; valueRole: "value"
                                currentIndex: {
                                    const idx = model.findIndex(item => item.value === dialog.playlistMode);
                                    return idx !== -1 ? idx : 0;
                                }
                                onActivated: (index) => {
                                    dialog.playlistMode = model[index].value;
                                    Playlists.setPlaylistMode(dialog.playlistId, dialog.playlistMode);
                                }
                            }
                        }

                        Label {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            text: qsTranslate("PlaylistPanel", "playlist_mode_hint")
                            color: Qt.rgba(Kirigami.Theme.textColor.r,
                                           Kirigami.Theme.textColor.g,
                                           Kirigami.Theme.textColor.b, 0.65)
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
