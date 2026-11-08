import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// URL input styled like the app's other dialogs (no system window).
Item {
    id: root

    // Where to add to (for "Add URL" in the playlist).
    property string targetId: ""

    signal urlAccepted(string url)

    property alias dialog: urlDialog

    Dialog {
        id: urlDialog
        modal: true; focus: true; width: 420; padding: 0
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        title: ""

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
            radius: 8; border.color: "transparent"
        }

        onOpened: {
            Qt.callLater(() => urlField.forceActiveFocus());
        }

        contentItem: Item {
            implicitHeight: layout.implicitHeight + 40
            ColumnLayout {
                id: layout; anchors.fill: parent; anchors.margins: 20; spacing: 16

                Label {
                    Layout.fillWidth: true
                    text: qsTranslate("MediaManager", "dialog_enter_url")
                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.8)
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    color: Kirigami.Theme.textColor
                    selectByMouse: true
                    placeholderText: "https://"

                    background: Rectangle {
                        implicitHeight: 36; radius: 4
                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05)
                        border.color: urlField.activeFocus ? Kirigami.Theme.highlightColor : "transparent"
                        border.width: 1
                    }

                    onAccepted: root.acceptUrl()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight; spacing: 8
                    Button { text: qsTr("url_cancel"); onClicked: urlDialog.close() }
                    Button {
                        text: qsTr("url_ok")
                        enabled: urlField.text.trim().length > 0
                        onClicked: root.acceptUrl()
                    }
                }
            }
        }
    }

    // Functions must be on the component's root object, otherwise they are not
    // visible from outside (urlDialog.openWith(...)).
    function openWith(titleText, target) {
        urlDialog.title = titleText;
        root.targetId = target ? target : "";
        urlField.text = "";
        urlDialog.open();
    }

    function acceptUrl() {
        const url = urlField.text.trim();
        if (url.length === 0) return;
        urlDialog.close();
        root.urlAccepted(url);
    }
}
