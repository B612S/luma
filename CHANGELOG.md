# Changelog

All notable changes to Luma are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[VERSIONING.md](VERSIONING.md). The current version lives in [VERSION](VERSION).

## [Unreleased]

### Added

- New plugins start disabled. Direct `mpv` access (`mpv:raw`) and external
  programs (`process`, through `luma.process.run(program, ...)`) use separate
  warning dialogs and separate approvals; both are requested before the plugin
  is enabled. Raw mpv process/script commands stay blocked. Plugins otherwise
  work through Luma's media API.
- JavaScript plugin system: plugins are auto-loaded from
  `~/.local/share/luma/plugins/<id>/` and can control playback and playlists,
  add native Kirigami menu actions, open host-rendered native windows, use
  plugin-scoped config/storage, and make permission-checked HTTP requests.
  Settings → Plugins can enable/disable plugins, show descriptions and
  versions, and add or delete plugin folders. See
  [`docs/PLUGINS.md`](docs/PLUGINS.md).
- Playlist properties (right-click a playlist tab): a playlist can watch
  folders. Existing media files are imported in the background when a folder
  is added and new files are appended automatically. Watching uses filesystem
  notifications plus a debounced background scan, so the UI is not blocked.
  The dialog follows the Settings layout (categories on the left, the watched
  folders on the right); it is not available for the Default playlist, which is
  per-window and not saved.
- Named playlists are now shared between running Luma instances: creating,
  renaming, deleting, reordering, changing the playback mode and adding or
  removing entries in one window is reflected in the others (last write wins).
  The Default playlist stays per-window.
- In multi-instance mode every window registers its own MPRIS player
  (`org.mpris.MediaPlayer2.luma.i<pid>`), so playerctl and the desktop media
  applet can control each instance separately.
- “Play in another window” in the playlist track menu when
  `single_instance=false`: starts a separate Luma process for the selected track
  while the current window keeps playing. The entry is hidden in single-instance
  mode.
- Add `G` as the default shortcut for toggling the equalizer.
- Prefer system `yt-dlp`/`yt-dlp-ejs`, fall back to private copies in
  `~/.local/share/luma`, show their versions and asynchronously check the private
  copies for updates.
- Install missing `yt-dlp`/`yt-dlp-ejs` as private pure-Python wheels from PyPI
  without pip; no files are installed system-wide or exposed to the system Python.
- Install a missing Deno privately into `~/.local/share/luma/deno` (system Deno is
  still preferred), with a version display and an asynchronous update check.
- A music crossfade option in **Settings → Playback → Music**. For consecutive
  local audio files it fades the current track out and the next one in; the duration
  is configurable from 1 to 12 seconds and `0` disables it. A trailing silent tail of
  about 5 s or more is detected in the background and skipped, so the next track starts at
  the last audible sample. It is a boundary fade (mpv plays one file at a time), so
  tracks do not overlap; the setting is stored in `luma.conf`.
- A 10/20-band graphic equalizer with a preamp, builtin presets (Flat, Rock, Pop, Jazz,
  Classical, Electronic, Hip-Hop, Vocal, Bass, Treble) and user presets that can be
  saved and reset; right-clicking a slider resets it to 0 dB. It has an on/off switch,
  applies to everything mpv plays (files, links and radio) and is also applied to
  Chromecast casts; it is opened from the Playback menu and stores its settings in
  `luma.conf`.
- The interface language can follow the system locale. The Language list in Settings
  now offers **System** (the default for new configurations), which picks Russian for
  Russian system locales and English otherwise; an explicit language choice is kept.
- Chapter navigation and chapter markers on the seek bar. `PageUp` / `PageDown` or
  **Playback → Previous / Next chapter** jump between chapters, and the seek bar draws a
  tick for every chapter (can be hidden in **Settings → Interface**). Chapters reported
  by yt-dlp are passed to mpv as well, so YouTube links get the same navigation.
- Playback resumes from the previous position for local audio/video files and
  YouTube/yt-dlp links; live streams are excluded. Positions are stored in
  `~/.cache/luma/playback-positions.json`, capped at the 500 most recently played files
  for 90 days, and a file watched to the end loses its entry. The feature can be turned
  off in **Settings → General**.
- Radio links in playlists now show the station name (the ICY `icy-name` header)
  instead of the URL while the stream is not playing. The name is fetched in the
  background when a link is added or a playlist is opened, saved in the playlist, and
  the live track title is shown while the station plays. The probe runs in its own
  thread, so the first (slow) network request cannot stall the GUI or drop video
  frames.
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

- Plugin-scoped data now lives under
  `~/.local/share/luma/plugins/data/<id>/` instead of `plugin-data/<id>/`;
  existing data is migrated on first load.
- Playlist settings moved out of Settings: the Default playlist's append
  switch and playback mode now live in its Properties dialog. Properties is
  available for the Default playlist again, and every playlist now exposes its
  own playback mode there.
- Selected playlist rows, tabs, Settings categories and checked menu entries
  use the semibold (DemiBold) weight, which is visibly lighter than Bold but
  still distinct from Normal.
- Dialogs no longer show a **Close** button; the information dialog's
  **Apply** action now sits next to the tag warning instead of in a footer.
- Dragged playlist rows/tabs use the Kirigami highlight at alpha 0.85.
  Bar-marked selections (the Settings and playlist-properties categories, the
  playing row, the open tab and checked menu entries) use a 0.35 background
  with an opaque bar; selected text uses DemiBold and the `▶` prefix is gone.
- Removed the “All playlists” and “New playlist” tooltips from the playlist
  tab bar, and slightly reduced the playlist search font.
- The AppImage now applies the KDE `kdeglobals` font explicitly when the
  bundled Plasma platform theme does not propagate it, matching the packaged build's
  font family and weight.
- The AppImage loads the bundled KDE platform theme on Plasma, inheriting the
  system font, palette and widget style; icons come from the system theme (with a small
  hicolor fallback), and the playlist tab uses the standard `view-list-symbolic` name.
- The information dialog no longer shows a double separator for video files.
- The AppImage now follows the current system icon theme (KDE/GTK/GNOME settings)
  instead of forcing the bundled hicolor/Breeze fallback; the small Breeze set is used
  only for icon names the system theme does not provide.
- The AppImage now bundles `ffmpeg` and `avahi-browse`, so HLS casting and
  Chromecast discovery work without installing them on the host.
- The statistics panel hides and omits the A/V sync line for audio/radio (it is
  only meaningful with a video track). Its height animates while statistics load
  (opening the panel or switching the source, capped by the playlist panel's
  height), but snaps immediately when the window or panel is resized. The
  statistics scroll position also returns to the top for a new source.
- Opening a single file starts playback immediately; the other videos in its
  folder are listed and prepared on a worker thread and merged into the default
  playlist when the scan finishes. A folder with thousands of files no longer
  delays the first frame or blocks the UI; the merge is skipped if the playlist
  was changed or another file was opened in the meantime.
- Saving tags from the media information dialog now runs TagLib on a worker
  thread; the dialog stays responsive and its fields are disabled until the write
  finishes.
- Picture-in-picture keep-above setup calls KWin over D-Bus with a 500 ms timeout
  instead of the 25 s D-Bus default, so a busy compositor cannot freeze the PiP
  transition.
- The audio/subtitle cycle shortcuts use the track list observed by `MpvItem`
  instead of a blocking mpv property read on the GUI thread.
- The file manager name in the playlist menu is resolved in the background; the
  first menu no longer waits up to a second for the `xdg-mime` query.
- Equalizer changes are written to `luma.conf` once a slider drag settles instead
  of rewriting the file on every 0.5 dB step.
- “Remove black bars” is now **Crop**, and “Output device” is now **Chromecast** and is
  listed above the aspect-ratio, fit-window and crop items.
- **Media information** is not offered while nothing is loaded and closes when playback
  stops; the previous/next chapter menu items are hidden unless the media has more than
  one chapter, and **Picture in picture** is hidden while nothing is loaded.
- Playlists with thousands of entries load faster and save without blocking the UI.
  The JSON is now converted directly between the entry objects and `QJsonObject`
  (the `QVariantMap` round trip was the largest part of the load/save time), and
  autosaves are serialised and written by a background thread. On a 10 000-entry
  playlist loading dropped from ~44 ms to ~16 ms and the GUI thread now spends no
  time on a full autosave; the file format stays the same human-editable JSON.
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
- New application icon: a flat orange rounded square with an outlined white play triangle
  (the vector source is in `assets/luma.svg`).
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

- Plugin windows no longer disable player shortcuts. Shortcuts stay active while
  a host-rendered plugin window is open; text fields still suppress them while
  focused.
- Plugin storage and external-process executable lookup now honor their
  declared permissions; a plugin without `filesystem:plugin` or `process`
  gets no path and cannot read/write plugin storage through the API.
- Hardened plugin execution: raw mpv process/script/media-open commands are
  blocked, direct mpv/process access requires explicit approval, and
  plugin-provided UI text is rendered as plain text to prevent markup
  injection.
- Plugin windows now receive live `positionChanged`, `pausedChanged` and
  `volumeChanged` events; the Now Playing example updates its position, state
  and Play/Pause label instead of freezing the snapshot taken at open time.
- Shuffle playback mode now actually reorders named playlists immediately
  (including when the mode is chosen in Properties or restored on startup);
  previously only the Default playlist was shuffled.
- The active playlist tab underline now spans the tab width instead of the
  label width, so short playlist names no longer get a shorter bar.
- The playlist tab menu hides **Properties**, **Rename** and **Delete** for
  the Default playlist instead of offering actions that cannot work (the
  Default playlist is not saved and cannot be renamed or deleted).
- Equalizer changes made in one Luma window now reach other running
  instances: mpv/luma settings are watched by `ConfigManager`, and the
  equalizer now reloads its curve from `luma.conf` when another process
  changes it.
- Dropping a folder onto the playlist, or adding it with “Add folder”, now
  adds the media files inside it recursively. Drag & drop used to ignore
  folders completely, and “Add folder” only scanned the top level.
- The all-playlists dropdown and the “Add to playlist” menu fit a small
  window: their width and scrollable list height follow the window size while
  they are open, long playlist lists scroll instead of running past the
  window, and an opaque menu background stops the playlist names behind from
  showing through the rows. An open menu is repositioned once the new window
  size has settled, so resizing the window to its minimum and back no longer
  leaves it away from its anchor.
- The seek-preview tooltip keeps the new file's aspect ratio after switching
  from a video with a different shape: it now follows mpv's video-parameter
  updates instead of keeping the previous file's aspect and black bars.
- The statistics panel no longer logs a QML warning that `Window.window` is
  only available on items: the window is now read from the panel itself.
- Playlist drag & drop: the entry being moved is highlighted in place with
  the theme's highlight colour and reordered live, so it always sits in the row
  under the pointer instead of in a separate floating copy that could lag
  behind, be drawn twice or leave a gap. When the pointer leaves the list the
  entry stays on the first or last visible row instead of being moved out of
  view, so auto-scrolling up and down is not cancelled. The scroll direction is
  remembered once the pointer reaches an edge, so the list keeps scrolling even
  when the pointer leaves the playlist or the window and no further move events
  arrive; the target also follows manual scrolling, and the list no longer
  scrolls to the playing row on its own, so reordering an entry in a long
  playlist keeps it under the pointer instead of losing it.
- Let the floating control bar auto-hide while a dialog is open, without
  hiding the cursor over that dialog.
- Keep the seek preview in the video's aspect ratio and size its tooltip to
  that ratio. The aspect is taken from mpv as soon as the video is known, and
  a current-position thumbnail is precached so the first seek shows a frame
  immediately instead of waiting on a time-only bubble. The thumbnail decoder
  fits the source aspect into a 320x180 bitmap instead of always stretching it
  to 16:9. When black-bar cropping is active, the preview follows the same crop
  and aspect.
- Keep the floating control bar visible while the pointer is over it; reliable
  whole-bar hover detection and no self-assignment of `isOpen` stop the
  auto-hide from toggling it.
- Force all control-bar and PiP icons to use the active theme colour, so a mixed
  set of light and dark icons no longer appears on other systems.
- Context menus now reveal the cursor even if the idle timer had hidden it, so
  the pointer is visible outside the menu again.
- The playlist panel button and the all-playlists dropdown use distinct icons from
  the system theme (`view-media-playlist` and `view-list-symbolic`).
- The seek time OSD is guaranteed to stay visible for a full 2.5 s; a hide timer
  left over from another OSD can no longer shorten it.
- Pressing next/previous (or reaching the end of a file) while another playlist
  tab is open now continues in the playlist that is actually playing instead of
  playing a file from the viewed playlist; the playback mode is taken from the
  playing playlist too. After playback stops the last playing model is released.
- The Shortcuts settings page lays its action/local/global columns out
  proportionally, so the key text no longer runs into the action labels on a
  narrow window.
- The statistics panel now uses the playlist panel's height as its maximum, so
  long statistics stop at the same bottom line and scroll; short statistics
  still size to their content instead of leaving a full-height empty panel.
- The mouse wheel over an open dialog now scrolls its Flickable (for example the
  media information page) instead of changing the player volume.
- The Settings, Equalizer and media information dialogs now fit inside the
  player window instead of being clipped when the window is smaller than their
  fixed size; the settings/equalizer content keeps working, and the information
  dialog scrolls when needed. The nested font picker follows the same limit.
- Opening the menu while a radio stream was still loading could freeze the interface:
  the ICY `StreamTitle` reader started right after the stream opened and made its
  first network request (proxy detection, TLS/CA setup) on the GUI thread. It now
  runs in its own thread, like the radio-name probe.
- Returning from fullscreen opened from picture-in-picture now goes back to the PiP
  window with its previous size instead of a normal window. PiP stays active in
  fullscreen, so its own controls (including the back button) remain available.
- Network streams through a SOCKS proxy no longer buffer slowly or stutter: the local
  SOCKS→HTTP bridge now runs on its own thread, so copying stream data does not compete
  with video rendering on the GUI thread.
- A video started from a playlist stays marked as playing (▶) in that playlist when the
  user switches tabs while a yt-dlp link is still being resolved. The playing state and
  the video title were applied to the tab that happened to be active when yt-dlp
  finished, so the original playlist lost its marker. Playback now remembers the
  playlist it was started from and re-finds the row after the link resolves.
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

- The unfinished Flatpak packaging; portable builds are handled by the AppImage script.
- The unofficial X11/xcb fallback. X11 is not supported and support is not planned.
- The “Close” button in the keyboard shortcuts help; the help still hides as soon as `?`
  is released or the window loses focus.

## [1.0.0]

Initial versioned baseline. See [README.md](README.md) for the feature set.
