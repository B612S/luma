import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Dialog {
    id: aboutDialog
    title: qsTr("about_title")
    modal: true
    focus: true
    width: 420
    padding: 24

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.2)
    }

    contentItem: ColumnLayout {
        spacing: 16

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 16

            Image {
                source: "qrc:/assets/luma.png"
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                fillMode: Image.PreserveAspectFit
            }

            ColumnLayout {
                spacing: 2
                Label {
                    text: "Luma"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: Kirigami.Theme.textColor
                }
                Label {
                    text: qsTr("about_version").arg(Qt.application.version || "1.0")
                    font.pixelSize: 14
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.7)
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("about_description")
            color: Kirigami.Theme.textColor
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("about_links")
                font.weight: Font.Bold
                color: Kirigami.Theme.textColor
            }

            Label {
                text: '<a href="https://github.com/">' + qsTr("about_github") + '</a>'
                textFormat: Text.RichText
                color: Kirigami.Theme.highlightColor
                onLinkActivated: Qt.openUrlExternally(link)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.NoButton
                }
            }

            Label {
                text: '<a href="https://mpv.io/">' + qsTr("about_mpv") + '</a>'
                textFormat: Text.RichText
                color: Kirigami.Theme.highlightColor
                onLinkActivated: Qt.openUrlExternally(link)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.NoButton
                }
            }
        }

        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("about_close")
            onClicked: aboutDialog.close()
        }
    }
}
