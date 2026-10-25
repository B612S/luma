import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Dialog {
    id: settingsDialog
    title: qsTr("settings_title")
    modal: true
    focus: true
    width: 750
    height: 550
    padding: 0

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

    readonly property int controlWidth: 180

    contentItem: RowLayout {
        spacing: 0

        // ЛЕВАЯ ПАНЕЛЬ
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.5)
            radius: 8

            Rectangle {
                width: 8; height: parent.height; anchors.right: parent.right
                color: parent.color
            }

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

                Item { Layout.fillHeight: true }
            }
        }

        // ПРАВАЯ ПАНЕЛЬ
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24

            ColumnLayout {
                width: parent.width
                spacing: 24

                // --- ОБЩИЕ ---
                ColumnLayout {
                    visible: settingsDialog.currentCategory === "general"
                    Layout.fillWidth: true
                    spacing: 16

                    Label { text: qsTr("settings_general"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                    // Строка: Remember Volume
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("settings_remember_volume"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                        Item {
                            Layout.preferredWidth: settingsDialog.controlWidth
                            Layout.preferredHeight: 40
                            Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.rememberVolume; onToggled: Config.rememberVolume = checked }
                        }
                    }

                    // Строка: Default Volume
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !Config.rememberVolume
                        Label { text: qsTr("settings_default_volume"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                        Item {
                            Layout.preferredWidth: settingsDialog.controlWidth
                            Layout.preferredHeight: 40
                            SpinBox {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 14
                                from: 0; to: 100; stepSize: 5; value: Config.defaultVolume; onValueModified: Config.defaultVolume = value
                            }
                        }
                    }

                    // Строка: Notifications
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("settings_notifications"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                        Item {
                            Layout.preferredWidth: settingsDialog.controlWidth
                            Layout.preferredHeight: 40
                            Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.notifications; onToggled: Config.notifications = checked }
                        }
                    }

                    // Строка: Single Instance
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("settings_single_instance"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                        Item {
                            Layout.preferredWidth: settingsDialog.controlWidth
                            Layout.preferredHeight: 40
                            Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.singleInstance; onToggled: Config.singleInstance = checked }
                        }
                    }
                }

                // --- ПЛЕЙЛИСТ ---
                ColumnLayout {
                    visible: settingsDialog.currentCategory === "playlist"
                    Layout.fillWidth: true
                    spacing: 16

                    Label { text: qsTr("settings_playlist"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                    // Строка: Append to current playlist
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("settings_append_playlist"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                        Item {
                            Layout.preferredWidth: settingsDialog.controlWidth
                            Layout.preferredHeight: 40
                            Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.appendToPlaylist; onToggled: Config.appendToPlaylist = checked }
                        }
                    }

                    // Строка: Playback Mode
                    RowLayout {
                        Layout.fillWidth: true
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
                                onActivated: (index) => { Config.playlistMode = model[index].value; Media.playMode = model[index].value }
                            }
                        }
                    }
                }

                // --- ИНТЕРФЕЙС ---
                ColumnLayout {
                    visible: settingsDialog.currentCategory === "interface"
                    Layout.fillWidth: true
                    spacing: 16

                    Label { text: qsTr("settings_interface"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                    // Строка: Language
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("settings_language"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                        Item {
                            Layout.preferredWidth: settingsDialog.controlWidth
                            Layout.preferredHeight: 40
                            ComboBox {
                                id: langCombo
                                anchors.fill: parent
                                font.pixelSize: 14
                                model: [
                                    { value: "en", text: qsTr("lang_en") },
                                    { value: "ru", text: qsTr("lang_ru") }
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
