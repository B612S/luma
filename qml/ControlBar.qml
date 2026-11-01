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
    property bool hasVideo: CastManager.casting || (mpv.duration > 0)

    // Прямой поток (радио): перематывать можно только внутри кэша,
    // поэтому ползунок показывает окно кэша и прижат вправо.
    readonly property bool isLive: !CastManager.casting && typeof Media !== "undefined"
                                   && Media.liveStream === true
    readonly property real liveWindow: 300 // секунд; совпадает с cache-secs в mpv
    // В эфире mpv держит буфер впереди плеера, поэтому «длительность» опережает
    // позицию даже без перемотки. Минимальное наблюдённое отставание считаем
    // точкой эфира и отчитываем перемотку уже от него.
    property real liveBaseline: -1
    readonly property real liveRawBehind: Math.max(0, root.displayDuration - root.displayPosition)
    readonly property real liveBehind: root.isLive
        ? (root.liveBaseline < 0
               ? 0   // точку эфира ещё не измерили — считаем, что мы в эфире
               : Math.max(0, Math.min(root.liveWindow, root.liveRawBehind - Math.max(0, root.liveBaseline))))
        : 0

    onIsLiveChanged: root.liveBaseline = -1

    Connections {
        target: mpv
        function onPositionChanged() {
            if (!root.isLive || seekSlider.seeking || root.displayDuration <= 0) return;
            const raw = root.liveRawBehind;
            if (root.liveBaseline < 0) {
                root.liveBaseline = raw;
                return;
            }
            // В начале потока буфер впереди ещё пустой и растёт; пока мы в эфире,
            // даём baseline подтягиваться за ним (но не прыжком — отмотку не съедаем).
            if (raw > root.liveBaseline && raw - root.liveBaseline <= 2)
                root.liveBaseline = raw;
        }
        function onSourceChanged() { root.liveBaseline = -1; }
    }

    property bool isPlaylistOpen: false
    signal playlistToggleRequested()

    property bool isOpen: true
    // В полноэкранном режиме панель можно отключить в настройках интерфейса.
    property bool panelsAllowed: true
    visible: opacity > 0 && panelsAllowed
    opacity: 1.0

    property double displayDuration: CastManager.casting
    ? (CastManager.remoteDuration > 0 ? CastManager.remoteDuration : (mpv.duration || 0))
    : (mpv.duration || 0)

    property double displayPosition: CastManager.casting ? CastManager.remotePosition : (mpv.position || 0)
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
                source: root.displayPaused ? "media-playback-start" : "media-playback-pause"
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
                to: root.isLive ? root.liveWindow : root.displayDuration
                focus: false
                enabled: root.hasVideo

                property bool seeking: false
                value: seeking ? value
                               : (root.isLive ? root.liveWindow - root.liveBehind
                                              : root.displayPosition)

                onPressedChanged: {
                    if (pressed && root.hasVideo) {
                        seeking = true
                        root.isSeeking = true
                        shortcuts.setSliderSeeking(true)
                    } else {
                        if (CastManager.casting) {
                            CastManager.seekTo(value)
                        } else if (root.isLive) {
                            // value: 0 — начало кэша, liveWindow — прямой эфир.
                            const behind = Math.max(0, root.liveWindow - value);
                            const livePos = Math.max(0, root.displayDuration - Math.max(0, root.liveBaseline));
                            if (livePos > 0)
                                mpv.seekExact(Math.max(0, livePos - behind));
                        } else {
                            mpv.seekExact(value)
                        }

                        seekDelayTimer.start()
                        shortcuts.setSliderSeeking(false)
                    }
                }

                Timer {
                    id: seekDelayTimer
                    interval: 300
                    onTriggered: {
                        seekSlider.seeking = false
                        root.isSeeking = false
                        Osd.requestTimeOsd(seekSlider.value, root.displayDuration)
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

            Rectangle {
                id: seekTooltip
                visible: seekSlider.seeking && !root.isLive
                z: 100
                width: 160
                height: 90 + 24
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
                y: seekSlider.topPadding + seekSlider.availableHeight / 2 - seekSlider.handle.height / 2 - 4 - height

                property real pendingTime: -1
                property string currentThumbId: ""

                Timer {
                    id: thumbDebounce
                    // Запрашиваем превью регулярно во время перетаскивания:
                    // декодирование идёт в фоновом потоке, а промежуточные
                    // запросы склеиваются в MpvItem.
                    interval: 70
                    repeat: true
                    running: seekSlider.seeking && !CastManager.casting
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
                        if (seekSlider.seeking && !CastManager.casting)
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
                    visible: !CastManager.casting
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 24
                    color: "#1a1a1a"

                    Text {
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
            text: root.isLive
                  ? (seekSlider.seeking
                        ? "-" + formatTime(root.liveWindow - seekSlider.value)
                        : (root.liveBehind < 1 ? qsTr("live_now") : "-" + formatTime(root.liveBehind)))
                  : formatTime(root.displayPosition) + " / " + formatTime(root.displayDuration)
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
                ? "audio-volume-muted" : "audio-volume-high"
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

            // Локальная переменная для отображения, чтобы не дергать mpv напрямую
            property real localValue: root.displayVolume
            property bool seeking: false

            value: seeking ? value : localValue

            onMoved: {
                if (seeking) {
                    if (CastManager.casting) {
                        CastManager.setVolume(value) // Отправляем в Chromecast
                    } else {
                        mpv.volume = value           // Отправляем в локальный mpv
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

            // Синхронизация с Chromecast
            Connections {
                target: CastManager
                function onRemoteStatusChanged() {
                    if (CastManager.casting && !volSlider.seeking) {
                        volSlider.localValue = CastManager.remoteVolume;
                    }
                }
            }

            // Синхронизация с mpv (только если НЕТ каста)
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

    function formatTime(seconds) {
        let s = Math.floor(seconds || 0);
        let h = Math.floor(s / 3600);
        let m = Math.floor((s % 3600) / 60);
        s = s % 60;
        return (h > 0 ? h + ":" : "") + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
}
