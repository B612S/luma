<p align="center">
  <img src="assets/luma.png" width="128" height="128" alt="Luma logo" />
</p>

<h1 align="center">Luma</h1>

<p align="center">
  A lightweight, fast media player for Linux built on <b>mpv</b> and <b>Qt 6 / QML</b> (Kirigami).
</p>

> **Platform: Wayland only.** Luma is built for Wayland sessions (KWin, GNOME, Sway,
> Hyprland, …). X11 is **not** supported, and support for it is **not planned**. The
> application refuses to start without a Wayland session.

<p align="center">
  <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-6.5%2B-green?style=flat-square&logo=qt" alt="Qt 6.5+" /></a>
  <a href="https://wayland.freedesktop.org/"><img src="https://img.shields.io/badge/platform-Wayland%20only-blue?style=flat-square" alt="Wayland only" /></a>
  <a href="https://www.gnu.org/licenses/gpl-3.0"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-orange?style=flat-square" alt="GPL-3.0-or-later" /></a>
</p>

<p align="center">
  <a href="docs/readme/README.ru.md">Русский</a> · <b>English</b> ·
  <a href="CHANGELOG.md">Changelog</a>
</p>

---

## Features

- **mpv inside** — plays virtually any video/audio format without extra codecs.
- **Multiple playlists** — tabs with create/rename/delete/clear, drag & drop reordering,
  add files or a whole folder, “Add to playlist” from the track menu.
- **Subtitle tuning** — font, size, weight, text/outline colors, outline width,
  background box, vertical position, ASS style override; applied to mpv live.
- **Links via yt-dlp** — open a URL from the menu and Luma resolves the direct stream
  with `yt-dlp` and plays it in mpv with normal seeking. The video title is shown in the
  notification and playlist. SOCKS proxies work through a local HTTP bridge, so mpv never
  bypasses the proxy.
- **Chromecast** — cast to a TV on the local network (discovery via `avahi-browse`,
  HLS re-encode via `ffmpeg`).
- **Picture in picture** — `P` or the context menu turns the window into a small
  frameless player (drag/resize it; play/pause, seek, time, volume, return) and back.
- **Shortcut cheat sheet** — hold `?` (`Shift+/`) to see
  all hotkeys while the key is held.
- **Keyboard-first** — playback, seeking, volume, tracks, aspect ratio, crop.
- **MPRIS2** — control from the desktop's media widget and media keys.
- **English and Russian UI** — switchable at runtime.

## Requirements

A **Wayland session** is required. X11 is not supported and support is not planned.

Build dependencies:

| Dependency | Notes |
| --- | --- |
| Qt 6.5+ | Core, Gui, Quick, QuickControls2, Widgets, DBus, Network, Concurrent, OpenGL, OpenGLWidgets, **WaylandClient**, LinguistTools |
| Wayland client libraries | `wayland-client` and `xkbcommon` development files (required; there is no X11 backend) |
| libmpv | `mpv` / `mpv-libs` |
| TagLib | reading and writing audio tags (`taglib`) |
| FFmpeg development libraries | `libavformat`, `libavcodec`, `libswscale`, `libavutil` |
| CMake ≥ 3.16, Ninja (or Make), a C++17 compiler | |

Runtime-only QML modules (CMake will **not** detect them, the app fails with a QML import
error instead):

- `Kirigami` (`kirigami` / `kirigami2`)
- `qqc2-desktop-style` (KDE desktop style for Qt Quick Controls)

Optional:

- `yt-dlp` — playback of links from video sites.
- `avahi` (`avahi-browse`) and the `ffmpeg` binary — Chromecast.

On Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-wayland \
    kirigami qqc2-desktop-style mpv ffmpeg taglib
```

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/bin/luma                       # the binary lives in build/bin/, not build/
./build/bin/luma /path/to/video.mkv    # open a file right away
```

Install system-wide:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

## Arch Linux package

A `PKGBUILD` is included:

```bash
makepkg -si
```

## AppImage

Build a portable AppImage with:

```bash
./build-appimage.sh
# -> dist/Luma-<version>-x86_64.AppImage
```

The script builds a Release AppDir and bundles Qt/QML (including Kirigami and
`qqc2-desktop-style`), libmpv, FFmpeg, TagLib and the Wayland platform plugins; the
resulting image is Wayland-only. The Breeze widget style and the small set of Breeze
icons used by the controls are included as well, so scrollbars and icons do not depend
on the host's KDE setup. On first use it downloads `linuxdeploy`, its Qt plugin and
`appimagetool` into `.appimage-tools/`. Optional tools such as `yt-dlp`, `ffmpeg` and
`avahi-browse` are not bundled; install them on the system when you need link playback
or casting.

Build on the oldest glibc-based distribution you intend to support — an AppImage cannot
bundle glibc. On systems without FUSE, run the image with
`--appimage-extract-and-run`.

## Tests

Tests are **off by default** — they are not built during a normal build or included in
packages. Enable them explicitly when you want to run them:

```bash
cmake -B build -S . -DLUMA_BUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The suite uses Qt Test and covers the core logic: the keyboard shortcut table, the
playlist model and manager, the configuration file, logging and the yt-dlp config. Tests run with
`XDG_CONFIG_HOME`/`XDG_CACHE_HOME` pointing at a temporary directory, so your own settings
are not touched. Qt6::Test is optional — if the module is missing, the tests are skipped
during configuration.

`tst_media_open` is an integration test that uses the real libmpv in headless mode
(`vo=null`, `ao=null`) to open the audio/video fixtures in `tests/data/` and a YouTube
URL routed through a fake `yt-dlp`. Run only it with
`ctest --test-dir build -R tst_media_open --output-on-failure`. A real network YouTube
test is skipped unless `LUMA_TEST_YOUTUBE_URL` is set; it uses the real `yt-dlp` and
network, so it is not part of the default run.

## Usage

- **Open a file/folder** — right-click the video → *Open*, or drag & drop a file onto
  the window, or pass a path on the command line.
- **Open a link** — right-click the video → *Open* → *Open link…*; yt-dlp resolves
  the stream and Luma plays it in mpv. Seeking works as usual.
- **Playlists** — the playlist button in the control bar opens the panel. Tabs at the
  top switch playlists; the playlist-menu button on the left opens a dropdown with every
  playlist at once. Right-click a tab for *Add files…*, *Add folder…*,
  *Clear playlist*, *Rename*, *Delete*. Right-click a track for playback, shuffle,
  “Add to playlist”, properties, opening the containing folder and removal.
  Each named playlist keeps its own playback mode (changed in *Playback → Playback
  mode*); the mode chosen in the settings applies only to the Default playlist.
- **Subtitles** — the context menu selects subtitle/audio tracks, or use the hotkeys.
- **Casting** — *Playback → Screen output* in the context menu.

## Keyboard shortcuts

The keys below are defaults. Reassign them in **Settings → Shortcuts**; the
bindings are saved in `~/.config/luma/shortcuts.conf`. Each action can have a
local key and, when the desktop provides
`org.freedesktop.portal.GlobalShortcuts`, a global key. Global shortcuts are
available only in single-instance mode (`single_instance=true`); with multiple
instances they are disabled to avoid ambiguous routing. The `?` help key is
intentionally fixed.

| Key | Action |
| --- | --- |
| `Space` | Play / pause |
| `←` / `→` | Seek back / forward 5 s |
| `Ctrl+←` / `Ctrl+→` | Seek back / forward 60 s |
| `Alt+←` / `Alt+→` | Previous / next frame |
| `↑` / `↓` | Volume +5 % / −5 % |
| `M` | Mute |
| `Q` / `E` | Previous / next file |
| `S` | Toggle the playlist panel |
| `I` | Toggle the media information panel |
| `F` | Toggle fullscreen |
| `T` | Pin the time counter on screen |
| `P` | Picture in picture |
| `?` (hold) | Show the keyboard shortcuts |
| `C` | Cycle subtitles |
| `A` | Cycle audio tracks |
| `D` | Cycle deinterlacing (`auto` / `yes` / `no`) |
| `R` | Cycle aspect ratio |
| `J` | Detect and crop black bars |
| Right click | Context menu |
| Double click | Toggle fullscreen |
| `Esc` | Exit fullscreen / close a menu |

“Show / hide window” is available in the Shortcuts settings but has no default
key.

## Configuration

All per-user files live in `~/.config/luma/`:

| File | Purpose |
| --- | --- |
| `luma.conf` | Application settings (INI) |
| `mpv.conf` | Extra mpv options, included at startup |
| `yt-dlp.conf` | Options passed to yt-dlp when opening links |
| `shortcuts.conf` | Local and global key bindings |
| `playlists/*.json` | Saved named playlists |

Logs are written to `~/.cache/luma/`: `luma.log`, `mpv.log` and `yt-dlp.log`.
Each component can be set to `off`, `error`, `info` or `debug` on the **Debug**
page in Settings, or under `[debug]` in `luma.conf`.

`luma.conf` sections:

```ini
[ui]
remember_volume=true
default_volume=80
volume=80
language=en              # en, ru
single_instance=true
notifications=true
append_to_playlist=false
control_bar_mode=float   # hidden, float, pinned
show_panels_fullscreen=true

[playlist]
mode=sequential          # Default playlist only: sequential, repeat, shuffle, stop

[debug]
luma=info                # off, error, info, debug
mpv=off
ytdlp=off

[window]
width=960
height=600

[subtitles]
font=sans-serif
size=55
bold=false
color=#FFFFFFFF         # #RRGGBB или #AARRGGBB
border_size=3
border_color=#FF000000
background=false
position=100            # 0..100, 100 = bottom
ignore_file_style=false
```

The subtitle page in the settings dialog writes these keys; the font picker lists the
system fonts.

The playback page stores `hwdec` and `ao` in `mpv.conf` using mpv's own syntax
(`hwdec=...`, `ao=...`); automatic audio output is represented by omitting `ao`. Older
versions kept these values in `luma.conf` under `[playback]`; they are migrated to
`mpv.conf` and removed from `luma.conf` on the first start. `no` in `hwdec` disables
hardware decoding.

`yt-dlp.conf` holds one option per line and is read for every link. Video quality and the
proxy are configured in *Settings → Playback*; Luma writes them into this file and keeps
any other lines you add, e.g.:

```ini
--no-playlist
--format=bestvideo[height<=1080]+bestaudio/best[height<=1080]
--proxy=socks5://127.0.0.1:1080
```

The proxy is set as a type (`SOCKS5`, `SOCKS5h`, `HTTP`, `HTTPS`) plus an address in
`host:port` form. yt-dlp uses it to resolve the stream. For SOCKS5, Luma additionally
starts a local HTTP bridge: mpv fetches the stream (including seeking and Range requests)
through it, so a SOCKS-only proxy works without extra tools. HTTP proxies are passed to
mpv directly. No files are downloaded.

## Localization

Source strings are in Qt `.ts` files under `locales/` (`en`, `ru`). Japanese and Chinese
are temporarily disabled. To add a language: create `locales/<lang>.ts`, add it to
`qt_add_translations(...)` and the `install(FILES ...)` list in `CMakeLists.txt`, and add
the language to the list in `qml/SettingsDialog.qml`.

Translations of this README live in [`docs/readme/`](docs/readme/).

## Troubleshooting

- **Launching a second copy does nothing.** Single-instance mode forwards arguments to
  the running player and exits. Quit the running instance, or set
  `single_instance=false` in `luma.conf`.
- **X11 is not supported.** Luma runs only on Wayland sessions (KWin, GNOME, Sway,
  Hyprland, …); X11 support is not planned. It checks for a Wayland session at startup
  and exits with an error if there is none, so run it inside a Wayland session. The
  AppImage does not include Qt's xcb platform plugin either.
- **A QML import error on startup.** Install the runtime QML modules: `kirigami` and
  `qqc2-desktop-style`.
- **Casting finds no devices.** Make sure `avahi` and `ffmpeg` are installed and the TV
  is on the same network.

## Credits

Luma is built on these projects:

- [Qt 6 / Qt Quick](https://www.qt.io/) — application framework and QML runtime.
- [KDE Kirigami](https://invent.kde.org/frameworks/kirigami) and
  [qqc2-desktop-style](https://invent.kde.org/frameworks/qqc2-desktop-style) — UI.
- [mpv / libmpv](https://mpv.io/) and [mpvqt](https://invent.kde.org/libraries/mpvqt) —
  playback engine and Qt bindings.
- [FFmpeg](https://ffmpeg.org/) — thumbnails and HLS for casting.
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) — resolving links from video sites.
- [TagLib](https://taglib.org/) — audio metadata.
- [Wayland](https://wayland.freedesktop.org/), [xkbcommon](https://xkbcommon.org/) and
  [Avahi](https://avahi.org/) — desktop integration.
- [AppImage](https://appimage.org/) and
  [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) — portable builds.

Thanks to the maintainers and contributors of these projects.

## Versioning

Luma uses `MAJOR.MINOR.PATCH`. The single source of the version is the
[`VERSION`](VERSION) file; `CMakeLists.txt`, `main.cpp`, `PKGBUILD` and
`build-appimage.sh` consume it. See [`VERSIONING.md`](VERSIONING.md) for the
rules for major, minor, patch and rebuild versions.

## License

Luma is licensed under the **GPL-3.0-or-later**. The vendored `mpvqt/` code is
LGPL-2.1-only (see the file headers); its license headers are kept intact.
