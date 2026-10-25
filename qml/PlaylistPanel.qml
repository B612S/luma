import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: panel
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                   Kirigami.Theme.backgroundColor.g,
                   Kirigami.Theme.backgroundColor.b, 0.85)
    radius: 8
    border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                          Kirigami.Theme.textColor.g,
                          Kirigami.Theme.textColor.b, 0.2)
    property var mpv
    property var model: null
    property bool hovered: hoverArea.containsMouse
    property bool isOpen: false
    property bool _processingClick: false

    property bool isDragging: false
    property int draggedIndex: -1
    property int readyIndex: -1

    property real dragGlobalY: -1
    property real dragZoneHeight: 40

    // Для плавной прокрутки колесиком
    property real wheelTargetY: -1

    opacity: isOpen ? 1.0 : 0.0
    visible: opacity > 0.0

    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

    onOpacityChanged: {
        if (opacity > 0.0) {
            visible = true;
            if (isOpen) scrollToCurrent();
        }
    }

    Timer {
        id: scrollTimer
        interval: 50; repeat: false
        onTriggered: {
            if (listView.currentIndex >= 0 && listView.currentIndex < listView.count)
                listView.positionViewAtIndex(listView.currentIndex, ListView.Center)
        }
    }

    function scrollToCurrent() {
        if (panel.model) listView.currentIndex = panel.model.currentIndex;
        scrollTimer.start();
    }

    Timer {
        id: clickResetTimer
        interval: 500; repeat: false
        onTriggered: panel._processingClick = false
    }

    Connections {
        target: panel.model
        function onCurrentIndexChanged() { listView.currentIndex = panel.model.currentIndex; }
    }

    // Следим за состоянием Drag (страховка)
    Connections {
        target: dragArea
        function onDragActiveChanged() {
            if (!dragArea.drag.active && panel.isDragging) {
                panel.endDrag();
            }
        }
    }

    function endDrag() {
        panel.isDragging = false;
        panel.readyIndex = -1;
        panel.draggedIndex = -1;
        panel.dragGlobalY = -1;
        autoScrollTimer.stop();

        // Принудительно возвращаем Y всем элементам
        for (var i = 0; i < listView.count; i++) {
            var item = listView.itemAtIndex(i);
            if (item && typeof item.itemIndex !== "undefined") {
                item.y = Qt.binding(function() { return item.itemIndex * 38; });
            }
        }
    }

    Timer {
        id: autoScrollTimer
        interval: 16
        repeat: true
        running: false
        onTriggered: {
            if (!panel.isDragging || panel.dragGlobalY < 0) return;
            var localY = listView.mapFromItem(null, 0, panel.dragGlobalY).y;
            if (localY < panel.dragZoneHeight) {
                var overflow = panel.dragZoneHeight - localY;
                var speed = Math.min(25, overflow * 0.8 + 5);
                listView.contentY = Math.max(0, listView.contentY - speed);
            } else if (localY > listView.height - panel.dragZoneHeight) {
                var dist = localY - (listView.height - panel.dragZoneHeight);
                var speedDown = Math.min(25, dist * 0.8 + 5);
                var maxY = Math.max(0, listView.contentHeight - listView.height);
                listView.contentY = Math.min(maxY, listView.contentY + speedDown);
            }
        }
    }

    // Анимация для колесика
    NumberAnimation {
        id: wheelAnim
        target: listView
        property: "contentY"
        duration: 150
        easing.type: Easing.OutQuad
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        propagateComposedEvents: false
        onPressed: (mouse) => { mouse.accepted = true }
        onClicked: (mouse) => { mouse.accepted = true }
        onDoubleClicked: (mouse) => { mouse.accepted = true }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.model
            boundsBehavior: Flickable.StopAtBounds
            focus: true
            interactive: !panel.isDragging

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    // Вычисляем, куда хотим прокрутить
                    var maxY = Math.max(0, listView.contentHeight - listView.height);

                    // Если анимация уже идет, берем её конечную точку как базу, иначе текущую
                    var baseY = wheelAnim.running ? wheelAnim.to : listView.contentY;

                    // Новая цель
                    var newTargetY = Math.max(0, Math.min(maxY, baseY - event.angleDelta.y));

                    // Запускаем/перезапускаем анимацию к новой цели
                    wheelAnim.stop();
                    wheelAnim.from = listView.contentY;
                    wheelAnim.to = newTargetY;
                    wheelAnim.start();
                }
            }

            currentIndex: panel.model ? panel.model.currentIndex : -1

            ScrollBar.vertical: ScrollBar {
                id: vbar
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: itemRect
                width: vbar.visible ? listView.width - vbar.width - 8 : listView.width
                height: 38
                radius: 4

                property int itemIndex: index
                property bool isReady: panel.readyIndex === itemIndex
                property bool isBeingDragged: panel.draggedIndex === itemIndex

                z: isBeingDragged ? 2 : 0

                color: {
                    if (isBeingDragged) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4);
                    if (isReady) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25);
                    return "transparent";
                }

                // Фиксация Y во время драга
                y: {
                    if (panel.isDragging && panel.draggedIndex === itemIndex) {
                        return itemRect.y;
                    }
                    return itemIndex * height;
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    width: parent.width - 24
                    text: model.display
                    color: Kirigami.Theme.textColor
                    font.pixelSize: 14
                    font.weight: model.isCurrent ? Font.Bold : Font.Medium
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                }

                Drag.active: dragArea.drag.active
                Drag.hotSpot.x: width / 2
                Drag.hotSpot.y: height / 2

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    propagateComposedEvents: false

                    drag.target: (panel.readyIndex === itemRect.itemIndex) ? itemRect : null
                    drag.axis: Drag.YAxis

                    property real pressX: 0
                    property real pressY: 0

                    onClicked: (mouse) => {
                        if (panel.isDragging) return;
                        listView.currentIndex = itemRect.itemIndex
                        if (panel.parent) panel.parent.forceActiveFocus()
                    }

                    onDoubleClicked: (mouse) => {
                        if (panel.isDragging) return;
                        if (panel._processingClick) return
                            panel._processingClick = true
                            clickResetTimer.start()
                            Media.playIndex(itemRect.itemIndex)
                            if (panel.parent) panel.parent.forceActiveFocus()
                    }

                    onPressed: (mouse) => {
                        pressX = mouse.x;
                        pressY = mouse.y;
                        dragHoldTimer.start();
                    }

                    onPositionChanged: (mouse) => {
                        if (!panel.isDragging && panel.readyIndex === -1) {
                            if (Math.abs(mouse.x - pressX) > 5 || Math.abs(mouse.y - pressY) > 5) {
                                dragHoldTimer.stop();
                            }
                        } else if (panel.isDragging) {
                            var globalMouse = mapToItem(null, mouse.x, mouse.y);
                            panel.dragGlobalY = globalMouse.y;

                            var localY = itemRect.mapToItem(listView.contentItem, 0, 0).y;
                            var newIndex = Math.round(localY / 38);
                            newIndex = Math.max(0, Math.min(listView.count - 1, newIndex));

                            if (newIndex !== panel.draggedIndex) {
                                panel.model.moveItem(panel.draggedIndex, newIndex);
                                panel.draggedIndex = newIndex;
                                panel.readyIndex = newIndex;
                            }
                        }
                    }

                    onReleased: {
                        dragHoldTimer.stop();
                        if (panel.isDragging) {
                            panel.endDrag();
                        }
                    }
                }

                Timer {
                    id: dragHoldTimer
                    interval: 1000
                    repeat: false
                    onTriggered: {
                        panel.readyIndex = itemRect.itemIndex;
                        panel.draggedIndex = itemRect.itemIndex;
                        panel.isDragging = true;
                        autoScrollTimer.start();
                    }
                }
            }
        }
    }
}
