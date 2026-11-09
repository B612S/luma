import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: itemRect
    width: {
        if (!playlistView) return parent ? parent.width : 200;
        return scrollBar && scrollBar.visible ? playlistView.width - scrollBar.width - 8 : playlistView.width;
    }
    height: playlistPanel ? playlistPanel.rowHeight : 38
    radius: 4

    property int itemIndex: index
    property bool isBeingDragged: false
    property bool isCurrent: false
    // IMPORTANT: don't name the properties panel/listView — they would shadow
    // the ids of the same name from PlaylistPanel.qml, and the binding
    // `panel: panel` would become a self-assignment (the property stays null).
    property var playlistView: null
    property var playlistPanel: null
    property var scrollBar: null

    // The playing bar is opaque; only a dragged row gets the strong
    // translucent background. The row is not otherwise filled.
    readonly property color selectionColor: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                                    Kirigami.Theme.highlightColor.g,
                                                    Kirigami.Theme.highlightColor.b, 0.85)
    readonly property color selectionBackgroundColor: Qt.rgba(Kirigami.Theme.highlightColor.r,
                                                              Kirigami.Theme.highlightColor.g,
                                                              Kirigami.Theme.highlightColor.b, 0.35)

    // The row being dragged is highlighted in place: it is moved through the
    // model as the pointer crosses rows, so it always occupies the slot that
    // will be dropped. No separate floating copy is drawn.
    color: isBeingDragged
           ? selectionColor
           : (isCurrent
              ? selectionBackgroundColor
              : (dragArea.containsMouse && !(playlistPanel && playlistPanel.isDragging)
                 ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                 : "transparent"))

    // The playing entry is marked by a Kirigami bar instead of the old "▶ "
    // prefix; only a dragged row gets a full background highlight.
    Rectangle {
        visible: isCurrent
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        width: 3
        radius: 1.5
        color: Kirigami.Theme.highlightColor
    }

    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
        Text {
            Layout.fillWidth: true; text: model.display
            // A file that failed to open (deleted or moved) stays red until it
            // is available again.
            color: isBeingDragged ? Kirigami.Theme.highlightedTextColor
                                  : (model.unavailable ? Kirigami.Theme.negativeTextColor
                                                       : Kirigami.Theme.textColor)
            font.pixelSize: 14
            font.weight: (isCurrent || isBeingDragged) ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight; wrapMode: Text.NoWrap
            verticalAlignment: Text.AlignVCenter; Layout.alignment: Qt.AlignVCenter
        }
        Text {
            visible: (model.artist || "").length > 0
            text: model.artist || ""
            color: isBeingDragged
                   ? Qt.rgba(Kirigami.Theme.highlightedTextColor.r,
                             Kirigami.Theme.highlightedTextColor.g,
                             Kirigami.Theme.highlightedTextColor.b, 0.75)
                   : Qt.rgba(Kirigami.Theme.textColor.r,
                             Kirigami.Theme.textColor.g,
                             Kirigami.Theme.textColor.b, 0.6)
            font.pixelSize: 12; elide: Text.ElideRight
            Layout.maximumWidth: 100
            verticalAlignment: Text.AlignVCenter; Layout.alignment: Qt.AlignVCenter
        }
    }

    // The row itself is never handed to the Drag attached property: while the
    // delegate is reordered through the model the view keeps owning its
    // position. This MouseArea only reports the pointer; PlaylistPanel moves
    // the row to the row under it. Coupling the drag to the delegate used to
    // detach it from the pointer after a few moves towards the top of the list.
    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true; propagateComposedEvents: false
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Once reordering starts, no other item (tabs, flickables, the search
        // field) may take the grab away: the drag must survive the pointer
        // leaving the row/list/panel.
        preventStealing: playlistPanel ? playlistPanel.isDragging : false

        property real pressX: 0
        property real pressY: 0
        property real lastSceneY: -1

        onClicked: (mouse) => {
            if (playlistPanel && playlistPanel.isDragging) return;
            if (mouse.button === Qt.RightButton) {
                // The panel sets the index and anchors the menu to this row.
                if (playlistPanel)
                    playlistPanel.openTrackMenu(itemIndex, itemRect, mouse.x, mouse.y);
            } else {
                if (playlistView) playlistView.currentIndex = itemIndex;
                if (playlistPanel && playlistPanel.parent) playlistPanel.parent.forceActiveFocus()
            }
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton) return;
            if (playlistPanel && playlistPanel.isDragging) return;
            if (playlistPanel && playlistPanel._processingClick) return
                if (playlistPanel) playlistPanel._processingClick = true
                    if (playlistPanel) playlistPanel.startClickResetTimer()
                        // The search bar shows filtered rows: play the source row.
                        if (playlistPanel) playlistPanel.playViewIndex(itemIndex)
                        if (playlistPanel && playlistPanel.parent) playlistPanel.parent.forceActiveFocus()
        }

        onPressed: (mouse) => {
            pressX = mouse.x; pressY = mouse.y;
            lastSceneY = mapToItem(null, mouse.x, mouse.y).y;
            // Reordering a filtered list makes no sense: its indices are not
            // playlist indices.
            if (mouse.button === Qt.LeftButton && playlistPanel
                    && !playlistPanel.isFiltering)
                playlistPanel.holdForDrag(itemIndex, lastSceneY);
        }

        onPositionChanged: (mouse) => {
            if (!playlistPanel) return;
            lastSceneY = mapToItem(null, mouse.x, mouse.y).y;
            if (!playlistPanel.isDragging) {
                if (Math.abs(mouse.x - pressX) > 5 || Math.abs(mouse.y - pressY) > 5)
                    playlistPanel.cancelDragHold();
                return;
            }
            playlistPanel.updateDrag(lastSceneY);
        }

        onReleased: {
            if (playlistPanel) playlistPanel.cancelDragHold();
            if (playlistPanel && playlistPanel.isDragging) playlistPanel.endDrag();
        }

        onCanceled: {
            if (playlistPanel) playlistPanel.cancelDragHold();
            if (playlistPanel && playlistPanel.isDragging) playlistPanel.endDrag();
        }
    }

    // A drag cannot survive the delegate being unloaded: stop it instead of
    // leaving the drag active and the list non-interactive behind.
    Component.onDestruction: {
        if (isBeingDragged && playlistPanel && playlistPanel.isDragging)
            playlistPanel.endDrag();
    }
}
