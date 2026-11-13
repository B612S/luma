# Temporary libmpv 0.42 packaging

mpv 0.42 requires libplacebo 7.387.0, while Arch currently ships libplacebo
7.360.1 and mpv 0.41. This directory bridges that gap until the repositories
catch up.

- `PKGBUILD` builds a `libmpv` 0.42 package against a **static** libplacebo
  7.387.0. It is the source of the temporary `libmpv` package the Arch Luma
  package depends on (`libmpv>=0.42`).
- `build-libmpv.sh` builds libplacebo and libmpv as **shared** libraries into a
  private prefix. The AppImage script uses it and bundles both libraries.

Build and install the Arch package with:

```bash
cd packaging/libmpv
makepkg -si
```

After Arch's libplacebo/libmpv are new enough, remove this directory and switch
the Luma PKGBUILD back to the distribution's `mpv`/`libmpv`.
