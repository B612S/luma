# AGENTS.md

Luma: a Linux video player. C++17 + Qt 6.5/QML (Kirigami) GUI on top of libmpv, with
FFmpeg for thumbnails and Chromecast casting. CMake is the only build system.

## Build & verify

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)          # single-target: --target luma
./build/bin/luma path/to/video.mkv      # note: build/bin/, not ./build/luma
```

- Tests live in `tests/` and are **opt-in** (`-DLUMA_BUILD_TESTS=ON`, off by default so they
  never end up in a package): `cmake -B build -S . -DLUMA_BUILD_TESTS=ON`, then
  `ctest --test-dir build --output-on-failure`. They cover core logic only (shortcuts table,
  playlist model/manager, config and the yt-dlp config file), need neither mpv nor a display,
  and redirect `XDG_CONFIG_HOME`/`XDG_CACHE_HOME` to a temp dir. There is no lint/format
  config at the repo root.
- The binary is `build/bin/luma` (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`), not `build/luma`.
  `README.md` is the English user documentation; other languages live in `docs/readme/`.
- Dependencies come from `find_package(Qt6 ...)` plus `pkg_check_modules` for `mpv`,
  `libavformat`, `libavcodec`, `libswscale`, `libavutil`. Kirigami and
  `qqc2-desktop-style` are imported by QML at runtime only — CMake will not catch them
  missing; failure appears as a QML import error on launch.
- Adding a `.cpp` requires editing the `qt_add_executable(luma ...)` source list. Adding
  a `.qml` file requires adding it to `qt_add_qml_module(... QML_FILES ...)`; QML is
  loaded from the resource URL `qrc:/luma/qml/Main.qml`, so unlisted files simply won't
  exist at runtime. C++ classes with `QML_ELEMENT` are registered automatically.
- Translations: `.qm` files are generated during the build from `locales/*.ts`
  (`en`, `ru`; Japanese/Chinese are temporarily disabled) and installed to
  `share/luma/translations`; keep the TS list in `CMakeLists.txt` in sync when adding a language.

## Runtime gotchas when launching

- `main.cpp` forces `QT_QPA_PLATFORM=wayland` and the OpenGL RHI before `QApplication`.
  It will not start on a pure X11 or headless machine without changing that line.
- Single-instance mode is on by default (`QLocalServer` name
  `luma_single_instance_server`): a second launch forwards its argv to the running
  instance and exits 0. During manual testing this looks like "nothing happened"; kill
  the running player or set `ui/single_instance=false` in the config file.
- Per-user state (all under `~/.config/luma/`): `luma.conf` (QSettings INI),
  `mpv.conf`, `playlists/*.json`. Logs go to `~/.cache/luma/luma.log`; set
  `ui/debug_logging=debug` (or `none`) for verbosity.
- `mpv.conf` is *included* by `mpvqt/mpvcontroller.cpp` while mpv's own config is
  disabled, then `vo=libmpv` and `keep-open=no` are forced. The playback settings are
  stored in `mpv.conf` in mpv syntax (`hwdec=...`, `ao=...`); legacy `[playback]` keys
  from `luma.conf` are migrated there and removed.
- Casting shells out to external `avahi-browse` (discovery) and `ffmpeg` (HLS
  re-encode over LAN); it silently fails if they are not on `PATH`.

## Architecture map

- `main.cpp` is the composition root: builds `ConfigManager`, `PlaylistManager`,
  `MediaManager`, `OsdManager`, `CastManager`, `ShortcutHandler`, exposes them to QML as
  context properties (`Config`, `Media`, `Playlists`, `Osd`, `CastManager`,
  `shortcuts`), then wires the `MpvItem` found inside `Main.qml`.
- C++↔QML wiring is partly by `objectName`: the C++ side looks up `"mpv"`,
  `"playlistPanel"` and `"statsPanel"`. Renaming those QML objects silently breaks
  shortcuts and panel toggles.
- `core/` — application logic: `MediaManager` (open/next/prev, tracks, DBus
  notifications, file dialogs), `OsdManager` (OSD state machine), `PlaylistManager` +
  `PlaylistModel` (JSON playlists), `MprisManager` (MPRIS D-Bus), `Cast*` (Cast V2 over
  TLS + local HLS server), `ConfigManager`, `Logger`.
- `mpvqt/` — vendored KDE mpvqt (MPVQT_EXPORT shim included) with local modifications
  (config include, forced properties, casting guards). It is LGPL-licensed third-party
  code, unlike the GPL-3.0-or-later rest of the app: keep headers intact, don't reformat
  wholesale, and prefer subclassing `MpvItem` over editing it.
- `qml/` — flat directory of QML components; no Qt Quick Designer or compiled UI forms.
- Keyboard handling is `ShortcutHandler::eventFilter` driven by the table in
  `core/ShortcutAction.hpp`, which hardcodes evdev keycodes (`+8`) alongside key names.
  The README has the full hotkey table.

## Conventions

- Code comments are in English; user-visible strings are `tr()`/`qsTr()`-wrapped.
- Version numbers live in three places: `project(... VERSION ...)` in `CMakeLists.txt`,
  `setApplicationVersion` in `main.cpp`, and `PKGBUILD`.
- Packaging is Arch-only: `PKGBUILD` copies the working tree (excluding `src/`, `build/`)
  and installs `luma.desktop` and `assets/luma.png` itself; `.gitignore` covers `src/`,
  `pkg/`, `AppDir/`, AppImage/deb artifacts.
