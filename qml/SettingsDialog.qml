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

    // Пока диалог открыт, горячие клавиши (включая колесо = громкость) выключены.
    onOpened: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(false); }
    onClosed: { if (typeof shortcuts !== "undefined" && shortcuts !== null) shortcuts.setShortcutsEnabled(true); }

    // Проверка цвета в формате mpv: #RRGGBB или #AARRGGBB.
    function validColor(value) {
        return /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(value) ? value : "transparent";
    }

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
                CategoryButton { text: qsTr("settings_subtitles"); isActive: settingsDialog.currentCategory === "subtitles"; onClicked: settingsDialog.currentCategory = "subtitles" }
                CategoryButton { text: qsTr("settings_playback"); isActive: settingsDialog.currentCategory === "playback"; onClicked: settingsDialog.currentCategory = "playback" }
                CategoryButton { text: qsTr("settings_about"); isActive: settingsDialog.currentCategory === "about"; onClicked: settingsDialog.currentCategory = "about" }

                Item { Layout.fillHeight: true }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            clip: true
            contentWidth: width
            contentHeight: contentLayout.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            // Ширину полосы задаём явно: в некоторых стилях у неё широкая
            // прозрачная область, которая перекрывала поля справа.
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            Item {
                // Оставляем место справа под полосу прокрутки, иначе элементы
                // (поля цвета) уходят под неё.
                width: parent.width - 22
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
                                        { value: "ru", text: "Русский" }
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

                            // --- РЕЖИМ ПАНЕЛИ УПРАВЛЕНИЯ ---
                            Label { text: qsTr("settings_control_bar"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                ComboBox {
                                    anchors.fill: parent
                                    font.pixelSize: 14
                                    model: [
                                        { text: qsTr("settings_control_bar_hidden"), value: "hidden" },
                                        { text: qsTr("settings_control_bar_float"), value: "float" },
                                        { text: qsTr("settings_control_bar_pinned"), value: "pinned" }
                                    ]
                                    textRole: "text"; valueRole: "value"
                                    currentIndex: {
                                        const idx = model.findIndex(item => item.value === Config.controlBarMode);
                                        return idx !== -1 ? idx : 1;
                                    }
                                    onActivated: (index) => { Config.controlBarMode = model[index].value; }
                                }
                            }

                            Label {
                                visible: Config.controlBarMode !== "hidden"
                                text: qsTr("settings_show_panels_fullscreen")
                                color: Kirigami.Theme.textColor; font.pixelSize: 14
                                Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                wrapMode: Text.WordWrap
                            }
                            Item {
                                visible: Config.controlBarMode !== "hidden"
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.showPanelsFullscreen; onToggled: Config.showPanelsFullscreen = checked }
                            }
                            // ------------------------------------
                        }
                    }

                    ColumnLayout {
                        visible: settingsDialog.currentCategory === "playback"
                        Layout.fillWidth: true
                        spacing: 16

                        Label { text: qsTr("settings_playback"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                        Label { text: qsTr("settings_ytdlp"); font.pixelSize: 14; font.weight: Font.Bold; color: Kirigami.Theme.textColor }

                        GridLayout {
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 16
                            Layout.fillWidth: true

                            Label { text: qsTr("settings_ytdlp_quality"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                ComboBox {
                                    anchors.fill: parent
                                    font.pixelSize: 14
                                    model: [
                                        { text: qsTr("settings_ytdlp_quality_best"), value: "best" },
                                        { text: "1080p", value: "1080" },
                                        { text: "720p", value: "720" },
                                        { text: "480p", value: "480" },
                                        { text: qsTr("settings_ytdlp_quality_audio"), value: "audio" }
                                    ]
                                    textRole: "text"; valueRole: "value"
                                    currentIndex: { let idx = model.findIndex(item => item.value === Config.ytdlpQuality); return idx !== -1 ? idx : 0 }
                                    onActivated: (index) => { Config.ytdlpQuality = model[index].value; }
                                }
                            }

                            Label { text: qsTr("settings_ytdlp_proxy"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                TextField {
                                    anchors.fill: parent
                                    font.pixelSize: 14
                                    placeholderText: "socks5://127.0.0.1:1080"
                                    text: Config.ytdlpProxy
                                    onEditingFinished: Config.ytdlpProxy = text.trim()
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        visible: settingsDialog.currentCategory === "subtitles"
                        Layout.fillWidth: true
                        spacing: 16

                        Label { text: qsTr("settings_subtitles"); font.pixelSize: 20; font.weight: Font.Bold; color: Kirigami.Theme.textColor }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2) }

                        GridLayout {
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 16
                            Layout.fillWidth: true

                            Label { text: qsTr("settings_sub_font"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                // Оформляем как остальные поля (TextField), но только для чтения:
                                // по клику открывается выбор шрифта.
                                TextField {
                                    anchors.fill: parent
                                    readOnly: true
                                    font.pixelSize: 14
                                    text: Config.subtitleFont
                                    rightPadding: 24
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "▾"
                                    color: Kirigami.Theme.textColor
                                    font.pixelSize: 14
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: fontDialog.open()
                                }
                            }

                            Label { text: qsTr("settings_sub_size"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Slider {
                                        Layout.fillWidth: true
                                        from: 20; to: 120; stepSize: 1
                                        value: Config.subtitleFontSize
                                        onMoved: Config.subtitleFontSize = value
                                    }
                                    Label {
                                        text: Math.round(Config.subtitleFontSize)
                                        color: Kirigami.Theme.textColor; font.pixelSize: 14
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Label { text: qsTr("settings_sub_bold"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.subtitleBold; onToggled: Config.subtitleBold = checked }
                            }

                            Label { text: qsTr("settings_sub_color"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    TextField {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 36
                                        font.pixelSize: 14
                                        placeholderText: "#AARRGGBB"
                                        text: Config.subtitleColor
                                        onEditingFinished: {
                                            let v = text.trim();
                                            if (/^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(v)) {
                                                if (Config.subtitleColor !== v) Config.subtitleColor = v;
                                            } else {
                                                text = Config.subtitleColor;
                                            }
                                        }
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 36; Layout.preferredHeight: 36
                                        radius: 4
                                        color: settingsDialog.validColor(Config.subtitleColor)
                                        border.width: 1
                                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.35)
                                    }
                                }
                            }

                            Label { text: qsTr("settings_sub_border_size"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Slider {
                                        Layout.fillWidth: true
                                        from: 0; to: 12; stepSize: 1
                                        value: Config.subtitleBorderSize
                                        onMoved: Config.subtitleBorderSize = value
                                    }
                                    Label {
                                        text: Math.round(Config.subtitleBorderSize)
                                        color: Kirigami.Theme.textColor; font.pixelSize: 14
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Label { text: qsTr("settings_sub_border_color"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    TextField {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 36
                                        font.pixelSize: 14
                                        placeholderText: "#AARRGGBB"
                                        text: Config.subtitleBorderColor
                                        onEditingFinished: {
                                            let v = text.trim();
                                            if (/^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(v)) {
                                                if (Config.subtitleBorderColor !== v) Config.subtitleBorderColor = v;
                                            } else {
                                                text = Config.subtitleBorderColor;
                                            }
                                        }
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 36; Layout.preferredHeight: 36
                                        radius: 4
                                        color: settingsDialog.validColor(Config.subtitleBorderColor)
                                        border.width: 1
                                        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.35)
                                    }
                                }
                            }

                            Label { text: qsTr("settings_sub_background"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.subtitleBackground; onToggled: Config.subtitleBackground = checked }
                            }

                            Label { text: qsTr("settings_sub_position"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    Slider {
                                        Layout.fillWidth: true
                                        from: 0; to: 100; stepSize: 1
                                        value: Config.subtitlePosition
                                        onMoved: Config.subtitlePosition = value
                                    }
                                    Label {
                                        text: Math.round(Config.subtitlePosition) + "%"
                                        color: Kirigami.Theme.textColor; font.pixelSize: 14
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            Label { text: qsTr("settings_sub_ignore_file_style"); color: Kirigami.Theme.textColor; font.pixelSize: 14; Layout.fillWidth: true; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter; wrapMode: Text.WordWrap }
                            Item {
                                Layout.preferredWidth: settingsDialog.controlWidth
                                Layout.preferredHeight: 40
                                Switch { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; checked: Config.subtitleIgnoreFileStyle; onToggled: Config.subtitleIgnoreFileStyle = checked }
                            }
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

    // Выбор шрифта субтитров. Список системных шрифтов большой, поэтому
    // ComboBox не используем: ListView создаёт делегаты только для видимых
    // строк, а список заполняется при первом открытии диалога.
    Dialog {
        id: fontDialog
        title: qsTr("settings_sub_font")
        modal: true
        focus: true
        width: 420
        height: 520
        padding: 12
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property var allFonts: []
        property var shownFonts: []

        function applyFilter() {
            const q = filterField.text.trim().toLowerCase();
            shownFonts = q.length === 0
                ? allFonts
                : allFonts.filter(f => f.toLowerCase().indexOf(q) !== -1);
        }

        onOpened: {
            filterField.text = "";
            filterField.forceActiveFocus();
            if (allFonts.length === 0) fontLoadTimer.start();
            else applyFilter();
        }

        Timer {
            id: fontLoadTimer
            interval: 0
            repeat: false
            onTriggered: {
                fontDialog.allFonts = Qt.fontFamilies();
                fontDialog.applyFilter();
            }
        }

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.95)
            radius: 8
            border.color: "transparent"
        }

        contentItem: ColumnLayout {
            spacing: 8

            TextField {
                id: filterField
                Layout.fillWidth: true
                font.pixelSize: 14
                placeholderText: qsTr("settings_sub_font_search")
                onTextChanged: fontDialog.applyFilter()
            }

            Label {
                visible: fontDialog.allFonts.length === 0
                Layout.fillWidth: true
                text: qsTr("settings_loading")
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.7)
                horizontalAlignment: Text.AlignHCenter
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: fontDialog.shownFonts
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

                // Если нужного шрифта нет в списке, можно применить введённое имя.
                header: Rectangle {
                    width: ListView.view ? ListView.view.width - 12 : 0
                    height: filterField.text.trim().length > 0 ? 34 : 0
                    visible: height > 0
                    radius: 4
                    color: customFontHover.containsMouse
                           ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                           : "transparent"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 18
                        text: qsTr("settings_sub_font_use").arg(filterField.text.trim())
                        color: Kirigami.Theme.highlightColor
                        font.pixelSize: 14
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        id: customFontHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            Config.subtitleFont = filterField.text.trim();
                            fontDialog.close();
                        }
                    }
                }

                delegate: Rectangle {
                    width: ListView.view ? ListView.view.width - 12 : 0
                    height: 34
                    radius: 4
                    color: fontRowHover.containsMouse
                           ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                           : "transparent"

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 18
                        text: modelData
                        color: Kirigami.Theme.textColor
                        font.pixelSize: 14
                        font.weight: modelData === Config.subtitleFont ? Font.Bold : Font.Normal
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        id: fontRowHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            Config.subtitleFont = modelData;
                            fontDialog.close();
                        }
                    }
                }
            }
        }
    }
}
