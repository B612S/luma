import QtQuick
import QtQuick.Controls

// Right-click menu of a playlist entry. Uses the same custom popup as the
// player's main menu (LumaMenu/ContextMenu.qml), with the playlist actions on
// one page and the "add to playlist" list on the next one.
LumaMenu {
    id: trackContextMenu
    width: Math.min(220, menuAvailableWidth)

    property int trackIndex: -1
    // The property must NOT be named panel: it shadows the id panel from
    // PlaylistPanel.qml, and the binding `panel: panel` becomes a self-assignment
    // (the property stays null forever).
    property var playlistPanel: null
    property int pendingPlayIndex: -1

    function trackPath() {
        return (playlistPanel && playlistPanel.model)
                ? playlistPanel.model.fileAt(trackIndex) : "";
    }

    // Always start from the first page when the menu is opened again.
    onAboutToShow: currentPage = "main"

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

    Column {
        width: parent.width
        spacing: 2

        // --- TRACK PAGE ---
        Column {
            visible: trackContextMenu.currentPage === "main"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                text: qsTranslate("PlaylistPanel", "playlist_play")
                onClicked: {
                    trackContextMenu.pendingPlayIndex = trackContextMenu.trackIndex;
                    trackContextMenu.close();
                    playStartTimer.start();
                }
            }

            // Launching a second player only makes sense when this instance
            // does not own the application's single-instance server.
            LumaMenuButton {
                text: qsTranslate("PlaylistPanel", "playlist_play_new_window")
                visible: typeof Config !== "undefined" && Config
                         && !Config.singleInstance
                onClicked: {
                    const path = trackContextMenu.trackPath();
                    if (!path) return;
                    trackContextMenu.close();
                    Media.openInNewWindow(path);
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("PlaylistPanel", "playlist_add_to")
                hasArrow: true
                onClicked: trackContextMenu.showPage("add_to")
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("PlaylistPanel", "playlist_info")
                // A deleted file has no metadata to show, so the information
                // dialog would be an empty window.
                visible: {
                    const path = trackContextMenu.trackPath();
                    return path !== "" && Media.isFileAvailable(path);
                }
                onClicked: {
                    const path = trackContextMenu.trackPath();
                    if (!path) return;
                    trackContextMenu.close();
                    if (trackContextMenu.playlistPanel)
                        trackContextMenu.playlistPanel.openInfoDialog(path);
                }
            }

            LumaMenuButton {
                text: qsTranslate("PlaylistPanel", "playlist_open_folder").arg(Media.defaultFileManagerName)
                onClicked: {
                    const path = trackContextMenu.trackPath();
                    if (!path) return;
                    trackContextMenu.close();
                    Media.openInFileManager(path);
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("PlaylistPanel", "playlist_remove")
                destructive: true
                onClicked: {
                    const index = trackContextMenu.trackIndex;
                    trackContextMenu.close();
                    Media.removeFromPlaylist(index);
                }
            }
        }

        // --- ADD TO PLAYLIST PAGE ---
        Column {
            visible: trackContextMenu.currentPage === "add_to"
            width: parent.width
            spacing: 2

            // Scrollable so a long playlist list cannot grow past the window.
            // The height follows the window size while the menu is open.
            ListView {
                id: addToPlaylistList
                width: parent.width
                height: Math.min(contentHeight,
                                 Math.max(40, trackContextMenu.menuMaxContentHeight - 40))
                implicitHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                spacing: 2
                model: Playlists.playlists

                ScrollBar.vertical: ScrollBar {
                    id: addToScroll
                    policy: ScrollBar.AsNeeded
                }

                delegate: LumaMenuButton {
                    width: addToPlaylistList.width
                           - (addToScroll.visible ? addToScroll.width : 0)
                    text: modelData.name
                    isEnabled: modelData.id !== Playlists.activeId
                    onClicked: {
                        const path = trackContextMenu.trackPath();
                        if (path) Playlists.addFilesToPlaylist(modelData.id, [path]);
                        trackContextMenu.close();
                    }
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("ContextMenu", "menu_back")
                onClicked: trackContextMenu.showPage("main")
            }
        }
    }
}
