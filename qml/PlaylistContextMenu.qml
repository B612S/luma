import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Menu {
    id: trackContextMenu
    width: 220
    modal: false
    z: 1000

    property int trackIndex: -1
    // The property must NOT be named panel: it shadows the id panel from
    // PlaylistPanel.qml, and the binding `panel: panel` becomes a self-assignment
    // (the property stays null forever).
    property var playlistPanel: null
    property int pendingPlayIndex: -1

    onOpened: {
        if (playlistPanel && playlistPanel.rootContextMenu) playlistPanel.rootContextMenu.close();
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8; border.color: "transparent"
    }

    MenuItem {
        id: playItem
        text: qsTranslate("PlaylistPanel", "playlist_play")

        // When the file changes, mpv briefly stops rendering frames, and the menu
        // is drawn on the same render thread — it "freezes" half-drawn.
        // So let the menu close first and only then start the file.
        Timer {
            id: playStartTimer
            interval: 200
            repeat: false
            onTriggered: {
                if (trackContextMenu.pendingPlayIndex < 0) return;
                const index = trackContextMenu.pendingPlayIndex;
                trackContextMenu.pendingPlayIndex = -1;
                Media.playIndex(index);
            }
        }

        onTriggered: {
            trackContextMenu.pendingPlayIndex = trackContextMenu.trackIndex;
            playStartTimer.start();
        }
    }
    MenuSeparator {}

    Menu {
        id: addToPlaylistSubmenu
        title: qsTranslate("PlaylistPanel", "playlist_add_to")
        Instantiator {
            model: Playlists.playlists
            delegate: MenuItem {
                text: modelData.name
                enabled: modelData.id !== Playlists.activeId
                onTriggered: {
                    let path = trackContextMenu.playlistPanel && trackContextMenu.playlistPanel.model ? trackContextMenu.playlistPanel.model.fileAt(trackContextMenu.trackIndex) : "";
                    if (path) Playlists.addFilesToPlaylist(modelData.id, [path]);
                }
            }
            onObjectAdded: (index, object) => addToPlaylistSubmenu.insertItem(index, object)
            onObjectRemoved: (index, object) => addToPlaylistSubmenu.removeItem(object)
        }
    }

    MenuSeparator {}

    MenuItem {
        text: qsTranslate("PlaylistPanel", "playlist_info")
        onTriggered: {
            let path = trackContextMenu.playlistPanel && trackContextMenu.playlistPanel.model ? trackContextMenu.playlistPanel.model.fileAt(trackContextMenu.trackIndex) : "";
            if (!path) return;
            if (trackContextMenu.playlistPanel) trackContextMenu.playlistPanel.openInfoDialog(path);
        }
    }

    MenuItem {
        text: qsTranslate("PlaylistPanel", "playlist_open_folder").arg(Media.getDefaultFileManagerName())
        onTriggered: {
            let path = trackContextMenu.playlistPanel && trackContextMenu.playlistPanel.model ? trackContextMenu.playlistPanel.model.fileAt(trackContextMenu.trackIndex) : "";
            if (path) Media.openInFileManager(path);
        }
    }

    MenuSeparator {}

    MenuItem {
        text: qsTranslate("PlaylistPanel", "playlist_remove")
        contentItem: Text {
            text: parent.text; color: Kirigami.Theme.negativeTextColor
            font.pixelSize: 14; verticalAlignment: Text.AlignVCenter; leftPadding: 8
        }
        onTriggered: Media.removeFromPlaylist(trackContextMenu.trackIndex)
    }
}
