import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: root
    height: 56
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                   Kirigami.Theme.backgroundColor.g,
                   Kirigami.Theme.backgroundColor.b, 0.85)
    radius: 8
    border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                          Kirigami.Theme.textColor.g,
                          Kirigami.Theme.textColor.b, 0.2)

    property var mpv
    property bool hovered: hoverArea.containsMouse
    property bool isSeeking: false
    property bool isVolumeSeeking: false
    property bool hasVideo: (mpv.duration > 0)

    property bool isPlaylistOpen: false
    signal playlistToggleRequested()

    property bool isOpen: true
    visible: opacity > 0
    opacity: 1.0

    NumberAnimation {
        id: fadeAnim
        target: root
        property: "opacity"
        duration: 150
        easing.type: Easing.InOutQuad
        onFinished: {
            if (fadeAnim.to === 0.0) {
                root.visible = false;
            }
        }
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
            root.visible = true;
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
                source: mpv.paused ? "media-playback-start" : "media-playback-pause"
                color: Kirigami.Theme.textColor
                width: 22
                height: 22
            }
            MouseArea {
                anchors.fill: parent
                onClicked: mpv.paused = !mpv.paused
            }
        }

        // Контейнер для слайдера и всплывающей подсказки
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            Slider {
                id: seekSlider
                anchors.fill: parent
                from: 0
                to: mpv.duration || 0
                focus: false
                enabled: root.hasVideo

                property bool seeking: false
                value: seeking ? value : (mpv.position || 0)

                onPressedChanged: {
                    if (pressed && root.hasVideo) {
                        seeking = true
                        root.isSeeking = true
                        shortcuts.setSliderSeeking(true)
                    } else {
                        mpv.seekExact(value)
                        Osd.requestTimeOsd(value, mpv.duration)

                        seekDelayTimer.start()
                        shortcuts.setSliderSeeking(false)
                        Qt.callLater(() => { Osd.endTemporaryOsd() })
                    }
                }

                Timer {
                    id: seekDelayTimer
                    interval: 200
                    onTriggered: {
                        seekSlider.seeking = false
                        root.isSeeking = false
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

            // Всплывающая подсказка
            Rectangle {
                id: seekTooltip
                visible: seekSlider.seeking
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
                    interval: 30
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
                            previewImage.source = "image://thumbnail/" + id
                        }
                    }
                }

                Connections {
                    target: seekSlider
                    function onValueChanged() {
                        if (seekSlider.seeking) {
                            seekTooltip.pendingTime = seekSlider.value;
                            thumbDebounce.restart();
                        }
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

        // Время воспроизведения
        Label {
            text: formatTime(mpv.position) + " / " + formatTime(mpv.duration)
            color: Kirigami.Theme.textColor
            font.pixelSize: 13
            font.weight: Font.Medium
        }

        Item {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            Kirigami.Icon {
                anchors.centerIn: parent
                source: (mpv.muted || mpv.volume === 0) ? "audio-volume-muted" : "audio-volume-high"
                color: Kirigami.Theme.textColor
                width: 22
                height: 22
            }
            MouseArea {
                anchors.fill: parent
                onClicked: {
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

        Slider {
            id: volSlider
            from: 0
            to: 100
            focus: false
            Layout.preferredWidth: 120

            property bool seeking: false
            value: mpv.volume || 0

            onMoved: {
                if (seeking) {
                    mpv.volume = value
                    Osd.showVolume(value)
                }
            }

            onPressedChanged: {
                if (pressed) {
                    seeking = true
                    root.isVolumeSeeking = true
                    Osd.showVolume()
                } else {
                    mpv.volume = value
                    Osd.showVolume()
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
