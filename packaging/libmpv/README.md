# libmpv builder for the AppImage

`build-libmpv.sh` builds libplacebo 7.387.0 and mpv 0.42.0 (libmpv) from pinned
releases into a private prefix. [`build-appimage.sh`](../../build-appimage.sh)
uses it and bundles both shared libraries, so the AppImage carries a libmpv
that matches the version Luma needs even on distributions whose own libmpv is
older.

Arch and other distributions package libmpv themselves; the Luma `PKGBUILD`
depends on the repositories' `mpv` and does not use this script.

Build the pair into a prefix with:

```bash
./build-libmpv.sh --prefix /tmp/libmpv-prefix
```
