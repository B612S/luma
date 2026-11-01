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
    border.color: "transparent" // Border removed

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
    property bool _staticImageTrack: false

    // Width/height stay zero for audio-only files and radio streams. Embedded
    // cover art is also a video stream (usually MJPEG); do not treat it as video.
    readonly property bool hasVideo: _staticWidth > 0 && _staticHeight > 0
                                     && !_staticImageTrack

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
        _staticImageTrack = false
    }

    function statsProperty(name) {
        if (!mpv) return undefined;
        if (typeof mpv.observedProperty === "function")
            return mpv.observedProperty(name);
        return mpv.property(name); // fallback for older builds
    }

    function currentDisplayTitle() {
        if (typeof Media === "undefined" || !Media.playlist) return "";
        const model = Media.playlist;
        if (!model || model.count === 0 || model.currentIndex < 0) return "";
        return model.data(model.index(model.currentIndex, 0), 262) || ""; // UserRole + 6 (title)
    }

    Timer {
        id: updateTimer
        interval: 1000
        repeat: true
        onTriggered: {
            // Do not issue blocking mpv property reads while a network file is
            // still being opened; that freezes the UI for the whole load.
            const live = typeof Media !== "undefined" && Media.liveStream === true;
            const loading = typeof Media !== "undefined" && Media.isLoading === true;
            if (loading || !mpv || (mpv.duration <= 0 && !live)) return;

            const title = currentDisplayTitle();
            if (title !== "") _staticFile = title;
            if (_staticFile === "")
                _staticFile = safeString(statsProperty("filename"))

            // These are cached by MpvItem (no blocking mpv calls) and can change
            // after an external audio track is attached, so refresh every tick.
            _staticSize = formatSize(statsProperty("file-size"))
            _staticFormat = safeString(statsProperty("file-format"))
            _staticVideoCodec = safeString(statsProperty("video-codec"))
            _staticAudioCodec = safeString(statsProperty("audio-codec"))
            _staticWidth = safeNumber(statsProperty("width"), 0)
            _staticHeight = safeNumber(statsProperty("height"), 0)

            // mpv marks attached cover art as an image/album-art track.
            const imageFlag = statsProperty("current-tracks/video/image");
            const albumFlag = statsProperty("current-tracks/video/albumart");
            if (imageFlag !== undefined || albumFlag !== undefined) {
                _staticImageTrack = safeBool(imageFlag) || safeBool(albumFlag);
            } else {
                const codec = safeString(_staticVideoCodec).toLowerCase();
                const hasAudio = _staticAudioCodec !== "" && _staticAudioCodec !== "N/A";
                _staticImageTrack = hasAudio
                    && (codec === "mjpeg" || codec === "png" || codec === "bmp");
            }

            fileLabel.text = "<b>" + qsTr("stats_file") + ":</b> " + _staticFile
            sizeLabel.text = "<b>" + qsTr("stats_size") + ":</b> " + _staticSize
            formatLabel.text = "<b>" + qsTr("stats_format") + ":</b> " + _staticFormat
                durationLabel.text = "<b>" + qsTr("stats_duration") + ":</b> " + formatTime(mpv.duration)

                videoLabel.text = "<b>" + qsTr("stats_video") + ":</b> " + _staticVideoCodec
                resLabel.text = "<b>" + qsTr("stats_resolution") + ":</b> " + _staticWidth + "x" + _staticHeight
                fpsLabel.text = "<b>" + qsTr("stats_fps") + ":</b> " + safeNumber(statsProperty("container-fps"), 2)

                let vidBitrate = statsProperty("video-bitrate");
            vidBitrateLabel.text = "<b>" + qsTr("stats_vid_bitrate") + ":</b> " + formatBitrate(vidBitrate)

            hwdecLabel.text = "<b>" + qsTr("stats_hwdec") + ":</b> " + safeString(statsProperty("hwdec-current"))
            droppedLabel.text = "<b>" + qsTr("stats_dropped") + ":</b> " + safeNumber(statsProperty("frame-drop-count"), 0)

            audioLabel.text = "<b>" + qsTr("stats_audio") + ":</b> " + _staticAudioCodec
            channelsLabel.text = "<b>" + qsTr("stats_channels") + ":</b> " + safeString(statsProperty("audio-params/channel-count"))
            samplerateLabel.text = "<b>" + qsTr("stats_samplerate") + ":</b> " + safeString(statsProperty("audio-params/samplerate")) + " Hz"

            let audBitrate = statsProperty("audio-bitrate");
            audBitrateLabel.text = "<b>" + qsTr("stats_aud_bitrate") + ":</b> " + formatAudioBitrate(audBitrate)

            // Smart Audio Output query (try QML first, then C++)
            let aoDriver = safeString(statsProperty("current-ao"));
            if (aoDriver === "N/A") {
                const aoParams = statsProperty("audio-out-params");
                if (aoParams && aoParams.driver)
                    aoDriver = aoParams.driver;
            }
            aoLabel.text = "<b>" + qsTr("stats_ao") + ":</b> " + safeString(aoDriver)

            audioDeviceLabel.text = "<b>" + qsTr("stats_audio_device") + ":</b> " + safeString(statsProperty("audio-device"))

            avsyncLabel.text = "<b>" + qsTr("stats_avsync") + ":</b> " + safeNumber(statsProperty("avsync"), 3) + " s"
            cacheLabel.text = "<b>" + qsTr("stats_cache") + ":</b> " + safeNumber(statsProperty("demuxer-cache-duration"), 1) + " s"
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
    }

    property bool hovered: hoverArea.containsMouse

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

            Rectangle { width: parent.width; height: 1; visible: statsPanel.hasVideo; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            Label { id: videoLabel; visible: statsPanel.hasVideo; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: resLabel; visible: statsPanel.hasVideo; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: fpsLabel; visible: statsPanel.hasVideo; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: vidBitrateLabel; visible: statsPanel.hasVideo; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: hwdecLabel; visible: statsPanel.hasVideo; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: droppedLabel; visible: statsPanel.hasVideo; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            Label { id: audioLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: channelsLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: samplerateLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: audBitrateLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: aoLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: audioDeviceLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }

            Rectangle { width: parent.width; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

            Label { id: avsyncLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: cacheLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
        }
    }

    function safeBool(val) {
        if (val === true || val === 1) return true;
        if (val === false || val === 0 || val === undefined || val === null) return false;
        const text = val.toString().toLowerCase();
        return text === "yes" || text === "true";
    }

    function safeString(val) {
        if (val === undefined || val === null || val === "") return "N/A";
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

    // Proper bitrate formatting (mpv reports in bps)
    function formatBitrate(bps) {
        if (!bps || isNaN(bps) || bps <= 0) return "0 kbps";

        if (bps >= 1000000) {
            return (bps / 1000000).toFixed(2) + " Mbps";
        }
        return (bps / 1000).toFixed(0) + " kbps";
    }

    // Guard against mpv glitches (it sometimes reports huge audio values at startup)
    function formatAudioBitrate(bps) {
        if (!bps || isNaN(bps) || bps <= 0) return "0 kbps";

        const kbps = bps / 1000;
        if (kbps > 1500) return "N/A"; // Audio > 1500 kbps is an mpv bug

        return kbps.toFixed(0) + " kbps";
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
