import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    implicitHeight: 36
    property string activeId: Playlists.activeId
    // Main menu (ContextMenu from Main.qml), so it can be closed from the tab menu.
    property var rootContextMenu: null

    function closeMenus() { tabContextMenu.close(); tabListMenu.close(); }

    property int draggedIndex: -1
    property int dragTargetIndex: -1
    property bool tabDragging: false
    property bool ignoreNextClick: false
    property real dragGlobalX: 0
    property real dragTranslateX: 0
    property real dragSourceX: 0
    property real dragTabWidth: 0
    property real dragGrabOffset: 0

    function tabChildren() {
        let result = [];
        const kids = tabsRow.children;
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].info !== undefined && kids[i].width > 0)
                result.push(kids[i]);
        }
        return result;
    }

    function beginTabDrag(index, grabX) {
        if (index <= 0 || tabDragging) return;
        const kids = tabChildren();
        if (index >= kids.length) return;
        tabDragging = true;
        draggedIndex = index;
        dragTargetIndex = index;
        dragSourceX = kids[index].x;
        dragTabWidth = kids[index].width;
        dragGrabOffset = Math.max(0, Math.min(dragTabWidth, grabX));
        dragTranslateX = 0;
        // Auto-scroll must only start when the pointer really approaches an
        // edge; use the actual press point until the first mouse move arrives.
        dragGlobalX = kids[index].mapToItem(null, dragGrabOffset, 0).x;
        flickable.interactive = false;
        tabAutoScrollTimer.start();
    }

    function updateTabDrag(globalX) {
        if (!tabDragging) return;
        dragGlobalX = globalX;
        const kids = tabChildren();
        if (kids.length <= 1) return;
        const local = tabsRow.mapFromItem(null, globalX, 0);
        dragTranslateX = local.x - (dragSourceX + dragGrabOffset);

        let target = 1;
        for (let i = 0; i < kids.length; ++i) {
            if (local.x >= kids[i].x + kids[i].width / 2)
                target = i + 1;
        }
        // Default stays at index 0; the other tabs keep their relative order.
        dragTargetIndex = Math.max(1, Math.min(kids.length - 1, target));
    }

    function tabShiftFor(index) {
        if (!tabDragging || index === draggedIndex) return 0;
        const step = dragTabWidth + tabsRow.spacing;
        if (dragTargetIndex > draggedIndex && index > draggedIndex && index <= dragTargetIndex)
            return -step;
        if (dragTargetIndex < draggedIndex && index >= dragTargetIndex && index < draggedIndex)
            return step;
        return 0;
    }

    function endTabDrag(commit) {
        if (!tabDragging) return;
        const from = draggedIndex;
        const to = dragTargetIndex;
        tabDragging = false;
        draggedIndex = -1;
        dragTargetIndex = -1;
        flickable.interactive = true;
        tabAutoScrollTimer.stop();
        dragTranslateX = 0;
        if (commit && from > 0 && to > 0 && from !== to)
            Playlists.movePlaylist(from, to);
    }

    signal createRequested()
    signal renameRequested(string id, string currentName)
    signal deleteRequested(string id, string name)
    signal addFilesRequested(string id)
    signal addFolderRequested(string id)
    signal addUrlRequested(string id)
    signal clearRequested(string id)

    RowLayout {
        anchors.fill: parent; spacing: 4

        // Dropdown with every playlist. It sits on the left so the tabs start
        // after it; the "+" button stays on the right.
        Item {
            id: allTabsBtn
            Layout.preferredWidth: 32; Layout.preferredHeight: 32; Layout.alignment: Qt.AlignVCenter

            Rectangle {
                anchors.fill: parent; radius: 6
                color: (allTabsHover.containsMouse || tabListMenu.visible)
                       ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                       : "transparent"
            }

            // Monochrome playlist icon provided by the active icon theme.
            Kirigami.Icon {
                anchors.centerIn: parent
                width: 18; height: 18
                source: "view-media-playlist-symbolic"
                color: Kirigami.Theme.textColor
                // Force the themed icon to be drawn as a monochrome mask.
                isMask: true
            }

            MouseArea {
                id: allTabsHover
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (tabListMenu.visible) tabListMenu.close();
                    else tabListMenu.open();
                }
            }

            ToolTip.visible: allTabsHover.containsMouse
            ToolTip.text: qsTranslate("PlaylistTabs", "tabs_all")
            ToolTip.delay: 500
        }

        Flickable {
            id: flickable
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            contentWidth: tabsRow.implicitWidth; contentHeight: height
            boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.HorizontalFlick
            maximumFlickVelocity: 2000

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    let maxX = Math.max(0, flickable.contentWidth - flickable.width);
                    let newX = flickable.contentX - (event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x);
                    flickable.contentX = Math.max(0, Math.min(maxX, newX));
                    event.accepted = true;
                }
            }

            Row {
                id: tabsRow; spacing: 4
                Repeater {
                    model: Playlists.playlists
                    delegate: Item {
                        id: tabBtn
                        width: Math.max(60, Math.min(tabLabel.implicitWidth + 24, 180))
                        height: flickable.height
                        property var info: modelData
                        property int tabIndex: index
                        property bool isActive: info.id === root.activeId
                        property bool isDefault: info.isDefault === true
                        property bool isDragged: root.tabDragging && root.draggedIndex === tabIndex
                        z: isDragged ? 10 : 0

                        transform: Translate {
                            x: root.tabDragging
                               ? (root.draggedIndex === tabBtn.tabIndex
                                  ? root.dragTranslateX
                                  : root.tabShiftFor(tabBtn.tabIndex))
                               : 0
                            Behavior on x {
                                enabled: !(root.tabDragging && root.draggedIndex === tabBtn.tabIndex)
                                NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                            }
                        }

                        Timer {
                            id: tabDragHold; interval: 1000; repeat: false
                            onTriggered: root.beginTabDrag(tabBtn.tabIndex, tabHover.pressX)
                        }

                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2; radius: 6
                            color: tabBtn.isDragged
                                   ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
                                   : (tabHover.containsMouse && !tabBtn.isActive
                                      ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
                                      : "transparent")
                            Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.InOutQuad } }
                        }

                        Text {
                            // Keep the tab names on the same optical line as the
                            // icon buttons (they used to sit 2 px too high).
                            id: tabLabel; anchors.centerIn: parent; anchors.verticalCenterOffset: 0
                            text: tabBtn.info.name; width: Math.min(implicitWidth, 180 - 24)
                            color: Kirigami.Theme.textColor; font.pixelSize: 13; font.weight: Font.Normal
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(tabLabel.width + 16, parent.width - 16); height: 3; radius: 1.5
                            color: Kirigami.Theme.highlightColor; visible: tabBtn.isActive
                        }

                        MouseArea {
                            id: tabHover; anchors.fill: parent; hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            preventStealing: root.tabDragging
                            property real pressX: 0
                            property real pressY: 0

                            onPressed: (mouse) => {
                                pressX = mouse.x;
                                pressY = mouse.y;
                                if (mouse.button === Qt.LeftButton && !tabBtn.isDefault)
                                    tabDragHold.start();
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return;
                                const dx = Math.abs(mouse.x - pressX);
                                const dy = Math.abs(mouse.y - pressY);
                                if (!root.tabDragging && (dx > 6 || dy > 6))
                                    tabDragHold.stop();
                                if (root.tabDragging)
                                    root.updateTabDrag(mapToItem(null, mouse.x, mouse.y).x);
                            }
                            onReleased: {
                                tabDragHold.stop();
                                if (root.tabDragging) {
                                    root.ignoreNextClick = true;
                                    ignoreClickTimer.restart();
                                    root.endTabDrag(true);
                                }
                            }
                            onCanceled: {
                                tabDragHold.stop();
                                if (root.tabDragging)
                                    root.endTabDrag(false);
                            }
                            onClicked: (mouse) => {
                                if (root.ignoreNextClick || root.tabDragging) return;
                                if (mouse.button === Qt.RightButton) {
                                    tabContextMenu.targetId = tabBtn.info.id;
                                    tabContextMenu.targetName = tabBtn.info.name;
                                    tabContextMenu.targetIsDefault = tabBtn.isDefault;

                                    // The menu position is relative to its parent (root),
                                    // not in window coordinates.
                                    let scenePos = mapToItem(null, mouse.x, mouse.y);
                                    let win = root.window;
                                    let maxX = win ? win.width - tabContextMenu.width - 10 : scenePos.x;
                                    let localPos = root.mapFromItem(null,
                                        Math.max(10, Math.min(scenePos.x, maxX)),
                                        Math.max(10, scenePos.y));
                                    tabContextMenu.x = localPos.x;
                                    tabContextMenu.y = localPos.y;
                                    tabContextMenu.open();
                                } else {
                                    Playlists.activeId = tabBtn.info.id;
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            id: addBtn
            Layout.preferredWidth: 32; Layout.preferredHeight: 32; Layout.alignment: Qt.AlignVCenter
            Rectangle {
                anchors.fill: parent; radius: 6
                color: addHover.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1) : "transparent"
            }
            Text { anchors.centerIn: parent; text: "+"; color: Kirigami.Theme.textColor; font.pixelSize: 20; font.weight: Font.Medium }
            MouseArea { id: addHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.createRequested() }
            ToolTip.visible: addHover.containsMouse; ToolTip.text: qsTranslate("PlaylistTabs", "tabs_new"); ToolTip.delay: 500
        }
    }

    Timer {
        id: ignoreClickTimer; interval: 200; repeat: false
        onTriggered: root.ignoreNextClick = false
    }

    Timer {
        id: tabAutoScrollTimer; interval: 16; repeat: true; running: false
        onTriggered: {
            if (!root.tabDragging) return;
            const left = flickable.mapToItem(null, 0, 0).x;
            const right = left + flickable.width;
            let delta = 0;
            if (root.dragGlobalX < left + 24) delta = -12;
            else if (root.dragGlobalX > right - 24) delta = 12;
            if (delta !== 0) {
                const maxX = Math.max(0, flickable.contentWidth - flickable.width);
                flickable.contentX = Math.max(0, Math.min(maxX, flickable.contentX + delta));
                root.updateTabDrag(root.dragGlobalX);
            }
        }
    }

    // List of all playlists (the tab row can be scrolled away).
    Menu {
        id: tabListMenu
        width: 220
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        x: Math.max(8, Math.min(root.width - width - 8,
                                allTabsBtn.mapToItem(root, 0, 0).x))
        y: allTabsBtn.mapToItem(root, 0, allTabsBtn.height).y + 4

        onOpened: {
            if (root.rootContextMenu) root.rootContextMenu.close();
            tabContextMenu.close();
        }

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
            radius: 8; border.color: "transparent"
        }

        Instantiator {
            model: Playlists.playlists
            delegate: MenuItem {
                text: (modelData.id === Playlists.activeId ? "✓ " : "") + modelData.name
                onTriggered: Playlists.activeId = modelData.id
            }
            onObjectAdded: (index, object) => tabListMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => tabListMenu.removeItem(object)
        }
    }

    PlaylistTabsContextMenu {
        id: tabContextMenu
        rootContextMenu: root.rootContextMenu
        onAddFilesRequested: (id) => root.addFilesRequested(id)
        onAddFolderRequested: (id) => root.addFolderRequested(id)
        onAddUrlRequested: (id) => root.addUrlRequested(id)
        onClearRequested: (id) => root.clearRequested(id)
        onRenameRequested: (id, name) => root.renameRequested(id, name)
        onDeleteRequested: (id, name) => root.deleteRequested(id, name)
    }
}
