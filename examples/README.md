# Plugin examples

Each directory is a complete Luma plugin. Copy one into the user plugin
directory and restart Luma:

```bash
mkdir -p ~/.local/share/luma/plugins
cp -r examples/now-playing ~/.local/share/luma/plugins/now-playing
```

New plugins start disabled. Manage them in **Settings → Plugins**
(enable/disable, inspect, add or delete). The API and plugin format are
documented in [docs/PLUGINS.md](../docs/PLUGINS.md).

- [now-playing](now-playing/) — native window with current media information
  and Play/Pause.
