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

    // Full metadata for audio files and radio streams (mpv metadata plus the
    // tags read asynchronously by MediaManager for local files).
    property var tagList: []
    property var secondaryTagList: []
    property string primaryTitle: ""
    property var _fileTags: ({})
    property string _tagsInfoPath: ""
    property string _tagsSignature: ""
    property var statLines: []

    readonly property bool audioOrRadio: !hasVideo
    readonly property bool isRadio: typeof Media !== "undefined" && Media.liveStream === true
    readonly property bool showTags: audioOrRadio && secondaryTagList.length > 0

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
            statsContextMenu.close()
        }
    }

    Connections {
        target: mpv
        function onSourceChanged() {
            resetStaticCache()
        }
    }

    Connections {
        target: Media
        function onFileInfoReady(path, info) {
            // MediaManager reads TagLib/FFmpeg tags off the GUI thread. Only
            // local audio files need this extra source; radio tags come from
            // mpv's ICY metadata.
            if (path !== statsPanel._tagsInfoPath) return;
            if (!info || info.isStream === true || info.isAudio !== true) return;

            const merged = {};
            const all = info.tagsAll || ({});
            for (const key in all) {
                if (all[key] !== undefined && all[key] !== null && all[key] !== "")
                    merged[String(key).toLowerCase()] = String(all[key]);
            }
            const tags = info.tags || ({});
            for (const key in tags) {
                if (tags[key] !== undefined && tags[key] !== null && tags[key] !== "")
                    merged[String(key).toLowerCase()] = String(tags[key]);
            }
            statsPanel._fileTags = merged;
            statsPanel.updateTags();
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
        tagList = []
        secondaryTagList = []
        primaryTitle = ""
        _fileTags = ({})
        _tagsInfoPath = ""
        _tagsSignature = ""
        statLines = []
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

            requestTags();

            const title = currentDisplayTitle();
            if (title !== "") _staticFile = title;
            if (_staticFile === "")
                _staticFile = safeString(statsProperty("filename"))

            // These are cached by MpvItem (no blocking mpv calls) and can change
            // after an external audio track is attached, so refresh every tick.
            _staticSize = formatSize(statsProperty("file-size"))
            _staticFormat = formatContainer(statsProperty("file-format"))
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

            updateTags();
            const shownTitle = displayTitle();

            fileLabel.text = "<b>" + qsTr("stats_file") + ":</b> " + shownTitle
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
            audBitrateLabel.text = "<b>" + qsTr("stats_aud_bitrate") + ":</b> " + formatBitrate(audBitrate)

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

            const video = _staticWidth > 0 && _staticHeight > 0 && !_staticImageTrack;
            let lines = [];
            lines.push(qsTr("stats_file") + ": " + shownTitle);
            lines.push(qsTr("stats_size") + ": " + _staticSize);
            lines.push(qsTr("stats_format") + ": " + _staticFormat);
            lines.push(qsTr("stats_duration") + ": " + formatTime(mpv.duration));
            if (video) {
                lines.push(qsTr("stats_video") + ": " + _staticVideoCodec);
                lines.push(qsTr("stats_resolution") + ": " + _staticWidth + "x" + _staticHeight);
                lines.push(qsTr("stats_fps") + ": " + safeNumber(statsProperty("container-fps"), 2));
                lines.push(qsTr("stats_vid_bitrate") + ": " + formatBitrate(vidBitrate));
                lines.push(qsTr("stats_hwdec") + ": " + safeString(statsProperty("hwdec-current")));
                lines.push(qsTr("stats_dropped") + ": " + safeNumber(statsProperty("frame-drop-count"), 0));
            }
            lines.push(qsTr("stats_audio") + ": " + _staticAudioCodec);
            lines.push(qsTr("stats_channels") + ": " + safeString(statsProperty("audio-params/channel-count")));
            lines.push(qsTr("stats_samplerate") + ": " + safeString(statsProperty("audio-params/samplerate")) + " Hz");
            lines.push(qsTr("stats_aud_bitrate") + ": " + formatBitrate(audBitrate));
            lines.push(qsTr("stats_ao") + ": " + safeString(aoDriver));
            lines.push(qsTr("stats_audio_device") + ": " + safeString(statsProperty("audio-device")));
            lines.push(qsTr("stats_avsync") + ": " + safeNumber(statsProperty("avsync"), 3) + " s");
            lines.push(qsTr("stats_cache") + ": " + safeNumber(statsProperty("demuxer-cache-duration"), 1) + " s");
            statLines = lines;
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

            // For audio and radio, put the main tags right under the title.
            Label {
                id: artistLabel
                text: statsPanel.primaryLine(["artist"], qsTr("stats_tag_artist"))
                visible: text !== ""
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
                width: parent.width
                wrapMode: Text.Wrap
            }
            Label {
                id: albumLabel
                text: statsPanel.primaryLine(["album"], qsTr("stats_tag_album"))
                visible: text !== ""
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
                width: parent.width
                wrapMode: Text.Wrap
            }
            Label {
                id: genreLabel
                text: statsPanel.primaryLine(["genre", "icy-genre"], qsTr("stats_tag_genre"))
                visible: text !== ""
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
                width: parent.width
                wrapMode: Text.Wrap
            }
            Label {
                id: yearLabel
                text: statsPanel.primaryLine(["date", "year"], qsTr("stats_tag_year"))
                visible: text !== ""
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
                width: parent.width
                wrapMode: Text.Wrap
            }

            Label { id: sizeLabel; visible: !statsPanel.isRadio; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: formatLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }
            Label { id: durationLabel; textFormat: Text.RichText; color: Kirigami.Theme.textColor; width: parent.width; wrapMode: Text.Wrap; text: "" }

            // Any remaining tags (track, disc, comment, encoder, ...).
            Repeater {
                model: statsPanel.secondaryTagList
                delegate: Label {
                    visible: statsPanel.showTags
                    textFormat: Text.RichText
                    color: Kirigami.Theme.textColor
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "<b>" + statsPanel.escapeHtml(modelData.label) + ":</b> "
                          + statsPanel.escapeHtml(modelData.value)
                }
            }

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

    function requestTags() {
        if (typeof Media === "undefined" || !mpv) return;
        // The tag section is only for audio and radio.
        if (hasVideo) return;
        const path = mpv.source;
        if (!path || path.indexOf("http://") === 0 || path.indexOf("https://") === 0) return;
        if (path === _tagsInfoPath) return;
        _tagsInfoPath = path;
        Media.requestFileInfo(path);
    }

    function displayTitle() {
        const audio = _staticWidth <= 0 || _staticHeight <= 0 || _staticImageTrack;
        if (audio && primaryTitle !== "") return primaryTitle;
        return _staticFile;
    }

    function primaryValue(keys) {
        // The primary block is only for audio files and radio streams.
        if (_staticWidth > 0 && _staticHeight > 0 && !_staticImageTrack) return "";
        for (const key of keys) {
            for (const tag of tagList) {
                if (tag.key === key && tag.value !== "") return tag.value;
            }
        }
        return "";
    }

    function primaryLine(keys, label) {
        const value = primaryValue(keys);
        if (value === "") return "";
        return "<b>" + label + ":</b> " + escapeHtml(value);
    }

    function primaryPlain(keys, label) {
        const value = primaryValue(keys);
        return value === "" ? "" : label + ": " + value;
    }

    function tagDisplayLabel(key) {
        const k = String(key).toLowerCase();
        const labels = {
            "title": qsTr("stats_tag_title"),
            "artist": qsTr("stats_tag_artist"),
            "album": qsTr("stats_tag_album"),
            "album_artist": qsTr("stats_tag_album_artist"),
            "albumartist": qsTr("stats_tag_album_artist"),
            "genre": qsTr("stats_tag_genre"),
            "date": qsTr("stats_tag_year"),
            "year": qsTr("stats_tag_year"),
            "track": qsTr("stats_tag_track"),
            "tracknumber": qsTr("stats_tag_track"),
            "tracktotal": qsTr("stats_tag_track_total"),
            "totaltracks": qsTr("stats_tag_track_total"),
            "track_total": qsTr("stats_tag_track_total"),
            "total_tracks": qsTr("stats_tag_track_total"),
            "disc": qsTr("stats_tag_disc"),
            "discnumber": qsTr("stats_tag_disc"),
            "disctotal": qsTr("stats_tag_disc_total"),
            "totaldiscs": qsTr("stats_tag_disc_total"),
            "disc_total": qsTr("stats_tag_disc_total"),
            "total_discs": qsTr("stats_tag_disc_total"),
            "comment": qsTr("stats_tag_comment"),
            "composer": qsTr("stats_tag_composer"),
            "encoder": qsTr("stats_tag_encoder"),
            "encoded_by": qsTr("stats_tag_encoded_by"),
            "encodedby": qsTr("stats_tag_encoded_by"),
            "encoded-by": qsTr("stats_tag_encoded_by"),
            "copyright": qsTr("stats_tag_copyright"),
            "publisher": qsTr("stats_tag_publisher"),
            "organization": qsTr("stats_tag_publisher"),
            "label": qsTr("stats_tag_publisher"),
            "language": qsTr("stats_tag_language"),
            "bpm": qsTr("stats_tag_bpm"),
            "isrc": qsTr("stats_tag_isrc"),
            "lyrics": qsTr("stats_tag_lyrics"),
            "unsyncedlyrics": qsTr("stats_tag_lyrics"),
            "syncedlyrics": qsTr("stats_tag_lyrics"),
            "lyricist": qsTr("stats_tag_lyricist"),
            "writer": qsTr("stats_tag_lyricist"),
            "conductor": qsTr("stats_tag_conductor"),
            "performer": qsTr("stats_tag_performer"),
            "producer": qsTr("stats_tag_producer"),
            "engineer": qsTr("stats_tag_engineer"),
            "mixer": qsTr("stats_tag_mixer"),
            "remixer": qsTr("stats_tag_mixer"),
            "arranger": qsTr("stats_tag_arranger"),
            "grouping": qsTr("stats_tag_grouping"),
            "compilation": qsTr("stats_tag_compilation"),
            "originalartist": qsTr("stats_tag_original_artist"),
            "originalalbum": qsTr("stats_tag_original_album"),
            "originaldate": qsTr("stats_tag_original_year"),
            "originalyear": qsTr("stats_tag_original_year"),
            "description": qsTr("stats_tag_description"),
            "synopsis": qsTr("stats_tag_description"),
            "creation_time": qsTr("stats_tag_creation_time"),
            "streamtitle": qsTr("stats_tag_title"),
            "icy-samplerate": qsTr("stats_tag_icy_samplerate"),
            "icy-channels": qsTr("stats_tag_icy_channels"),
            "icy-pub": qsTr("stats_tag_icy_pub"),
            "icy-title": qsTr("stats_tag_icy_title"),
            "icy-name": qsTr("stats_tag_icy_name"),
            "icy-genre": qsTr("stats_tag_icy_genre"),
            "icy-url": qsTr("stats_tag_icy_url"),
            "icy-description": qsTr("stats_tag_icy_description"),
            "icy-br": qsTr("stats_tag_icy_br"),
            "icy-sr": qsTr("stats_tag_icy_samplerate"),
            "icy-notice1": qsTr("stats_tag_icy_notice1"),
            "icy-notice2": qsTr("stats_tag_icy_notice2")
        };
        if (labels[k] !== undefined) return labels[k];

        // Unknown tags still stay readable instead of showing raw metadata keys.
        return String(key)
            .replace(/[_\-.]+/g, " ")
            .replace(/\b\w/g, c => c.toUpperCase());
    }

    function decodeHtmlEntities(text) {
        return String(text)
            .replace(/&nbsp;/gi, " ")
            .replace(/&lt;/gi, "<")
            .replace(/&gt;/gi, ">")
            .replace(/&quot;/gi, "\"")
            .replace(/&#0*39;/g, "'")
            .replace(/&#x0*27;/gi, "'")
            .replace(/&amp;/gi, "&");
    }

    function cleanTagValue(key, value) {
        const k = String(key).toLowerCase();
        if (k.indexOf("icy-notice") !== 0) return String(value);

        // Shoutcast notices are HTML fragments; show their readable text.
        let text = String(value)
            .replace(/<br\s*\/?>/gi, " ")
            .replace(/<[^>]*>/g, "");
        text = decodeHtmlEntities(text)
            .replace(/\s+/g, " ")
            .trim();
        return text;
    }

    function updateTags() {
        const merged = {};
        const metadata = statsProperty("metadata");
        if (metadata && typeof metadata === "object") {
            for (const key in metadata) {
                const value = metadata[key];
                if (value === undefined || value === null || value === "") continue;
                if (typeof value === "object") continue;
                merged[String(key).toLowerCase()] = String(value);
            }
        }
        for (const key in _fileTags) {
            const value = _fileTags[key];
            if (value === undefined || value === null || value === "") continue;
            merged[String(key).toLowerCase()] = String(value);
        }

        // Radio updates the ICY title through media-title; keep it current
        // even if the metadata map itself lags behind.
        const isLive = typeof Media !== "undefined" && Media.liveStream === true;
        const mediaTitle = safeString(statsProperty("media-title"));
        if (isLive && mediaTitle !== "N/A" && mediaTitle !== "") {
            if (merged["icy-title"] !== undefined)
                merged["icy-title"] = mediaTitle;
            else
                merged["title"] = mediaTitle;
        }

        // Metadata often repeats "date" as "year"; keep one label.
        if (merged["date"] !== undefined)
            delete merged["year"];
        // Some radio streams only expose the current ICY title.
        if (Object.keys(merged).length === 0) {
            const title = safeString(statsProperty("media-title"));
            if (title !== "N/A" && title !== "") merged["title"] = title;
        }

        const order = ["title", "icy-title", "artist", "album", "album_artist",
                       "albumartist", "genre", "date", "year", "track",
                       "tracknumber", "tracktotal", "totaltracks", "disc",
                       "discnumber", "disctotal", "totaldiscs", "comment",
                       "composer", "encoder", "encoded_by", "copyright",
                       "publisher", "organization", "label", "language", "bpm",
                       "isrc", "lyrics", "unsyncedlyrics", "lyricist",
                       "conductor", "performer", "producer", "engineer",
                       "mixer", "arranger", "grouping", "compilation",
                       "originalartist", "originalalbum", "originaldate",
                       "description", "creation_time", "icy-title", "icy-name",
                       "icy-genre", "icy-url", "icy-description", "icy-br",
                       "icy-samplerate", "icy-sr", "icy-channels", "icy-pub",
                       "icy-notice1", "icy-notice2"];
        const keys = Object.keys(merged);
        keys.sort((a, b) => {
            const ia = order.indexOf(a);
            const ib = order.indexOf(b);
            if (ia !== -1 || ib !== -1) {
                if (ia === -1) return 1;
                if (ib === -1) return -1;
                return ia - ib;
            }
            return a.localeCompare(b);
        });

        const list = [];
        for (const key of keys)
            list.push({ key: key, label: tagDisplayLabel(key),
                        value: cleanTagValue(key, merged[key]) });

        // The first five fields are shown directly under the title. Everything
        // else (track, disc, comment, encoder, ...) follows the main stats.
        const primaryKeys = ["title", "icy-title", "artist", "album",
                             "genre", "icy-genre", "date", "year"];
        const secondary = list.filter(tag => primaryKeys.indexOf(tag.key) === -1);

        let mainTitle = "";
        if (isLive && merged["icy-title"] !== undefined)
            mainTitle = merged["icy-title"];
        else if (merged["title"] !== undefined)
            mainTitle = merged["title"];
        else if (merged["icy-title"] !== undefined)
            mainTitle = merged["icy-title"];

        const signature = JSON.stringify(list);
        if (signature === _tagsSignature) return;
        _tagsSignature = signature;
        tagList = list;
        secondaryTagList = secondary;
        primaryTitle = mainTitle;
    }

    function escapeHtml(text) {
        return String(text)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;");
    }

    function statsPlainText() {
        const lines = [];
        if (statLines.length > 0) lines.push(statLines[0]);

        if (audioOrRadio) {
            const primary = [
                primaryPlain(["artist"], qsTr("stats_tag_artist")),
                primaryPlain(["album"], qsTr("stats_tag_album")),
                primaryPlain(["genre", "icy-genre"], qsTr("stats_tag_genre")),
                primaryPlain(["date", "year"], qsTr("stats_tag_year"))
            ];
            for (const line of primary) {
                if (line !== "") lines.push(line);
            }
        }

        for (let i = 1; i < Math.min(4, statLines.length); ++i) {
            if (i === 1 && isRadio) continue; // no file size for a live stream
            lines.push(statLines[i]);
        }

        if (audioOrRadio) {
            for (let i = 0; i < secondaryTagList.length; ++i)
                lines.push(secondaryTagList[i].label + ": " + secondaryTagList[i].value);
        }

        for (let i = 4; i < statLines.length; ++i)
            lines.push(statLines[i]);
        return lines.join("\n");
    }

    function copyStats() {
        Media.copyToClipboard(statsPlainText());
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

    function formatContainer(value) {
        const raw = safeString(value);
        if (raw === "N/A" || raw === "") return raw;

        const key = raw.toLowerCase();
        const names = {
            "mov,mp4,m4a,3gp,3g2,mj2": "MP4",
            "matroska,webm": "MKV/WebM",
            "matroska": "MKV",
            "webm": "WebM",
            "avi": "AVI",
            "mpegts": "MPEG-TS",
            "mpeg": "MPEG",
            "flv": "FLV",
            "mp3": "MP3",
            "aac": "AAC",
            "flac": "FLAC",
            "ogg": "OGG",
            "wav": "WAV",
            "hls": "HLS"
        };
        if (names[key] !== undefined) return names[key];

        // Unknown demuxer alias lists: show the primary name instead of the
        // whole comma-separated list.
        if (key.indexOf(",") !== -1)
            return key.split(",")[0].trim().toUpperCase();
        return raw;
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

    function openContextMenu(localX, localY) {
        if (statsContextMenu.visible) {
            statsContextMenu.close();
            return;
        }
        // The menu is parented to the window overlay: use scene coordinates.
        // LumaMenu keeps it inside the window.
        const scene = statsPanel.mapToItem(null, localX, localY);
        statsContextMenu.x = scene.x;
        statsContextMenu.y = scene.y;
        statsContextMenu.open();
    }

    function closeContextMenu() {
        statsContextMenu.close();
    }

    LumaMenu {
        id: statsContextMenu
        width: Math.max(140, copyItem.contentWidth + leftPadding + rightPadding)

        Column {
            width: parent.width
            spacing: 2

            LumaMenuButton {
                id: copyItem
                text: qsTr("stats_copy")
                onClicked: {
                    statsContextMenu.close();
                    statsPanel.copyStats();
                }
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onSingleTapped: (eventPoint, button) => {
            statsPanel.openContextMenu(eventPoint.position.x, eventPoint.position.y);
        }
    }
}
