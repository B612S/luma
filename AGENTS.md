# AGENTS.md

Luma: a Linux video player. C++17 + Qt 6.9/QML (Kirigami) GUI on top of libmpv, with
FFmpeg for thumbnails and Chromecast casting. CMake is the only build system.

**Platform: Wayland only.** Luma targets Wayland compositors; X11 is not supported and
support for it is not planned. Do not add X11-specific code paths, X11 packaging
dependencies or documentation that promises X11 support. This is enforced in code:
`main.cpp` refuses to start without a Wayland session and forces
`QT_QPA_PLATFORM=wayland`; the AppImage build removes Qt's xcb platform plugin.

## Build & verify

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)          # single-target: --target luma
./build/bin/luma path/to/video.mkv      # note: build/bin/, not ./build/luma
```

- Tests live in `tests/` and are **opt-in** (`-DLUMA_BUILD_TESTS=ON`, off by default so they
  never end up in a package): `cmake -B build -S . -DLUMA_BUILD_TESTS=ON`, then
  `ctest --test-dir build --output-on-failure`. They cover core logic only (shortcuts table,
  playlist model/manager, config, logging, shortcut metadata and the yt-dlp config
  file), need neither mpv nor a display, and redirect
  `XDG_CONFIG_HOME`/`XDG_CACHE_HOME` to a temp dir.
  `wayland_only` runs the
  built binary with no Wayland environment and asserts the startup refusal (exit 1 plus the
  expected message). There is no lint/format config at the repo root.
  `tst_media_open` is a separate libmpv integration test: it runs
  headless with `vo=null`/`ao=null`, uses the fixtures in `tests/data/`, and is labelled
  `integration;libmpv`. Its YouTube case uses the real `yt-dlp`, network and
  `~/.config/luma/yt-dlp.conf` (including the proxy); it runs only when
  `LUMA_TEST_YOUTUBE_URL` is set, and can be disabled with `LUMA_TEST_NO_NETWORK=1`.
- The binary is `build/bin/luma` (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`), not `build/luma`.
  `README.md` is the English user documentation; other languages live in `docs/readme/`.
- **Rebuild the AppImage only when it is genuinely required** — for example after a
  packaging change, a change to a bundled dependency, or when verifying an
  AppImage-only issue. Never rebuild it as routine verification: use the CMake build
  and tests above. Rebuild only after the user asks or agrees.
- Dependencies come from `find_package(Qt6 ...)` plus `pkg_check_modules` for `mpv`,
  `libavformat`, `libavcodec`, `libswscale`, `libavutil`, `wayland-client` and
  `xkbcommon`. `Qt6::Concurrent` is used for background file/tag reads. Global
  shortcuts use the optional XDG Desktop Portal `GlobalShortcuts` interface
  (runtime only; no extra link dependency).
  `Qt6::WaylandClient` is a REQUIRED component (no X11 backend). Kirigami and
  `qqc2-desktop-style` are imported by QML at runtime only — CMake will not catch
  them missing; failure appears as a QML import error on launch.
- Adding a `.cpp` requires editing the `qt_add_executable(luma ...)` source list. Adding
  a `.qml` file requires adding it to `qt_add_qml_module(... QML_FILES ...)`; QML is
  loaded from the resource URL `qrc:/qt/qml/luma/qml/Main.qml`, so unlisted files simply
  won't exist at runtime. C++ classes with `QML_ELEMENT` are registered automatically.
- Translations: `.qm` files are generated during the build from `locales/*.ts`
  (`en`, `ru`; Japanese/Chinese are temporarily disabled) and installed to
  `share/luma/translations`; keep the TS list in `CMakeLists.txt` in sync when adding a language.

### Single-instance and multi-instance verification

Every new feature and every fix must be checked in **both** modes before it is
reported as done — do not test only the default one:

- **Single instance** (`ui/single_instance=true`): one process, argument forwarding
  to the running window, global shortcuts enabled, one MPRIS player
  (`org.mpris.MediaPlayer2.luma`).
- **Multiple instances** (`ui/single_instance=false`): start two windows and verify
  that named-playlist/settings/equalizer changes are synchronized, the Default
  playlist stays per-window, and every window exposes its own MPRIS player
  (`org.mpris.MediaPlayer2.luma.i<pid>`, visible in `playerctl -l`); global shortcuts
  must stay disabled.

State which modes were exercised when reporting the result. Changes touching shared
state (playlists, config/equalizer, MPRIS, single-instance plumbing) need the
two-window check in particular; purely visual changes still need both modes for the
settings/menu behavior.

## Runtime gotchas when launching

- `main.cpp` is Wayland-only: it exits with an error when neither `WAYLAND_DISPLAY`
  nor `WAYLAND_SOCKET` is set, then unconditionally forces `QT_QPA_PLATFORM=wayland`
  and verifies `QGuiApplication::platformName()` before continuing. There is no X11
  fallback path; the OpenGL RHI is forced before `QApplication`.
- Single-instance mode is on by default (`QLocalServer` name
  `luma_single_instance_server`): a second launch forwards its argv to the running
  instance and exits 0. During manual testing this looks like "nothing happened"; kill
  the running player or set `ui/single_instance=false` in the config file.
- Per-user state (all under `~/.config/luma/`): `luma.conf` (QSettings INI),
  `mpv.conf`, `shortcuts.conf`, `playlists/*.json`. Logs go to
  `~/.cache/luma/{luma,mpv,yt-dlp}.log`;
  the Debug page writes `debug/luma`, `debug/mpv` and `debug/ytdlp`
  (`off`/`error`/`info`/`debug`). Cover art for notifications is cached in
  `~/.cache/luma/covers/`; only the current file's cover is kept.
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
- `core/` — application logic: `MediaManager` (playback orchestration: open/next/prev,
  tracks, stream URL resolution, file dialogs; delegates yt-dlp runtime to
  `YtDlpRuntime`, file info/tags/bitrate to `MediaInfoService`, notifications to
  `MediaNotifier`, trailing-silence detection to `TrailingSilenceDetector`, and file
  actions to `MediaFileActions`), `OsdManager` (OSD state machine), `PlaylistManager` +
  `PlaylistModel` (JSON playlists), `MprisManager` (MPRIS D-Bus), `Cast*` (Cast V2 over
  TLS + local HLS server), `ConfigManager`, `Logger`.
- `mpvqt/` — vendored KDE mpvqt v1.2.1 (LGPL-2.1-only OR LGPL-3.0-only OR
  LicenseRef-KDE-Accepted-LGPL) with local modifications (config include, forced
  properties, casting guards, no X11 renderer path). Vendored files **may** be edited where
  the license permits it (it does), but: keep the upstream SPDX header, add a
  `Modified for Luma` comment at the top of every changed file listing the changes, and
  keep [`mpvqt/README.md`](mpvqt/README.md) in sync. Avoid wholesale reformatting and
  prefer subclassing `MpvItem` over editing vendored code where practical. `MpvItem.*`,
  `FrameDecoder.*` and `mpvqt_export.h` are Luma additions, not upstream mpvqt.
- `qml/` — flat directory of QML components; no Qt Quick Designer or compiled UI forms.
- Plugins are JavaScript integrations auto-loaded from
  `~/.local/share/luma/plugins/<id>/`; the host lives in `core/Plugin*.{hpp,cpp}`
  and `qml/Plugin*.qml`. **Settings → Plugins** can enable/disable plugins,
  show descriptions/versions/permissions and add or delete plugin folders.
  New plugins start disabled; direct `mpv:raw` and `process` permissions
  require an explicit warning-gated approval. See [`docs/PLUGINS.md`](docs/PLUGINS.md).
- Keyboard handling is `ShortcutHandler::eventFilter`. Bindings are loaded from
  `~/.config/luma/shortcuts.conf` (local and global) and matched by evdev keycode
  (Wayland native scan code); `core/ShortcutAction.hpp` holds the action metadata
  and default table. Global shortcuts use `org.freedesktop.portal.GlobalShortcuts`
  through `core/GlobalShortcuts.*`; the portal is optional and global bindings are
  disabled when `ui/single_instance=false`. The `?` help key is deliberately hardcoded.
  The README has the default hotkey table.

## Conventions

- Code comments and commit messages are written in English; user-visible strings are `tr()`/`qsTr()`-wrapped.
- Keep [`CHANGELOG.md`](CHANGELOG.md) up to date. Every user-visible change (feature,
  behaviour change, fix, translation, packaging) goes under `## [Unreleased]` in the
  matching Keep a Changelog section (`Added`/`Changed`/`Fixed`/`Removed`). When `VERSION`
  changes, move those entries under the new version with its release date.
- `VERSION` is the single source of the application version (`MAJOR.MINOR.PATCH`).
  CMake reads it for the project and passes `LUMA_VERSION` to `main.cpp`; the
  AppImage script and `PKGBUILD` read it too. `pkgrel` is packaging-only. Bump
  rules and rebuild rules are in `VERSIONING.md`. `luma.desktop`'s `Version`
  key is the desktop-entry spec version, not the app version.
- Packaging: `PKGBUILD` (Arch, local) copies the working tree and installs
  `luma.desktop` and `assets/luma.png`; `build-deb.sh` creates a best-effort
  Debian package, and `.github/workflows/packages.yml` builds the `.deb` and
  AppImage on release/manual request. `.gitignore` covers `src/`, `pkg/`,
  `AppDir/`, and package artifacts.
