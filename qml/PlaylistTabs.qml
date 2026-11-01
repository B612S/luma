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

    function closeMenus() { tabContextMenu.close(); }

    signal createRequested()
    signal renameRequested(string id, string currentName)
    signal deleteRequested(string id, string name)
    signal addFilesRequested(string id)
    signal addFolderRequested(string id)
    signal addUrlRequested(string id)
    signal clearRequested(string id)

    RowLayout {
        anchors.fill: parent; spacing: 4
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
                        property bool isActive: info.id === root.activeId
                        property bool isDefault: info.isDefault === true

                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2; radius: 6
                            color: tabHover.containsMouse && !tabBtn.isActive ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08) : "transparent"
                            Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.InOutQuad } }
                        }

                        Text {
                            id: tabLabel; anchors.centerIn: parent; anchors.verticalCenterOffset: -2
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
                            onClicked: (mouse) => {
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
