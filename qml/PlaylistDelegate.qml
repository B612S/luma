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
    height: 38
    radius: 4

    property int itemIndex: index
    property bool isReady: false
    property bool isBeingDragged: false
    property bool isCurrent: false
    // IMPORTANT: don't name the properties panel/listView — they would shadow
    // the ids of the same name from PlaylistPanel.qml, and the binding
    // `panel: panel` would become a self-assignment (the property stays null).
    property var playlistView: null
    property var playlistPanel: null
    property var scrollBar: null

    z: isBeingDragged ? 2 : 0
    color: {
        if (isBeingDragged) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4);
        if (isReady) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25);
        if (dragArea.containsMouse) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1);
        return "transparent";
    }

    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
        Text {
            Layout.fillWidth: true; text: model.display
            // A file that failed to open (deleted or moved) stays red until it
            // is available again.
            color: model.unavailable ? Kirigami.Theme.negativeTextColor
                                     : Kirigami.Theme.textColor
            font.pixelSize: 14
            font.weight: isCurrent ? Font.Bold : Font.Medium
            elide: Text.ElideRight; wrapMode: Text.NoWrap
            verticalAlignment: Text.AlignVCenter; Layout.alignment: Qt.AlignVCenter
        }
        Text {
            visible: (model.artist || "").length > 0
            text: model.artist || ""
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
            font.pixelSize: 12; elide: Text.ElideRight
            Layout.maximumWidth: 100
            verticalAlignment: Text.AlignVCenter; Layout.alignment: Qt.AlignVCenter
        }
    }

    Drag.active: dragArea.drag.active
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2

    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true; propagateComposedEvents: false
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        drag.target: (playlistPanel && playlistPanel.readyIndex === itemIndex) ? itemRect : null
        drag.axis: Drag.YAxis

        property real pressX: 0
        property real pressY: 0

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
            // Reordering a filtered list makes no sense: its indices are not
            // playlist indices.
            if (mouse.button === Qt.LeftButton && playlistPanel
                    && !playlistPanel.isFiltering) dragHoldTimer.start();
        }

        onPositionChanged: (mouse) => {
            if (!playlistPanel) return;
            if (!playlistPanel.isDragging && playlistPanel.readyIndex === -1) {
                if (Math.abs(mouse.x - pressX) > 5 || Math.abs(mouse.y - pressY) > 5) dragHoldTimer.stop();
            } else if (playlistPanel.isDragging && playlistView) {
                var globalMouse = mapToItem(null, mouse.x, mouse.y);
                playlistPanel.dragGlobalY = globalMouse.y;
                // Compute the index from the cursor, not from the item's own position:
                // after moveItem the view rearranges the delegates, the position
                // "drifts", and repeated drags to the edge started jumping.
                var viewPos = playlistView.mapFromItem(null, globalMouse.x, globalMouse.y);
                var contentY = viewPos.y + playlistView.contentY;
                var newIndex = Math.floor(contentY / 38);
                newIndex = Math.max(0, Math.min(playlistView.count - 1, newIndex));
                if (newIndex !== playlistPanel.draggedIndex) {
                    playlistPanel.model.moveItem(playlistPanel.draggedIndex, newIndex);
                    playlistPanel.draggedIndex = newIndex;
                    playlistPanel.readyIndex = newIndex;
                }
            }
        }

        onReleased: {
            dragHoldTimer.stop();
            if (playlistPanel && playlistPanel.isDragging) playlistPanel.endDrag();
        }
    }

    Timer {
        id: dragHoldTimer; interval: 1000; repeat: false
        onTriggered: {
            if (playlistPanel && !playlistPanel.isFiltering) {
                playlistPanel.readyIndex = itemIndex; playlistPanel.draggedIndex = itemIndex;
                playlistPanel.isDragging = true; playlistPanel.startAutoScroll();
            }
        }
    }
}
