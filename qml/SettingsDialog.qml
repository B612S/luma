import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Dialog {
    id: settingsDialog
    title: qsTr("settings_title")
    modal: true
    focus: true
    width: 800
    height: 600
    padding: 0

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.85)
        radius: 8
        border.color: "transparent"
    }

    readonly property int controlWidth: 200

    contentItem: RowLayout {
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.5)
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                Item { Layout.preferredHeight: 8 }

                component CategoryButton: Rectangle {
                    id: btn
                    property string text: ""
                    property bool isActive: false
                    signal clicked()

                    Layout.fillWidth: true
                    height: 36
                    color: isActive ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) :
                    (catHover.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08) : "transparent")
                    radius: 4

                    Text {
                        text: btn.text
                        color: Kirigami.Theme.textColor
                        font.pixelSize: 14
                        font.weight: isActive ? Font.Bold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                    }

                    MouseArea {
                        id: catHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: btn.clicked()
                    }
                }

                CategoryButton { text: qsTr("settings_general"); isActive: settingsDialog.currentCategory === "general"; onClicked: settingsDialog.currentCategory = "general" }
                CategoryButton { text: qsTr("settings_playlist"); isActive: settingsDialog.currentCategory === "playlist"; onClicked: settingsDialog.currentCategory = "playlist" }
                CategoryButton { text: qsTr("settings_interface"); isActive: settingsDialog.currentCategory === "interface"; onClicked: settingsDialog.currentCategory = "interface" }
                CategoryButton { text: qsTr("settings_about"); isActive: settingsDialog.currentCategory === "about"; onClicked: settingsDialog.currentCategory = "about" }

                Item { Layout.fillHeight: true }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24

            Item {
                width: parent.width
                implicitHeight: contentLayout.implicitHeight

                ColumnLayout {
                    id: contentLayout
                    width: parent.width
                    spacing: 24

                    ColumnLayout {
                        visible: settingsDialog.currentCategory === "general"
                        Layout.fillWidth: true
                        spacing: 16

                        Label { text: qsTr("settings_general"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                        GridLayout {
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 16
                            Layout.fillWidth: true

                            Label { text: qsTr("settings_remember_volume"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.rememberVolume; onToggled: Config.rememberVolume = checked }
                            }

                            Label { text: qsTr("settings_default_volume"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter; visible: !Config.rememberVolume }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                visible: !Config.rememberVolume
                                SpinBox {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    font.pixelSize: 14
                                    from: 0; to: 100; stepSize: 5; value: Config.defaultVolume; onValueModified: Config.defaultVolume = value
                                }
                            }

                            Label { text: qsTr("settings_notifications"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.notifications; onToggled: Config.notifications = checked }
                            }

                            Label { text: qsTr("settings_single_instance"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.singleInstance; onToggled: Config.singleInstance = checked }
                            }
                        }
                    }

                    ColumnLayout {
                        visible: settingsDialog.currentCategory === "playlist"
                        Layout.fillWidth: true
                        spacing: 16

                        Label { text: qsTr("settings_playlist"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                        GridLayout {
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 16
                            Layout.fillWidth: true

                            Label { text: qsTr("settings_append_playlist"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.appendToPlaylist; onToggled: Config.appendToPlaylist = checked }
                            }

                            Label { text: qsTr("settings_play_mode"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                ComboBox {
                                    anchors.fill: parent
                                    font.pixelSize: 14
                                    model: [
                                        { text: qsTranslate("ContextMenu", "playback_sequential"), value: "sequential" },
                                        { text: qsTranslate("ContextMenu", "playback_repeat"), value: "repeat" },
                                        { text: qsTranslate("ContextMenu", "playback_shuffle"), value: "shuffle" },
                                        { text: qsTranslate("ContextMenu", "playback_stop"), value: "stop" }
                                    ]
                                    textRole: "text"; valueRole: "value"
                                    currentIndex: { let idx = model.findIndex(item => item.value === Config.playlistMode); return idx !== -1 ? idx : 0 }
                                    onActivated: (index) => {
                                        Config.playlistMode = model[index].value;
                                        Media.playMode = model[index].value;
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        visible: settingsDialog.currentCategory === "interface"
                        Layout.fillWidth: true
                        spacing: 16

                        Label { text: qsTr("settings_interface"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                        GridLayout {
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 16
                            Layout.fillWidth: true

                            Label { text: qsTr("settings_language"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                ComboBox {
                                    id: langCombo
                                    anchors.fill: parent
                                    font.pixelSize: 14
                                    // Используем нативные названия языков (эндонимы)
                                    model: [
                                        { value: "en", text: "English" },
                                        { value: "ru", text: "Русский" },
                                        { value: "ja", text: "日本語" },
                                        { value: "zh_CN", text: "简体中文" }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: {
                                        let idx = model.findIndex(item => item.value === Config.language);
                                        return idx !== -1 ? idx : 0;
                                    }
                                    onActivated: (index) => {
                                        let newLang = model[index].value;
                                        if (Config.language !== newLang) {
                                            Config.language = newLang;
                                        }
                                    }
                                }
                            }

                            // --- НАСТРОЙКА ЗАКРЕПЛЕНИЯ ПАНЕЛИ ---
                            Label { text: qsTr("settings_pin_controls"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.pinControls; onToggled: Config.pinControls = checked }
                            }
                            // ------------------------------------
                        }
                    }

                    ColumnLayout {
                        visible: settingsDialog.currentCategory === "about"
                        Layout.fillWidth: true
                        spacing: 16

                        Label { text: qsTr("about_title"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 16
                            Layout.topMargin: 16

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

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Layout.topMargin: 16

                            Label {
                                text: qsTr("about_links")
                                font.weight: Font.Bold
                                color: Kirigami.Theme.textColor
                            }

                            Label {
                                text: '<a href="https://github.com/B612S/luma">' + qsTr("about_github") + '</a>'
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

                            Label {
                                text: '<a href="https://invent.kde.org/frameworks/kirigami">' + qsTr("about_kirigami") + '</a>'
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
                                text: '<a href="https://invent.kde.org/libraries/mpvqt">' + qsTr("about_mpvqt") + '</a>'
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
                    }
                }
            }
        }
    }

    property string currentCategory: "general"

    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        Button {
            text: qsTr("settings_close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: settingsDialog.close()
        }
        Item { implicitWidth: 16; implicitHeight: 1 }
    }
}
