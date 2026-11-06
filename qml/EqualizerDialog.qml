import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Equalizer window. Uses the same dialog chrome as the settings: the rounded
// translucent card, the section header and the row spacing.
Dialog {
    id: equalizerDialog
    title: qsTr("eq_title")
    modal: true
    focus: true
    // Fit the player window: at 940x620 the card was clipped on small windows.
    readonly property real maxDialogWidth: {
        const w = Overlay.overlay ? Overlay.overlay.width : 0;
        return Math.max(480, (w > 0 ? w : 940) - 32);
    }
    readonly property real maxDialogHeight: {
        const h = Overlay.overlay ? Overlay.overlay.height : 0;
        return Math.max(320, (h > 0 ? h : 620) - 32);
    }
    width: Math.min(940, maxDialogWidth)
    height: Math.min(620, maxDialogHeight)
    // The band columns are ~20 px wide on a small window; smaller labels keep
    // the 20-band frequencies readable instead of eliding every value.
    readonly property bool compact: width < 760
    padding: 22

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property color textColor: Kirigami.Theme.textColor
    readonly property color accentColor: Kirigami.Theme.highlightColor
    readonly property color dimText: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.65)
    readonly property color lineColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.18)
    readonly property color cardColor: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.05)

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.96)
        radius: 8
        border.color: "transparent"
    }

    // Builtin presets come from the backend, their labels are translated here;
    // the user's own presets are listed by their name.
    readonly property var presetModel: {
        const items = [{ value: "", text: qsTr("eq_preset_custom"), builtin: false, custom: false }];
        for (let i = 0; i < Equalizer.builtinPresets.length; ++i) {
            const id = Equalizer.builtinPresets[i];
            items.push({ value: "builtin:" + id, text: qsTr("eq_preset_" + id),
                         builtin: true, custom: false, id: id });
        }
        for (let i = 0; i < Equalizer.customPresets.length; ++i) {
            const preset = Equalizer.customPresets[i];
            items.push({ value: "custom:" + preset.name, text: preset.name,
                         builtin: false, custom: true, name: preset.name });
        }
        return items;
    }

    function frequencyLabel(frequency) {
        const value = Number(frequency);
        if (value >= 1000) {
            const k = value / 1000;
            return (Math.abs(k - Math.round(k)) < 0.05 ? k.toFixed(0) : k.toFixed(1)) + "K";
        }
        return Math.abs(value - Math.round(value)) < 0.05
                ? String(Math.round(value)) : value.toFixed(1);
    }

    function gainLabel(gain) {
        const value = Number(gain);
        if (Math.abs(value) < 0.05) return "0";
        return (value > 0 ? "+" : "") + value.toFixed(1);
    }

    function savePreset() {
        const name = presetNameField.text.trim();
        if (name.length === 0) return;
        Equalizer.saveCustomPreset(name);
        saveDialog.close();
    }

    onOpened: content.forceActiveFocus()

    // Vertical band/preamp slider. The value is a pure binding: dragging only
    // moves the handle and reports the requested value back to the backend, so
    // a preset change can move the handle again.
    component EqSlider: Item {
        id: slider
        property real value: 0
        property real from: -12
        property real to: 12
        property bool emphasized: false
        signal moved(real value)

        implicitWidth: 24
        implicitHeight: 200

        readonly property real handleHeight: 12
        readonly property real travel: Math.max(1, height - handleHeight)
        readonly property real handleY: (1 - (value - from) / (to - from)) * travel
        readonly property real zeroY: (1 - (0 - from) / (to - from)) * travel + handleHeight / 2

        Rectangle {
            id: track
            x: (slider.width - width) / 2
            width: slider.emphasized ? 6 : 4
            radius: width / 2
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                           Kirigami.Theme.textColor.b, 0.16)
        }

        Rectangle {
            x: track.x
            width: track.width
            radius: width / 2
            color: Kirigami.Theme.highlightColor
            opacity: 0.65
            y: Math.min(slider.zeroY, slider.handleY + slider.handleHeight / 2)
            height: Math.abs(slider.handleY + slider.handleHeight / 2 - slider.zeroY)
        }

        Rectangle {
            x: track.x - 4
            width: track.width + 8
            height: 1
            y: slider.zeroY - 0.5
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                           Kirigami.Theme.textColor.b, 0.35)
        }

        Rectangle {
            id: handle
            width: slider.emphasized ? 18 : 14
            height: slider.handleHeight
            radius: 3
            x: (slider.width - width) / 2
            y: slider.handleY
            color: sliderMouse.containsMouse || sliderMouse.pressed
                   ? Qt.lighter(Kirigami.Theme.highlightColor, 1.15)
                   : Kirigami.Theme.highlightColor
            border.width: 0
        }

        MouseArea {
            id: sliderMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            preventStealing: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            // A right click is a reset, not a drag.
            property bool dragActive: false

            function moveTo(y) {
                if (slider.travel <= 0) return;
                const position = 1 - (y - slider.handleHeight / 2) / slider.travel;
                const clamped = Math.max(0, Math.min(1, position));
                let value = slider.from + clamped * (slider.to - slider.from);
                value = Math.round(value * 2) / 2;
                slider.moved(value);
            }

            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    // Reset this band (or the preamp) to 0 dB.
                    slider.moved(0);
                    return;
                }
                dragActive = true;
                moveTo(mouse.y);
            }
            onReleased: dragActive = false
            onCanceled: dragActive = false
            onPositionChanged: (mouse) => { if (dragActive) moveTo(mouse.y) }
        }
    }

    contentItem: ColumnLayout {
        id: content
        focus: true
        spacing: 14

        Keys.onEscapePressed: (event) => {
            equalizerDialog.close();
            event.accepted = true;
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("eq_enabled")
                color: equalizerDialog.textColor
                font.pixelSize: 14
            }
            Switch {
                checked: Equalizer.enabled
                onToggled: Equalizer.enabled = checked
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("eq_20_bands")
                color: equalizerDialog.textColor
                font.pixelSize: 14
            }
            Switch {
                checked: Equalizer.bands === 20
                onToggled: Equalizer.bands = checked ? 20 : 10
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: equalizerDialog.lineColor
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: qsTr("eq_preset")
                color: equalizerDialog.textColor
                font.pixelSize: 14
            }
            ComboBox {
                id: presetCombo
                Layout.preferredWidth: 250
                font.pixelSize: 14
                textRole: "text"
                valueRole: "value"
                model: equalizerDialog.presetModel
                currentIndex: -1

                function syncSelection() {
                    const index = model.findIndex(item => item.value === Equalizer.presetId);
                    currentIndex = index;
                }

                Component.onCompleted: syncSelection()
                onModelChanged: syncSelection()
                Connections {
                    target: Equalizer
                    function onPresetChanged() { presetCombo.syncSelection(); }
                }
                onActivated: (index) => {
                    const item = model[index];
                    if (!item) return;
                    if (item.builtin) Equalizer.applyBuiltinPreset(item.id);
                    else if (item.custom) Equalizer.applyCustomPreset(item.name);
                    else syncSelection();
                }
            }

            Button {
                text: qsTr("eq_save_preset")
                onClicked: saveDialog.open()
            }

            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: equalizerDialog.cardColor
            border.width: 1
            border.color: equalizerDialog.lineColor
            opacity: Equalizer.enabled ? 1.0 : 0.55

            Behavior on opacity {
                NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                // Preamp: a strip with its value on the left and the caption
                // below, so the wide bands get the rest of the window.
                Item {
                    Layout.preferredWidth: 50
                    Layout.fillHeight: true

                    EqSlider {
                        id: preampSlider
                        x: 30
                        y: 14
                        width: 20
                        height: parent.height - 28
                        emphasized: true
                        value: Equalizer.preamp
                        onMoved: (value) => Equalizer.preamp = value
                    }
                    Label {
                        width: 28
                        text: equalizerDialog.gainLabel(Equalizer.preamp)
                        font.pixelSize: equalizerDialog.compact ? 8 : 9
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        color: equalizerDialog.dimText
                        y: Math.max(preampSlider.y,
                                    Math.min(preampSlider.y + preampSlider.handleY
                                             + preampSlider.handleHeight / 2 - height / 2,
                                             preampSlider.y + preampSlider.height - height))
                    }
                    Label {
                        width: implicitWidth
                        height: 14
                        x: preampSlider.x + (preampSlider.width - width) / 2
                        anchors.bottom: parent.bottom
                        text: qsTr("eq_preamp")
                        font.pixelSize: equalizerDialog.compact ? 8 : 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: equalizerDialog.textColor
                    }
                }

                // dB scale, aligned with the band sliders.
                ColumnLayout {
                    Layout.preferredWidth: 24
                    Layout.fillHeight: true
                    spacing: 0

                    Item { Layout.preferredHeight: 14 }
                    Item {
                        id: scaleArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Repeater {
                            model: [12, 6, 0, -6, -12]
                            Label {
                                text: (modelData > 0 ? "+" : "") + modelData
                                font.pixelSize: equalizerDialog.compact ? 8 : 9
                                color: equalizerDialog.dimText
                                x: 0
                                y: 6 + (1 - (modelData + 12) / 24) * (scaleArea.height - 12) - height / 2
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 14 }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 2

                    Repeater {
                        model: Equalizer.frequencies

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 14
                                text: equalizerDialog.gainLabel(Equalizer.gains[index])
                                font.pixelSize: equalizerDialog.compact ? 8 : 9
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: equalizerDialog.dimText
                                elide: Text.ElideRight
                            }
                            EqSlider {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                value: Number(Equalizer.gains[index])
                                onMoved: (value) => Equalizer.setGain(index, value)
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 14
                                text: equalizerDialog.frequencyLabel(modelData)
                                font.pixelSize: equalizerDialog.compact ? 8 : 9
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: equalizerDialog.textColor
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("eq_reset_custom")
                enabled: Equalizer.customPresets.length > 0
                onClicked: resetDialog.open()
            }
            Button {
                text: qsTr("eq_close")
                onClicked: equalizerDialog.close()
            }
        }
    }

    // Saving the current curve as a named preset.
    Dialog {
        id: saveDialog
        modal: true
        focus: true
        width: 400
        padding: 20
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        title: qsTr("eq_save_preset")

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.97)
            radius: 8
            border.color: "transparent"
        }

        onOpened: {
            presetNameField.text = "";
            Qt.callLater(() => presetNameField.forceActiveFocus());
        }

        contentItem: ColumnLayout {
            spacing: 14

            Label {
                Layout.fillWidth: true
                text: qsTr("eq_preset_name")
                color: equalizerDialog.textColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            TextField {
                id: presetNameField
                Layout.fillWidth: true
                font.pixelSize: 14
                color: equalizerDialog.textColor
                selectByMouse: true
                placeholderText: qsTr("eq_preset_placeholder")
                onAccepted: equalizerDialog.savePreset()

                background: Rectangle {
                    implicitHeight: 36
                    radius: 4
                    color: Qt.rgba(equalizerDialog.textColor.r, equalizerDialog.textColor.g,
                                   equalizerDialog.textColor.b, 0.05)
                    border.width: 1
                    border.color: presetNameField.activeFocus
                                  ? equalizerDialog.accentColor : "transparent"
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                Button {
                    text: qsTr("eq_cancel")
                    onClicked: saveDialog.close()
                }
                Button {
                    text: qsTr("eq_save")
                    enabled: presetNameField.text.trim().length > 0
                    onClicked: equalizerDialog.savePreset()
                }
            }
        }
    }

    // Resetting the user's presets never touches the builtin ones.
    Dialog {
        id: resetDialog
        modal: true
        focus: true
        width: 420
        padding: 20
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        title: qsTr("eq_reset_custom")

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.97)
            radius: 8
            border.color: "transparent"
        }

        contentItem: ColumnLayout {
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("eq_reset_confirm")
                color: equalizerDialog.textColor
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                Button {
                    text: qsTr("eq_cancel")
                    onClicked: resetDialog.close()
                }
                Button {
                    text: qsTr("eq_reset")
                    onClicked: {
                        Equalizer.resetCustomPresets();
                        resetDialog.close();
                    }
                }
            }
        }
    }
}
