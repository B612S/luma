import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One native field of a plugin window. Plugins only describe controls; the
// host renders them with the same controls and theme as the rest of Luma.
ColumnLayout {
    id: root

    property var itemData: ({})
    property var dialogRef: null
    readonly property string kind: itemData.type || ""

    Layout.fillWidth: true
    spacing: 6

    function publish(value) {
        if (dialogRef && itemData.id)
            dialogRef.setValue(itemData.id, value);
    }

    Label {
        Layout.fillWidth: true
        visible: root.kind === "heading"
        text: root.itemData.text || ""
        textFormat: Text.PlainText
        font.pixelSize: 20
        font.weight: Font.Bold
        color: Kirigami.Theme.textColor
    }

    Label {
        Layout.fillWidth: true
        visible: root.kind === "label"
        text: root.itemData.text || ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font.pixelSize: 13
        color: Kirigami.Theme.textColor
    }

    Rectangle {
        Layout.fillWidth: true
        visible: root.kind === "separator"
        Layout.preferredHeight: 1
        color: Qt.rgba(Kirigami.Theme.textColor.r,
                       Kirigami.Theme.textColor.g,
                       Kirigami.Theme.textColor.b, 0.2)
    }

    Item {
        Layout.fillWidth: true
        visible: root.kind === "spacer"
        Layout.preferredHeight: root.itemData.height !== undefined
                                ? Number(root.itemData.height) : 12
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.kind === "text" || root.kind === "textarea" || root.kind === "select"
        spacing: 6

        Label {
            Layout.fillWidth: true
            visible: (root.itemData.label || "") !== ""
            text: root.itemData.label || ""
            textFormat: Text.PlainText
            font.pixelSize: 13
            color: Kirigami.Theme.textColor
        }

        TextField {
            Layout.fillWidth: true
            visible: root.kind === "text"
            text: root.itemData.value !== undefined ? String(root.itemData.value) : ""
            placeholderText: root.itemData.placeholder || ""
            font.pixelSize: 13
            onTextChanged: root.publish(text)
        }

        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            visible: root.kind === "textarea"
            text: root.itemData.value !== undefined ? String(root.itemData.value) : ""
            placeholderText: root.itemData.placeholder || ""
            font.pixelSize: 13
            wrapMode: TextEdit.Wrap
            onTextChanged: root.publish(text)
        }

        ComboBox {
            Layout.fillWidth: true
            visible: root.kind === "select"
            model: root.itemData.options || []
            textRole: "label"
            valueRole: "value"
            font.pixelSize: 13
            currentIndex: {
                const options = root.itemData.options || [];
                for (let i = 0; i < options.length; ++i) {
                    if (options[i].value === root.itemData.value)
                        return i;
                }
                return 0;
            }
            onActivated: (index) => root.publish(model[index].value)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.kind === "switch"
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: root.itemData.label || ""
            textFormat: Text.PlainText
            font.pixelSize: 13
            color: Kirigami.Theme.textColor
            wrapMode: Text.WordWrap
        }
        Switch {
            checked: root.itemData.checked === true
            onToggled: root.publish(checked)
        }
    }

    Button {
        Layout.alignment: Qt.AlignLeft
        visible: root.kind === "button"
        text: root.itemData.label || ""
        onClicked: {
            if (dialogRef)
                dialogRef.submit(root.itemData.id || "");
        }
    }
}
