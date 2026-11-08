# Bundled plugins

These plugins are optional and are installed into
`/usr/share/luma/plugins/<id>/`. They are listed in **Settings → Plugins** but
are disabled by default; enable one there. Bundled plugins cannot be deleted
from Settings.

Plugin data remains per-user in `~/.local/share/luma/plugins/data/<id>/`.
Translations live in each plugin's own `locales/` directory, not in Luma's
root `locales/`.

Current bundled plugins:

- `smooth-motion` — Smooth Motion: generate frames for the display refresh rate
  or a fixed target frame rate. The host assigns the first free statistics key
  to its generated/source/display FPS page; numeric keys are reserved and only
  switch pages while Stats is already open.
- `hdr-output` — HDR Output: request HDR signalling from mpv when the display,
  compositor and driver support it.
- `resource-monitor` — draws CPU, RAM and GPU graphs on a plugin-owned stats
  page. It prefers the third page/key `3`, reads its metrics through the
  `process` permission, and draws the charts as SVG inside the native Stats
  window. It follows the DRM/render node actually opened by Luma, so a
  multi-GPU system reports the GPU used for rendering rather than the busiest
  GPU. DRM `gpu_busy_percent` is used on AMD/Intel and `nvidia-smi` on NVIDIA;
  unsupported drivers show `n/a`.
