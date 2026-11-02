# Changelog

All notable changes to Luma are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[VERSIONING.md](VERSIONING.md). The current version lives in [VERSION](VERSION).

## [Unreleased]

### Added

- Playlist tab bar: an all-playlists dropdown on the left, using the current icon theme's
  monochrome playlist icon (also bundled in the AppImage fallback theme).
- Per-playlist playback modes. Each named playlist stores its own
  sequential/repeat/shuffle/stop mode in its JSON file.
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

### Fixed

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
- The Settings dialog now closes on Escape, including while one of its controls has
  keyboard focus.
- Fixed a memory leak in the vendored mpvqt layer: the caller-created `mpv_node` trees used
  by `setProperty` and `command` were never freed (every seek/loadfile/string property leaked).
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

## [1.0.0]

Initial versioned baseline. See [README.md](README.md) for the feature set.
