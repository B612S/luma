// A small example plugin: add an item to the main context menu that opens a
// host-rendered window with the current media information and a Play/Pause
// action. The plugin never draws QML itself.
//
// Playback events keep the window body and action label up to date; without
// them the snapshot would stay frozen at the moment the window was opened.

function formatTime(seconds) {
    const total = Math.max(0, Math.floor(Number(seconds) || 0));
    const minutes = Math.floor(total / 60);
    const rest = total % 60;
    return minutes + ":" + (rest < 10 ? "0" : "") + rest;
}

function currentTitle(state) {
    return state.title || state.path || "Nothing playing";
}

function contentFor(state) {
    return [
        { type: "heading", text: currentTitle(state) },
        { type: "label", text: state.path || "" },
        { type: "separator" },
        {
            type: "label",
            text: "Position: " + formatTime(state.position)
                  + " / " + formatTime(state.duration)
        },
        {
            type: "label",
            text: state.paused ? "Paused" : "Playing"
        }
    ];
}

function actionsFor(state) {
    return [
        { id: "toggle", label: state.paused ? "Play" : "Pause" },
        { id: "close", label: "Close" }
    ];
}

function activate(luma) {
    luma.log.info("Now Playing example plugin loaded");

    let win = null;

    function refresh() {
        if (!win || win.isClosed)
            return;
        const state = luma.media.state();
        win.setContent(contentFor(state));
        win.setActions(actionsFor(state));
    }

    luma.events.on("positionChanged", refresh);
    luma.events.on("pausedChanged", refresh);
    luma.events.on("mediaChanged", refresh);

    luma.ui.menuAdd({
        id: "show",
        location: "main",
        label: "Now Playing (plugin)"
    }, () => {
        const state = luma.media.state();
        win = luma.ui.window({
            title: "Now Playing",
            content: contentFor(state),
            actions: actionsFor(state)
        }, (event) => {
            if (event.action === "toggle") {
                luma.media.togglePause();
                refresh();
            } else if (event.action === "close") {
                const current = win;
                win = null;
                if (current)
                    current.close();
            } else if (event.action === "__closed") {
                win = null;
            }
        });
    });
}
