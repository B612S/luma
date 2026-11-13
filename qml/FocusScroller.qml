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

    // Flickable has no ensureVisible() of its own (only the item views do), so
    // move it just enough to bring the focused child into the viewport.
    function ensureVisible(item) {
        const f = flickable;
        if (!f || !f.contentItem || !item)
            return;

        const contentHeight = f.contentHeight > 0 ? f.contentHeight : f.contentItem.height;
        if (contentHeight <= f.height)
            return;

        const pos = item.mapToItem(f.contentItem, 0, 0);
        const topMargin = f.topMargin || 0;
        const bottomMargin = f.bottomMargin || 0;
        const margin = Math.min(8, f.height / 4);
        const visibleTop = f.contentY + topMargin;
        const visibleBottom = f.contentY + f.height - bottomMargin;

        let target = f.contentY;
        if (pos.y < visibleTop + margin)
            target = pos.y - topMargin - margin;
        else if (pos.y + item.height > visibleBottom - margin)
            target = pos.y + item.height + bottomMargin + margin - f.height;

        const minY = f.originY;
        const maxY = Math.max(minY, f.originY + contentHeight - f.height);
        target = Math.max(minY, Math.min(maxY, target));
        if (target !== f.contentY)
            f.contentY = target;
    }

    Connections {
        target: root.flickable ? root.flickable.Window.window : null
        function onActiveFocusItemChanged() {
            const win = root.flickable ? root.flickable.Window.window : null;
            const item = win ? win.activeFocusItem : null;
            if (!item || !root.flickable || !root.flickable.contentItem)
                return;
            if (root.isInside(item, root.flickable.contentItem))
                root.ensureVisible(item);
        }
    }
}
