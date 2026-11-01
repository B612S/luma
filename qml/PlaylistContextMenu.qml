import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Menu {
    id: trackContextMenu
    width: 220
    modal: false
    z: 1000

    property int trackIndex: -1
    // Свойство НЕ должно называться panel: оно перекрывает id panel из
    // PlaylistPanel.qml, и привязка `panel: panel` становится самоприсваиванием
    // (свойство навсегда остаётся null).
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

        // При смене файла mpv на мгновение останавливает вывод кадров, а меню
        // рисуется тем же потоком рендера — оно «зависает» недорисованным.
        // Поэтому сначала даём меню закрыться и только потом запускаем файл.
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
    MenuItem { text: qsTranslate("PlaylistPanel", "playlist_shuffle"); visible: Media.playMode === "shuffle"; onTriggered: { if (trackContextMenu.playlistPanel && trackContextMenu.playlistPanel.model) trackContextMenu.playlistPanel.model.setShuffle(true); } }
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
