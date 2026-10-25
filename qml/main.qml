import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import luma 1.0
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: root
    visible: true
    width: 960; height: 640
    minimumWidth: 640
    minimumHeight: 480
    title: "Luma"
    color: "black"

    property bool controlsVisible: true
    property bool showTitle: false
    signal toggleCropRequested()

    Component.onCompleted: {
        root.requestActivate()
        hideTimer.start()
    }

    function isContextMenuVisible() {
        if (typeof contextMenu !== "undefined" && contextMenu !== null) {
            return contextMenu.visible;
        }
        return false;
    }

    function isStatsOpen() {
        if (typeof statsPanel !== "undefined" && statsPanel !== null) {
            return statsPanel.isOpen;
        }
        return false;
    }

    HoverHandler {
        id: windowHover
    }

    Timer {
        id: hideTimer
        interval: 2500
        repeat: false
        onTriggered: {
            var isOverControls = (typeof controlBar !== "undefined" && controlBar !== null && controlBar.hovered);
            var isMouseInBottomZone = windowHover.point.position.y > root.height - 90;

            var noVideo = (mpv.duration <= 0);
            var isPaused = mpv.paused;

            if (isOverControls || isMouseInBottomZone ||
                (typeof controlBar !== "undefined" && controlBar && (controlBar.isSeeking || controlBar.isVolumeSeeking)) ||
                isContextMenuVisible() || isPaused || noVideo) {
                hideTimer.restart()
                } else {
                    root.controlsVisible = false
                }
        }
    }

    Timer {
        id: titleHideTimer
        interval: 4000
        repeat: false
        onTriggered: {
            root.showTitle = false;
        }
    }

    function wakeUp() {
        if (!root.controlsVisible) root.controlsVisible = true
            hideTimer.restart()
    }

    Connections {
        target: mpv
        function onPausedChanged() {
            if (mpv.paused) {
                root.controlsVisible = true;
                hideTimer.stop();
            } else if (mpv.duration > 0) {
                hideTimer.start();
            }
        }
        function onDurationChanged() {
            if (mpv.duration > 0) {
                if (root.controlsVisible) hideTimer.start();
            }
        }
    }

    Connections {
        target: controlBar
        function onIsSeekingChanged() {
            if (controlBar.isSeeking) hideTimer.stop()
                else hideTimer.start()
        }
        function onIsVolumeSeekingChanged() {
            if (controlBar.isVolumeSeeking) hideTimer.stop()
                else hideTimer.start()
        }
        function onHoveredChanged() {
            if (controlBar.hovered) {
                hideTimer.stop()
                root.controlsVisible = true
            } else {
                hideTimer.start()
            }
        }
    }

    Connections {
        target: Media
        function onFileChanged() {
            root.showTitle = true;
            titleHideTimer.restart();
        }
    }

    // ВИДЕО
    MpvItem {
        id: mpv
        objectName: "mpv"
        anchors.fill: parent
        volume: Config.volume

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            cursorShape: (root.controlsVisible || mpv.duration <= 0 || mpv.paused) ? Qt.ArrowCursor : Qt.BlankCursor

            onPositionChanged: root.wakeUp()

            onClicked: (mouse) => {
                root.wakeUp()
                if (mouse.button === Qt.RightButton) {
                    contextMenu.x = mouse.x;
                    contextMenu.y = mouse.y;
                    contextMenu.currentPage = "main"
                    contextMenu.open();
                }
            }

            onDoubleClicked: {
                if (root.visibility === Window.FullScreen) root.showNormal()
                    else root.showFullScreen()
            }
        }
    }

    // ПЛЕЙЛИСТ
    PlaylistPanel {
        id: playlistPanel
        objectName: "playlistPanel"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        anchors.bottom: controlBar.top
        anchors.bottomMargin: 20
        width: 300
        mpv: mpv
        model: Media.playlist
        isOpen: false
        z: 4

        onIsOpenChanged: {
            hideTimer.restart()
        }
    }

    // СТАТИСТИКА
    StatsPanel {
        id: statsPanel
        objectName: "statsPanel"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 20
        width: 300
        maxHeight: root.height - controlBar.height - 60
        mpv: mpv
        isOpen: false
        z: 5
    }

    // OSD
    OsdToast {
        id: osd
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 40
        text: Osd.message
        isOpen: Osd.visible
        z: 2
    }

    // Название видео
    Item {
        id: titleSpace
        anchors.bottom: controlBar.top
        anchors.bottomMargin: 2
        anchors.top: playlistPanel.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        z: 1

        visible: root.showTitle

        Label {
            id: titleLabel
            anchors.fill: parent
            text: Media.currentFileName !== "" ? Media.currentFileName : ""
            color: "white"
            font.pixelSize: 12
            font.weight: Font.Bold
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            visible: text !== ""
            style: Text.Outline
            styleColor: "black"
        }
    }

    // ПАНЕЛЬ УПРАВЛЕНИЯ
    ControlBar {
        id: controlBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        mpv: mpv
        isOpen: root.controlsVisible
        isPlaylistOpen: playlistPanel.isOpen
        z: 10

        onPlaylistToggleRequested: {
            if (typeof playlistPanel !== "undefined" && playlistPanel !== null) {
                playlistPanel.isOpen = !playlistPanel.isOpen
                if (!playlistPanel.isOpen) hideTimer.restart()
            }
        }
    }

    ContextMenu {
        id: contextMenu
        mpv: mpv
        z: 20
        onToggleCropRequested: root.toggleCropRequested()
    }

    DropArea {
        anchors.fill: parent
        z: 1
        onDropped: (drop) => {
            if (drop.hasUrls) {
                let urlString = drop.urls[0].toString();
                if (urlString) Media.openFile(urlString);
            }
        }
    }

    AboutDialog {
        id: aboutDialog
    }

    SettingsDialog {
        id: settingsDialog
    }
}
