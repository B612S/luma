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
    border.color: "transparent" // Убран бордюр

    property var mpv
    property var model: null
    property bool hovered: hoverArea.containsMouse
    property bool isOpen: false
    property bool _processingClick: false

    property var rootContextMenu: null

    property bool isDragging: false
    property int draggedIndex: -1
    property int readyIndex: -1

    property real dragGlobalY: -1
    property real dragZoneHeight: 40

    property int contextMenuIndex: -1
    property var fileInfoData: ({})

    visible: opacity > 0.0
    enabled: isOpen
    opacity: isOpen ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

    onOpacityChanged: {
        if (opacity > 0.0) {
            visible = true;
            if (isOpen) scrollToCurrent();
        }
    }

    onIsOpenChanged: {
        if (!isOpen) {
            contextMenu.close();
            infoDialog.close();
        }
    }

    function closeContextMenu() {
        if (contextMenu.visible) {
            contextMenu.close();
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

    PropertyAnimation {
        id: wheelAnim
        target: listView
        property: "contentY"
        duration: 100
        easing.type: Easing.OutQuad
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
            cacheBuffer: 400

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    var maxY = Math.max(0, listView.contentHeight - listView.height);
                    var baseY = wheelAnim.running ? wheelAnim.to : listView.contentY;
                    var newTargetY = Math.max(0, Math.min(maxY, baseY - event.angleDelta.y));

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
                property bool isCurrent: model.isCurrent

                z: isBeingDragged ? 2 : 0

                color: {
                    if (isBeingDragged) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4);
                    if (isReady) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25);
                    if (dragArea.containsMouse) return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1);
                    return "transparent";
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    width: parent.width - 24
                    text: model.display
                    color: Kirigami.Theme.textColor
                    font.pixelSize: 14
                    font.weight: isCurrent ? Font.Bold : Font.Medium
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

                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    drag.target: (panel.readyIndex === itemRect.itemIndex) ? itemRect : null
                    drag.axis: Drag.YAxis

                    property real pressX: 0
                    property real pressY: 0

                    onClicked: (mouse) => {
                        if (panel.isDragging) return;
                        if (mouse.button === Qt.RightButton) {
                            if (contextMenu.visible && panel.contextMenuIndex === itemRect.itemIndex) {
                                contextMenu.close();
                                return;
                            }

                            panel.contextMenuIndex = itemRect.itemIndex;
                            var globalPos = mapToItem(null, mouse.x, mouse.y);
                            contextMenu.popup(globalPos.x, globalPos.y);
                        } else {
                            listView.currentIndex = itemRect.itemIndex
                            if (panel.parent) panel.parent.forceActiveFocus()
                        }
                    }

                    onDoubleClicked: (mouse) => {
                        if (mouse.button !== Qt.LeftButton) return; // Защита от двойного клика правой кнопкой

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
                        if (mouse.button === Qt.LeftButton) {
                            dragHoldTimer.start();
                        }
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

    Menu {
        id: contextMenu
        width: 200
        modal: false

        onOpened: {
            if (panel.rootContextMenu) {
                panel.rootContextMenu.close();
            }
        }

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                           Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.95)
            radius: 8
            border.color: "transparent" // Убран бордюр
        }

        MenuItem {
            text: qsTr("playlist_play")
            onTriggered: Media.playIndex(panel.contextMenuIndex)
        }

        MenuItem {
            text: qsTr("playlist_shuffle")
            visible: Media.playMode === "shuffle"
            onTriggered: {
                Media.playlist.setShuffle(true);
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("playlist_info")
            onTriggered: {
                let path = Media.playlist.fileAt(panel.contextMenuIndex);
                panel.fileInfoData = Media.getFileInfo(path);
                infoDialog.open();
            }
        }

        MenuItem {
            text: qsTr("playlist_open_folder").arg(Media.getDefaultFileManagerName())
            onTriggered: {
                let path = Media.playlist.fileAt(panel.contextMenuIndex);
                Media.openInFileManager(path);
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("playlist_remove")
            contentItem: Text {
                text: parent.text
                color: Kirigami.Theme.negativeTextColor
                font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
            onTriggered: Media.removeFromPlaylist(panel.contextMenuIndex)
        }
    }

    MediaInfoDialog {
        id: infoDialog
        fileInfo: panel.fileInfoData
    }
}
