import QtQuick
import QtQuick.Controls

// Right-click menu of a playlist tab. Uses the same custom popup as the
// player's main menu (LumaMenu/ContextMenu.qml), with the sorting actions on a
// second page.
LumaMenu {
    id: tabContextMenu
    width: Math.min(220, menuAvailableWidth)

    property string targetId: ""
    property string targetName: ""
    property bool targetIsDefault: false

    signal addFilesRequested(string id)
    signal addFolderRequested(string id)
    signal propertiesRequested(string id)
    signal addUrlRequested(string id)
    signal clearRequested(string id)
    signal renameRequested(string id, string name)
    signal deleteRequested(string id, string name)

    // Always start from the first page when the menu is opened again.
    onAboutToShow: currentPage = "main"

    Column {
        width: parent.width
        spacing: 2

        // --- TAB PAGE ---
        Column {
            visible: tabContextMenu.currentPage === "main"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_add_file")
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.addFilesRequested(tabContextMenu.targetId);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_add_folder")
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.addFolderRequested(tabContextMenu.targetId);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_add_url")
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.addUrlRequested(tabContextMenu.targetId);
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_sort")
                hasArrow: true
                isEnabled: tabContextMenu.targetId.length > 0
                onClicked: tabContextMenu.showPage("sort")
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_properties")
                // The Default playlist is not saved and has no properties.
                visible: !tabContextMenu.targetIsDefault
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.propertiesRequested(tabContextMenu.targetId);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_rename")
                // The Default playlist cannot be renamed or deleted; hide the
                // actions instead of showing them permanently disabled.
                visible: !tabContextMenu.targetIsDefault
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.renameRequested(tabContextMenu.targetId, tabContextMenu.targetName);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_clear")
                destructive: true
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.clearRequested(tabContextMenu.targetId);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_delete")
                destructive: true
                visible: !tabContextMenu.targetIsDefault
                onClicked: {
                    tabContextMenu.close();
                    tabContextMenu.deleteRequested(tabContextMenu.targetId, tabContextMenu.targetName);
                }
            }
        }

        // --- SORT PAGE ---
        Column {
            visible: tabContextMenu.currentPage === "sort"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_sort_name_asc")
                onClicked: {
                    tabContextMenu.close();
                    Playlists.sortPlaylist(tabContextMenu.targetId, "name", true);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_sort_name_desc")
                onClicked: {
                    tabContextMenu.close();
                    Playlists.sortPlaylist(tabContextMenu.targetId, "name", false);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_shuffle")
                onClicked: {
                    tabContextMenu.close();
                    Playlists.shufflePlaylist(tabContextMenu.targetId);
                }
            }

            LumaMenuSeparator {
                visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
            }

            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_sort_artist")
                visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
                onClicked: {
                    tabContextMenu.close();
                    Playlists.sortPlaylist(tabContextMenu.targetId, "artist", true);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_sort_year")
                visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
                onClicked: {
                    tabContextMenu.close();
                    Playlists.sortPlaylist(tabContextMenu.targetId, "year", true);
                }
            }
            LumaMenuButton {
                text: qsTranslate("PlaylistTabs", "tabs_sort_album")
                visible: Playlists.playlistHasAudio(tabContextMenu.targetId)
                onClicked: {
                    tabContextMenu.close();
                    Playlists.sortPlaylist(tabContextMenu.targetId, "album", true);
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTranslate("ContextMenu", "menu_back")
                onClicked: tabContextMenu.showPage("main")
            }
        }
    }
}
