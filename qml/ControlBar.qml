import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root
    height: 56

    property bool pinned: false

    color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                   Kirigami.Theme.backgroundColor.g,
                   Kirigami.Theme.backgroundColor.b, 0.85)
    radius: pinned ? 0 : 8
    border.color: "transparent"

    property var mpv
    property bool hovered: hoverArea.containsMouse
    property bool isSeeking: false
    property bool isVolumeSeeking: false

    // The slider stays at the requested position until mpv actually reports a
    // position close to it. Releasing it earlier made the handle jump back to
    // the old position (and then forward again) on a slow or exact seek.
    property double seekTarget: 0
    property bool seekPending: false
    readonly property double seekTolerance: 1.0
    property bool hasVideo: CastManager.casting || (mpv.duration > 0)

    // Audio file: preview frames while seeking aren't needed. For files opened
    // directly kind is always "video", so check the extension too.
    readonly property bool audioFile: {
        const model = (typeof Media !== "undefined") ? Media.playlist : null;
        if (!model || model.count === 0 || model.currentIndex < 0) return false;
        if (model.data(model.index(model.currentIndex, 0), 258) === "audio")
            return true;
        const path = (model.fileAt(model.currentIndex) || "").split("?")[0].toLowerCase();
        return [".mp3", ".flac", ".aac", ".wav", ".ogg", ".m4a", ".opus", ".wma"]
               .some(ext => path.endsWith(ext));
    }
    // Network stream (YouTube etc.): preview frames are unavailable/black.
    readonly property bool networkStream: {
        if (typeof mpv === "undefined" || mpv === null) return false;
        const source = mpv.source || "";
        return source.startsWith("http://") || source.startsWith("https://");
    }
    // Only show previews for local video.
    readonly property bool showSeekPreview: !CastManager.casting && !root.audioFile
                                            && !root.isLive && !root.networkStream

    // Live stream (online TV/radio): there is no rewind/DVR any more.
    // The control bar only shows a LIVE indicator; if playback falls behind
    // the live edge (network pause, buffering), followLiveEdge() jumps back.
    readonly property bool isLive: !CastManager.casting && typeof Media !== "undefined"
                                   && Media.liveStream === true
    Timer {
        id: liveFollowTimer
        interval: 3000
        repeat: true
        running: root.isLive && !root.displayPaused
        onTriggered: root.followLiveEdge()
    }

    Connections {
        target: mpv
        function onSourceChanged() {
            // A pending seek belongs to the old file; never keep the new
            // file's slider pinned to its position.
            root.finishSeek();
            // Drop the last preview of the previous file.
            if (previewImage) previewImage.source = "";
            if (seekTooltip) {
                seekTooltip.currentThumbId = "";
                seekTooltip.pendingTime = -1;
            }
            if (root.isLive) liveFollowTimer.restart();
        }
        function onDurationChanged() {
            // The file was stopped/ended while a seek was still settling.
            if (mpv.duration <= 0) root.finishSeek();
        }
    }

    property bool isPlaylistOpen: false
    signal playlistToggleRequested()

    property bool isOpen: true
    // In fullscreen the bar can be disabled in the interface settings.
    property bool panelsAllowed: true
    visible: opacity > 0 && panelsAllowed
    opacity: 1.0

    property double displayDuration: CastManager.casting
    ? (CastManager.remoteDuration > 0 ? CastManager.remoteDuration : (mpv.duration || 0))
    : (mpv.duration || 0)

    property double displayPosition: CastManager.casting ? CastManager.remotePosition : (mpv.position || 0)
    onDisplayPositionChanged: root.checkSeekFinished()
    property bool displayPaused: CastManager.casting ? !CastManager.isRemotePlaying : mpv.paused
    property double displayVolume: CastManager.casting ? CastManager.remoteVolume : (mpv.volume || 0)

    NumberAnimation {
        id: fadeAnim
        target: root
        property: "opacity"
        duration: 150
        easing.type: Easing.InOutQuad
    }

    Timer {
        id: hoverLostTimer
        interval: 250
        onTriggered: {
            if (!hoverArea.containsMouse && !root.isOpen) {
                fadeAnim.stop();
                fadeAnim.from = root.opacity;
                fadeAnim.to = 0.0;
                fadeAnim.start();
            }
        }
    }

    onIsOpenChanged: {
        if (isOpen) {
            hoverLostTimer.stop();
            fadeAnim.stop();
            fadeAnim.from = root.opacity;
            fadeAnim.to = 1.0;
            fadeAnim.start();
        } else {
            if (!hoverArea.containsMouse) {
                fadeAnim.stop();
                fadeAnim.from = root.opacity;
                fadeAnim.to = 0.0;
                fadeAnim.start();
            } else {
                hoverLostTimer.restart();
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onClicked: (mouse) => { mouse.accepted = false }
        onEntered: {
            hoverLostTimer.stop();
            if (!root.isOpen) {
                root.isOpen = true;
            }
            if (root.opacity < 1.0) {
                fadeAnim.stop();
                fadeAnim.from = root.opacity;
                fadeAnim.to = 1.0;
                fadeAnim.start();
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12
        z: 1

        Item {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            Kirigami.Icon {
                anchors.centerIn: parent
                source: root.displayPaused ? "media-playback-start-symbolic" : "media-playback-pause-symbolic"
                color: Kirigami.Theme.textColor
                width: 22
                height: 22
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (CastManager.casting) {
                        if (CastManager.isRemotePlaying) CastManager.pause()
                            else CastManager.play()
                    } else {
                        mpv.paused = !mpv.paused
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            Slider {
                id: seekSlider
                anchors.fill: parent
                from: 0
                to: root.displayDuration
                focus: false
                visible: !root.isLive
                enabled: root.hasVideo && !root.isLive

                property bool seeking: false
                value: seeking ? value
                               : (root.seekPending ? root.seekTarget
                                                  : root.displayPosition)

                onValueChanged: {
                    if (seeking) root.seekTarget = value;
                }

                onPressedChanged: {
                    if (pressed) {
                        if (!root.hasVideo) return;
                        seekDelayTimer.stop();
                        seekWatchdog.stop();
                        seeking = true;
                        root.seekPending = false;
                        root.seekTarget = value;
                        root.isSeeking = true;
                        shortcuts.setSliderSeeking(true);
                    } else if (seeking) {
                        const target = seekSlider.value;
                        root.seekTarget = target;
                        // Keep the handle at the requested position while mpv
                        // performs the seek; displayPosition may still report
                        // the old position for a moment.
                        root.seekPending = true;
                        seekWatchdog.restart();

                        if (CastManager.casting) {
                            CastManager.seekTo(target)
                        } else {
                            mpv.seekExact(target)
                        }

                        seeking = false;
                        root.isSeeking = false;
                        shortcuts.setSliderSeeking(false);
                        // A click on (or very near) the current position may not
                        // produce any position change to wait for.
                        root.checkSeekFinished();
                        seekDelayTimer.start();
                    }
                }

                // Fallback for a seek that never reports a matching position
                // (for example an unseekable stream): stop holding the handle.
                Timer {
                    id: seekWatchdog
                    interval: 5000
                    onTriggered: root.finishSeek()
                }

                Timer {
                    id: seekDelayTimer
                    interval: 300
                    onTriggered: {
                        Osd.requestTimeOsd(root.seekTarget, root.displayDuration)
                        Qt.callLater(() => { Osd.endTemporaryOsd() })
                    }
                }

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    implicitWidth: 200
                    implicitHeight: 12
                    width: seekSlider.availableWidth
                    height: implicitHeight
                    radius: 6
                    color: root.hasVideo
                    ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                    : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)

                    Rectangle {
                        width: seekSlider.leftPadding + (seekSlider.visualPosition * (seekSlider.availableWidth - seekSlider.handle.width)) + (seekSlider.handle.width / 2)
                        height: parent.height
                        color: root.hasVideo ? Kirigami.Theme.highlightColor : "transparent"
                        radius: 6
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    implicitWidth: 16
                    implicitHeight: 16
                    radius: 8
                    color: root.hasVideo ? Kirigami.Theme.textColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.5)
                }
            }

            Label {
                visible: root.isLive
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 2
                text: "● " + qsTr("live_now")
                color: Kirigami.Theme.negativeTextColor
                font.pixelSize: 14
                font.bold: true
            }

            Rectangle {
                id: seekTooltip
                visible: seekSlider.seeking && !root.isLive
                z: 100
                width: root.showSeekPreview ? 160 : (seekTimeText.implicitWidth + 16)
                height: root.showSeekPreview ? (90 + 24) : 24
                radius: 6
                color: "#1a1a1a"
                clip: true

                x: {
                    let handleCenter = seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - seekSlider.handle.width) + seekSlider.handle.width / 2;
                    let targetX = handleCenter - width / 2;
                    let minX = 0;
                    let maxX = seekSlider.width - width;
                    return Math.max(minX, Math.min(maxX, targetX));
                }
                anchors.bottom: seekSlider.verticalCenter
                anchors.bottomMargin: 12

                property real pendingTime: -1
                property string currentThumbId: ""

                Timer {
                    id: thumbDebounce
                    // Only request previews for local video.
                    interval: 70
                    repeat: true
                    running: seekSlider.seeking && root.showSeekPreview
                    onTriggered: {
                        if (seekTooltip.pendingTime >= 0) {
                            let id = "thumb";
                            seekTooltip.currentThumbId = id;
                            mpv.requestThumbnail(seekTooltip.pendingTime, id);
                            seekTooltip.pendingTime = -1;
                        }
                    }
                }

                Connections {
                    target: mpv
                    function onThumbnailReady(id) {
                        if (id.startsWith(seekTooltip.currentThumbId)) {
                            previewImage.source = ""
                            previewImage.source = "image://thumbnail/" + id;
                        }
                    }
                }

                Connections {
                    target: seekSlider
                    function onValueChanged() {
                        if (seekSlider.seeking && root.showSeekPreview)
                            seekTooltip.pendingTime = seekSlider.value;
                    }
                }

                Image {
                    id: previewImage
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 90
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: false
                    visible: root.showSeekPreview
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 24
                    color: "#1a1a1a"

                    Text {
                        id: seekTimeText
                        anchors.centerIn: parent
                        text: formatTime(seekSlider.value)
                        color: "white"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }

        Label {
            visible: !root.isLive
            text: formatTime(root.displayPosition) + " / " + formatTime(root.displayDuration)
            color: Kirigami.Theme.textColor
            font.pixelSize: 13
            font.weight: Font.Medium
        }

        Item {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            Kirigami.Icon {
                anchors.centerIn: parent
                source: (CastManager.casting ? (CastManager.remoteVolume === 0) : (mpv.muted || mpv.volume === 0))
                ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic"
                color: Kirigami.Theme.textColor
                width: 22
                height: 22
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    if (CastManager.casting) {
                        if (CastManager.remoteVolume > 0) {
                            CastManager.setVolume(0)
                            Osd.showVolume(0)
                        } else {
                            CastManager.setVolume(50)
                            Osd.showVolume(50)
                        }
                    } else {
                        if (mpv.volume === 0) {
                            mpv.volume = 50;
                            mpv.muted = false;
                        } else {
                            mpv.muted = !mpv.muted;
                        }
                        Osd.showVolume()
                    }
                }
            }
        }

        Slider {
            id: volSlider
            from: 0
            to: 100
            focus: false
            Layout.preferredWidth: 120

            // Local display variable to avoid poking mpv directly
            property real localValue: root.displayVolume
            property bool seeking: false

            value: seeking ? value : localValue

            onMoved: {
                if (seeking) {
                    if (CastManager.casting) {
                        CastManager.setVolume(value) // Send to Chromecast
                    } else {
                        mpv.volume = value           // Send to local mpv
                    }
                    Osd.showVolume(value)
                }
            }

            onPressedChanged: {
                if (pressed) {
                    seeking = true
                    root.isVolumeSeeking = true
                    Osd.showVolume()
                } else {
                    if (CastManager.casting) {
                        CastManager.setVolume(value)
                    } else {
                        mpv.volume = value
                    }
                    Osd.showVolume()

                    localValue = value
                    volDelayTimer.start()
                }
            }

            Timer {
                id: volDelayTimer
                interval: 100
                onTriggered: {
                    volSlider.seeking = false
                    root.isVolumeSeeking = false
                }
            }

            // Sync with Chromecast
            Connections {
                target: CastManager
                function onRemoteStatusChanged() {
                    if (CastManager.casting && !volSlider.seeking) {
                        volSlider.localValue = CastManager.remoteVolume;
                    }
                }
            }

            // Sync with mpv (only when NOT casting)
            Connections {
                target: mpv
                function onVolumeChanged() {
                    if (!CastManager.casting && !volSlider.seeking) {
                        volSlider.localValue = mpv.volume;
                    }
                }
            }

            background: Rectangle {
                x: volSlider.leftPadding
                y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                implicitWidth: 120
                implicitHeight: 12
                width: volSlider.availableWidth
                height: implicitHeight
                radius: 6
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)

                Rectangle {
                    width: volSlider.leftPadding + (volSlider.visualPosition * (volSlider.availableWidth - volSlider.handle.width)) + (volSlider.handle.width / 2)
                    height: parent.height
                    color: Kirigami.Theme.highlightColor
                    radius: 6
                }
            }
            handle: Rectangle {
                x: volSlider.leftPadding + volSlider.visualPosition * (volSlider.availableWidth - width)
                y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                implicitWidth: 16
                implicitHeight: 16
                radius: 8
                color: Kirigami.Theme.textColor
            }
        }

        Item {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            Rectangle {
                anchors.fill: parent
                color: root.isPlaylistOpen
                ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                : "transparent"
                radius: 4
            }
            Kirigami.Icon {
                anchors.centerIn: parent
                source: "view-list-symbolic"
                color: Kirigami.Theme.textColor
                width: 22
                height: 22
            }
            MouseArea {
                anchors.fill: parent
                onClicked: root.playlistToggleRequested()
            }
        }
    }

    // Live streams are always kept at the live edge. A pause or a network
    // hiccup can leave playback far behind; jump back instead of offering a
    // DVR window that no longer exists.
    function followLiveEdge() {
        if (!root.isLive || root.displayPaused) return;
        // A live HLS stream without DVR is not seekable at all; asking mpv to
        // seek would only spam "Cannot seek in this stream" every 3 seconds.
        if (mpv.observedProperty("seekable") !== true) return;
        // Read the observed value directly: it is not a QML property with a
        // change signal, and a cached binding would go stale.
        const cacheTime = Number(mpv.observedProperty("demuxer-cache-time"));
        const edge = cacheTime > 0 ? cacheTime : root.displayDuration;
        if (!(edge > 0)) return;
        if (edge - root.displayPosition > 4)
            mpv.seekFast(edge);
    }

    function checkSeekFinished() {
        if (!root.seekPending) return;
        if (Math.abs(root.displayPosition - root.seekTarget) <= root.seekTolerance)
            root.finishSeek();
    }

    function finishSeek() {
        root.seekPending = false;
        if (seekWatchdog) seekWatchdog.stop();
    }

    function formatTime(seconds) {
        let s = Math.floor(seconds || 0);
        let h = Math.floor(s / 3600);
        let m = Math.floor((s % 3600) / 60);
        s = s % 60;
        return (h > 0 ? h + ":" : "") + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
}
