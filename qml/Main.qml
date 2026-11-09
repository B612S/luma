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
    // The "PiP" title is what the KWin keep-above script looks for (see KWinBridge).
    title: root.pipMode ? "Luma PiP" : "Luma"
    // Windowed PiP is a frameless card with no transparent border. The video is
    // overscanned by one pixel and the FBO texture is clamped, so the window
    // edge never samples outside the frame. A single semi-transparent pixel
    // keeps the surface out of KWin's direct-scanout path without creating a
    // visible border or a gap when PiP is snapped.
    readonly property bool pipCard: root.pipMode && root.visibility !== Window.FullScreen
    readonly property int pipContentMargin: 0
    readonly property int pipOverscan: root.pipCard ? -1 : 0
    color: "transparent"

    property bool controlsVisible: true

    // Playlist and statistics panels share the same top/bottom bounds, so they
    // stay the same height at every window size.
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

    function toggleFullscreen() {
        // PiP stays active in fullscreen: its own controls handle this mode,
        // and leaving fullscreen returns to the PiP window with its geometry.
        if (root.visibility === Window.FullScreen) root.showNormal();
        else root.showFullScreen();
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

        // Size the window to the video's aspect ratio, so windowed PiP has no
        // letterbox bars. The card margin around the content is added on top.
        let aspect = 16 / 9;
        const videoW = Number(mpv ? mpv.observedProperty("width") : 0);
        const videoH = Number(mpv ? mpv.observedProperty("height") : 0);
        if (videoW > 0 && videoH > 0)
            aspect = Math.max(0.4, Math.min(3, videoW / videoH));
        const margin = root.pipContentMargin * 2;
        let contentW = Math.min(480, Math.round(screenW * 0.4));
        let contentH = Math.round(contentW / aspect);
        if (contentH > screenH * 0.5 - margin) {
            contentH = Math.round(screenH * 0.5 - margin);
            contentW = Math.round(contentH * aspect);
        }
        const w = contentW + margin;
        const h = contentH + margin;

        // On Wayland window flags only apply to a hidden window. The size is
        // set before show() as well: the window's first buffer is then created
        // at the PiP size, and KWin's open animation runs for that geometry.
        root.hide();
        root.flags = root.flags | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint;
        root.width = w;
        root.height = h;

        // Wayland cannot raise the window itself: KWin keeps the PiP window
        // above the others with a small helper script.
        KWinBridge.setPipActive(true);
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

        // Hiding the window for the flag change keeps KWin's close/open
        // animation and re-runs the placement, so the restored window does not
        // stay in the corner where the PiP was.
        root.hide();
        root.flags = root.flags & ~Qt.WindowStaysOnTopHint & ~Qt.FramelessWindowHint;
        if (pipSavedGeometry.width > 0) {
            root.width = pipSavedGeometry.width;
            root.height = pipSavedGeometry.height;
        }
        root.show();
        KWinBridge.setPipActive(false);
        // Do not restore x/y here: Wayland ignores client-side top-level
        // positioning, and setting it after show can fight KWin's placement.
        // KWin places the re-created window; the saved size is enough.
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

    // ---- Fit the current window to the video ----
    // One-shot: the window takes the picture's aspect ratio once. The aspect is
    // not locked, so the window can be resized freely afterwards.
    function fitWindowToVideo() {
        // In fullscreen the size is owned by the compositor.
        if (root.visibility === Window.FullScreen) return;
        // While casting the window shows the cast overlay, not the local video.
        if (CastManager.casting) return;

        // Display aspect: mpv has already applied the pixel aspect ratio and
        // the Playback → Aspect ratio override to this value.
        let aspect = Number(mpv ? mpv.observedProperty("video-params/aspect") : 0);
        if (!(aspect > 0)) {
            const videoW = Number(mpv ? mpv.observedProperty("width") : 0);
            const videoH = Number(mpv ? mpv.observedProperty("height") : 0);
            if (!(videoW > 0) || !(videoH > 0)) return;
            aspect = videoW / videoH;
        }

        // Only the pinned control bar takes height away from the picture; the
        // floating bar is drawn on top of it.
        const barHeight = (root.controlBarPinned && typeof controlBar !== "undefined" && controlBar)
                          ? Math.round(controlBar.height) : 0;
        const screenW = root.screen ? root.screen.width : root.width;
        const screenH = root.screen ? root.screen.height : root.height;

        // Use the window as it is now as the limit: the picture is fitted into
        // the current window, so a 4:3 film in a wide window makes the window
        // narrower instead of stretching it to the screen.
        const pictureHeight = Math.max(1, root.height - barHeight);
        const scale = Math.min(root.width / aspect, pictureHeight);
        let videoW = scale * aspect;
        let videoH = scale;

        // The window minimums are hard limits. Grow the other dimension only as
        // far as they require, so the picture keeps its aspect ratio.
        if (videoH < root.minimumHeight) {
            videoH = root.minimumHeight;
            videoW = videoH * aspect;
        }
        if (videoW < root.minimumWidth) {
            videoW = root.minimumWidth;
            videoH = videoW / aspect;
        }
        // Never place the window off-screen.
        if (videoW > screenW) {
            videoW = screenW;
            videoH = videoW / aspect;
        }
        if (videoH + barHeight > screenH) {
            videoH = screenH - barHeight;
            videoW = videoH * aspect;
        }

        root.width = Math.round(videoW);
        root.height = Math.round(videoH) + barHeight;
    }

    property bool isDialogOpen: settingsDialog.visible ||
    equalizerDialog.visible ||
    playlistPanel.hasOpenDialog ||
    infoDialog.visible ||
    fileUnavailableDialog.visible ||
    urlDialog.dialog.visible ||
    (typeof Plugins !== "undefined" && Plugins && Plugins.windows.length > 0)

    onIsDialogOpenChanged: {
        if (typeof shortcuts !== "undefined" && shortcuts !== null)
            shortcuts.setShortcutsEnabled(!isDialogOpen);
        // Any dialog may have left active focus on a hidden button or text
        // field. Give it back to the video surface so Space reaches playback.
        if (!isDialogOpen && typeof videoMouseArea !== "undefined")
            videoMouseArea.forceActiveFocus();
    }

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
            if (root.isInteracting()) { idleTimer.restart(); return; }
            const noVideo = (mpv.duration <= 0) && !CastManager.casting;
            const isPaused = CastManager.casting ? !CastManager.isRemotePlaying : mpv.paused;
            if (noVideo || isPaused) { idleTimer.restart(); return; }
            const inBottomZone = root.cursorY > root.height - root.bottomZoneHeight;
            if (inBottomZone) { idleTimer.restart(); return; }
            // An open menu doesn't prevent hiding the bar: it is drawn on top
            // and doesn't depend on the bottom bar. A dialog is different for
            // the cursor: hiding it over a dialog would leave the user without
            // a pointer, but the bar itself may still auto-hide.
            if (!root.currentOpenMenu && !root.isDialogOpen) root.cursorVisible = false;
            if (!root.controlBarPinnedMode) root.controlsVisible = false;
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

    // Opaque backdrop for the normal window and for PiP.
    Rectangle {
        anchors.fill: parent
        z: -2
        color: "black"
    }

    MpvItem {
        id: mpv
        objectName: "mpv"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: root.controlBarPinned ? controlBar.top : parent.bottom
        anchors.margins: root.pipOverscan
        clip: true
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
            focus: true
            activeFocusOnTab: false
            cursorShape: root.cursorVisible ? Qt.ArrowCursor : Qt.BlankCursor

            onPressed: (mouse) => {
                // Clicking the picture must take focus away from text inputs
                // (e.g. the playlist search), otherwise all shortcuts stay
                // swallowed by the focused text field.
                if (mouse.button === Qt.LeftButton) forceActiveFocus();
            }

            onPositionChanged: (mouse) => {
                if (Math.abs(mouse.x - root.lastMouseX) < 3 && Math.abs(mouse.y - root.lastMouseY) < 3) return;
                root.lastMouseX = mouse.x;
                root.lastMouseY = mouse.y;
                const globalPos = mapToItem(null, mouse.x, mouse.y);
                root.cursorY = globalPos.y;
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
                    contextMenu.openAt(videoMouseArea, mouse.x, mouse.y);
                }
            }
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton)
                    root.toggleFullscreen();
            }
        }
    }

    Rectangle {
        id: castOverlay
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
                    contextMenu.openAt(castOverlay, mouse.x, mouse.y);
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
    }

    StatsPanel {
        id: statsPanel; objectName: "statsPanel"
        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 20
        // The statistics panel fills exactly the playlist panel's height.
        width: 300; maxHeight: playlistPanel.height; mpv: mpv; isOpen: false; z: 5
    }

    OsdToast {
        id: osd; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 40
        text: Osd.message; isOpen: Osd.visible && !root.pipMode; z: 2
    }

    // In picture-in-picture mode — only minimal custom buttons.
    // One semi-transparent pixel is enough to keep the whole surface out of
    // direct scanout. It sits in the bottom-right corner under the controls.
    Rectangle {
        visible: root.pipCard
        width: 1; height: 1
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: Qt.rgba(0, 0, 0, 0.5)
        z: 100
    }

    PipControls {
        anchors.fill: parent
        mpv: mpv
        active: root.pipMode
        onCloseRequested: root.exitPip()
        onFullscreenRequested: root.toggleFullscreen()
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
        onToggleCropRequested: root.toggleCropRequested()
        onTogglePipRequested: root.togglePip()
        onFitWindowRequested: root.fitWindowToVideo()
        onOpenUrlRequested: root.openUrlDialog()
        onOpenEqualizerRequested: equalizerDialog.open()
        onTogglePlaylistRequested: { playlistPanel.isOpen = !playlistPanel.isOpen; idleTimer.restart(); }
        onOpened: {
            // Close the playlist menus (tracks and tabs) and the stats menu.
            if (typeof playlistPanel !== "undefined" && playlistPanel !== null) {
                playlistPanel.closeContextMenu();
            }
            if (typeof statsPanel !== "undefined" && statsPanel !== null)
                statsPanel.closeContextMenu();
            root.cursorY = -1;
            idleTimer.restart();
        }
        onClosed: {
            idleTimer.restart();
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
    EqualizerDialog { id: equalizerDialog }
    MediaInfoDialog { id: infoDialog }
    FileUnavailableDialog { id: fileUnavailableDialog }

    Repeater {
        model: typeof Plugins !== "undefined" && Plugins ? Plugins.windows : []
        // Repeater delegates must be Items; the Dialog itself anchors to the
        // window overlay.
        delegate: Item {
            PluginWindowDialog {
                pluginWindow: modelData
            }
        }
    }

    Connections {
        target: Media
        function onFileUnavailable(path) { fileUnavailableDialog.openWith(path); }
    }

    AddUrlDialog {
        id: urlDialog
        onUrlAccepted: (url) => Media.openFile(url)
    }

    function openUrlDialog() {
        urlDialog.openWith(qsTranslate("MediaManager", "dialog_open_url"), "");
    }

    // Qt fills the scene-graph glyph cache the first time it paints a text
    // run. Paint the playlist titles once here, with the same Text properties
    // as the list rows, so the first switch to a playlist with CJK/emoji
    // titles does not freeze the video while it rasterises them.
    Item {
        id: glyphWarmup
        width: 420
        height: 600
        opacity: 0.004
        z: -1

        Column {
            Repeater {
                model: Playlists.warmupTitles
                Text {
                    width: 400
                    text: modelData
                    elide: Text.ElideRight
                    font.pixelSize: 14
                }
            }
        }

        Component.onCompleted: console.log("[perf] glyph warmup rows",
                                           Playlists.warmupTitles.length)

        Timer {
            interval: 600
            running: true
            onTriggered: {
                console.log("[perf] glyph warmup done")
                glyphWarmup.destroy()
            }
        }
    }

}
