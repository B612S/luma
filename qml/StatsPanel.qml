import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: statsPanel
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                   Kirigami.Theme.backgroundColor.g,
                   Kirigami.Theme.backgroundColor.b, 0.85)
    radius: 8
    border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                          Kirigami.Theme.textColor.g,
                          Kirigami.Theme.textColor.b, 0.2)
    property var mpv: null
    property bool isOpen: false
    property real maxHeight: 600

    property string _staticFile: ""
    property string _staticSize: ""
    property string _staticFormat: ""
    property int    _staticWidth: 0
    property int    _staticHeight: 0
    property string _staticVideoCodec: ""
    property string _staticAudioCodec: ""

    height: Math.min(contentColumn.implicitHeight + 32, maxHeight)
    Behavior on height {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    visible: opacity > 0
    opacity: 0.0

    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
    }

    onIsOpenChanged: {
        if (isOpen) {
            statsPanel.opacity = 1.0
            resetStaticCache()
            updateTimer.start()
        } else {
            statsPanel.opacity = 0.0
            updateTimer.stop()
        }
    }

    Connections {
        target: mpv
        function onSourceChanged() {
            resetStaticCache()
        }
    }

    function resetStaticCache() {
        _staticFile = ""
        _staticSize = ""
        _staticFormat = ""
        _staticWidth = 0
        _staticHeight = 0
        _staticVideoCodec = ""
        _staticAudioCodec = ""
    }

    Timer {
        id: updateTimer
        interval: 1000
        repeat: true
        onTriggered: {
            if (_staticFile === "") {
                _staticFile = safeString(mpv.property("filename"))
                _staticSize = formatSize(mpv.property("file-size"))
                _staticFormat = safeString(mpv.property("file-format"))
                _staticVideoCodec = safeString(mpv.property("video-codec"))
                _staticAudioCodec = safeString(mpv.property("audio-codec"))
            }

            if (_staticWidth === 0) {
                _staticWidth = safeNumber(mpv.property("width"), 0)
                _staticHeight = safeNumber(mpv.property("height"), 0)
            }

            fileLabel.text = "<b>" + qsTr("stats_file") + ":</b> " + _staticFile
            sizeLabel.text = "<b>" + qsTr("stats_size") + ":</b> " + _staticSize
            formatLabel.text = "<b>" + qsTr("stats_format") + ":</b> " + _staticFormat
                durationLabel.text = "<b>" + qsTr("stats_duration") + ":</b> " + formatTime(mpv.duration)

                videoLabel.text = "<b>" + qsTr("stats_video") + ":</b> " + _staticVideoCodec
                resLabel.text = "<b>" + qsTr("stats_resolution") + ":</b> " + _staticWidth + "x" + _staticHeight
                fpsLabel.text = "<b>" + qsTr("stats_fps") + ":</b> " + safeNumber(mpv.property("container-fps"), 2)
                let vidBitrate = mpv.property("video-bitrate");
            vidBitrateLabel.text = "<b>" + qsTr("stats_vid_bitrate") + ":</b> " + formatBitrate(vidBitrate)
            hwdecLabel.text = "<b>" + qsTr("stats_hwdec") + ":</b> " + safeString(mpv.property("hwdec-current"))
            droppedLabel.text = "<b>" + qsTr("stats_dropped") + ":</b> " + safeNumber(mpv.property("frame-drop-count"), 0)

            audioLabel.text = "<b>" + qsTr("stats_audio") + ":</b> " + _staticAudioCodec
            channelsLabel.text = "<b>" + qsTr("stats_channels") + ":</b> " + safeString(mpv.property("audio-params/channel-count"))
            samplerateLabel.text = "<b>" + qsTr("stats_samplerate") + ":</b> " + safeString(mpv.property("audio-params/samplerate")) + " Hz"
            let audBitrate = mpv.property("audio-bitrate");
            audBitrateLabel.text = "<b>" + qsTr("stats_aud_bitrate") + ":</b> " + formatBitrate(audBitrate)
            aoLabel.text = "<b>" + qsTr("stats_ao") + ":</b> " + safeString(mpv.getAudioOutput())

            avsyncLabel.text = "<b>" + qsTr("stats_avsync") + ":</b> " + safeNumber(mpv.property("avsync"), 3) + " s"
            cacheLabel.text = "<b>" + qsTr("stats_cache") + ":</b> " + safeNumber(mpv.property("demuxer-cache-duration"), 1) + " s"
        }
    }

    // Невидимая зона для перехвата кликов (чтобы не нажимать на видео под панелью)
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
    }

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height
        boundsBehavior: Flickable.StopAtBounds
        interactive: true

        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
            stepSize: 0.1
        }

        Column {
            id: contentColumn
            width: flickable.width - 16
            spacing: 4

            Label { id: fileLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: sizeLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: formatLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: durationLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            Label { id: videoLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: resLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: fpsLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: vidBitrateLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: hwdecLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: droppedLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            Label { id: audioLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: channelsLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: samplerateLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: audBitrateLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: aoLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            Label { id: avsyncLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: cacheLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
        }
    }

    function safeString(val) {
        if (val === undefined || val === null) return "N/A";
        let str = val.toString();
        if (str.includes("ErrorReturn") || str.includes("MPV_ERROR") || str.includes("[object")) return "N/A";
        return str;
    }

    function safeNumber(val, decimals) {
        if (val === undefined || val === null) return (0).toFixed(decimals);
        let num = Number(val);
        if (isNaN(num)) return (0).toFixed(decimals);
        return num.toFixed(decimals);
    }

    function formatSize(bytes) {
        if (!bytes || isNaN(bytes)) return "N/A";
        let mb = bytes / (1024 * 1024);
        return mb.toFixed(2) + " MiB";
    }

    function formatBitrate(bps) {
        if (!bps || isNaN(bps)) return "0 kbps";
        if (bps > 100000) bps = bps / 1000;
        return bps.toFixed(0) + " kbps";
    }

    function formatTime(seconds) {
        if (seconds === undefined || seconds === null) return "00:00";
        let total = Math.floor(seconds);
        let h = Math.floor(total / 3600);
        let m = Math.floor((total % 3600) / 60);
        let s = total % 60;
        if (h > 0) {
            return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
        }
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
}
