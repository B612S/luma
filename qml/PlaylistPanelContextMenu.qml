import QtQuick
import QtQuick.Controls

// Clear-playlist menu for the body of the playlist panel. Not used at the
// moment (the tab menu has the same action), but kept in the same style as the
// other in-app menus.
LumaMenu {
    id: playlistContextMenu
    width: 220

    // The property must NOT be named panel (see PlaylistContextMenu.qml).
    property var playlistPanel: null

    Column {
        width: parent.width
        spacing: 2

        LumaMenuButton {
            text: qsTranslate("PlaylistPanel", "playlist_clear")
            destructive: true
            onClicked: {
                playlistContextMenu.close();
                if (playlistContextMenu.playlistPanel)
                    playlistContextMenu.playlistPanel.openClearDialog(Playlists.activeId,
                                                                     Playlists.playlistName(Playlists.activeId));
            }
        }
    }
}
