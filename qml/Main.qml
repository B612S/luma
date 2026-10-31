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
    signal toggleCropRequested()

    readonly property int bottomZoneHeight: 70

    property real cursorY: root.height
    property bool cursorVisible: true
    property real lastMouseX: -1
    property real lastMouseY: -1

    Component.onCompleted: {
        root.requestActivate()
        if (Config.pinControls) {
            root.controlsVisible = true;
        }
        idleTimer.start()
    }

    Connections {
        target: Config
        function onPinControlsChanged() {
            if (Config.pinControls) {
                root.controlsVisible = true;
            }
        }
    }

    function isInteracting() {
        if (typeof controlBar === "undefined" || controlBar === null) return false;
        return controlBar.hovered || controlBar.isSeeking || controlBar.isVolumeSeeking;
    }

    function wakeCursor() {
        root.cursorVisible = true;
        idleTimer.restart();
    }

    function onCursorMoved() {
        root.cursorVisible = true;
        idleTimer.restart();

        const inBottomZone = root.cursorY > root.height - root.bottomZoneHeight;
        if (inBottomZone && !root.controlsVisible) {
            root.controlsVisible = true;
        }
    }

    Timer {
        id: idleTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (root.isInteracting()) { idleTimer.restart(); return; }

            const noVideo = (mpv.duration <= 0) && !CastManager.casting;
            const isPaused = CastManager.casting ? !CastManager.isRemotePlaying : mpv.paused;

            if (noVideo || isPaused) {
                idleTimer.restart();
                return;
            }

            const inBottomZone = root.cursorY > root.height - root.bottomZoneHeight;
            if (inBottomZone) { idleTimer.restart(); return; }

            root.cursorVisible = false;

            if (!Config.pinControls) {
                root.controlsVisible = false;
            }
        }
    }

    Connections {
        target: mpv
        function onPausedChanged() {
            if (mpv.paused && !CastManager.casting) {
                root.controlsVisible = true;
                root.cursorVisible = true;
                idleTimer.stop();
            } else if (mpv.duration > 0) {
                idleTimer.start();
            }
        }
        function onDurationChanged() {
            if (mpv.duration > 0 && root.controlsVisible) {
                idleTimer.restart();
            }
        }
    }

    Connections {
        target: controlBar
        function onIsSeekingChanged() {
            if (controlBar.isSeeking) { idleTimer.stop(); root.cursorVisible = true; }
            else idleTimer.start();
        }
        function onIsVolumeSeekingChanged() {
            if (controlBar.isVolumeSeeking) { idleTimer.stop(); root.cursorVisible = true; }
            else idleTimer.start();
        }
        function onHoveredChanged() {
            if (controlBar.hovered) {
                idleTimer.stop();
                root.controlsVisible = true;
                root.cursorVisible = true;
            } else {
                if (root.controlsVisible) {
                    idleTimer.restart();
                }
            }
        }
    }

    // ---------- Видео ----------
    MpvItem {
        id: mpv
        objectName: "mpv"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: Config.pinControls ? controlBar.top : parent.bottom

        // ПЕРЕДАЕМ СОСТОЯНИЕ КАСТА НАПРЯМУЮ В C++
        // В C++ этот сеттер отключит vid и aid, а также заблокирует изменение громкости
        isCasting: CastManager.casting

        z: 0
        visible: !CastManager.casting
        enabled: !CastManager.casting

        MouseArea {
            id: videoMouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            cursorShape: root.cursorVisible ? Qt.ArrowCursor : Qt.BlankCursor

            onPositionChanged: (mouse) => {
                if (Math.abs(mouse.x - root.lastMouseX) < 3 &&
                    Math.abs(mouse.y - root.lastMouseY) < 3) {
                    return;
                    }
                    root.lastMouseX = mouse.x;
                root.lastMouseY = mouse.y;

                const globalPos = mapToItem(null, mouse.x, mouse.y);
                root.cursorY = globalPos.y;

                if (contextMenu.visible && !contextMenu.cursorWasVisible) {
                    return;
                }

                root.onCursorMoved();
            }

            onExited: {
                root.cursorY = -1;
                root.lastMouseX = -1;
                root.lastMouseY = -1;
                idleTimer.restart();
            }

            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (contextMenu.visible) {
                        contextMenu.close();
                        return;
                    }

                    var controlBarTop = root.height - 20 - controlBar.height;
                    if (mouse.y >= controlBarTop) return;

                    contextMenu.currentPage = "main";

                    var menuHeight = contextMenu.getContentHeight();
                    var menuWidth = contextMenu.width;

                    var globalPos = mapToItem(null, mouse.x, mouse.y);
                    var px = globalPos.x;
                    var py = globalPos.y;

                    if (px + menuWidth > root.width) px = root.width - menuWidth - 10;
                    if (py + menuHeight > root.height) py = globalPos.y - menuHeight;

                    px = Math.max(10, px);
                    py = Math.max(10, py);

                    contextMenu.x = px;
                    contextMenu.y = py;
                    contextMenu.open();
                }
            }

            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    if (root.visibility === Window.FullScreen) root.showNormal();
                    else root.showFullScreen();
                }
            }
        }
    }

    // ---------- Черный экран-заглушка при касте ----------
    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: CastManager.casting
        z: 1

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (contextMenu.visible) {
                        contextMenu.close();
                        return;
                    }
                    contextMenu.currentPage = "main";
                    contextMenu.open();
                }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 16

            Text {
                text: qsTr("Трансляция на ТВ")
                color: "gray"
                font.pixelSize: 24
                font.weight: Font.Bold
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: Media.playlist.currentFile().split('/').pop()
                color: "white"
                font.pixelSize: 18
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: qsTr("Управление доступно в нижней панели")
                color: "gray"
                font.pixelSize: 14
                anchors.horizontalCenter: parent.horizontalCenter
                visible: CastManager.isRemotePlaying
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
        rootContextMenu: contextMenu
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

    // ПАНЕЛЬ УПРАВЛЕНИЯ
    ControlBar {
        id: controlBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Config.pinControls ? 0 : 20
        anchors.rightMargin: Config.pinControls ? 0 : 20
        anchors.bottomMargin: Config.pinControls ? 0 : 20

        pinned: Config.pinControls
        mpv: mpv
        isOpen: root.controlsVisible || Config.pinControls
        isPlaylistOpen: playlistPanel.isOpen
        z: 10

        onPlaylistToggleRequested: {
            if (typeof playlistPanel !== "undefined" && playlistPanel !== null) {
                playlistPanel.isOpen = !playlistPanel.isOpen;
                idleTimer.restart();
            }
        }
    }

    ContextMenu {
        id: contextMenu
        mpv: mpv
        z: 1000
        parent: Overlay.overlay
        onToggleCropRequested: root.toggleCropRequested()
        onTogglePlaylistRequested: {
            playlistPanel.isOpen = !playlistPanel.isOpen;
            idleTimer.restart();
        }

        property bool cursorWasVisible: true

        onOpened: {
            if (typeof playlistPanel !== "undefined" && playlistPanel !== null) {
                playlistPanel.closeContextMenu();
            }
            cursorWasVisible = root.cursorVisible;
            root.cursorY = -1;
            idleTimer.restart();
        }
        onClosed: {
            idleTimer.restart();
        }
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

    SettingsDialog {
        id: settingsDialog
    }
}
