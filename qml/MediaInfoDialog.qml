import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Dialog {
    id: dialog
    title: qsTranslate("StatsPanel", "stats_file")
    modal: true
    focus: true
    width: 450
    padding: 0 // Убираем внутренний padding диалога, чтобы footer прилегал к краям

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var fileInfo: ({})

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.98)
        radius: 8
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.2)
    }

    // Контент с отступами
    contentItem: Item {
        implicitHeight: layout.implicitHeight + 40 // Отступы сверху и снизу для контента

        ColumnLayout {
            id: layout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: dialog.fileInfo.name || ""
                font.pixelSize: 16
                font.weight: Font.Bold
                color: Kirigami.Theme.textColor
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_file") + ":</b> " + (dialog.fileInfo.path || "")
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_size") + ":</b> " + (dialog.fileInfo.size_str || "")
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_format") + ":</b> " + (dialog.fileInfo.format || "N/A")
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_duration") + ":</b> " + formatTime(dialog.fileInfo.duration || 0)
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_resolution") + ":</b> " + (dialog.fileInfo.width || 0) + "x" + (dialog.fileInfo.height || 0)
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_video") + ":</b> " + (dialog.fileInfo.video_codec || "N/A")
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_vid_bitrate") + ":</b> " + (dialog.fileInfo.video_bitrate || 0) + " kbps"
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_audio") + ":</b> " + (dialog.fileInfo.audio_codec || "N/A")
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_channels") + ":</b> " + (dialog.fileInfo.channels || 0) + " (" + (dialog.fileInfo.sample_rate || 0) + " Hz)"
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }

            Label {
                Layout.fillWidth: true
                text: "<b>" + qsTranslate("StatsPanel", "stats_aud_bitrate") + ":</b> " + (dialog.fileInfo.audio_bitrate || 0) + " kbps"
                textFormat: Text.RichText
                color: Kirigami.Theme.textColor
            }
        }
    }

    // ИСПОЛЬЗУЕМ FOOTER, КАК В НАСТРОЙКАХ
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            text: qsTranslate("SettingsDialog", "about_close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.close()
        }
        Item { implicitWidth: 16; implicitHeight: 1 } // Отступ от правого края (как в настройках)
    }

    function formatTime(seconds) {
        if (!seconds || isNaN(seconds)) return "00:00";
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
