import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Host-rendered window for plugin UI. `pluginWindow` is a PluginWindow QObject
// from C++; its content is a declarative list of native controls.
Dialog {
    id: dialog
    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var pluginWindow: null
    property var values: ({})
    property bool closingFromPlugin: false

    readonly property var content: pluginWindow ? pluginWindow.content : []
    readonly property var actions: pluginWindow ? pluginWindow.actions : []

    readonly property real maxDialogWidth: {
        const w = Overlay.overlay ? Overlay.overlay.width : 0;
        return Math.max(360, (w > 0 ? w : 560) - 32);
    }
    readonly property real maxDialogHeight: {
        const h = Overlay.overlay ? Overlay.overlay.height : 0;
        return Math.max(280, (h > 0 ? h : 620) - 32);
    }
    width: Math.min(560, maxDialogWidth)
    height: Math.min(implicitHeight, maxDialogHeight)

    title: pluginWindow ? pluginWindow.title : ""

    function resetValues() {
        const next = {};
        for (let i = 0; i < dialog.content.length; ++i) {
            const item = dialog.content[i];
            if (!item.id)
                continue;
            if (item.type === "switch")
                next[item.id] = item.checked === true;
            else if (item.type === "select") {
                const options = item.options || [];
                next[item.id] = item.value !== undefined ? item.value
                                : (options.length > 0 ? options[0].value : "");
            } else if (item.type === "text" || item.type === "textarea") {
                next[item.id] = item.value !== undefined ? item.value : "";
            }
        }
        dialog.values = next;
    }

    function setValue(id, value) {
        const next = Object.assign({}, dialog.values);
        next[id] = value;
        dialog.values = next;
    }

    function submit(actionId) {
        if (pluginWindow)
            pluginWindow.submit(actionId, dialog.values);
    }

    onPluginWindowChanged: {
        resetValues();
        if (pluginWindow)
            dialog.open();
    }

    onOpened: Qt.callLater(() => contentFlick.forceActiveFocus())

    Connections {
        target: dialog.pluginWindow
        function onChanged() {
            dialog.resetValues();
        }
        function onClosed() {
            dialog.closingFromPlugin = true;
            dialog.close();
        }
    }

    onClosed: {
        if (!dialog.closingFromPlugin && dialog.pluginWindow)
            dialog.pluginWindow.close();
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }

    contentItem: Item {
        implicitHeight: Math.min(column.implicitHeight + 40, 560)

        Flickable {
            id: contentFlick
            anchors.fill: parent
            anchors.margins: 20
            clip: true
            focus: true
            activeFocusOnTab: true
            contentWidth: width
            contentHeight: column.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 10 }

            FocusScroller { flickable: contentFlick }

            ColumnLayout {
                id: column
                width: parent.width - 12
                spacing: 12

                Repeater {
                    model: dialog.content
                    delegate: PluginWindowField {
                        itemData: modelData
                        dialogRef: dialog
                    }
                }
            }
        }
    }

    footer: Item {
        implicitHeight: dialog.actions.length > 0 ? 56 : 0
        visible: dialog.actions.length > 0

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Item { Layout.fillWidth: true }

            Repeater {
                model: dialog.actions
                delegate: Button {
                    text: modelData.label || modelData.id || ""
                    onClicked: dialog.submit(modelData.id || "")
                }
            }
        }
    }
}
