# Plugin system v1

Plugins add small integrations to Luma in JavaScript. They are loaded
automatically from the user's data directory:

```
$XDG_DATA_HOME/luma/plugins/<id>/
# normally ~/.local/share/luma/plugins/<id>/
```

Manage installed plugins in **Settings → Plugins**: enable/disable, read the
description/version/permissions, add a plugin folder, or delete it. A disabled
plugin is unloaded immediately. Newly installed plugins start disabled;
enable one explicitly. Permissions declared in the manifest are enforced by the
host.

Bundled optional plugins are installed read-only in
`/usr/share/luma/plugins/<id>/`; they can be enabled and configured but not
deleted. User-installed plugins live in `~/.local/share/luma/plugins/<id>/`
and take precedence over a bundled plugin with the same id. Plugin data is
always per-user in `~/.local/share/luma/plugins/data/<id>/`.

## Language

v1 uses JavaScript (ECMAScript 2020) through Qt's `QJSEngine`.

Reasons:

- No new runtime or packaging dependency.
- The host exposes a narrow API and keeps ownership of Qt, MPV, files and the
  network; plugins never receive QML or raw C++ objects.
- No per-release native ABI.
- Writing a menu action, OSD message or playlist helper is a few lines of JS.

Lua/Python are not embedded in-process. Plugins are JavaScript. Direct
mpv, ffmpeg/yt-dlp and other external-task access are explicit, warning-gated
permissions; Native C++ plugins are deliberately not part of v1.

## Package

```
com.example.hello/
  plugin.json
  main.js
  assets/            # optional, read through luma.storage
```

`plugin.json`:

```json
{
  "id": "com.example.hello",
  "name": "Hello",
  "version": "1.0.0",
  "apiVersion": 1,
  "main": "main.js",
  "description": "Shows the current title in the OSD.",
  "author": "Example",
  "license": "MIT",
  "permissions": ["osd", "menus", "media:read"]
}
```

- `id`: reverse-DNS, stable, unique, no slashes or `..`.
- `apiVersion`: must be `1`.
- `main`: a file inside the plugin directory.
- Unknown fields are ignored. Invalid manifests are logged and skipped.
- Each plugin runs in its own `QJSEngine`; errors are logged and do not crash
  the player.

## Lifecycle

`main.js` is evaluated once. The host then calls the global `activate(luma)`
function if it exists. The optional global `deactivate()` is called at shutdown.

```js
function activate(luma) {
    luma.log.info("hello activated");
    luma.events.on("fileOpened", (event) => {
        luma.ui.osd("Now playing: " + (event.state.title || event.state.path));
    });
}

function deactivate() {
    // optional cleanup
}
```

## API

The host creates one `luma` object. Payloads are plain JSON-compatible values.

```js
luma.apiVersion      // 1
luma.id              // plugin id
luma.version         // plugin version
luma.dataDir         // ~/.local/share/luma/plugins/data/<id>/
```

### Events

```js
const off = luma.events.on("fileOpened", (event) => { ... });
luma.events.once("startup", (event) => { ... });
off();
```

Implemented events:

| Event | Payload |
| --- | --- |
| `startup` | `{ version }` |
| `shutdown` | `{}` |
| `fileOpened` | `{ path, state }` — state is `luma.media.state()` |
| `mediaChanged` | the current `luma.media.state()` |
| `liveStreamChanged` | `{ live }` |
| `playModeChanged` | `{ mode }` |
| `playlistChanged` | `{ active, playing }` |
| `playlistActivated` | `{ id, name, isDefault }` |
| `playingChanged` | `{ id }` |
| `playlistModeChanged` | `{ id, mode }` |
| `positionChanged` | playback state plus `{ position, duration }` (about once per second) |
| `pausedChanged` | `{ paused }` |
| `volumeChanged` | `{ volume }` |

Callbacks run from host signals; keep them short and use the API for work.

### Media

Permissions: `media:read` for `state()`, `media:control` for the rest.

```js
const state = luma.media.state();
// { hasMedia, loading, live, path, title, position, duration,
//   paused, volume, muted, hasVideo }

luma.media.play();
luma.media.pause();
luma.media.togglePause();
luma.media.seek(seconds);
luma.media.setVolume(0..100);
luma.media.next();
luma.media.previous();
luma.media.stop();
luma.media.open(pathOrUrl); // true when opening started
```

### Playlists

Permissions: `playlists:read`, `playlists:write`.

```js
luma.playlists.list();               // [{ id, name, count, isDefault, isPlaying }]
luma.playlists.active();             // active playlist id
luma.playlists.entries(id);          // playlist entries
luma.playlists.add(id, [path]);      // true when accepted
luma.playlists.remove(id, [index]);  // number removed
luma.playlists.create(name);         // new playlist id
luma.playlists.setMode(id, "repeat");
```

Modes: `sequential`, `repeat`, `shuffle`, `stop`.

### UI: OSD and native menus

Permissions: `osd`, `menus`, `windows`.

```js
luma.ui.osd("text");

luma.ui.menuAdd({
    id: "copy-title",
    location: "main",       // "main" | "playlist" | "tab" | "stats"
    label: "Copy title"
}, (context) => {
    luma.ui.osd("copied");
});
```

Menu contexts:

- `main`: `{ path }`.
- `playlist`: `{ playlistId, path, index }`.
- `tab`: `{ playlistId }`.
- `stats`: reserved; no UI yet.

Menu actions are rendered with the same `LumaMenuButton` as built-in actions,
including the Kirigami selection bar and font weight. There is no `when`
predicate; inspect the context inside the callback.

### UI: native windows

The plugin describes controls; the host renders them with the same native
controls and theme. Plugins cannot inject QML.

```js
const win = luma.ui.window({
    title: "Plugin window",
    content: [
        { type: "heading", text: "Plugin window" },
        { type: "label", text: "Native controls rendered by Luma." },
        { type: "separator" },
        { type: "text", id: "name", label: "Name", value: "Luma" },
        { type: "textarea", id: "notes", label: "Notes", value: "" },
        { type: "switch", id: "enabled", label: "Enabled", checked: true },
        { type: "select", id: "mode", label: "Mode", value: "a",
          options: [{ label: "A", value: "a" }, { label: "B", value: "b" }] },
        { type: "button", id: "do", label: "Do it" },
        { type: "spacer", height: 12 }
    ],
    actions: [{ id: "ok", label: "OK" }]
}, (event) => {
    // event = { action, ...values, pluginId }
    if (event.action === "ok")
        luma.log.info("name=" + event.name);
});

win.close();
win.setTitle("New title");
win.setContent([ ... ]);
win.submit("custom", { value: 1 });
```

A window can also be closed with Escape or by clicking outside it. In that
case the callback receives `{ action: "__closed", pluginId }`; drop the
reference (`win = null`) so live event handlers stop updating it. The returned
object also exposes `win.isClosed`, `win.setActions([...])`, `win.setContent`
and `win.setTitle`.

Without `actions`, the host draws no footer.

### Raw mpv

Permission: `mpv:raw` (dangerous, requires explicit approval).

```js
luma.mpv.getProperty("volume");
luma.mpv.setProperty("volume", 70);
luma.mpv.command(["seek", "10", "absolute"]);
```

`getProperty` can block briefly on the mpv thread. The normal media API
(`luma.media`) is safer and preferred; raw access is for plugins that need an
mpv feature Luma does not wrap. `command()` returns `false` when mpv rejects
the command (unknown filter, invalid argument, blocked command); `setProperty`
returns `false` for blocked properties. Note that a successful `vf add` only
means mpv accepted the filter: it can still disable a filter later when the
format negotiation fails, so read `vf`/`estimated-vf-fps` back if the plugin
depends on the result.

It can change player state, but commands that
execute processes or load scripts are blocked: `run`, `subprocess`,
`load-script`, `loadfile`, `loadlist`, `script-*`, `define-section`,
`enable-section`, `keybind`, and properties such as `script`, `script-opts`,
`input-conf`, `ytdl-raw-options`, `include`, `config` and
`stream-open-filename`. Open media through `luma.media.open()`; raw mpv
commands cannot start playback or a process.

### External programs

Permission: `process` (dangerous, requires explicit approval).

```js
luma.process.path("ffmpeg"); // absolute path or ""
luma.process.path("yt-dlp");

luma.process.run("ffmpeg", ["-version"], { timeoutMs: 10000 }, (response) => {
    // response = { ok, exitCode, stdout, stderr, error }
});

luma.process.run("yt-dlp", ["-J", url], { timeoutMs: 30000 }, (response) => {
    // response.stdout is yt-dlp's JSON
});
```

Any executable available on `PATH` can be started: ffmpeg, yt-dlp, a shell or a
user tool. The host starts the binary without a shell, using the plugin data
directory as cwd. `stdin` can be supplied in `options`; stdout/stderr are
capped; the default timeout is 60 seconds. This is direct access outside Luma:
it can read and write files, use the network and run other programs.

yt-dlp runs with `--ignore-config` and rejects options that execute arbitrary
commands, load external config or load yt-dlp plugins (`--exec`,
`--exec-before-download`, `--config-location`, `--batch-file`,
`--load-info-json`, `--update`, `--rm-cache-dir`, `--downloader`,
`--plugin-dirs`).

### Plugin information

A plugin can add a short `info` text (5-6 lines) to `plugin.json`. The Plugins
page shows an information icon that opens a separate dialog with this text;
the value can be translated through the plugin's own `locales/` files just
like its name and description.

```json
"info": "Samples CPU/RAM and draws SVG graphs.\nOpen Stats and press the assigned number."
```

If `info` is omitted, the dialog falls back to the description.

### Plugin settings

A plugin can declare native settings in `plugin.json`. Luma renders the
controls itself; plugins do not provide QML:

```json
"settings": [
  { "key": "enabled", "type": "switch", "label": "Enabled", "default": true },
  { "key": "mode", "type": "select", "label": "Mode",
    "options": [{ "label": "Auto", "value": "auto" },
                { "label": "60 fps", "value": "60" }],
    "default": "auto",
    "description": "Shown below the control." },
  { "key": "target", "type": "number", "label": "Target",
    "min": 1, "max": 240, "step": 1, "default": 60 }
]
```

Supported types are `switch`, `select`, `text`, `textarea` and `number`.
If a plugin has settings, **Settings → Plugins** shows a **Settings** button.
Values are stored in the plugin's own `config.ini`; changing one dispatches the
`settingsChanged` event to a loaded plugin.

### Translations

Plugins keep their own translations in a `locales/` directory, separate from
Luma's root `locales/`:

```
com.example.plugin/
  plugin.json
  main.js
  locales/
    en.json
    ru.json
```

Keys are the English strings used by the plugin:

```json
{
  "Smooth Motion": "Плавное движение",
  "Frame rate": "Частота кадров"
}
```

The host picks the language of the player (`Config.language`, or the system
locale for `auto`) and falls back to `en.json`, then to the key itself. Plugin
names, descriptions, settings labels/options/descriptions are translated by
the host. JavaScript can translate its own labels with:

```js
luma.i18n.t("Frame rate", "Frame rate");
```

Plugin translations must not be added to Luma's root `locales/*.ts`.

### Statistics

Permission: `stats`.

```js
const stats = luma.stats.snapshot();
stats["estimated-vf-fps"];

luma.stats.set("custom", "My value", 42);
luma.stats.remove("custom");
luma.stats.clear();
```

Plugin stats appear in the native Stats panel under a **Plugins** section.
`snapshot()` returns the current media/playback values, including
`container-fps`, `estimated-vf-fps`, `display-fps`, codecs, bitrates and cache
state.

A plugin can also add a full page to the same native Stats window:

```js
const result = luma.stats.setPage("smooth", "Smooth Motion stats", [
    { type: "heading", text: "Smooth Motion" },
    { type: "label", text: "Output FPS: 120" },
    { type: "separator" }
]);

if (!result.ok)
    luma.log.warn(result.error);
```

Page content supports `heading`, `label`, `separator` and `svg`. The `svg`
item contains base64 SVG markup and a `height`; the plugin draws the graphics
itself and the host only displays the image:

```js
luma.stats.setPage("resources", "Resource usage", [
    { type: "svg", svg: base64Svg, height: 330 }
], 3);
```

The optional fourth argument is a preferred slot. The host assigns the first
free statistics key automatically, preferring the requested slot when it is
available: `2`, `3`, ..., `9`, then `0` for the tenth slot. Key `1` is always
Luma's own page. There are at most ten slots; after that `setPage()` returns
`{ ok: false, error: ... }` and the plugin should continue without a statistics
page.

Keys `1`-`0` only switch pages while Stats is already open. They never open
the panel; use the normal Stats menu item or shortcut for that. Plugins cannot
reassign these numeric keys. The plugin does not create another QML window.

### Keyboard shortcuts

Permission: `shortcuts`.

Reassignable actions are declared in `plugin.json` and appear in
**Settings → Shortcuts** next to Luma's own actions, where each one gets a local
and a global key (both can be reassigned or cleared; the binding is stored in
`shortcuts.conf`):

```json
"shortcuts": [
  { "id": "toggle", "label": "Smooth Motion on / off", "default": "F9" }
]
```

`default` is only used until the user assigns a key. Register the handler in
`activate()`; it receives `{ id }`:

```js
luma.shortcuts.on("toggle", () => {
    // Do something when the action's key is pressed.
});
```

Declared actions follow the same rules as Luma's shortcuts: Luma's own
bindings win, and they are ignored while a modal dialog is open or a text field
has focus. They are only active while the plugin is enabled.

A plugin can also bind an arbitrary key that is not offered in Settings:

```js
luma.shortcuts.add("F2", () => {
    luma.stats.showPage("smooth");
});

let win = null;
luma.shortcuts.add("F3", () => {
    win = luma.ui.window({
        title: "Plugin window",
        content: [{ type: "label", text: "Opened with F3" }],
        actions: [{ id: "close", label: "Close" }]
    }, (event) => {
        if (event.action === "close")
            win.close();
    });
});
```

Use portable key names such as `F2`, `F3` or `F5`. The bare number keys
`1`-`0` are reserved for statistics pages and rejected by the shortcut API and
by the shortcut settings. They only switch pages while Stats is already open;
they do not open the panel. Luma's own shortcuts always take priority; plugin
shortcuts only receive unhandled keys and are disabled while a modal dialog is
open or while a text field has focus.

### Configuration and storage

Permission: `filesystem:plugin` for storage.

```js
luma.config.get("prefix", "Now playing");
luma.config.set("prefix", "Playing");
```

Plugin settings are stored per plugin, outside `luma.conf`.

```js
luma.storage.readText("notes.txt");
luma.storage.writeText("notes.txt", "text");
luma.storage.exists("notes.txt");
```

Storage names are single file names inside `luma.dataDir`
(`~/.local/share/luma/plugins/data/<id>/`); slashes and `..` are rejected.

### Network

Permission: `network`.

```js
luma.net.fetch(url, {
    method: "GET",
    headers: { "Accept": "application/json" },
    timeoutMs: 15000
}, (response) => {
    // { ok, status, headers, body, error }
});
```

The host uses `QNetworkAccessManager`, allows only HTTP(S), caps the body at
2 MiB and enforces a timeout. Raw sockets and arbitrary certificates are not
exposed.

### Logging

```js
luma.log.info("message");
luma.log.warn("message");
luma.log.error("message");
```

Logs are prefixed with the plugin id.

## Security

The host treats plugins as untrusted input:

- Plugin JavaScript runs in a private `QJSEngine`; it gets the `luma` API, not
  QML, Qt objects or raw host process APIs. External programs are reachable
  only through the warning-gated `luma.process` wrapper.
- Plugins cannot inject QML: plugin windows are declarative descriptions
  rendered by Luma, and plugin-provided text is rendered as plain text.
- **New plugins start disabled.** Installing a plugin does not run it; the user
  enables it explicitly.
- There is no unapproved process API. Direct mpv access (`mpv:raw`) and external
  programs (`process`) are separate permissions with separate consent. Enabling such a plugin first asks about direct mpv access,
  then about external programs; each warning explains the requested access.
  The plugin stays disabled unless the user agrees to every requested category.
  Approvals are tied to the requested permissions and are asked again if a
  plugin adds new ones.
- Raw mpv blocks `run`, `subprocess`, `loadfile`, `loadlist` and
  script/input command paths. `luma.media.open()` remains the way to open
  media through Luma.
- `luma.storage` is confined to the plugin data directory; paths with `/`,
  `\` or `..` are rejected.
- There is no native C++ plugin loading.
- Permissions are declared in `plugin.json` and enforced by the host. Review a
  plugin's manifest before enabling it. `mpv:raw` and `process` are deliberately
  powerful.

Limitations: plugins run in-process. A malicious plugin can still consume
CPU/memory or misuse the permissions it declares. A plugin with direct
mpv/process access can change the player, read/write files and use the network
outside Luma; enable it only if you trust it. Use the Settings switch to disable or delete a
plugin.

## Permissions

| Permission | Grants |
| --- | --- |
| `media:read` | `luma.media.state()` |
| `media:control` | play/pause/seek/volume/next/previous/stop/open |
| `playlists:read` | list playlists and entries |
| `playlists:write` | add/remove entries, create playlists, change mode |
| `osd` | OSD messages |
| `menus` | native menu actions |
| `windows` | host-rendered plugin windows |
| `network` | `luma.net.fetch` |
| `filesystem:plugin` | `luma.storage` |
| `mpv:raw` | raw mpv properties and commands (requires approval warning) |
| `stats` | publish extra rows in the Stats panel and read a stats snapshot |
| `shortcuts` | register non-numeric shortcuts for otherwise unhandled keys |
| `process` | run external programs via `luma.process` (requires approval warning) |

Denied calls return an empty value or `false` and are logged. Enabling a
plugin with `process` shows a warning and requires explicit approval; other
permissions are granted by enabling the plugin. `mpv:raw` and `process` are
deliberately powerful; review a plugin's manifest before enabling it.

## Host architecture

```
core/plugins/PluginManifest.*  plugin.json parsing and validation
core/plugins/PluginManager.*   discovery in ~/.local/share/luma/plugins and
                               /usr/share/luma/plugins, runtimes, event fan-out,
                               windows and plugin settings
core/plugins/PluginRuntime.*   one QJSEngine, lifecycle, menu callbacks
core/plugins/PluginApi.*       API namespaces and permission checks
core/plugins/PluginWindow.*    host-rendered window object
qml/PluginMenu.qml             plugin actions in the native menus
qml/PluginWindowDialog.qml     window host
qml/PluginWindowField.qml      native control renderer
```

Plugins are scanned at startup; **Settings → Plugins** shows the installed
plugins and can reload, add or delete them. The switch is persisted in
`luma.conf` and synchronized across instances.

Remaining ideas (not implemented): dynamic menu predicates, track events,
notifications and installation from archives.

## Example

A complete, installable example lives in
[`examples/now-playing`](../examples/now-playing/); the minimal version is
below.

`plugin.json`:

```json
{
  "id": "com.example.now-playing",
  "name": "Now Playing OSD",
  "version": "1.0.0",
  "apiVersion": 1,
  "main": "main.js",
  "permissions": ["osd", "menus", "media:read"]
}
```

`main.js`:

```js
function activate(luma) {
    luma.events.on("fileOpened", (event) => {
        const state = event.state;
        luma.ui.osd("Now playing: " + (state.title || state.path));
    });

    luma.ui.menuAdd({
        id: "current-title",
        location: "main",
        label: "Show current title"
    }, () => {
        const state = luma.media.state();
        luma.ui.osd(state.title || state.path);
    });
}
```
