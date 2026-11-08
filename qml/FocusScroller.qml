import QtQuick

// Scrolls a Flickable when keyboard focus moves to one of its children.
// Qt does not do this automatically for custom Flickable-based pages.
Item {
    id: root
    property Flickable flickable: null
    visible: false

    function isInside(item, ancestor) {
        while (item) {
            if (item === ancestor)
                return true;
            item = item.parent;
        }
        return false;
    }

    Connections {
        target: root.flickable ? root.flickable.Window.window : null
        function onActiveFocusItemChanged() {
            const win = root.flickable ? root.flickable.Window.window : null;
            const item = win ? win.activeFocusItem : null;
            if (!item || !root.flickable || !root.flickable.contentItem)
                return;
            if (root.isInside(item, root.flickable.contentItem))
                root.flickable.ensureVisible(item);
        }
    }
}
