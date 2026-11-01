import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Menu {
    id: tabContextMenu
    width: 200
    modal: false
    z: 1000

    property string targetId: ""
    property string targetName: ""
    property bool targetIsDefault: false
    // App main menu, so it can be closed when the tab menu opens.
    property var rootContextMenu: null

    signal addFilesRequested(string id)
    signal addFolderRequested(string id)
    signal addUrlRequested(string id)
    signal clearRequested(string id)
    signal renameRequested(string id, string name)
    signal deleteRequested(string id, string name)

    onOpened: {
        if (rootContextMenu) rootContextMenu.close();
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8; border.color: "transparent"
    }

    MenuItem { text: qsTranslate("PlaylistTabs", "tabs_add_file"); onTriggered: tabContextMenu.addFilesRequested(tabContextMenu.targetId) }
    MenuItem { text: qsTranslate("PlaylistTabs", "tabs_add_folder"); onTriggered: tabContextMenu.addFolderRequested(tabContextMenu.targetId) }
    MenuItem { text: qsTranslate("PlaylistTabs", "tabs_add_url"); onTriggered: tabContextMenu.addUrlRequested(tabContextMenu.targetId) }
    MenuSeparator {}

    Menu {
        id: sortMenu
        title: qsTranslate("PlaylistTabs", "tabs_sort")
        enabled: tabContextMenu.targetId.length > 0

        MenuItem {
            text: qsTranslate("PlaylistTabs", "tabs_sort_name_asc")
            onTriggered: Playlists.sortPlaylist(tabContextMenu.targetId, "name", true)
        }
        MenuItem {
            text: qsTranslate("PlaylistTabs", "tabs_sort_name_desc")
            onTriggered: Playlists.sortPlaylist(tabContextMenu.targetId, "name", false)
        }
        MenuItem {
            text: qsTranslate("PlaylistTabs", "tabs_shuffle")
            onTriggered: Playlists.shufflePlaylist(tabContextMenu.targetId)
        }
        MenuSeparator { visible: Playlists.playlistHasAudio(tabContextMenu.targetId) }
        MenuItem {
            text: qsTranslate("PlaylistTabs", "tabs_sort_artist")
            visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
            onTriggered: Playlists.sortPlaylist(tabContextMenu.targetId, "artist", true)
        }
        MenuItem {
            text: qsTranslate("PlaylistTabs", "tabs_sort_year")
            visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
            onTriggered: Playlists.sortPlaylist(tabContextMenu.targetId, "year", true)
        }
        MenuItem {
            text: qsTranslate("PlaylistTabs", "tabs_sort_album")
            visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
            onTriggered: Playlists.sortPlaylist(tabContextMenu.targetId, "album", true)
        }
    }

    MenuSeparator {}
    MenuItem {
        text: qsTranslate("PlaylistTabs", "tabs_rename"); enabled: !tabContextMenu.targetIsDefault
        onTriggered: tabContextMenu.renameRequested(tabContextMenu.targetId, tabContextMenu.targetName)
    }
    MenuItem {
        text: qsTranslate("PlaylistTabs", "tabs_clear")
        contentItem: Text {
            text: parent.text
            color: Kirigami.Theme.negativeTextColor
            font.pixelSize: 14; verticalAlignment: Text.AlignVCenter; leftPadding: 8
        }
        onTriggered: tabContextMenu.clearRequested(tabContextMenu.targetId)
    }
    MenuItem {
        text: qsTranslate("PlaylistTabs", "tabs_delete"); enabled: !tabContextMenu.targetIsDefault
        contentItem: Text {
            text: parent.text
            color: parent.enabled ? Kirigami.Theme.negativeTextColor : Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.4)
            font.pixelSize: 14; verticalAlignment: Text.AlignVCenter; leftPadding: 8
        }
        onTriggered: tabContextMenu.deleteRequested(tabContextMenu.targetId, tabContextMenu.targetName)
    }
}
