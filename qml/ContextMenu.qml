import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Main player menu. Styled by LumaMenu, like every other in-app context menu.
LumaMenu {
    id: menu
    width: 240
    parent: Overlay.overlay

    property var tracksList: []
    property int currentTrackId: -1
    property var mpv: null
    property string currentAspect: "original"
    property bool hasVideo: false

    signal toggleCropRequested()
    signal togglePlaylistRequested()
    signal togglePipRequested()
    signal fitWindowRequested()
    signal openUrlRequested()

    onOpened: {
        hasVideo = Media.hasVideo();
    }

    onCurrentPageChanged: {
        if (currentPage === "aspect_ratio") {
            currentAspect = Media.getAspectRatio()
        }
        if (currentPage === "renderer") {
            CastManager.discovery.refresh()
        }
    }

    Column {
        id: contentColumn
        width: parent.width
        spacing: 2

        // --- MAIN PAGE ---
        Column {
            visible: menu.currentPage === "main"
            width: parent.width
            spacing: 2

            LumaMenuButton { text: qsTr("menu_open"); hasArrow: true; onClicked: menu.showPage("open") }

            LumaMenuButton {
                text: qsTr("menu_audio"); hasArrow: true; isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false
                onClicked: {
                    menu.tracksList = Media.getTracks("audio")
                    menu.currentTrackId = Media.currentTrackId("aid")
                    menu.showPage("audio")
                }
            }

            LumaMenuButton {
                text: qsTr("menu_video"); hasArrow: true; isEnabled: menu.hasVideo
                onClicked: {
                    menu.tracksList = Media.getTracks("video")
                    menu.currentTrackId = Media.currentTrackId("vid")
                    menu.showPage("video")
                }
            }

            LumaMenuButton {
                text: qsTr("menu_subtitles"); hasArrow: true; isEnabled: menu.hasVideo
                onClicked: {
                    menu.tracksList = Media.getTracks("sub")
                    menu.currentTrackId = Media.currentTrackId("sid")
                    menu.showPage("sub")
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_playback"); hasArrow: true; isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false; onClicked: menu.showPage("playback") }

            LumaMenuSeparator {}

            LumaMenuButton {
                text: qsTr("menu_playlist")
                onClicked: { menu.togglePlaylistRequested(); menu.close() }
            }

            LumaMenuButton {
                text: qsTr("menu_pip")
                isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false
                onClicked: { menu.togglePipRequested(); menu.close() }
            }

            LumaMenuButton { text: qsTr("menu_stats"); onClicked: { statsPanel.isOpen = !statsPanel.isOpen; menu.close() } }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_settings"); onClicked: { menu.close(); settingsDialog.open() } }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_exit"); onClicked: Qt.quit() }
        }

        // --- "OPEN" PAGE ---
        Column {
            visible: menu.currentPage === "open"
            width: parent.width
            spacing: 2

            // A single button for all media files
            LumaMenuButton { text: qsTr("menu_open_file"); onClicked: { menu.close(); Media.openFileDialog() } }
            LumaMenuButton { text: qsTr("menu_open_folder"); onClicked: { menu.close(); Media.openFolderDialog() } }
            LumaMenuButton { text: qsTr("menu_open_url"); onClicked: { menu.close(); menu.openUrlRequested() } }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_back"); onClicked: menu.showPage("main") }
        }

        // --- TRACKS PAGE ---
        Column {
            visible: menu.currentPage === "audio" || menu.currentPage === "sub" || menu.currentPage === "video"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                visible: menu.currentPage === "sub" || menu.currentPage === "audio"
                text: menu.currentPage === "sub" ? qsTr("menu_open_sub_file") : qsTr("menu_open_audio_file")
                hasArrow: true
                onClicked: {
                    menu.close()
                    if (menu.currentPage === "sub") Media.openSubtitlesDialog()
                        else Media.openAudioDialog()
                }
            }

            Repeater {
                model: menu.tracksList
                LumaMenuButton {
                    id: trackBtn
                    readonly property int trackId: modelData.id
                    readonly property bool isActive: trackId === menu.currentTrackId
                    text: {
                        let title = modelData.title
                        if (modelData.lang) title += " (" + modelData.lang + ")"
                        return title
                    }
                    checked: isActive
                    onClicked: {
                        let typeStr = menu.currentPage === "audio" ? "aid" : (menu.currentPage === "sub" ? "sid" : "vid")
                        if (trackBtn.isActive) Media.setTrackOff(typeStr)
                        else Media.setTrack(typeStr, trackBtn.trackId)
                        menu.close()
                    }
                }
            }

            LumaMenuSeparator {
                visible: menu.tracksList.length > 0 || menu.currentPage === "sub" || menu.currentPage === "audio"
            }

            LumaMenuButton { text: qsTr("menu_back"); onClicked: menu.showPage("main") }
        }

        // --- PLAYBACK PAGE ---
        Column {
            visible: menu.currentPage === "playback"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                text: qsTr("menu_previous_chapter")
                isEnabled: menu.mpv !== null && menu.mpv.chapterList !== undefined
                           && menu.mpv.chapterList.length > 0
                onClicked: { Media.previousChapter(); menu.close() }
            }
            LumaMenuButton {
                text: qsTr("menu_next_chapter")
                isEnabled: menu.mpv !== null && menu.mpv.chapterList !== undefined
                           && menu.mpv.chapterList.length > 0
                onClicked: { Media.nextChapter(); menu.close() }
            }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_playback_mode"); hasArrow: true; onClicked: menu.showPage("playback_mode") }
            LumaMenuButton { text: qsTr("menu_aspect_ratio"); hasArrow: true; isEnabled: menu.hasVideo; onClicked: menu.showPage("aspect_ratio") }
            LumaMenuButton {
                text: qsTr("menu_fit_window")
                isEnabled: menu.hasVideo
                onClicked: { menu.fitWindowRequested(); menu.close() }
            }
            LumaMenuButton { text: qsTr("menu_renderer"); hasArrow: true; onClicked: menu.showPage("renderer") }

            LumaMenuButton {
                text: (menu.mpv && menu.mpv.cropEnabled ? "✓ " : "") + qsTr("menu_crop")
                isEnabled: menu.hasVideo
                onClicked: { menu.toggleCropRequested(); menu.close() }
            }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_back"); onClicked: menu.showPage("main") }
        }

        // --- PLAYBACK MODE PAGE ---
        Column {
            visible: menu.currentPage === "playback_mode"
            width: parent.width
            spacing: 2

            // The mode belongs to the active playlist: Default takes it from
            // the settings, the named ones store their own.
            Label {
                width: parent.width
                height: 26
                leftPadding: 12
                rightPadding: 12
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                text: qsTranslate("ContextMenu", "playback_mode_for")
                      .arg(Playlists.playlistName(Playlists.activeId))
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.65)
                font.pixelSize: 12
            }

            LumaMenuButton {
                text: (Media.playMode === "sequential" ? "✓ " : "") + qsTr("playback_sequential")
                onClicked: {
                    Media.playMode = "sequential";
                    Osd.showInfo(qsTr("osd_playback_mode").arg(qsTr("playback_sequential")));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (Media.playMode === "repeat" ? "✓ " : "") + qsTr("playback_repeat")
                onClicked: {
                    Media.playMode = "repeat";
                    Osd.showInfo(qsTr("osd_playback_mode").arg(qsTr("playback_repeat")));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (Media.playMode === "shuffle" ? "✓ " : "") + qsTr("playback_shuffle")
                onClicked: {
                    Media.playMode = "shuffle";
                    Osd.showInfo(qsTr("osd_playback_mode").arg(qsTr("playback_shuffle")));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (Media.playMode === "stop" ? "✓ " : "") + qsTr("playback_stop")
                onClicked: {
                    Media.playMode = "stop";
                    Osd.showInfo(qsTr("osd_playback_mode").arg(qsTr("playback_stop")));
                    menu.close()
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_back"); onClicked: menu.showPage("playback") }
        }

        // --- ASPECT RATIO PAGE ---
        Column {
            visible: menu.currentPage === "aspect_ratio"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                text: (menu.currentAspect === "original" ? "✓ " : "") + qsTr("aspect_original")
                onClicked: {
                    Media.setAspectRatio("original");
                    menu.currentAspect = "original";
                    Osd.showInfo(qsTr("osd_aspect").arg(qsTr("aspect_original")));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (menu.currentAspect === "16:9" ? "✓ " : "") + "16:9"
                onClicked: {
                    Media.setAspectRatio("16:9");
                    menu.currentAspect = "16:9";
                    Osd.showInfo(qsTr("osd_aspect").arg("16:9"));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (menu.currentAspect === "4:3" ? "✓ " : "") + "4:3"
                onClicked: {
                    Media.setAspectRatio("4:3");
                    menu.currentAspect = "4:3";
                    Osd.showInfo(qsTr("osd_aspect").arg("4:3"));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (menu.currentAspect === "21:9" ? "✓ " : "") + "21:9"
                onClicked: {
                    Media.setAspectRatio("21:9");
                    menu.currentAspect = "21:9";
                    Osd.showInfo(qsTr("osd_aspect").arg("21:9"));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (menu.currentAspect === "1:1" ? "✓ " : "") + "1:1"
                onClicked: {
                    Media.setAspectRatio("1:1");
                    menu.currentAspect = "1:1";
                    Osd.showInfo(qsTr("osd_aspect").arg("1:1"));
                    menu.close()
                }
            }
            LumaMenuButton {
                text: (menu.currentAspect === "9:16" ? "✓ " : "") + "9:16"
                onClicked: {
                    Media.setAspectRatio("9:16");
                    menu.currentAspect = "9:16";
                    Osd.showInfo(qsTr("osd_aspect").arg("9:16"));
                    menu.close()
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_back"); onClicked: menu.showPage("playback") }
        }

        // --- RENDERER (CHROMECAST) PAGE ---
        Column {
            visible: menu.currentPage === "renderer"
            width: parent.width
            spacing: 2

            LumaMenuButton {
                text: CastManager.casting ? qsTr("menu_stop_cast") : qsTr("menu_refresh_devices")
                onClicked: {
                    if (CastManager.casting) {
                        CastManager.stopCast()
                        Osd.showInfo(qsTr("osd_cast_stopped"))
                    } else {
                        CastManager.discovery.refresh()
                    }
                }
            }

            LumaMenuSeparator {}

            LumaMenuButton {
                visible: CastManager.discovery.scanning
                text: qsTr("menu_searching_devices")
                isEnabled: false
            }

            Repeater {
                model: CastManager.discovery.devices
                LumaMenuButton {
                    text: modelData.name
                    tooltipText: modelData.address
                    onClicked: {
                        let filePath = Media.playlist.currentFile()
                        if (!filePath) {
                            Osd.showInfo(qsTr("osd_cast_no_file"))
                            return
                        }
                        CastManager.castTo(modelData.address, modelData.port, filePath)
                        Osd.showInfo(qsTr("osd_cast_started").arg(modelData.name))
                        menu.close()
                    }
                }
            }

            LumaMenuButton {
                visible: !CastManager.discovery.scanning && CastManager.discovery.devices.length === 0
                text: qsTr("menu_no_devices")
                isEnabled: false
            }

            LumaMenuSeparator { visible: CastManager.casting }

            LumaMenuButton {
                visible: CastManager.casting
                text: "✓ " + qsTr("menu_casting")
                isEnabled: false
            }

            LumaMenuSeparator {}

            LumaMenuButton { text: qsTr("menu_back"); onClicked: menu.showPage("playback") }
        }
    }
}
