import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    implicitHeight: 36
    property string activeId: Playlists.activeId

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
        wheelAnimation.stop();
        tabScrollAnimation.stop();
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

    onActiveIdChanged: Qt.callLater(scrollToActive)
    Component.onCompleted: Qt.callLater(scrollToActive)

    function scrollToActive() {
        if (!flickable || flickable.width <= 0) return;
        const kids = tabChildren();
        for (let i = 0; i < kids.length; ++i) {
            const child = kids[i];
            if (!child.info || child.info.id !== root.activeId) continue;

            const maxX = Math.max(0, flickable.contentWidth - flickable.width);
            const left = child.x;
            const right = child.x + child.width;
            // Already fully visible: don't move the tab strip unnecessarily.
            if (left >= flickable.contentX && right <= flickable.contentX + flickable.width)
                return;

            const target = Math.max(0, Math.min(maxX,
                child.x + child.width / 2 - flickable.width / 2));
            wheelAnimation.stop();
            tabScrollAnimation.stop();
            tabScrollAnimation.from = flickable.contentX;
            tabScrollAnimation.to = target;
            tabScrollAnimation.start();
            return;
        }
    }

    NumberAnimation {
        id: tabScrollAnimation
        target: flickable
        property: "contentX"
        duration: 220
        easing.type: Easing.OutCubic
    }

    Connections {
        target: Playlists
        function onPlaylistsChanged() { Qt.callLater(root.scrollToActive); }
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
                    if (tabListMenu.visible) {
                        tabListMenu.close();
                        return;
                    }
                    // Cap the list height so a long playlist list scrolls
                    // instead of growing past the window.
                    const win = allTabsBtn.Window.window;
                    tabListMenu.maxListHeight =
                            Math.max(120, (win ? win.height : 600) - 20);
                    // The menu follows the button when the window is resized.
                    tabListMenu.openAt(allTabsBtn, 0, allTabsBtn.height + 4);
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

            onWidthChanged: Qt.callLater(root.scrollToActive)
            property real wheelTargetX: contentX

            function scrollByWheel(delta) {
                const maxX = Math.max(0, contentWidth - width);
                // Accumulate wheel notches while the smooth animation is still
                // running, otherwise quick wheel input would be lost.
                const base = wheelAnimation.running ? wheelTargetX : contentX;
                wheelTargetX = Math.max(0, Math.min(maxX, base + delta));
                tabScrollAnimation.stop();
                wheelAnimation.stop();
                wheelAnimation.to = wheelTargetX;
                wheelAnimation.start();
            }

            onMovementStarted: { wheelAnimation.stop(); tabScrollAnimation.stop(); }

            NumberAnimation {
                id: wheelAnimation
                target: flickable
                property: "contentX"
                duration: 180
                easing.type: Easing.OutCubic
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    const delta = event.angleDelta.y !== 0
                        ? event.angleDelta.y : event.angleDelta.x;
                    if (delta !== 0)
                        flickable.scrollByWheel(-delta);
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

                                    // The menu follows the tab when the window is
                                    // resized or the tab row is scrolled.
                                    tabContextMenu.openAt(tabBtn, mouse.x, mouse.y);
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
            Kirigami.Icon {
                anchors.centerIn: parent
                width: 18; height: 18
                source: "list-add-symbolic"
                color: Kirigami.Theme.textColor
                isMask: true
            }
            MouseArea { id: addHover; anchors.fill: parent; hoverEnabled: true; onClicked: root.createRequested() }            ToolTip.visible: addHover.containsMouse; ToolTip.text: qsTranslate("PlaylistTabs", "tabs_new"); ToolTip.delay: 500
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
    LumaMenu {
        id: tabListMenu
        width: 220
        // Height cap set by the button before opening: with many playlists the
        // list scrolls instead of running past the window.
        property int maxListHeight: 360
        // x/y are set in the button's handler in scene coordinates; the menu is
        // parented to the window overlay and LumaMenu keeps it inside the window.

        ListView {
            id: playlistsList
            width: parent ? parent.width : 220
            height: Math.min(contentHeight, tabListMenu.maxListHeight)
            implicitHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: 2
            model: Playlists.playlists

            ScrollBar.vertical: ScrollBar {
                id: playlistsScroll
                policy: ScrollBar.AsNeeded
            }

            delegate: LumaMenuButton {
                width: playlistsList.width
                       - (playlistsScroll.visible ? playlistsScroll.width : 0)
                text: modelData.name
                checked: modelData.id === Playlists.activeId
                onClicked: {
                    Playlists.activeId = modelData.id;
                    tabListMenu.close();
                }
            }
        }
    }

    PlaylistTabsContextMenu {
        id: tabContextMenu
        onAddFilesRequested: (id) => root.addFilesRequested(id)
        onAddFolderRequested: (id) => root.addFolderRequested(id)
        onAddUrlRequested: (id) => root.addUrlRequested(id)
        onClearRequested: (id) => root.clearRequested(id)
        onRenameRequested: (id, name) => root.renameRequested(id, name)
        onDeleteRequested: (id, name) => root.deleteRequested(id, name)
    }
}
