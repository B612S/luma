import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Rectangle {
    id: panel
    color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
    radius: 8; border.color: "transparent"

    property var mpv
    property var model: Media.playlist
    // Search bar at the bottom filters the current playlist; while it is
    // non-empty the list shows the proxy instead of the model itself.
    readonly property bool isFiltering: searchField.text.trim() !== ""
    readonly property var viewModel: isFiltering ? playlistFilter : panel.model
    property bool hovered: hoverArea.containsMouse
    property bool isOpen: false
    property bool _processingClick: false

    property bool isDragging: false
    property int draggedIndex: -1
    property real dragGlobalY: -1
    property real dragZoneHeight: 40
    // -1 = scrolling up, 1 = scrolling down, 0 = not at an edge. The direction
    // is latched so auto-scroll keeps running even when the pointer leaves the
    // panel/window and no more move events arrive (or they stop at the border).
    property int dragScrollDirection: 0
    readonly property int rowHeight: 38

    property int contextMenuIndex: -1
    property real swipeAccumX: 0
    property bool swipeTriggered: false
    property var fileInfoData: ({})
    // Path of the media-information request currently being displayed.
    property string pendingInfoPath: ""

    property bool menuVisible: contextMenu.visible
    property bool hasOpenDialog: dialogs.nameDialog.visible || dialogs.deleteDialog.visible || dialogs.clearDialog.visible || infoDialog.visible || urlDialog.dialog.visible || propertiesDialog.visible

    visible: opacity > 0.0; enabled: isOpen; opacity: isOpen ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

    onOpacityChanged: { if (opacity > 0.0 && isOpen) scrollToCurrent(); }
    onIsOpenChanged: {
        if (!isOpen) {
            // A drag must not survive the panel: its release may never arrive.
            panel.endDrag();
            contextMenu.close(); tabs.closeMenus(); infoDialog.close(); propertiesDialog.close();
            // Never keep keyboard focus inside the closed search field.
            searchField.focus = false;
            if (panel.parent) panel.parent.forceActiveFocus();
        }
    }

    onIsFilteringChanged: {
        if (isFiltering) {
            // The rows are proxy rows now, so a reorder cannot continue.
            panel.endDrag();
            listView.currentIndex = -1;
            listView.positionViewAtBeginning();
        } else {
            scrollToCurrent();
        }
    }

    Connections {
        target: Playlists
        function onActiveIdChanged() {
            panel.endDrag(); contextMenu.close(); tabs.closeMenus(); infoDialog.close();
            // The search belongs to the playlist being left.
            searchField.text = "";
        }
    }

    Connections {
        target: model
        function onCurrentIndexChanged() {
            // While filtering the list rows are proxy rows, not model rows.
            // During a drag the current item is irrelevant, and changing it can
            // make the view scroll away from the dragged row.
            if (model && !panel.isFiltering && !panel.isDragging)
                listView.currentIndex = model.currentIndex;
        }
    }

    Connections {
        target: Media
        function onFileInfoReady(path, info) {
            if (path === panel.pendingInfoPath)
                panel.fileInfoData = info;
        }
    }

    function closeContextMenu() { contextMenu.close(); tabs.closeMenus(); }

    // The list shows either the playlist model or the filtered proxy, so rows
    // must be translated before playing or opening the context menu.
    function sourceIndex(viewIndex) {
        if (!isFiltering) return viewIndex;
        return playlistFilter.mapToSource(viewIndex);
    }

    function viewIndexForSource(sourceRow) {
        if (!isFiltering) return sourceRow;
        return playlistFilter.mapFromSource(sourceRow);
    }

    function playViewIndex(viewIndex) {
        const source = sourceIndex(viewIndex);
        if (source >= 0) Media.playIndex(source);
    }

    function scrollToCurrent() {
        if (!panel.model) return;
        const viewIndex = viewIndexForSource(panel.model.currentIndex);
        if (viewIndex >= 0) listView.currentIndex = viewIndex;
        scrollTimer.start();
    }

    function switchPlaylist(direction) {
        const list = Playlists.playlists;
        let current = -1;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === Playlists.activeId) { current = i; break; }
        }
        if (current < 0) return;
        const target = Math.max(0, Math.min(list.length - 1, current + direction));
        if (target === current) return;
        Playlists.activeId = list[target].id;
    }

    function handleHorizontalSwipe(delta) {
        if (panel.isDragging || contextMenu.visible || panel.hasOpenDialog)
            return;
        if (panel.swipeTriggered) return;
        panel.swipeAccumX += delta;
        if (Math.abs(panel.swipeAccumX) < 120) return;

        panel.swipeTriggered = true;
        // Positive horizontal scroll means moving towards the next playlist.
        panel.switchPlaylist(panel.swipeAccumX > 0 ? 1 : -1);
        swipeResetTimer.restart();
    }

    Timer {
        id: swipeResetTimer
        interval: 350
        onTriggered: {
            panel.swipeAccumX = 0;
            panel.swipeTriggered = false;
        }
    }

    // The track menu lives in this file: from outside (the delegate) we call it by
    // function name, not via `panel.contextMenu` — an id is not an object property.
    // The menu is anchored to the clicked row, so it follows the row (and the
    // window) instead of staying at stale window coordinates.
    function openTrackMenu(viewIndex, anchorItem, localX, localY) {
        const source = sourceIndex(viewIndex);
        if (source < 0) return;
        contextMenuIndex = source;
        contextMenu.openAt(anchorItem, localX, localY);
    }

    function openInfoDialog(path) {
        pendingInfoPath = path;
        const isStream = path.indexOf("http://") === 0 || path.indexOf("https://") === 0;
        // Open immediately with a placeholder; MediaInfoDialog fills in the
        // details when Media.fileInfoReady arrives (FFmpeg runs off the GUI thread).
        fileInfoData = {
            path: path,
            name: isStream ? path : path.split("/").pop(),
            isStream: isStream,
            isAudio: isStream,
            loading: true
        };
        infoDialog.open();
        Media.requestFileInfo(path);
    }

    function openClearDialog(id, name) {
        dialogs.clearDialog.openFor(id, name);
    }

    // The timers are ids inside this file, not properties of the panel object,
    // so the delegate cannot access them directly.
    function startClickResetTimer() { clickResetTimer.start(); }

    function beginDrag(index, sceneY) {
        if (!model || isFiltering || index < 0) return;
        draggedIndex = index;
        dragGlobalY = sceneY;
        isDragging = true;
        updateDragScrollDirection(sceneY);
        autoScrollTimer.start();
    }

    // The pointer may leave the list, the panel or the whole window while the
    // button is held. Remember which edge it was on so the auto-scroll timer
    // can keep going without new move events.
    function updateDragScrollDirection(sceneY) {
        if (!listView || !isDragging || sceneY < 0) {
            dragScrollDirection = 0;
            return;
        }
        const localY = listView.mapFromItem(null, 0, sceneY).y;
        if (localY < dragZoneHeight)
            dragScrollDirection = -1;
        else if (localY > listView.height - dragZoneHeight)
            dragScrollDirection = 1;
        else
            dragScrollDirection = 0;
    }

    // Model row under the pointer. Everything is computed from the pointer,
    // never from the moved delegate's position. The result is clamped to the
    // rows that are actually visible: when the pointer leaves the list, the
    // dragged row must stay on the first/last visible row. Moving it outside
    // the viewport makes the view drop the grabbed delegate (the drag is then
    // cancelled) and the auto-scroll never starts, which looked like the entry
    // vanishing at the top or bottom.
    function viewIndexAtSceneY(sceneY) {
        if (!listView || listView.count === 0) return -1;
        const pointer = listView.mapFromItem(null, 0, sceneY).y + listView.contentY;
        const index = Math.floor(pointer / rowHeight);
        const firstVisible = Math.floor(listView.contentY / rowHeight);
        const lastVisible = Math.floor((listView.contentY + listView.height - 1) / rowHeight);
        const visibleIndex = Math.max(firstVisible, Math.min(lastVisible, index));
        return Math.max(0, Math.min(listView.count - 1, visibleIndex));
    }

    function updateDrag(sceneY) {
        if (!isDragging || sceneY < 0) return;
        dragGlobalY = sceneY;
        updateDragScrollDirection(sceneY);
        const target = viewIndexAtSceneY(sceneY);
        if (target < 0 || target === draggedIndex) return;
        model.moveItem(draggedIndex, target);
        // The move may end the drag synchronously (the grab can be dropped).
        if (!isDragging) return;
        // Keep the highlight in sync before any relayout.
        draggedIndex = target;
        // ListView can defer the relayout of moved rows while the pointer keeps
        // moving. The delegates then stay in their previous slots, the row
        // under the pointer is not the highlighted one any more, and repeated
        // drags start from a broken layout. Re-layout after every move.
        listView.forceLayout();
        // forceLayout() can also cancel the drag; never leave a stale index.
        if (!isDragging) draggedIndex = -1;
    }

    // The model was reordered live, so there is nothing to commit here;
    // updateDrag() has already kept the list layout in sync after every move.
    function endDrag() {
        isDragging = false;
        draggedIndex = -1;
        dragGlobalY = -1;
        dragScrollDirection = 0;
        autoScrollTimer.stop();
    }

    Timer {
        id: scrollTimer; interval: 50; repeat: false
        onTriggered: {
            if (listView.currentIndex >= 0 && listView.currentIndex < listView.count)
                listView.positionViewAtIndex(listView.currentIndex, ListView.Center)
        }
    }

    Timer {
        id: clickResetTimer; interval: 500; repeat: false
        onTriggered: panel._processingClick = false
    }

    Timer {
        id: autoScrollTimer; interval: 16; repeat: true; running: false
        onTriggered: {
            if (!panel.isDragging) return;
            if (panel.dragScrollDirection === 0) return;
            var maxY = Math.max(0, listView.contentHeight - listView.height);
            if (panel.dragScrollDirection < 0)
                listView.contentY = Math.max(0, listView.contentY - 18);
            else
                listView.contentY = Math.min(maxY, listView.contentY + 18);
            // The view scrolled under a stationary pointer, so the row under it
            // changed: move the dragged entry to the visible edge row.
            panel.updateDrag(panel.dragGlobalY);
        }
    }

    PropertyAnimation { id: wheelAnim; target: listView; property: "contentY"; duration: 100; easing.type: Easing.OutQuad }

    MouseArea { id: hoverArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton; propagateComposedEvents: true }

    DropArea {
        anchors.fill: parent; z: 50
        onEntered: (drag) => { if (drag.hasUrls) drag.acceptProposedAction(); }
        onDropped: (drop) => {
            if (!drop.hasUrls) return;
            let paths = [];
            for (let i = 0; i < drop.urls.length; i++) {
                let p = drop.urls[i].toString();
                if (p.startsWith("file://")) p = decodeURIComponent(p.substring(7));
                    paths.push(p);
            }
            if (paths.length > 0) Playlists.addFilesToPlaylist(Playlists.activeId, paths);
            drop.acceptProposedAction();
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; anchors.bottomMargin: 0; spacing: 6

        PlaylistTabs {
            id: tabs; Layout.fillWidth: true; Layout.preferredHeight: 36
            onCreateRequested: dialogs.nameDialog.openForCreate()
            onRenameRequested: (id, currentName) => dialogs.nameDialog.openForRename(id, currentName)
            onDeleteRequested: (id, name) => dialogs.deleteDialog.openFor(id, name)
            onAddFilesRequested: (id) => Playlists.openAddFilesDialog(id)
            onAddFolderRequested: (id) => Playlists.openAddFolderDialog(id)
            onPropertiesRequested: (id) => propertiesDialog.openFor(id)
            onAddUrlRequested: (id) => urlDialog.openWith(qsTranslate("PlaylistManager", "dialog_add_url"), id)
            onClearRequested: (id) => dialogs.clearDialog.openFor(id, Playlists.playlistName(id))
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 1
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        }

        Item {
            Layout.fillWidth: true; Layout.fillHeight: true

            ListView {
                id: listView
                anchors.fill: parent
                clip: true; model: panel.viewModel
                boundsBehavior: Flickable.StopAtBounds
                focus: true; interactive: !panel.isDragging; cacheBuffer: 400
                // The panel scrolls to the current item explicitly (see
                // scrollToCurrent). Letting the view follow currentIndex makes
                // it jump to the playing row while a drag reorders the list.
                highlightFollowsCurrentItem: false
                currentIndex: panel.isFiltering ? -1
                                                : (panel.model ? panel.model.currentIndex : -1)

                // Scrolling while a drag is active (wheel, trackpad, or any
                // programmatic scroll) changes the row under the pointer
                // without a mouse move, so the dragged entry is re-targeted.
                onContentYChanged: {
                    if (panel.isDragging && panel.dragGlobalY >= 0)
                        panel.updateDrag(panel.dragGlobalY);
                }

                ScrollBar.vertical: ScrollBar { id: vbar; policy: ScrollBar.AsNeeded }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        // Horizontal two-finger swipe / tilt switches playlists;
                        // vertical wheel keeps scrolling the tracks.
                        if (Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y)) {
                            panel.handleHorizontalSwipe(event.angleDelta.x);
                            event.accepted = true;
                            return;
                        }

                        var maxY = Math.max(0, listView.contentHeight - listView.height);
                        var baseY = wheelAnim.running ? wheelAnim.to : listView.contentY;
                        wheelAnim.stop(); wheelAnim.from = listView.contentY;
                        wheelAnim.to = Math.max(0, Math.min(maxY, baseY - event.angleDelta.y));
                        wheelAnim.start();
                        event.accepted = true;
                    }
                }

                // PASS REFERENCES TO THE DELEGATE
                delegate: PlaylistDelegate {
                    playlistView: listView
                    playlistPanel: panel
                    scrollBar: vbar
                    isBeingDragged: panel.draggedIndex === index
                    isCurrent: model.isCurrent
                }
            }

            Label {
                anchors.centerIn: parent
                visible: panel.isFiltering && listView.count === 0
                text: qsTranslate("PlaylistPanel", "playlist_search_empty")
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.6)
                font.pixelSize: 13
            }
        }

        // Search bar, styled like the playlist panel itself. It reaches the
        // bottom edge of the panel so the field can be centered between the
        // separator and that edge.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: "transparent"

            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 1
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                               Kirigami.Theme.textColor.b, 0.15)
            }

            Item {
                id: searchBox
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: 6
                anchors.bottomMargin: 6

                TextField {
                    id: searchField
                    anchors.fill: parent
                    placeholderText: qsTranslate("PlaylistPanel", "playlist_search")
                    placeholderTextColor: Qt.rgba(Kirigami.Theme.textColor.r,
                                                  Kirigami.Theme.textColor.g,
                                                  Kirigami.Theme.textColor.b, 0.5)
                    color: Kirigami.Theme.textColor
                    font.pixelSize: 14
                    selectByMouse: true
                    // Reserve room for the magnifier on the left and the clear
                    // button on the right; the field itself spans the full
                    // playlist width.
                    leftPadding: 34
                    rightPadding: searchField.text.length > 0 ? 40 : 12
                    topPadding: 0
                    bottomPadding: 0
                    background: Rectangle {
                        radius: 6
                        color: searchField.activeFocus
                               ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                         Kirigami.Theme.textColor.b, 0.1)
                               : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                         Kirigami.Theme.textColor.b, 0.06)
                    }
                    onAccepted: if (panel.parent) panel.parent.forceActiveFocus()
                    Keys.onEscapePressed: {
                        text = "";
                        if (panel.parent) panel.parent.forceActiveFocus();
                    }
                }

                Kirigami.Icon {
                    anchors.left: parent.left
                    anchors.leftMargin: 9
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18; height: 18
                    source: "edit-find-symbolic"
                    color: Kirigami.Theme.textColor
                    isMask: true
                }

                Item {
                    id: clearButton
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 28
                    visible: searchField.text.length > 0

                    Rectangle {
                        anchors.fill: parent; radius: 6
                        color: clearHover.containsMouse
                               ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                         Kirigami.Theme.textColor.b, 0.1)
                               : "transparent"
                    }

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: 18; height: 18
                        source: "window-close-symbolic"
                        color: Kirigami.Theme.textColor
                        isMask: true
                    }

                    MouseArea {
                        id: clearHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            searchField.text = "";
                            searchField.forceActiveFocus();
                        }
                    }
                }
            }
        }
    }

    PlaylistFilterModel {
        id: playlistFilter
        playlistModel: panel.model
        filter: searchField.text
    }

    // Track menu
    PlaylistContextMenu {
        id: contextMenu
        playlistPanel: panel
        trackIndex: panel.contextMenuIndex
    }

    // Dialogs
    PlaylistDialogs {
        id: dialogs
    }

    PlaylistPropertiesDialog {
        id: propertiesDialog
    }

    AddUrlDialog {
        id: urlDialog
        onUrlAccepted: (url) => Playlists.addUrlToPlaylist(urlDialog.targetId, url)
    }

    MediaInfoDialog {
        id: infoDialog
        fileInfo: panel.fileInfoData
    }
}
