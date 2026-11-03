import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

// Base popup for the in-app context menus: the same look and the same
// open/close animation as the player's main menu (ContextMenu.qml).
// Declare a single Column with LumaMenuButton and LumaMenuSeparator rows inside
// it; the popup sizes itself to that column, like the main menu does.
Popup {
    id: menu

    // Every in-app menu is shown in the window overlay, so x/y are window
    // coordinates. openAt() anchors the menu to an item and keeps it inside the
    // window; the anchor is followed on resize.
    parent: Overlay.overlay
    margins: 10

    padding: 8
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    z: 1000

    // Page shown by the menu; the multi-page menus switch pages instead of
    // opening nested popups.
    property string currentPage: "main"

    // Window that tracks the open menu, remembered while the menu is open: the
    // content item may already be out of the scene when the menu is closing.
    property var menuHostWindow: null

    // Original anchor: the menu follows this item (and the local point inside
    // it) when the window is resized or the layout moves the item, instead of
    // keeping stale window coordinates.
    property Item anchorItem: null
    property point anchorPoint: Qt.point(0, 0)
    property bool anchored: false

    function openAt(item, localX, localY) {
        anchorItem = item;
        anchorPoint = Qt.point(localX, localY);
        anchored = item !== null;
        positionAtAnchor();
        open();
    }

    function menuContentHeight() {
        if (contentItem && contentItem.implicitHeight > 0)
            return contentItem.implicitHeight + topPadding + bottomPadding;
        return height > 0 ? height : 0;
    }

    // Place the menu at the anchor, keeping it inside the window. If it does
    // not fit below the anchor, open it above the point (like the main menu).
    function positionAtAnchor() {
        if (!anchored || !anchorItem) return;
        const scene = anchorItem.mapToItem(null, anchorPoint.x, anchorPoint.y);
        const win = menuWindow();
        if (!win) {
            x = scene.x;
            y = scene.y;
            return;
        }

        const margin = 10;
        const w = width > 0 ? width : implicitWidth;
        const h = menuContentHeight();
        let ny = scene.y;
        if (h > 0 && ny + h > win.height - margin) {
            const above = scene.y - h;
            if (above >= margin) ny = above;
        }
        x = Math.max(margin, Math.min(scene.x, Math.max(margin, win.width - w - margin)));
        y = Math.max(margin, Math.min(ny, Math.max(margin, win.height - h - margin)));
    }

    // Follow window resizes, content size changes (pages) and the anchor moving
    // (scrolling a tab row or a playlist).
    Connections {
        target: menu.menuHostWindow
        function onWidthChanged() { menu.positionAtAnchor(); }
        function onHeightChanged() { menu.positionAtAnchor(); }
    }

    Connections {
        target: menu.anchorItem
        function onXChanged() { menu.positionAtAnchor(); }
        function onYChanged() { menu.positionAtAnchor(); }
    }

    Connections {
        target: menu.contentItem
        function onImplicitWidthChanged() { menu.positionAtAnchor(); }
        function onImplicitHeightChanged() { menu.positionAtAnchor(); }
    }

    // Only one menu may be open at a time: the application window tracks the
    // open menu and the previous one is closed before the new one is shown.
    function menuWindow() {
        if (menuHostWindow) return menuHostWindow;
        // The popup itself is not an Item: take the window from its parent (the
        // overlay) or, once shown, from the content item.
        if (parent) {
            const parentWindow = parent.Window.window;
            if (parentWindow) return parentWindow;
        }
        return contentItem ? contentItem.Window.window : null;
    }

    function registerMenu() {
        const win = menuWindow();
        if (!win) return;
        menuHostWindow = win;
        if (win.currentOpenMenu && win.currentOpenMenu !== menu)
            win.currentOpenMenu.close();
        win.currentOpenMenu = menu;
        positionAtAnchor();
    }

    function unregisterMenu() {
        const win = menuHostWindow || menuWindow();
        if (win && win.currentOpenMenu === menu)
            win.currentOpenMenu = null;
    }

    // Switches to another page with the same fade as the player's main menu.
    function showPage(page) {
        if (currentPage === page) return;
        currentPage = page;
        pageFadeAnim.restart();
    }

    onAboutToShow: registerMenu()
    // The window may only be reachable from the content item once shown.
    onOpened: registerMenu()
    onClosed: unregisterMenu()

    NumberAnimation {
        id: pageFadeAnim
        target: menu
        property: "opacity"
        from: 0.0
        to: 1.0
        duration: 150
        easing.type: Easing.InOutQuad
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 150; easing.type: Easing.InOutQuad }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 150; easing.type: Easing.InOutQuad }
    }

    background: Rectangle {
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r,
                       Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.95)
        radius: 8
        border.color: "transparent"
    }
}
