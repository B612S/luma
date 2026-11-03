# Changelog

All notable changes to Luma are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[VERSIONING.md](VERSIONING.md). The current version lives in [VERSION](VERSION).

## [Unreleased]

### Added

- “Fit window to video”: `W` or **Playback → Fit window to video** in the context
  menu fits the current window to the video's aspect ratio without exceeding its
  current size, so a 4:3 film in a wide window becomes narrower instead of stretching
  to the screen height. The pinned control bar is taken into account. The aspect is not
  locked, so the window can be resized freely afterwards.
- A search bar at the bottom of the playlist filters the current playlist by title,
  artist or path. It is styled like the panel, has a clear button, shows “Nothing
  found” for an empty result and does not affect drag & drop reordering (disabled
  while a filter is active). The search field spans the playlist width, with the
  magnifier and clear button inside it.
- The all-playlists dropdown caps its height and scrolls when the playlists no longer
  fit on screen.
- Playlist entries whose file was deleted or moved are drawn in red, and opening one shows
  a notice dialog instead of doing nothing. The file's directory is watched, so the red
  mark disappears as soon as the file is available again; a successful play also clears it.
  The mark is saved in named playlists and restored on the next start (a file restored while
  Luma was closed loses it); the Default playlist is not stored on disk.
- Picture-in-picture stays above all other windows on KDE Plasma: Wayland has no client
  request for this and KWin ignores `Qt::WindowStaysOnTopHint`, so while PiP is active a
  small KWin script is loaded through KWin's D-Bus scripting interface and unloaded when
  PiP ends. The PiP window's title becomes “Luma PiP” so the script only raises that
  window. On other compositors nothing changes.
- Playlist tab bar: an all-playlists dropdown on the left, using the current icon theme's
  monochrome playlist icon (also bundled in the AppImage fallback theme).
- Per-playlist playback modes. Each named playlist stores its own
  sequential/repeat/shuffle/stop mode in its JSON file.
- Configurable shortcuts: **Settings → Shortcuts** lists every action (except the fixed
  `?` help key), captures local key combinations and stores them in
  `~/.config/luma/shortcuts.conf`.
- Global shortcut bindings through the XDG Desktop Portal
  `org.freedesktop.portal.GlobalShortcuts`; local and global combinations are configured
  independently and applied when the portal is available.
- Global shortcuts register the application id with the portal before creating a session,
  use one `BindShortcuts` call per session, and start a fresh session when the bindings
  change.
- Global shortcuts are disabled when Luma is configured for multiple instances
  (`single_instance=false`); local bindings keep working and the settings explain why.
- Fixed the GlobalShortcuts request: `BindShortcuts` was sent without its shortcuts
  array because the D-Bus argument had no signature; the registered `a(sa{sv})` argument
  now carries every shortcut ID and preferred trigger.
- Global shortcuts now repair KDE's stale “current shortcut = none” entries through
  KGlobalAccel and actually send `BindShortcuts`; the `Activated` path was verified end
  to end.
- Shortcut labels are derived from the physical keycode using a fixed US display map,
  so switching to a Russian layout no longer turns `M` into `b` (matching stays
  keycode-based). Existing configs are normalized when loaded.
- A “Show / hide window” action, available in the shortcuts settings and global portal
  bindings, with no default key.
- A Debug page in Settings: Luma, mpv and yt-dlp logging can be set to
  off/error/info/debug independently, with each component writing its own file in
  `~/.cache/luma/` (`luma.log`, `mpv.log`, `yt-dlp.log`).
- The statistics panel shows full tags for audio files and radio streams (format
  metadata plus TagLib tags), and has a right-click context menu with a Copy action.
- Video stats show a short container name (`MP4` instead of
  `mov,mp4,m4a,3gp,3g2,mj2`, `MKV/WebM`, and so on).
- The common `encoded_by` tag is shown as “Encoded by” / «Кодировал» instead of a raw
  metadata key.
- The statistics panel no longer shows a file size for radio streams (and Copy omits it).
- Shoutcast/ICY notice tags are shown as readable text: HTML tags are stripped, entities
  decoded, and the labels are localized (“Stream notice 1/2” / «Уведомление потока 1/2»).
- More common audio tags are localized (album artist, track/disc totals, copyright,
  publisher, language, BPM, ISRC, lyrics, lyricist, conductor, performer, producer,
  engineer, mixer, arranger, grouping, compilation, original release fields, descriptions
  and stream details). Unknown keys are humanized (`encoded_by` → “Encoded by”) instead
  of being shown raw with underscores.
- The statistics context menu now sizes itself to its Copy item and adapts to the current
  language instead of using a fixed width. For audio and radio the panel (and Copy) now
  places Title, Artist, Album, Genre and Year first, followed by the remaining stats and
  the other tags.

### Changed

- The 5-minute stream cache (300 s, 256 MiB) is now enabled only for network video on
  demand. Local files use mpv's default (no demuxer cache), which removes up to 256 MiB
  of unnecessary memory during local playback. Live TV/radio uses a 30 s forward-only
  buffer and no rewind cache: the seek slider is hidden, seek shortcuts and MPRIS seeking
  are ignored, and playback is pulled back to the live edge when it falls behind.
- The “yt-dlp” section in Playback settings is now called “Internet playback”, since it
  covers all link-based playback rather than one helper program.
- yt-dlp now runs with the Deno JavaScript runtime when it is installed and the user has
  not configured another runtime. This lets it solve YouTube's JavaScript challenge
  without reading cookies from the user's browser. Videos that require a YouTube account
  or a PO token can still ask for cookies, which is outside the player's control.
- *Settings → Playback → Internet playback* shows whether `yt-dlp`, Deno and
  `yt-dlp-ejs` are available; a missing `yt-dlp` or `yt-dlp-ejs` gets an install button,
  while found components only show their status. Both packages are installed on demand
  into `~/.local/share/luma` without touching the system Python, and Luma prefers the
  local copies when launching yt-dlp.
- `yt-dlp` is no longer listed as a PKGBUILD optional dependency or described as an
  AppImage requirement; both `yt-dlp` and `yt-dlp-ejs` are installed from the Internet
  playback settings when the user asks for it.
- Twitch HLS playback starts three segments before the live edge instead of the oldest
  segment of the DVR window, so a stale preroll block at the beginning is not replayed.
  Twitch's server-side ad at the live edge itself is outside the player's control.
- The playlist search field is 8 px taller and uses a larger font and icons, so its text and controls have more breathing room.
- The “Crop black bars” menu item is now called “Remove black bars” / «Убрать чёрные
  поля».
- Luma now enforces the mpv options its embedding depends on: mpv stays idle between
  playlist entries, demuxing runs on its own thread, network playback pauses on an empty
  cache, the disk cache is off, and mpv's own window, OSC, OSD and default keyboard
  bindings are disabled. Setting these in `mpv.conf` can no longer break Luma's UI,
  shortcuts or playback flow.
- The statistics panel shows only the basic extra tags (track, disc and radio stream
  information) instead of every tag found in the file. Arbitrary or corrupt metadata
  keys are no longer displayed.
- Luma now requires Qt 6.9 or newer (enforced by CMake and the Arch package dependencies).
  The playlist filter uses the non-deprecated `beginFilterChange()`/`endFilterChange()`
  API instead of `invalidateFilter()`.
- Playlist files no longer store a per-entry `duration` (it was never displayed or used
  for sorting), and empty metadata is omitted: `artist`/`album` are written only when set
  and `year` only when it is not 0, so video entries keep just path, kind and title.
  Existing playlists lose the redundant fields on their next save.
- New application icon: a flat orange rounded square with a white play button (the vector
  source is in `assets/luma.svg`).
- Picture-in-picture is now drawn as a square floating card with a small soft shadow, and
  the window is sized to the video's aspect ratio so it has no letterbox bars. The window
  keeps a transparent margin for the shadow, so KWin on NVIDIA no longer leaves a 1px strip
  of the video around the PiP window after it is moved. The card is only used in windowed
  PiP; fullscreen PiP is unchanged.

- Luma is now Wayland-only in code: it refuses to start without a Wayland session, forces
  the Wayland QPA plugin, rejects other platform plugins, and has no X11 renderer path.
- CMake requires `Qt6::WaylandClient`; the AppImage no longer ships Qt's xcb platform
  plugin, and its launcher sets `QT_QPA_PLATFORM=wayland`.
- The playback mode in Settings applies only to the Default playlist. Named playlists keep
  their own mode, and the playback-mode menu shows which playlist it affects.
- The Settings label and hint (and their Russian translation) now state explicitly that the
  mode belongs to the Default playlist.
- Picture-in-picture seeking uses the same exact seek-on-release behaviour as the main
  control bar.
- The yt-dlp opening OSD shows `Opening <URL>` instead of `Opening link…`.
- Playlist names (including “Default”) are refreshed when the UI language changes.
- Vendored mpvqt sources now document their local modifications (see
  [`mpvqt/README.md`](mpvqt/README.md)).
- The old `ui/debug_logging` setting is migrated to `debug/luma`; new logging
  settings live under the `[debug]` section.
- The Settings dialog background is less transparent (0.95 alpha), matching the keyboard
  shortcuts help, so the video shows through much less while settings are open.
- The media information dialog uses the same less transparent background as the Settings
  dialog and the shortcuts help.
- The playlist entry, playlist tab, all-playlists and statistics context menus now use the
  same custom popup as the player's main menu instead of the platform menu style. The
  submenus (“Add to playlist”, “Sort”) are pages of the same popup. The shared row,
  separator and popup styling lives in `LumaMenu.qml`, `LumaMenuButton.qml` and
  `LumaMenuSeparator.qml`, which the main menu uses as well. The menus are anchored to the
  element they were opened from and kept inside the window, switching pages fades like in
  the main menu, and opening one menu closes the previously open one, so only one menu is
  visible at a time.

### Fixed

- Videos no longer lose one or two frames at the very start. On `fileLoaded` Luma read
  several mpv properties synchronously from the GUI thread, but mpv's core was waiting
  for the first rendered frame, so the reads blocked it for ~200 ms, the first frame
  arrived too late and mpv counted it as dropped (display-resample then dropped another
  frame while catching up). The handler now takes the values from the observed-property
  cache, so the first frame is rendered in time and the statistics counter stays at 0 —
  without resetting it.
- Opening Settings no longer freezes the playing video. The yt-dlp runtime check ran
  `yt-dlp --version` and a Python import probe synchronously on the GUI thread and
  blocked it for up to ~200 ms (longer on a cold start); the version string was not
  used anywhere. The check is now cheap and the remaining Python probe runs in the
  background, delivering its result to the settings page through a signal.
- Radio streams show the current song again. mpv exposes only the static station
  name in its metadata for these Icecast/Shoutcast streams, so Luma now reads the
  per-track ICY `StreamTitle` over a separate metadata connection and updates the
  playlist entry and notification when the song changes. The reader starts only
  after mpv has opened the stream, and URL/host titles are ignored, so opening
  radio does not send several duplicate notifications. Radio uses a 4 s forward
  cache and the title update is delayed by the measured mpv cache distance, so the
  notification appears when the song actually starts rather than ~15 s earlier.
- Windowed PiP no longer has a transparent shadow margin or a visible border. The video
  is overscanned by one pixel with a clamped FBO texture, and a single semi-transparent
  pixel keeps the surface out of KWin's direct-scanout path. PiP snaps without a visible
  gap; the remaining artifacts are a KWin/NVIDIA direct-scanout issue that also affects
  Firefox PiP.
- Leaving PiP no longer races KWin's keep-above cleanup: the helper script stays
  loaded until the application quits, so the normal window cannot remain keep-above and
  refuse to snap. Restoring the saved x/y position was removed because Wayland ignores
  client-side top-level positioning and the request could fight KWin's placement.
- Live streams show a single LIVE indicator again: the duplicate time label was removed
  and the remaining red `● LIVE` label is aligned with the transport icon.
- YouTube/Twitch live streams now get their audio track. yt-dlp reports the audio-only
  HLS format of a live stream with `acodec` empty/`none`, so Luma used to classify it as
  neither video nor audio and never passed it to mpv. Stream type is now determined from
  `audio_ext`/`video_ext`/`resolution` as well, and the audio is attached at load time.
- Twitch and other live streams can no longer be dragged back into their DVR window: the
  seek slider is hidden for live content, keyboard seek/frame-step actions and MPRIS
  `Seek`/`SetPosition` are ignored, and `CanSeek` is false.
- Intermittent “SOCKS proxy connection failed” errors during live playback are retried
  once before returning a 502 to mpv. HLS opens many segment connections, and a refused
  connection used to surface as a dropped segment and visible stutter.
- YouTube and other yt-dlp links no longer lose the first seconds of audio. yt-dlp
  returns video and audio as separate URLs; the audio is now passed to mpv's `loadfile`
  as a file-local `audio-file=...` option, so both tracks open and start together. It used
  to be attached with `audio-add` only after `fileLoaded`, when playback had already begun.
- Seek-preview frames from a previous file are released when switching files. The
  thumbnail provider (up to 50 frames) and the frame decoder (30 frames) kept decoded
  images until new previews replaced them, and the QML preview kept the last texture; all
  of them are cleared on a source change now.
- Changing the volume with the mouse wheel (or a touchpad) now moves the volume slider on
  the same event: the wheel handler used to accumulate the steps and apply them after an
  80 ms debounce, so the handle stayed still and then jumped to the new position.
- The seek bar stays at the requested position after releasing the handle until mpv
  reports a position close to it (with a 5 s fallback). Releasing it after a fixed 300 ms
  let a slow or exact seek show the old position first, so the handle jumped back and
  then forward again.
- Saved playlists are no longer rescanned for audio tags at startup. Reading the tags of
  every playlist entry (hundreds of files) competed with the first paint and with mpv
  opening the requested file; a playlist's tags are refreshed when it is opened, and new
  files are still scanned as soon as they are added.
- Seek-bar previews now decode forward from the previous keyframe to the requested
  timestamp instead of returning the keyframe itself. Previously the image changed only
  when a new keyframe was reached (often every ~10 seconds); now it follows the video
  continuously, like YouTube.
- Keyboard shortcuts work again after using the playlist search: clicking anywhere outside
  a focused text field, or closing the playlist panel, now releases keyboard focus instead
  of leaving every subsequent key in the search field.
- Startup after the Qt 6.9 minimum bump: the QML module uses the standard `/qt/qml`
  resource prefix now, so the engine loads `qrc:/qt/qml/luma/qml/Main.qml` instead of the
  old `qrc:/luma/qml/Main.qml` path.
- Context menus stay attached to the button, tab, playlist row or statistics panel they
  were opened from when the window is resized, and are kept inside the window instead of
  drifting over other elements.
- The saved “unavailable” mark of a playlist entry is removed from the playlist file once
  the file is available again: the directory watcher triggers a save while Luma runs, and
  loading rewrites the playlist when the file returned while Luma was closed.
- The playlist context menu hides Information for an entry whose file was deleted or
  moved: the dialog had no metadata to show. The item stays for streams, where the URL
  can still be edited.
- The media information dialog no longer shows “0 kbps” for Matroska/WebM files, which
  store no per-track bitrate: the missing values are estimated from packet sizes over a
  bounded scan (the video track uses the exact container total minus the other streams)
  and marked with “≈”.
- The statistics panel samples the video bitrate of a short window around the playback
  position, so it updates while playing instead of showing mpv's last-GOP value, which
  could stay at 9 kbps for the first seconds of a multi-Mbps video because mpv only
  updates it at keyframes. The file's average video bitrate stays in the information
  dialog.
- Re-entering picture-in-picture no longer shows a black border around the video: the
  window is always translucent, with an opaque backdrop behind the normal and fullscreen
  window, so KWin keeps compositing the PiP margin with its alpha.

- The statistics panel no longer shows “N/A” for the audio bitrate of high-bitrate audio
  (for example multichannel FLAC, whose bitrate can legitimately exceed 1500 kbps); the
  value is formatted like the video bitrate and switches to Mbps when it is that high.
- When the last file of the playlist ends (or the “stop” playback mode is used), the file is
  now unloaded instead of leaving its last frame on screen together with the finished
  position/duration: the video area goes black and the control bar shows `00:00 / 00:00`.
- Settings changed by another running instance are reloaded from `luma.conf`, `mpv.conf`
  and `yt-dlp.conf`; an open Settings dialog updates without a restart.
- A link edited in the media information dialog fetches its title again instead of staying
  untitled.
- Playlist entries for links without a title show the full URL, so a malformed YouTube link
  is no longer displayed as a valid-looking `youtube.com/watch`.
- Selecting a playback mode no longer reorders a named playlist, and the mode from the
  settings is not applied to named playlists.
- The playback-mode OSD no longer shows a literal `%1` placeholder (for example
  “Mode: %1: Repeat current file”); the mode name is now substituted correctly.
- Playlist tab names are vertically centred with the icon buttons instead of sitting a
  couple of pixels too high.
- The English and Russian READMEs now show the new playback and shortcuts screenshots.
- Playlist tabs scroll smoothly on mouse-wheel/touchpad input; horizontal drag/swipe
  scrolling remains available on the tab bar.
- A horizontal two-finger/mouse-wheel swipe over the track list switches to the previous
  or next playlist; the tab bar highlights the new playlist and scrolls its tab into view.
- The Settings dialog now closes on Escape, including while one of its controls has
  keyboard focus.
- The keyboard-shortcut help is now a regular dialog styled like Settings and the media
  information dialog, and its key rows are generated from the current bindings.
- The shortcut help has a single title, scrolls with the mouse wheel, and no longer lets
  the volume-wheel handler consume its scrolling.
- Keyboard layout auto-detection for the shortcut help was removed; the help overlay is
  styled as a normal dialog and no longer depends on a Wayland keymap watcher.
- Fixed a memory leak in the vendored mpvqt layer: the caller-created `mpv_node` trees used
  by `setProperty` and `command` were never freed (every seek/loadfile/string property leaked).
- The debug log no longer records every expected null mpv property-change event.
- The harmless duplicate “Connection already associated with an application ID”
  warning from Qt's portal registration is no longer written to the log.
- Fixed a crash when an observed mpv property became unavailable (null event data) and read
  `MPV_FORMAT_FLAG` as the C `int` used by mpv.
- Fixed a data race between thumbnail decoding and the QML image provider.
- Fixed Cast discovery getting stuck in “searching” when `avahi-browse` fails to start.
- Fixed Cast timer/elapsed-timer leaks and a stale encoding wait that could load the previous
  stream when a new cast was started before the first one finished.
- Fixed a null dereference and leaked client sockets when the cast HTTP server was stopped
  while a client was connected.
- Fixed a shutdown race in the logger.
- Fixed a reconnection race in the Cast V2 client where an old socket could affect the new one.
- Reduced GUI stalls: single-instance arguments are read asynchronously, replacing a
  running `yt-dlp`/`ffmpeg` no longer waits a full second, and the file-manager name is cached.
- Playlist files are now written atomically, and adding a large folder no longer performs an
  O(n²) duplicate scan.
- Media information, notification tag reading, track lists and aspect-ratio queries no longer
  block the GUI thread: file metadata is read by a worker, and track/aspect values come from
  mpv properties observed by `MpvItem`. The information dialog shows a brief loading state.
- Cover art for notifications is no longer cached for every played audio file: only the
  current file's cover is kept, and stale covers are removed.
- The Debug settings page now uses the same label/control alignment, spacing and hint
  style as the other settings pages.

### Security

- The Cast HLS server binds to the interface advertised to the TV, serves files only under a
  random per-cast URL, caps request size, and closes active clients when stopped.
- Cast V2 message parsing now bounds-checks frame and protobuf field lengths.
- The single-instance socket is restricted to the current user and has capped, asynchronous
  input; the SOCKS bridge limits handshake buffers, credential lengths and connection count.

### Removed

- The unofficial X11/xcb fallback. X11 is not supported and support is not planned.
- The “Close” button in the keyboard shortcuts help; the help still hides as soon as `?`
  is released or the window loses focus.

## [1.0.0]

Initial versioned baseline. See [README.md](README.md) for the feature set.
