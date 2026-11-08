# Versioning

Luma uses `MAJOR.MINOR.PATCH` versions (for example `1.4.2`). The single source
is the [`VERSION`](VERSION) file: edit that file, not the places that consume it.

- `CMakeLists.txt` reads `VERSION`, defines `LUMA_VERSION`, and passes it to
  `QCoreApplication::applicationVersion()`.
- `build-appimage.sh` uses it for `luma-<version>-<arch>.AppImage`.
- `PKGBUILD` uses it for `pkgver`; `pkgrel` remains a packaging-only counter.
- `luma.desktop`'s `Version` key is the Desktop Entry Specification version, not
  the application version. It is deliberately not set.

## What gets which bump

| Change | Bump | Example |
| --- | --- | --- |
| Incompatible change: users must adapt, configuration/API break, large redesign | **MAJOR** | `1.4.2` → `2.0.0` |
| New user-visible feature, backwards compatible | **MINOR** | `1.4.2` → `1.5.0` |
| Bug fix, performance fix, UI/translation/documentation fix shipped to users | **PATCH** | `1.4.2` → `1.4.3` |
| Rebuild without an app change: packaging, build flags, desktop/AppImage metadata | **no app bump** | `1.4.3-1` → `1.4.3-2` (Arch `pkgrel`) |

Guidelines:

- **MAJOR** — a release that can break existing setups. Examples: removing or
  renaming configuration keys without migration, changing the playlist format
  incompatibly, replacing the playback architecture, dropping a supported
  platform.
- **MINOR** — new functionality that keeps old files, settings and behavior
  working. Examples: new playback features, new settings sections, new supported
  protocols or formats, new keyboard actions.
- **PATCH** — fixes and polish that do not add a feature. Examples: a crash, a
  wrong seek, a broken proxy, a wrong icon, a bad translation, a documentation
  correction that ships with the release.
- **Rebuild** — the application version stays the same. Use this for packaging
  changes (PKGBUILD, AppImage script, desktop entry, build flags) or for
  rebuilding the same sources. For Arch, increment `pkgrel`; for the AppImage,
  rebuild the same `luma-<version>-<arch>.AppImage`.

When `pkgver` changes, reset `pkgrel` to `1`.

## Releasing

1. Choose the bump using the table above.
2. Move the entries under `## [Unreleased]` in [`CHANGELOG.md`](CHANGELOG.md) to a new
   `## [X.Y.Z] - YYYY-MM-DD` section.
3. Edit [`VERSION`](VERSION) only (strictly `MAJOR.MINOR.PATCH`; CMake rejects
   anything else).
4. Reconfigure and build; run the tests.
5. Reset `pkgrel=1` in `PKGBUILD` for a new `pkgver`, or increment it for a
   packaging-only rebuild.
6. Rebuild the AppImage if it is part of the release.
7. Tag the release as `vX.Y.Z`.

The current baseline is `1.0.0`.
