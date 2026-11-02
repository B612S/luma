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
    // coordinates and Qt can keep the popup inside the window with margins.
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
