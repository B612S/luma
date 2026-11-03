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
    property int readyIndex: -1
    property real dragGlobalY: -1
    property real dragZoneHeight: 40

    property int contextMenuIndex: -1
    property real swipeAccumX: 0
    property bool swipeTriggered: false
    property var fileInfoData: ({})
    // Path of the media-information request currently being displayed.
    property string pendingInfoPath: ""

    property bool menuVisible: contextMenu.visible
    property bool hasOpenDialog: dialogs.nameDialog.visible || dialogs.deleteDialog.visible || dialogs.clearDialog.visible || infoDialog.visible || urlDialog.dialog.visible

    visible: opacity > 0.0; enabled: isOpen; opacity: isOpen ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

    onOpacityChanged: { if (opacity > 0.0 && isOpen) scrollToCurrent(); }
    onIsOpenChanged: {
        if (!isOpen) {
            contextMenu.close(); tabs.closeMenus(); infoDialog.close();
            // Never keep keyboard focus inside the closed search field.
            searchField.focus = false;
            if (panel.parent) panel.parent.forceActiveFocus();
        }
    }

    onIsFilteringChanged: {
        if (isFiltering) {
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
            if (model && !panel.isFiltering) listView.currentIndex = model.currentIndex;
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
    function startAutoScroll() { autoScrollTimer.start(); }
    function startClickResetTimer() { clickResetTimer.start(); }
    function endDrag() {
        panel.isDragging = false; panel.readyIndex = -1; panel.draggedIndex = -1;
        panel.dragGlobalY = -1; autoScrollTimer.stop();
        // Qt.binding must capture its own copy of the delegate:
        // with a shared var item, the closure ends up with null after the loop.
        for (var i = 0; i < listView.count; i++) {
            const delegate = listView.itemAtIndex(i);
            if (!delegate || delegate.itemIndex === undefined) continue;
            delegate.y = Qt.binding(() => delegate.itemIndex * 38);
        }
        // Rebuild the layout, otherwise "holes" remain after moving an item up.
        listView.forceLayout();
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
            if (!panel.isDragging || panel.dragGlobalY < 0) return;
            var localY = listView.mapFromItem(null, 0, panel.dragGlobalY).y;
            if (localY < panel.dragZoneHeight) {
                listView.contentY = Math.max(0, listView.contentY - Math.min(25, (panel.dragZoneHeight - localY) * 0.8 + 5));
            } else if (localY > listView.height - panel.dragZoneHeight) {
                var maxY = Math.max(0, listView.contentHeight - listView.height);
                listView.contentY = Math.min(maxY, listView.contentY + Math.min(25, (localY - (listView.height - panel.dragZoneHeight)) * 0.8 + 5));
            }
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
                currentIndex: panel.isFiltering ? -1
                                                : (panel.model ? panel.model.currentIndex : -1)

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
                    isReady: panel.readyIndex === index
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
                    font.pixelSize: 15
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

    AddUrlDialog {
        id: urlDialog
        onUrlAccepted: (url) => Playlists.addUrlToPlaylist(urlDialog.targetId, url)
    }

    MediaInfoDialog {
        id: infoDialog
        fileInfo: panel.fileInfoData
    }
}
