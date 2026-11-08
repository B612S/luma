# Now Playing example plugin

A minimal JavaScript plugin that:

- adds **Now Playing (plugin)** to the main context menu;
- opens a host-rendered native window with the current title, path, position
  and playback state;
- provides Play/Pause and Close buttons.

All UI is drawn by Luma; the plugin only supplies a declarative window
description. See [docs/PLUGINS.md](../../docs/PLUGINS.md) for the API.

## Install

Copy the directory into the user plugin directory and restart Luma:

```bash
mkdir -p ~/.local/share/luma/plugins
cp -r examples/now-playing ~/.local/share/luma/plugins/now-playing
```

Then right-click the player and choose **Now Playing (plugin)**.

New plugins start disabled. Enable it in **Settings → Plugins**; use the delete
button there to uninstall it.
