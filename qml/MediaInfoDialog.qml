import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Dialog {
    id: dialog
    title: qsTranslate("MediaInfoDialog", "info_title")
    modal: true
    focus: true
    readonly property real maxDialogWidth: {
        const w = Overlay.overlay ? Overlay.overlay.width : 0;
        return Math.max(360, (w > 0 ? w : 480) - 32);
    }
    readonly property real maxDialogHeight: {
        const h = Overlay.overlay ? Overlay.overlay.height : 0;
        return Math.max(280, (h > 0 ? h : 620) - 32);
    }
    width: Math.min(480, maxDialogWidth)
    height: Math.min(implicitHeight, maxDialogHeight)
    padding: 0

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var fileInfo: ({})

    property var tagValues: ({})
    property var originalTags: ({})
    property bool tagsDirty: false
    property bool tagsSaving: false
    property var pendingTags: ({})
    property string saveMessage: ""

    readonly property bool isAudio: dialog.fileInfo.isAudio === true
    readonly property bool isStream: dialog.fileInfo.isStream === true
    readonly property bool loading: dialog.fileInfo.loading === true

    property string urlValue: ""
    property string originalUrl: ""
    property bool urlDirty: false

    // The save button looks at the actual difference, not at the flags.
    readonly property bool dirty: dialog.isStream
        ? (urlField.text.trim() !== dialog.originalUrl.trim())
        : dialog.tagsDirty

    readonly property var tagFields: [
        { key: "title",   label: qsTranslate("MediaInfoDialog", "tag_title") },
        { key: "artist",  label: qsTranslate("MediaInfoDialog", "tag_artist") },
        { key: "album",   label: qsTranslate("MediaInfoDialog", "tag_album") },
        { key: "genre",   label: qsTranslate("MediaInfoDialog", "tag_genre") },
        { key: "year",    label: qsTranslate("MediaInfoDialog", "tag_year") },
        { key: "track",   label: qsTranslate("MediaInfoDialog", "tag_track") },
        { key: "comment", label: qsTranslate("MediaInfoDialog", "tag_comment") }
    ]

    readonly property color lineColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }

    onOpened: dialog.reloadTags()
    onClosed: {
        dialog.tagsDirty = false;
        dialog.urlDirty = false;
        dialog.tagsSaving = false;
        dialog.saveMessage = "";
    }
    // The information is collected asynchronously; refresh the fields when it
    // arrives instead of leaving the dialog empty.
    onFileInfoChanged: if (dialog.visible) dialog.reloadTags()

    function reloadTags() {
        const tags = dialog.fileInfo.tags || ({});
        const original = {};
        const edited = {};
        for (const key in tags) { original[key] = tags[key]; edited[key] = tags[key]; }
        dialog.originalTags = original;
        dialog.tagValues = edited;
        dialog.tagsDirty = false;
        dialog.tagsSaving = false;
        dialog.pendingTags = ({});
        dialog.originalUrl = dialog.fileInfo.path || "";
        dialog.urlValue = dialog.originalUrl;
        dialog.urlDirty = false;
        dialog.saveMessage = "";
    }

    function isDirty() {
        for (const key in dialog.tagValues) {
            if ((dialog.originalTags[key] || "") !== (dialog.tagValues[key] || "")) return true;
        }
        return false;
    }

    function updateTag(key, value) {
        dialog.tagValues[key] = value;
        dialog.tagsDirty = dialog.isDirty();
    }

    function saveChanges() {
        if (dialog.isStream) {
            // Take exactly what's in the field: the input handler may not have updated urlValue.
            const newUrl = urlField.text.trim();
            if (Media.updateStreamUrl(dialog.originalUrl, newUrl)) {
                dialog.originalUrl = newUrl;
                dialog.urlValue = newUrl;
                dialog.urlDirty = false;
                dialog.saveMessage = qsTranslate("MediaInfoDialog", "tags_saved");
            } else {
                dialog.saveMessage = qsTranslate("MediaInfoDialog", "tags_save_failed");
            }
            return;
        }
        const path = dialog.fileInfo.path || "";
        if (path.length === 0 || dialog.tagsSaving) return;

        const snapshot = {};
        for (const key in dialog.tagValues) snapshot[key] = dialog.tagValues[key];
        dialog.pendingTags = snapshot;
        dialog.tagsSaving = true;
        Media.requestSaveTags(path, snapshot);
    }

    Connections {
        target: Media
        function onTagsSaved(path, success) {
            dialog.tagsSaving = false;
            if (path !== (dialog.fileInfo.path || "")) return;
            if (success) {
                dialog.originalTags = dialog.pendingTags;
                dialog.tagsDirty = dialog.isDirty();
                dialog.saveMessage = qsTranslate("MediaInfoDialog", "tags_saved");
            } else {
                dialog.saveMessage = qsTranslate("MediaInfoDialog", "tags_save_failed");
            }
        }
    }

    contentItem: Item {
        implicitHeight: Math.min(column.implicitHeight + 40, 620)

        Flickable {
            anchors.fill: parent
            anchors.margins: 20
            clip: true
            enabled: !dialog.loading && !dialog.tagsSaving
            contentWidth: width
            contentHeight: column.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            ColumnLayout {
                id: column
                width: parent.width - 12
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    text: dialog.fileInfo.name || ""
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    color: Kirigami.Theme.textColor
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: dialog.loading
                    text: qsTranslate("MediaInfoDialog", "info_loading")
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                   Kirigami.Theme.textColor.b, 0.7)
                    font.pixelSize: 13
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: dialog.lineColor }

                // ---- Stream URL (radio) ----
                Label {
                    Layout.fillWidth: true
                    visible: dialog.isStream
                    text: "<b>URL:</b>"
                    textFormat: Text.RichText
                    color: Kirigami.Theme.textColor
                }

                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    visible: dialog.isStream
                    font.pixelSize: 13
                    color: Kirigami.Theme.textColor
                    padding: 2
                    text: dialog.urlValue
                    onTextChanged: { dialog.urlValue = text; dialog.urlDirty = (text !== dialog.originalUrl); }
                    background: Item {
                        implicitHeight: 22
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            height: 1
                            color: dialog.lineColor
                        }
                    }
                }

                // ---- Audio file tags ----
                Repeater {
                    model: dialog.isAudio && !dialog.isStream ? dialog.tagFields : []

                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.preferredWidth: 130
                            text: "<b>" + modelData.label + ":</b>"
                            textFormat: Text.RichText
                            color: Kirigami.Theme.textColor
                            font.pixelSize: 13
                        }

                        TextField {
                            id: tagField
                            Layout.fillWidth: true
                            font.pixelSize: 13
                            color: Kirigami.Theme.textColor
                            padding: 2
                            text: dialog.tagValues[modelData.key] !== undefined ? dialog.tagValues[modelData.key] : ""
                            onTextChanged: dialog.updateTag(modelData.key, text)
                            background: Item {
                                implicitHeight: 22
                                Rectangle {
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    height: 1
                                    color: tagField.activeFocus ? Kirigami.Theme.highlightColor : dialog.lineColor
                                }
                            }
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    visible: dialog.isAudio && dialog.tagsDirty && !dialog.isStream
                    text: qsTranslate("MediaInfoDialog", "tags_warning")
                    color: Kirigami.Theme.negativeTextColor
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: dialog.saveMessage !== ""
                    text: dialog.saveMessage
                    color: Kirigami.Theme.positiveTextColor
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }

                Rectangle { Layout.fillWidth: true; height: 1; visible: !dialog.isStream; color: dialog.lineColor }

                Label {
                    Layout.fillWidth: true
                    visible: !dialog.isStream
                    text: "<b>" + qsTranslate("MediaInfoDialog", "field_path") + ":</b> " + (dialog.fileInfo.path || "")
                    textFormat: Text.RichText
                    color: Kirigami.Theme.textColor
                    wrapMode: Text.Wrap
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_size") + ":</b> " + (dialog.fileInfo.size_str || "")
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_format") + ":</b> " + (dialog.fileInfo.format || "N/A")
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_duration") + ":</b> " + formatTime(dialog.fileInfo.duration || 0)
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isAudio && !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_resolution") + ":</b> " + (dialog.fileInfo.width || 0) + "x" + (dialog.fileInfo.height || 0)
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isAudio && !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_video") + ":</b> " + (dialog.fileInfo.video_codec || "N/A")
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isAudio && !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_vid_bitrate") + ":</b> " + dialog.bitrateText(dialog.fileInfo.video_bitrate, dialog.fileInfo.video_bitrate_estimated)
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_audio") + ":</b> " + (dialog.fileInfo.audio_codec || "N/A")
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_channels") + ":</b> " + (dialog.fileInfo.channels || 0) + " (" + (dialog.fileInfo.sample_rate || 0) + " Hz)"
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }

                Label {
                    Layout.fillWidth: true; visible: !dialog.isStream
                    text: "<b>" + qsTranslate("StatsPanel", "stats_aud_bitrate") + ":</b> " + dialog.bitrateText(dialog.fileInfo.audio_bitrate, dialog.fileInfo.audio_bitrate_estimated)
                    textFormat: Text.RichText; color: Kirigami.Theme.textColor
                }
            }
        }
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignRight

        Button {
            text: qsTranslate("MediaInfoDialog", "tags_save")
            visible: dialog.dirty
            enabled: !dialog.tagsSaving
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dialog.saveChanges()
        }
        Item { implicitWidth: 8; implicitHeight: 1 }
        Button {
            text: qsTranslate("SettingsDialog", "about_close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.close()
        }
        Item { implicitWidth: 16; implicitHeight: 1 }
    }

    // The container does not always store a per-stream bitrate; the backend
    // estimates it then and says so, so mark the value as approximate and
    // never show a misleading "0 kbps".
    function bitrateText(kbps, estimated) {
        if (!kbps || isNaN(kbps) || kbps <= 0) return "N/A";
        return (estimated ? "≈" : "") + kbps + " kbps";
    }

    function formatTime(seconds) {
        if (!seconds || isNaN(seconds)) return "00:00";
        let total = Math.floor(seconds);
        let h = Math.floor(total / 3600);
        let m = Math.floor((total % 3600) / 60);
        let s = total % 60;
        if (h > 0) return (h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }
}
