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
    // ВАЖНО: не называем свойства panel/listView — одноимённые id из
    // PlaylistPanel.qml перекрывались бы ими, и привязка `panel: panel`
    // превращалась бы в самоприсваивание (свойство оставалось null).
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
            color: Kirigami.Theme.textColor; font.pixelSize: 14
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
                // Панель сама выставит index и позицию меню.
                if (playlistPanel)
                    playlistPanel.openTrackMenu(itemIndex, mapToItem(null, mouse.x, mouse.y));
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
                        Media.playIndex(itemIndex)
                        if (playlistPanel && playlistPanel.parent) playlistPanel.parent.forceActiveFocus()
        }

        onPressed: (mouse) => {
            pressX = mouse.x; pressY = mouse.y;
            if (mouse.button === Qt.LeftButton && playlistPanel) dragHoldTimer.start();
        }

        onPositionChanged: (mouse) => {
            if (!playlistPanel) return;
            if (!playlistPanel.isDragging && playlistPanel.readyIndex === -1) {
                if (Math.abs(mouse.x - pressX) > 5 || Math.abs(mouse.y - pressY) > 5) dragHoldTimer.stop();
            } else if (playlistPanel.isDragging && playlistView) {
                var globalMouse = mapToItem(null, mouse.x, mouse.y);
                playlistPanel.dragGlobalY = globalMouse.y;
                // Индекс считаем по курсору, а не по позиции самого элемента:
                // после moveItem представление переставляет делегаты, позиция
                // «плывёт», и при повторных переносах к краю начинались скачки.
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
            if (playlistPanel) {
                playlistPanel.readyIndex = itemIndex; playlistPanel.draggedIndex = itemIndex;
                playlistPanel.isDragging = true; playlistPanel.startAutoScroll();
            }
        }
    }
}
