import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Controls for picture-in-picture mode: window dragging and resizing,
// pause, progress, time, volume, fullscreen and exit.
// Styling follows the app's overall look (Kirigami).
Item {
    id: pip

    property var mpv: null
    property bool active: false

    signal closeRequested()
    signal fullscreenRequested()

    visible: active

    readonly property bool windowed: Window.window ? Window.window.visibility !== Window.FullScreen : true
    readonly property bool fullscreen: !windowed
    readonly property real duration: (mpv && mpv.duration > 0) ? mpv.duration : 0
    readonly property real position: (mpv && mpv.position > 0) ? mpv.position : 0

    // In normal PiP the UI is visible while the cursor is over the small window.
    // In fullscreen PiP the window covers the whole screen, so hovering
    // doesn't work — hide the UI on a timer, as in the main window.
    property bool fullscreenControls: false
    readonly property bool showControls: fullscreen
        ? fullscreenControls
        : (hoverArea.containsMouse || windowHover.hovered || introTimer.running)

    function revealFullscreenControls() {
        if (!fullscreen) return;
        fullscreenControls = true;
        hideTimer.restart();
    }

    // Colors match the other panels of the app.
    readonly property color panelColor: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                                                Kirigami.Theme.backgroundColor.g,
                                                Kirigami.Theme.backgroundColor.b, 0.85)
    readonly property color iconColor: Kirigami.Theme.textColor
    readonly property color accentColor: Kirigami.Theme.highlightColor
    readonly property color trackColor: Qt.rgba(Kirigami.Theme.textColor.r,
                                                Kirigami.Theme.textColor.g,
                                                Kirigami.Theme.textColor.b, 0.3)

    function formatTime(seconds) {
        if (!(seconds > 0)) return "0:00";
        const total = Math.floor(seconds);
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const mm = (h > 0 && m < 10) ? "0" + m : String(m);
        const ss = (s < 10) ? "0" + s : String(s);
        return (h > 0 ? h + ":" + mm : String(m)) + ":" + ss;
    }

    HoverHandler { id: windowHover }

    Timer {
        id: introTimer
        interval: 2000
        repeat: false
    }

    Timer {
        id: hideTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (pip.fullscreen)
                pip.fullscreenControls = false;
        }
    }

    onActiveChanged: {
        if (active) {
            introTimer.restart();
            if (fullscreen) {
                fullscreenControls = true;
                hideTimer.restart();
            }
        } else {
            introTimer.stop();
            hideTimer.stop();
            fullscreenControls = false;
        }
    }

    onFullscreenChanged: {
        if (!active) return;
        if (fullscreen) {
            fullscreenControls = true;
            hideTimer.restart();
        } else {
            hideTimer.stop();
            fullscreenControls = false;
        }
    }

    // ---- Drag the window by empty space ----
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: (mouse) => {
            const win = Window.window;
            if (win) win.startSystemMove();
        }
    }

    // ---- Resize from the edges ----
    MouseArea {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: Window.window.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: 6
        cursorShape: Qt.SizeHorCursor
        onPressed: Window.window.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 6
        cursorShape: Qt.SizeVerCursor
        onPressed: Window.window.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 6
        cursorShape: Qt.SizeVerCursor
        onPressed: Window.window.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        width: 12; height: 12
        anchors { left: parent.left; top: parent.top }
        cursorShape: Qt.SizeFDiagCursor
        onPressed: Window.window.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
    }
    MouseArea {
        width: 12; height: 12
        anchors { right: parent.right; top: parent.top }
        cursorShape: Qt.SizeBDiagCursor
        onPressed: Window.window.startSystemResize(Qt.RightEdge | Qt.TopEdge)
    }
    MouseArea {
        width: 12; height: 12
        anchors { left: parent.left; bottom: parent.bottom }
        cursorShape: Qt.SizeBDiagCursor
        onPressed: Window.window.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
    }
    MouseArea {
        width: 12; height: 12
        anchors { right: parent.right; bottom: parent.bottom }
        cursorShape: Qt.SizeFDiagCursor
        onPressed: Window.window.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }

    // ---- Fullscreen / back ----
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        width: 30; height: 30; radius: 6
        color: pip.panelColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        opacity: pip.showControls ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 18; height: 18
            color: pip.iconColor
            source: pip.windowed ? "view-fullscreen" : "view-restore"
        }

        MouseArea { anchors.fill: parent; onClicked: pip.fullscreenRequested() }
    }

    // ---- Close PiP (return to the main window) ----
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        width: 30; height: 30; radius: 6
        color: pip.panelColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        opacity: pip.showControls ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 18; height: 18
            color: pip.iconColor
            source: "window-close"
        }

        MouseArea { anchors.fill: parent; onClicked: pip.closeRequested() }
    }

    // ---- Center pause button ----
    Rectangle {
        anchors.centerIn: parent
        width: 46; height: 46; radius: 23
        color: pip.panelColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        opacity: pip.showControls ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        Kirigami.Icon {
            anchors.centerIn: parent
            width: 22; height: 22
            color: pip.iconColor
            source: pip.mpv && pip.mpv.paused ? "media-playback-start" : "media-playback-pause"
        }

        MouseArea {
            anchors.fill: parent
            onClicked: if (pip.mpv) pip.mpv.paused = !pip.mpv.paused
        }
    }

    // ---- Bottom bar: seek, time, volume ----
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 62
        color: pip.panelColor
        opacity: pip.showControls ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 120 } }

        HoverHandler {
            id: bottomBarHover
            onHoveredChanged: {
                if (!pip.fullscreen) return;
                if (hovered) {
                    hideTimer.stop();
                    pip.fullscreenControls = true;
                } else {
                    hideTimer.restart();
                }
            }
        }

        Slider {
            id: seekSlider
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 18
            padding: 0
            from: 0
            to: Math.max(1, pip.duration)
            onMoved: if (pip.mpv) pip.mpv.position = value

            background: Item {
                x: 0
                y: (seekSlider.height - height) / 2
                width: seekSlider.availableWidth
                height: 4

                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: pip.trackColor
                }
                Rectangle {
                    width: seekSlider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: pip.accentColor
                }
            }

            handle: Rectangle {
                x: seekSlider.visualPosition * (seekSlider.availableWidth - width)
                y: (seekSlider.height - height) / 2
                width: 12; height: 12; radius: 6
                color: pip.accentColor
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.25)
            }

            // While dragging, the slider follows the cursor; after release
            // don't let it snap back until mpv reports the new position.
            Binding {
                target: seekSlider
                property: "value"
                value: pip.position
                when: !seekSlider.pressed && !seekGrace.running
            }

            Timer { id: seekGrace; interval: 400; repeat: false }

            onPressedChanged: {
                if (!pressed) seekGrace.restart();
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.topMargin: 20
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 6
            spacing: 8

            Label {
                text: pip.formatTime(seekSlider.value) + " / " + pip.formatTime(pip.duration)
                color: pip.iconColor
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24

                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: 18; height: 18
                    color: pip.iconColor
                    source: pip.mpv && pip.mpv.muted ? "audio-volume-muted" : "audio-volume-high"
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (!pip.mpv) return;
                        pip.mpv.muted = !pip.mpv.muted;
                    }
                }
            }

            Slider {
                id: volumeSlider
                Layout.preferredWidth: 90
                Layout.preferredHeight: 18
                Layout.alignment: Qt.AlignVCenter
                height: 18
                padding: 0
                from: 0; to: 100
                onMoved: if (pip.mpv) pip.mpv.volume = value

                background: Item {
                    x: 0
                    y: (volumeSlider.height - height) / 2
                    width: volumeSlider.availableWidth
                    height: 4

                    Rectangle {
                        anchors.fill: parent
                        radius: 2
                        color: pip.trackColor
                    }
                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: pip.accentColor
                    }
                }

                handle: Rectangle {
                    x: volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: (volumeSlider.height - height) / 2
                    width: 12; height: 12; radius: 6
                    color: pip.accentColor
                    border.width: 1
                    border.color: Qt.rgba(0, 0, 0, 0.25)
                }

                Binding {
                    target: volumeSlider
                    property: "value"
                    value: pip.mpv ? pip.mpv.volume : 0
                    when: !volumeSlider.pressed
                }
            }
        }
    }

    // Top transparent layer: catches hover across the window, clicks pass
    // through it (NoButton) to the buttons below.
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        z: 100
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        cursorShape: pip.fullscreen && !pip.showControls ? Qt.BlankCursor : Qt.ArrowCursor
        onPositionChanged: pip.revealFullscreenControls()
        onEntered: pip.revealFullscreenControls()
    }
}
