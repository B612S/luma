import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Popup {
    id: menu
    width: 240
    padding: 8
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 150; easing.type: Easing.InOutQuad }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 150; easing.type: Easing.InOutQuad }
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }

    property string currentPage: "main"
    property var tracksList: []
    property int currentTrackId: -1
    property var mpv: null
    property string currentAspect: "original"

    signal toggleCropRequested()

    onCurrentPageChanged: {
        if (currentPage === "aspect_ratio") {
            currentAspect = Media.getAspectRatio()
        }

        pageFadeAnim.stop()
        menu.opacity = 0.0
        pageFadeAnim.start()
    }

    NumberAnimation {
        id: pageFadeAnim
        target: menu
        property: "opacity"
        from: 0.0
        to: 1.0
        duration: 150
        easing.type: Easing.InOutQuad
    }

    component MenuButton: Rectangle {
        id: btn
        property string text: ""
        property bool hasArrow: false
        property bool isEnabled: true
        signal clicked()

        width: parent.width
        height: 32
        color: (hoverArea.containsMouse && isEnabled) ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1) : "transparent"
        radius: 4

        Text {
            text: btn.text
            color: btn.isEnabled ? Kirigami.Theme.textColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
            font.pixelSize: 14
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
        }

        Text {
            text: ">"
            visible: btn.hasArrow
            color: btn.isEnabled ? Kirigami.Theme.textColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
            font.pixelSize: 14
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
        }

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: { if (btn.isEnabled) btn.clicked() }
        }
    }

    Column {
        id: contentColumn
        width: parent.width
        spacing: 2

        // --- ГЛАВНАЯ СТРАНИЦА ---
        Column {
            visible: menu.currentPage === "main"
            width: parent.width
            spacing: 2

            MenuButton { text: qsTr("menu_open"); hasArrow: true; onClicked: menu.currentPage = "open" }

            MenuButton {
                text: qsTr("menu_audio"); hasArrow: true; isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false
                onClicked: {
                    menu.tracksList = Media.getTracks("audio")
                    menu.currentTrackId = Media.currentTrackId("aid")
                    menu.currentPage = "audio"
                }
            }

            MenuButton {
                text: qsTr("menu_video"); hasArrow: true; isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false
                onClicked: {
                    menu.tracksList = Media.getTracks("video")
                    menu.currentTrackId = Media.currentTrackId("vid")
                    menu.currentPage = "video"
                }
            }

            MenuButton {
                text: qsTr("menu_subtitles"); hasArrow: true; isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false
                onClicked: {
                    menu.tracksList = Media.getTracks("sub")
                    menu.currentTrackId = Media.currentTrackId("sid")
                    menu.currentPage = "sub"
                }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_playback"); hasArrow: true; isEnabled: menu.mpv ? (menu.mpv.duration > 0) : false; onClicked: menu.currentPage = "playback" }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_stats"); onClicked: { statsPanel.isOpen = !statsPanel.isOpen; menu.close() } }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_settings"); onClicked: { menu.close(); settingsDialog.open() } }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_exit"); onClicked: Qt.quit() }
        }

        // --- СТРАНИЦА "ОТКРЫТЬ" ---
        Column {
            visible: menu.currentPage === "open"
            width: parent.width
            spacing: 2

            MenuButton { text: qsTr("menu_open_file"); onClicked: { menu.close(); Media.openFileDialog() } }
            MenuButton { text: qsTr("menu_open_folder"); onClicked: { menu.close(); Media.openFolderDialog() } }
            MenuButton { text: qsTr("menu_open_url"); onClicked: { menu.close(); Media.openUrlDialog() } }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_back"); onClicked: menu.currentPage = "main" }
        }

        // --- СТРАНИЦА ДОРОЖЕК ---
        Column {
            visible: menu.currentPage === "audio" || menu.currentPage === "sub" || menu.currentPage === "video"
            width: parent.width
            spacing: 2

            MenuButton {
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
                Rectangle {
                    id: trackBtn
                    width: parent.width
                    height: 32
                    color: trackHoverArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1) : "transparent"
                    radius: 4

                    property int trackId: modelData.id
                    property bool isActive: trackId === menu.currentTrackId
                    property string trackTitle: {
                        let title = modelData.title
                        if (modelData.lang) title += " (" + modelData.lang + ")"
                            return title
                    }

                    Text {
                        text: (trackBtn.isActive ? "✓ " : "") + trackBtn.trackTitle
                        color: Kirigami.Theme.textColor
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        elide: Text.ElideRight
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                    }

                    MouseArea {
                        id: trackHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            let typeStr = menu.currentPage === "audio" ? "aid" : (menu.currentPage === "sub" ? "sid" : "vid")
                            if (trackBtn.isActive) Media.setTrackOff(typeStr)
                                else Media.setTrack(typeStr, trackBtn.trackId)
                                    menu.close()
                        }
                    }
                }
            }

            Rectangle {
                visible: menu.tracksList.length > 0 || menu.currentPage === "sub" || menu.currentPage === "audio"
                width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
            }

            MenuButton { text: qsTr("menu_back"); onClicked: menu.currentPage = "main" }
        }

        // --- СТРАНИЦА PLAYBACK ---
        Column {
            visible: menu.currentPage === "playback"
            width: parent.width
            spacing: 2

            MenuButton { text: qsTr("menu_playback_mode"); hasArrow: true; onClicked: menu.currentPage = "playback_mode" }
            MenuButton { text: qsTr("menu_aspect_ratio"); hasArrow: true; onClicked: menu.currentPage = "aspect_ratio" }

            MenuButton {
                text: (menu.mpv && menu.mpv.cropEnabled ? "✓ " : "") + qsTr("menu_crop")
                onClicked: { menu.toggleCropRequested(); menu.close() }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_back"); onClicked: menu.currentPage = "main" }
        }

        // --- СТРАНИЦА РЕЖИМА ВОСПРОИЗВЕДЕНИЯ (Только для текущей сессии) ---
        Column {
            visible: menu.currentPage === "playback_mode"
            width: parent.width
            spacing: 2

            MenuButton {
                text: (Media.playMode === "sequential" ? "✓ " : "") + qsTr("playback_sequential")
                onClicked: {
                    Media.playMode = "sequential";
                    Osd.showInfo(qsTr("osd_playback_mode") + ": " + qsTr("playback_sequential"));
                    menu.close()
                }
            }
            MenuButton {
                text: (Media.playMode === "repeat" ? "✓ " : "") + qsTr("playback_repeat")
                onClicked: {
                    Media.playMode = "repeat";
                    Osd.showInfo(qsTr("osd_playback_mode") + ": " + qsTr("playback_repeat"));
                    menu.close()
                }
            }
            MenuButton {
                text: (Media.playMode === "shuffle" ? "✓ " : "") + qsTr("playback_shuffle")
                onClicked: {
                    Media.playMode = "shuffle";
                    Osd.showInfo(qsTr("osd_playback_mode") + ": " + qsTr("playback_shuffle"));
                    menu.close()
                }
            }
            MenuButton {
                text: (Media.playMode === "stop" ? "✓ " : "") + qsTr("playback_stop")
                onClicked: {
                    Media.playMode = "stop";
                    Osd.showInfo(qsTr("osd_playback_mode") + ": " + qsTr("playback_stop"));
                    menu.close()
                }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_back"); onClicked: menu.currentPage = "playback" }
        }

        // --- СТРАНИЦА СООТНОШЕНИЯ СТОРОН ---
        Column {
            visible: menu.currentPage === "aspect_ratio"
            width: parent.width
            spacing: 2

            MenuButton {
                text: (menu.currentAspect === "original" ? "✓ " : "") + qsTr("aspect_original")
                onClicked: {
                    Media.setAspectRatio("original");
                    menu.currentAspect = "original";
                    Osd.showInfo(qsTr("osd_aspect").arg(qsTr("aspect_original")));
                    menu.close()
                }
            }
            MenuButton {
                text: (menu.currentAspect === "16:9" ? "✓ " : "") + "16:9"
                onClicked: {
                    Media.setAspectRatio("16:9");
                    menu.currentAspect = "16:9";
                    Osd.showInfo(qsTr("osd_aspect").arg("16:9"));
                    menu.close()
                }
            }
            MenuButton {
                text: (menu.currentAspect === "4:3" ? "✓ " : "") + "4:3"
                onClicked: {
                    Media.setAspectRatio("4:3");
                    menu.currentAspect = "4:3";
                    Osd.showInfo(qsTr("osd_aspect").arg("4:3"));
                    menu.close()
                }
            }
            MenuButton {
                text: (menu.currentAspect === "21:9" ? "✓ " : "") + "21:9"
                onClicked: {
                    Media.setAspectRatio("21:9");
                    menu.currentAspect = "21:9";
                    Osd.showInfo(qsTr("osd_aspect").arg("21:9"));
                    menu.close()
                }
            }
            MenuButton {
                text: (menu.currentAspect === "1:1" ? "✓ " : "") + "1:1"
                onClicked: {
                    Media.setAspectRatio("1:1");
                    menu.currentAspect = "1:1";
                    Osd.showInfo(qsTr("osd_aspect").arg("1:1"));
                    menu.close()
                }
            }
            MenuButton {
                text: (menu.currentAspect === "9:16" ? "✓ " : "") + "9:16"
                onClicked: {
                    Media.setAspectRatio("9:16");
                    menu.currentAspect = "9:16";
                    Osd.showInfo(qsTr("osd_aspect").arg("9:16"));
                    menu.close()
                }
            }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            MenuButton { text: qsTr("menu_back"); onClicked: menu.currentPage = "playback" }
        }
    }

    function getContentHeight() {
        return contentColumn.implicitHeight + menu.topPadding + menu.bottomPadding;
    }
}
