import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Native, host-rendered settings for a plugin. The plugin only declares a
// schema in plugin.json; it never supplies QML.
Dialog {
    id: dialog
    modal: true
    focus: true
    width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 32 : 460)
    padding: 20
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string pluginId: ""
    property string pluginName: ""
    property var settings: []

    title: qsTranslate("SettingsDialog", "plugins_settings_title").arg(pluginName)

    function openFor(plugin) {
        pluginId = plugin.id;
        pluginName = plugin.name;
        settings = Plugins.pluginSettings(plugin.id);
        open();
    }

    onOpened: Qt.callLater(() => settingsContent.forceActiveFocus())

    function save(key, value) {
        Plugins.setPluginSetting(pluginId, key, value);
    }

    function selectedIndex(item, value) {
        const options = item.options || [];
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i;
        }
        return 0;
    }

    contentItem: Flickable {
        id: settingsFlickable
        implicitHeight: Math.min(settingsContent.implicitHeight, 520)
        contentWidth: width
        contentHeight: settingsContent.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

        FocusScroller { flickable: settingsFlickable }

        ColumnLayout {
            id: settingsContent
            width: parent.width - 16
            focus: true
            activeFocusOnTab: true
            spacing: 14

            Repeater {
            model: dialog.settings

            delegate: ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        Layout.fillWidth: true
                        text: modelData.label || modelData.key
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        color: Kirigami.Theme.textColor
                    }

                    Switch {
                        visible: modelData.type === "switch"
                        checked: modelData.value === true
                        onToggled: dialog.save(modelData.key, checked)
                    }

                    ComboBox {
                        visible: modelData.type === "select"
                        Layout.preferredWidth: 170
                        model: modelData.options || []
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: dialog.selectedIndex(modelData, modelData.value)
                        onActivated: (index) => dialog.save(modelData.key, modelData.options[index].value)
                    }

                    SpinBox {
                        visible: modelData.type === "number"
                        Layout.preferredWidth: 140
                        from: modelData.min !== undefined ? modelData.min : 0
                        to: modelData.max !== undefined ? modelData.max : 999999
                        stepSize: modelData.step !== undefined ? modelData.step : 1
                        value: Number(modelData.value)
                        onValueModified: dialog.save(modelData.key, value)
                    }
                }

                TextField {
                    Layout.fillWidth: true
                    visible: modelData.type === "text"
                    text: modelData.value !== undefined ? String(modelData.value) : ""
                    placeholderText: modelData.placeholder || ""
                    onEditingFinished: dialog.save(modelData.key, text)
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            dialog.save(modelData.key, text);
                    }
                }

                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 90
                    visible: modelData.type === "textarea"
                    text: modelData.value !== undefined ? String(modelData.value) : ""
                    placeholderText: modelData.placeholder || ""
                    wrapMode: TextEdit.Wrap
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            dialog.save(modelData.key, text)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: modelData.description !== undefined && modelData.description !== ""
                    text: modelData.description || ""
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Qt.rgba(Kirigami.Theme.textColor.r,
                                   Kirigami.Theme.textColor.g,
                                   Kirigami.Theme.textColor.b, 0.65)
                }
                }
            }
        }
    }
}
