Fallback icons for the Luma AppImage.

The SVGs in `actions/` and `status/` are from the KDE Breeze icon theme
(`breeze-icons`), licensed under LGPL-3.0-or-later:
https://invent.kde.org/frameworks/breeze-icons

They are only a fallback for icon names that the current system icon theme does
not provide. The AppImage uses the user's theme (KDE/GTK/GNOME settings); these
files are used through the hicolor fallback theme, not forced. `view-list-symbolic.svg`
is Breeze's `view-list-details-symbolic.svg` renamed for the all-playlists dropdown;
`view-media-playlist.svg` is the Breeze playlist icon for the control-bar button.
