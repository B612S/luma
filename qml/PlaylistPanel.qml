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

    property bool menuVisible: contextMenu.visible
    property bool hasOpenDialog: dialogs.nameDialog.visible || dialogs.deleteDialog.visible || dialogs.clearDialog.visible || infoDialog.visible || urlDialog.dialog.visible

    visible: opacity > 0.0; enabled: isOpen; opacity: isOpen ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.InOutQuad } }

    onOpacityChanged: { if (opacity > 0.0 && isOpen) scrollToCurrent(); }
    onIsOpenChanged: {
        if (!isOpen) {
            contextMenu.close(); tabs.closeMenus(); infoDialog.close();
        }
    }

    Connections {
        target: Playlists
        function onActiveIdChanged() {
            panel.endDrag(); contextMenu.close(); tabs.closeMenus(); infoDialog.close();
        }
    }

    Connections {
        target: model
        function onCurrentIndexChanged() { if (model) listView.currentIndex = model.currentIndex; }
    }

    function closeContextMenu() { contextMenu.close(); tabs.closeMenus(); }
    function scrollToCurrent() { if (panel.model) listView.currentIndex = panel.model.currentIndex; scrollTimer.start(); }

    // The track menu lives in this file: from outside (the delegate) we call it by
    // function name, not via `panel.contextMenu` — an id is not an object property.
    function openTrackMenu(index, scenePos) {
        contextMenuIndex = index;
        let win = panel.window;
        let maxX = win ? win.width - contextMenu.width - 10 : scenePos.x;
        let localPos = panel.mapFromItem(null,
            Math.max(10, Math.min(scenePos.x, maxX)),
            Math.max(10, scenePos.y));
        contextMenu.x = localPos.x;
        contextMenu.y = localPos.y;
        contextMenu.open();
        if (contextMenu.y + contextMenu.height > panel.height - 10)
            contextMenu.y = Math.max(10, panel.height - contextMenu.height - 10);
    }

    function openInfoDialog(path) {
        fileInfoData = Media.getFileInfo(path);
        infoDialog.open();
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
        anchors.fill: parent; anchors.margins: 8; spacing: 6

        PlaylistTabs {
            id: tabs; Layout.fillWidth: true; Layout.preferredHeight: 36
            rootContextMenu: panel.rootContextMenu
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

        ListView {
            id: listView
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; model: panel.model
            boundsBehavior: Flickable.StopAtBounds
            focus: true; interactive: !panel.isDragging; cacheBuffer: 400
            currentIndex: panel.model ? panel.model.currentIndex : -1

            ScrollBar.vertical: ScrollBar { id: vbar; policy: ScrollBar.AsNeeded }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    var maxY = Math.max(0, listView.contentHeight - listView.height);
                    var baseY = wheelAnim.running ? wheelAnim.to : listView.contentY;
                    wheelAnim.stop(); wheelAnim.from = listView.contentY;
                    wheelAnim.to = Math.max(0, Math.min(maxY, baseY - event.angleDelta.y));
                    wheelAnim.start();
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
