import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

// Row of the in-app context menus (LumaMenu), used by the main player menu and
// by the playlist, tab and statistics menus: a 32 px row highlighted on hover,
// with an optional check mark, arrow and tooltip.
Rectangle {
    id: item

    property string text: ""
    property string tooltipText: ""
    property bool hasArrow: false
    property bool isEnabled: true
    // Destructive action (remove, clear, delete): red label.
    property bool destructive: false
    // Selected entry (playback mode, aspect ratio, track, active playlist).
    property bool checked: false
    // Selected entries get a subtle background and an opaque bar.
    readonly property color selectionBackgroundColor: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                                              Kirigami.Theme.highlightColor.g,
                                                              Kirigami.Theme.highlightColor.b, 0.35)
    // Label width with margins and arrow, so a popup can be sized to its text.
    readonly property real contentWidth: leftMargin + label.implicitWidth
                                          + (arrow.visible ? 8 + arrow.implicitWidth : 0)
                                          + rightMargin

    signal clicked()

    readonly property int leftMargin: 12
    readonly property int rightMargin: 12

    readonly property color labelColor: {
        if (!item.isEnabled)
            return item.destructive
                    ? Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g,
                              Kirigami.Theme.negativeTextColor.b, 0.4)
                    : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b, 0.4);
        return item.destructive ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor;
    }

    width: parent ? parent.width : 0
    height: 32
    radius: 4
    color: item.checked
           ? item.selectionBackgroundColor
           : ((hoverArea.containsMouse && item.isEnabled)
              ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                        Kirigami.Theme.textColor.b, 0.1)
              : "transparent")

    // A selected entry is shown with a Kirigami bar and bold text instead of
    // a check mark.
    Rectangle {
        visible: item.checked
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        width: 3
        radius: 1.5
        color: Kirigami.Theme.highlightColor
    }

    Text {
        id: label
        text: item.text
        color: item.labelColor
        font.pixelSize: 14
        font.weight: item.checked ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: item.leftMargin
        anchors.right: arrow.visible ? arrow.left : parent.right
        anchors.rightMargin: item.rightMargin
    }

    Text {
        id: arrow
        text: ">"
        visible: item.hasArrow
        color: item.labelColor
        font.pixelSize: 14
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: item.rightMargin
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { if (item.isEnabled) item.clicked() }
    }

    ToolTip.visible: hoverArea.containsMouse && item.tooltipText !== ""
    ToolTip.text: item.tooltipText
    ToolTip.delay: 500
}
