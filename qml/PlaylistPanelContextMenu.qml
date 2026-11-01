import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Menu {
    id: playlistContextMenu
    width: 220
    modal: false
    z: 1000

    // Свойство НЕ должно называться panel (см. PlaylistContextMenu.qml).
    property var playlistPanel: null

    onOpened: {
        if (playlistPanel && playlistPanel.rootContextMenu) playlistPanel.rootContextMenu.close();
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8; border.color: "transparent"
    }

    MenuItem {
        text: qsTranslate("PlaylistPanel", "playlist_clear")
        contentItem: Text {
            text: parent.text; color: Kirigami.Theme.negativeTextColor
            font.pixelSize: 14; verticalAlignment: Text.AlignVCenter; leftPadding: 8
        }
        onTriggered: {
            if (playlistContextMenu.playlistPanel)
                playlistContextMenu.playlistPanel.openClearDialog(Playlists.activeId, Playlists.playlistName(Playlists.activeId));
        }
    }
}
