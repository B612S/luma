import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import luma 1.0
import org.kde.kirigami as Kirigami

ApplicationWindow {
    id: root
    visible: true
    width: 960; height: 640
    // In PiP the window is small — the usual minimum only gets in the way there,
    // but it can't be too tiny: the controls would start overlapping.
    minimumWidth: pipMode ? 320 : 640
    minimumHeight: pipMode ? 180 : 480
    title: "Luma"
    color: "black"

    property bool controlsVisible: true
    signal toggleCropRequested()

    readonly property int bottomZoneHeight: 70

    property real cursorY: root.height
    property bool cursorVisible: true
    property real lastMouseX: -1
    property real lastMouseY: -1

    // Global reference to the currently open menu (tab, track or main)
    property var currentOpenMenu: null

    // The setting only affects the control bar: it can be hidden
    // in fullscreen. The playlist and stats panels remain available as usual.
    readonly property bool fullscreen: root.visibility === Window.FullScreen
    readonly property bool controlBarAllowed: !fullscreen || Config.showPanelsFullscreen
    // Control bar mode: "hidden" | "float" | "pinned".
    readonly property bool controlBarHidden: Config.controlBarMode === "hidden"
    readonly property bool controlBarPinnedMode: Config.controlBarMode === "pinned"
    // The bar only takes up space at the bottom when it is actually visible
    // (e.g. in fullscreen it may be hidden while pinned).
    readonly property bool controlBarPinned: root.controlBarPinnedMode && root.controlBarAllowed
                                             && !root.pipMode && !root.controlBarHidden

    onFullscreenChanged: root.applyControlBarPolicy()
    Connections {
        target: Config
        function onShowPanelsFullscreenChanged() { root.applyControlBarPolicy(); }
    }

    function applyControlBarPolicy() {
        if (controlBarAllowed) return;
        root.controlsVisible = false;
    }

    // If the window loses focus, the key release may not arrive — hide the help.
    onActiveChanged: if (!active) root.shortcutsHelpVisible = false;

    // ---- Picture in picture (small window above the others) ----
    property bool pipMode: false
    property bool pipWasFullscreen: false
    property rect pipSavedGeometry: Qt.rect(0, 0, 0, 0)
    property bool shortcutsHelpVisible: false

    function togglePip() {
        if (pipMode) exitPip();
        else enterPip();
    }

    function enterPip() {
        if (pipMode) return;
        pipSavedGeometry = Qt.rect(root.x, root.y, root.width, root.height);
        pipWasFullscreen = (root.visibility === Window.FullScreen);

        pipMode = true;
        playlistPanel.isOpen = false;
        statsPanel.isOpen = false;

        if (pipWasFullscreen) root.showNormal();

        const screenW = root.screen ? root.screen.width : 1280;
        const screenH = root.screen ? root.screen.height : 720;
        let w = Math.min(480, Math.round(screenW * 0.4));
        let h = Math.round(w * 9 / 16);
        if (h > screenH * 0.5) {
            h = Math.round(screenH * 0.5);
            w = Math.round(h * 16 / 9);
        }

        // On Wayland window flags only apply to a hidden window. The size is
        // also set before show(): otherwise the window appears for a frame at the old
        // large size, and KWin/NVIDIA sometimes leaves the old plane behind —
        // a flickering strip far from the PiP window.
        root.hide();
        root.flags = root.flags | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
        root.width = w;
        root.height = h;
        // Show on the next tick: by then the flags and size are already
        // applied, and the window's first buffer is created at PiP size right away.
        Qt.callLater(function() {
            if (!root.pipMode) return;
            root.show();
            root.requestActivate();
        });
        Qt.callLater(function() {
            if (!root.pipMode) return;
            root.width = w;
            root.height = h;
        });
        idleTimer.restart();
    }

    function exitPip() {
        if (!pipMode) return;
        pipMode = false;
        if (root.visibility === Window.FullScreen) root.showNormal();

        root.hide();
        root.flags = root.flags & ~Qt.WindowStaysOnTopHint & ~Qt.FramelessWindowHint;
        // As when entering PiP, restore the size before show() so the window
        // isn't shown for a frame at PiP size.
        if (pipSavedGeometry.width > 0) {
            root.width = pipSavedGeometry.width;
            root.height = pipSavedGeometry.height;
        }
        root.show();

        if (pipSavedGeometry.width > 0) {
            root.x = pipSavedGeometry.x;
            root.y = pipSavedGeometry.y;
        }
        Qt.callLater(function() {
            if (root.pipMode || pipSavedGeometry.width <= 0) return;
            root.width = pipSavedGeometry.width;
            root.height = pipSavedGeometry.height;
        });
        root.controlsVisible = true;
        if (pipWasFullscreen) root.showFullScreen();
        pipWasFullscreen = false;
        idleTimer.restart();
    }

    property bool isDialogOpen: settingsDialog.visible ||
    playlistPanel.hasOpenDialog ||
    infoDialog.visible ||
    urlDialog.dialog.visible

    Component.onCompleted: {
        root.requestActivate()
        if (root.controlBarPinnedMode) root.controlsVisible = true
            idleTimer.start()
    }

    Connections {
        target: Config
        function onControlBarModeChanged() {
            if (root.controlBarHidden) root.controlsVisible = false;
            else if (root.controlBarPinnedMode) root.controlsVisible = true;
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
        if (inBottomZone && root.controlBarAllowed && !root.controlBarHidden && !root.controlsVisible) root.controlsVisible = true;
    }

    Timer {
        id: idleTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (root.isInteracting() || root.isDialogOpen) { idleTimer.restart(); return; }
            const noVideo = (mpv.duration <= 0) && !CastManager.casting;
            const isPaused = CastManager.casting ? !CastManager.isRemotePlaying : mpv.paused;
            if (noVideo || isPaused) { idleTimer.restart(); return; }
            const inBottomZone = root.cursorY > root.height - root.bottomZoneHeight;
            if (inBottomZone) { idleTimer.restart(); return; }
            // An open menu doesn't prevent hiding the bar: it is drawn on top
            // and doesn't depend on the bottom bar.
            if (!root.currentOpenMenu) root.cursorVisible = false;
            if (!Config.pinControls) root.controlsVisible = false;
        }
    }

    Connections {
        target: mpv
        function onPausedChanged() {
            if (mpv.paused && !CastManager.casting) {
                root.controlsVisible = true; root.cursorVisible = true; idleTimer.stop();
            } else if (mpv.duration > 0) idleTimer.start();
        }
        function onDurationChanged() { if (mpv.duration > 0 && root.controlsVisible) idleTimer.restart(); }
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
            if (controlBar.hovered) { idleTimer.stop(); root.controlsVisible = true; root.cursorVisible = true; }
            else if (root.controlsVisible) idleTimer.restart();
        }
    }

    MpvItem {
        id: mpv
        objectName: "mpv"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: root.controlBarPinned ? controlBar.top : parent.bottom
        isCasting: CastManager.casting
        z: 0
        visible: !CastManager.casting
        enabled: !CastManager.casting && !root.isDialogOpen
        focus: false
        activeFocusOnTab: false

        MouseArea {
            id: videoMouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            enabled: !root.isDialogOpen
            focus: false
            activeFocusOnTab: false
            cursorShape: root.cursorVisible ? Qt.ArrowCursor : Qt.BlankCursor

            onPositionChanged: (mouse) => {
                if (Math.abs(mouse.x - root.lastMouseX) < 3 && Math.abs(mouse.y - root.lastMouseY) < 3) return;
                root.lastMouseX = mouse.x;
                root.lastMouseY = mouse.y;
                const globalPos = mapToItem(null, mouse.x, mouse.y);
                root.cursorY = globalPos.y;
                if (contextMenu.visible && !contextMenu.cursorWasVisible) return;
                root.onCursorMoved();
            }

            onExited: {
                root.cursorY = -1; root.lastMouseX = -1; root.lastMouseY = -1; idleTimer.restart();
            }

            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    // In picture-in-picture mode there is no menu — only its own buttons.
                    if (root.pipMode) return;
                    if (contextMenu.visible) { contextMenu.close(); return; }
                    // Don't open the main menu inside the open playlist panel.
                    if (playlistPanel.isOpen) {
                        const globalClick = mapToItem(null, mouse.x, mouse.y);
                        const localClick = playlistPanel.mapFromItem(null, globalClick.x, globalClick.y);
                        if (localClick.x >= 0 && localClick.y >= 0
                                && localClick.x <= playlistPanel.width && localClick.y <= playlistPanel.height)
                            return;
                    }
                    var controlBarTop = root.height - 20 - controlBar.height;
                    if (mouse.y >= controlBarTop) return;
                    contextMenu.currentPage = "main";
                    var menuHeight = contextMenu.getContentHeight();
                    var menuWidth = contextMenu.width;
                    var globalPos = mapToItem(null, mouse.x, mouse.y);
                    var px = globalPos.x; var py = globalPos.y;
                    if (px + menuWidth > root.width) px = root.width - menuWidth - 10;
                    if (py + menuHeight > root.height) py = globalPos.y - menuHeight;
                    contextMenu.x = Math.max(10, px);
                    contextMenu.y = Math.max(10, py);
                    contextMenu.open();
                }
            }
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    if (root.pipMode) { root.exitPip(); return; }
                    if (root.visibility === Window.FullScreen) root.showNormal();
                    else root.showFullScreen();
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: CastManager.casting
        z: 1
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            enabled: !root.isDialogOpen && !root.currentOpenMenu
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (root.pipMode) return;
                    if (contextMenu.visible) { contextMenu.close(); return; }
                    contextMenu.currentPage = "main";
                    contextMenu.open();
                }
            }
        }
        Column {
            anchors.centerIn: parent; spacing: 16
            Text { text: qsTr("cast_title"); color: "gray"; font.pixelSize: 24; font.weight: Font.Bold; anchors.horizontalCenter: parent.horizontalCenter }
            Text { text: Media.playlist.currentFile().split('/').pop(); color: "white"; font.pixelSize: 18; anchors.horizontalCenter: parent.horizontalCenter }
            Text { text: qsTr("cast_controls_hint"); color: "gray"; font.pixelSize: 14; anchors.horizontalCenter: parent.horizontalCenter; visible: CastManager.isRemotePlaying }
        }
    }

    PlaylistPanel {
        id: playlistPanel
        objectName: "playlistPanel"
        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 20
        // Keep a 20 px gap above the floating bar too; when it is hidden, the panel can use the full height.
        anchors.bottom: (root.controlBarPinned || (controlBar.visible && controlBar.isOpen)) ? controlBar.top : parent.bottom
        anchors.bottomMargin: 20
        width: 300; mpv: mpv; model: Media.playlist; isOpen: false; z: 4
        rootContextMenu: contextMenu
    }

    StatsPanel {
        id: statsPanel; objectName: "statsPanel"
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 20
        width: 300; maxHeight: root.height - controlBar.height - 60; mpv: mpv; isOpen: false; z: 5
        hostWindow: root
    }

    OsdToast {
        id: osd; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 40
        text: Osd.message; isOpen: Osd.visible && !root.pipMode; z: 2
    }

    // In picture-in-picture mode — only minimal custom buttons.
    PipControls {
        anchors.fill: parent
        mpv: mpv
        active: root.pipMode
        onCloseRequested: root.exitPip()
        onFullscreenRequested: {
            if (root.visibility === Window.FullScreen) root.showNormal();
            else root.showFullScreen();
        }
        z: 3
    }

    ShortcutsHelp {
        helpVisible: root.shortcutsHelpVisible && !root.pipMode
        onCloseRequested: root.shortcutsHelpVisible = false
    }

    ControlBar {
        id: controlBar
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: root.controlBarPinnedMode ? 0 : 20; anchors.rightMargin: root.controlBarPinnedMode ? 0 : 20
        anchors.bottomMargin: root.controlBarPinnedMode ? 0 : 20
        pinned: root.controlBarPinnedMode; mpv: mpv
        panelsAllowed: root.controlBarAllowed && !root.pipMode && !root.controlBarHidden
        isOpen: root.controlsVisible || root.controlBarPinnedMode
        isPlaylistOpen: playlistPanel.isOpen; z: 10

        onPlaylistToggleRequested: {
            if (typeof playlistPanel !== "undefined" && playlistPanel !== null) {
                playlistPanel.isOpen = !playlistPanel.isOpen; idleTimer.restart();
            }
        }
    }

    ContextMenu {
        id: contextMenu
        mpv: mpv; z: 1000; parent: Overlay.overlay
        hostWindow: root
        onToggleCropRequested: root.toggleCropRequested()
        onTogglePipRequested: root.togglePip()
        onOpenUrlRequested: root.openUrlDialog()
        onTogglePlaylistRequested: { playlistPanel.isOpen = !playlistPanel.isOpen; idleTimer.restart(); }
        property bool cursorWasVisible: true

        onOpened: {
            // Close the playlist menus (tracks and tabs) and the stats menu.
            if (typeof playlistPanel !== "undefined" && playlistPanel !== null) {
                playlistPanel.closeContextMenu();
            }
            if (typeof statsPanel !== "undefined" && statsPanel !== null)
                statsPanel.closeContextMenu();
            cursorWasVisible = root.cursorVisible;
            root.cursorY = -1;
            idleTimer.restart();
            // Register in the global variable
            root.currentOpenMenu = contextMenu;
        }
        onClosed: {
            idleTimer.restart();
            if (root.currentOpenMenu === contextMenu) root.currentOpenMenu = null;
        }
    }

    DropArea {
        anchors.fill: parent; z: 1; enabled: !root.isDialogOpen && !root.currentOpenMenu
        onDropped: (drop) => {
            if (drop.hasUrls) {
                let urlString = drop.urls[0].toString();
                if (urlString) Media.openFile(urlString);
            }
        }
    }

    SettingsDialog { id: settingsDialog }
    MediaInfoDialog { id: infoDialog }

    AddUrlDialog {
        id: urlDialog
        onUrlAccepted: (url) => Media.openFile(url)
    }

    function openUrlDialog() {
        urlDialog.openWith(qsTranslate("MediaManager", "dialog_open_url"), "");
    }

}
