import QtQuick

// Plugin actions for one menu location. The host renders them with the same
// LumaMenuButton used by built-in actions, so they keep the native style.
Column {
    id: root

    property string location: ""
    property var context: ({})
    property var closeMenu: null

    width: parent ? parent.width : 0
    spacing: 2
    visible: actions.length > 0

    readonly property var actions: {
        if (typeof Plugins === "undefined" || !Plugins)
            return [];
        return Plugins.menuActions.filter(action => action.location === root.location);
    }

    Repeater {
        model: root.actions
        delegate: LumaMenuButton {
            text: modelData.label || ""
            onClicked: {
                if (root.closeMenu)
                    root.closeMenu();
                if (typeof Plugins !== "undefined" && Plugins)
                    Plugins.runMenuAction(modelData.id, root.context);
            }
        }
    }
}
