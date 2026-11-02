#!/usr/bin/env bash
# Build a portable Luma AppImage.
#
# The result is a self-contained x86_64/aarch64 AppImage for glibc-based
# distributions. It bundles Qt, QML modules, libmpv, FFmpeg, TagLib and the
# Wayland platform plugins. Luma is Wayland-only: there is no X11 support and
# none is planned. Build on the oldest distribution you intend to support: the
# AppImage cannot bundle the build host's glibc.
#
# Requirements: cmake, ninja (or make), a C++17 compiler, Qt 6 development
# packages, qmake6, curl, and the runtime QML modules used by the app
# (Kirigami, qqc2-desktop-style). Network access is needed the first time to
# download linuxdeploy/appimagetool.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="${BUILD_DIR:-build-appimage}"
APPDIR="${APPDIR:-AppDir}"
DIST_DIR="${DIST_DIR:-dist}"
TOOLS_DIR="${TOOLS_DIR:-$SCRIPT_DIR/.appimage-tools}"
ARCH="${ARCH:-$(uname -m)}"
VERSION="$(tr -d '[:space:]' < VERSION 2>/dev/null || true)"
VERSION="${VERSION:-1.0.0}"

if [[ "$ARCH" != "x86_64" && "$ARCH" != "aarch64" ]]; then
    echo "Unsupported architecture: $ARCH (expected x86_64 or aarch64)" >&2
    exit 1
fi

for tool in cmake curl qmake6; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing required tool: $tool" >&2
        exit 1
    }
done
CMAKE_GENERATOR=""
if command -v ninja >/dev/null 2>&1; then
    CMAKE_GENERATOR="-G Ninja"
fi
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

mkdir -p "$TOOLS_DIR" "$DIST_DIR"

download_tool() {
    local url="$1" dest="$2"
    if [[ ! -x "$dest" ]]; then
        echo "Downloading $(basename "$dest")..."
        curl -fL --retry 3 --connect-timeout 20 -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
        chmod +x "$dest"
    fi
}

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-$ARCH.AppImage"
QT_PLUGIN="$TOOLS_DIR/linuxdeploy-plugin-qt-$ARCH.AppImage"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-$ARCH.AppImage"

download_tool "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" "$LINUXDEPLOY"
download_tool "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$ARCH.AppImage" "$QT_PLUGIN"
download_tool "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage" "$APPIMAGETOOL"

# Run AppImage tools without FUSE (useful in containers/CI).
export APPIMAGE_EXTRACT_AND_RUN=1
# The linuxdeploy bundle ships an older strip that cannot handle .relr.dyn.
export NO_STRIP=1
export PATH="$TOOLS_DIR:$PATH"
ln -sf "$QT_PLUGIN" "$TOOLS_DIR/linuxdeploy-plugin-qt"
ln -sf "$LINUXDEPLOY" "$TOOLS_DIR/linuxdeploy"

echo "Building Luma $VERSION ($ARCH)..."
cmake -S . -B "$BUILD_DIR" $CMAKE_GENERATOR \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLUMA_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" -j "$JOBS"

rm -rf "$APPDIR"
DESTDIR="$SCRIPT_DIR/$APPDIR" cmake --install "$BUILD_DIR"

# Luma is Wayland-only, so the Wayland platform plugin is added explicitly.
# (The Qt plugin may still deploy xcb by default; that is not a supported
# platform.)
export QML_SOURCES_PATHS="$SCRIPT_DIR/qml"
export QMAKE="${QMAKE:-$(command -v qmake6)}"
QT_PLUGINS_DIR="$(qmake6 -query QT_INSTALL_PLUGINS)"
WAYLAND_PLATFORMS=""
for plugin in libqwayland.so libqwayland-egl.so libqwayland-generic.so; do
    if [[ -f "$QT_PLUGINS_DIR/platforms/$plugin" ]]; then
        WAYLAND_PLATFORMS="${WAYLAND_PLATFORMS:+$WAYLAND_PLATFORMS;}$plugin"
    fi
done
export EXTRA_PLATFORM_PLUGINS="$WAYLAND_PLATFORMS"
export EXTRA_QT_MODULES="svg"

echo "Deploying Qt/QML dependencies..."
"$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt \
    --executable "$APPDIR/usr/bin/luma" \
    --desktop-file "$APPDIR/usr/share/applications/luma.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/luma.png"

# Luma is Wayland-only. The Qt plugin deploys the host's platform plugin (xcb
# on an X11 host) by default, so remove the X11 platform plugin and the X11 GL
# integrations — the image must not be able to start a non-Wayland session.
rm -f "$APPDIR/usr/plugins/platforms/libqxcb.so"
rm -rf "$APPDIR/usr/plugins/xcbglintegrations"

# Some mpv builds (e.g. Arch's) link against libmujs for JavaScript support.
# linuxdeploy treats it as a system library and may leave it out; on a machine
# without the mujs package the AppImage then fails to start. Bundle it.
if [[ ! -e "$APPDIR/usr/lib/libmujs.so" ]]; then
    MUJS_PATH=""
    if command -v ldconfig >/dev/null 2>&1; then
        MUJS_PATH="$(ldconfig -p 2>/dev/null | awk '$1 == "libmujs.so" { print $NF; exit }')"
    fi
    if [[ -z "$MUJS_PATH" ]]; then
        for candidate in /usr/lib/libmujs.so /usr/lib64/libmujs.so; do
            if [[ -f "$candidate" ]]; then MUJS_PATH="$candidate"; break; fi
        done
    fi
    if [[ -n "$MUJS_PATH" && -f "$MUJS_PATH" ]]; then
        cp -L "$MUJS_PATH" "$APPDIR/usr/lib/libmujs.so"
        "$LINUXDEPLOY" --appdir "$APPDIR" --library "$APPDIR/usr/lib/libmujs.so" >/dev/null
    fi
fi

# The KDE widget style is used by qqc2-desktop-style for native widgets and
# scrollbars. Without it Qt falls back to Fusion (the classic arrow bars).
if [[ -f "$QT_PLUGINS_DIR/styles/breeze6.so" ]]; then
    mkdir -p "$APPDIR/usr/plugins/styles"
    cp "$QT_PLUGINS_DIR/styles/breeze6.so" "$APPDIR/usr/plugins/styles/"
    "$LINUXDEPLOY" --appdir "$APPDIR" --library "$APPDIR/usr/plugins/styles/breeze6.so"
fi

# A tiny fallback icon theme: the full Breeze theme is ~100 MB, but these are
# the icons Luma actually uses in its controls.
ICON_ASSETS="$SCRIPT_DIR/assets/appimage-icons"
if [[ -d "$ICON_ASSETS" ]]; then
    ICON_DEST="$APPDIR/usr/share/icons/hicolor"
    mkdir -p "$ICON_DEST/22x22/actions" "$ICON_DEST/22x22/status"
    cp "$ICON_ASSETS/actions/"*.svg "$ICON_DEST/22x22/actions/"
    cp "$ICON_ASSETS/status/"*.svg "$ICON_DEST/22x22/status/"
    cp "$ICON_ASSETS/index.theme" "$ICON_DEST/index.theme"
fi

# qqc2-desktop-style is selected by Qt at runtime rather than imported from a
# QML file, so the import scanner cannot discover it. Bundle it explicitly.
QT_QML_DIR="$(qmake6 -query QT_INSTALL_QML)"
QML_DEST=""
for candidate in "$APPDIR/usr/qml" "$APPDIR/usr/lib/qt6/qml"; do
    if [[ -d "$candidate" ]]; then
        QML_DEST="$candidate"
        break
    fi
done
if [[ -z "$QML_DEST" ]]; then
    QML_DEST="$APPDIR/usr/lib/qt6/qml"
    mkdir -p "$QML_DEST"
fi

if [[ -d "$QT_QML_DIR/org/kde/desktop" ]]; then
    mkdir -p "$QML_DEST/org/kde"
    cp -a "$QT_QML_DIR/org/kde/desktop" "$QML_DEST/org/kde/"
fi
if [[ -d "$QT_QML_DIR/org/kde/qqc2desktopstyle" ]]; then
    mkdir -p "$QML_DEST/org/kde"
    cp -a "$QT_QML_DIR/org/kde/qqc2desktopstyle" "$QML_DEST/org/kde/"
fi
# qqc2-desktop-style links its text fields to Sonnet for spell checking.
if [[ -d "$QT_QML_DIR/org/kde/sonnet" ]]; then
    mkdir -p "$QML_DEST/org/kde"
    cp -a "$QT_QML_DIR/org/kde/sonnet" "$QML_DEST/org/kde/"
fi

# Bundle dependencies of the extra QML plugins (the runtime style and, if the
# import scanner left them out, Kirigami).
for lib in \
    "$QML_DEST/org/kde/desktop/libqqc2desktopstyleplugin.so" \
    "$QML_DEST/org/kde/qqc2desktopstyle/private/libqqc2desktopstyleplugin.so" \
    "$QML_DEST/org/kde/sonnet/libsonnetquickplugin.so" \
    "$QML_DEST/org/kde/kirigami/libkirigamiplugin.so"; do
    if [[ -f "$lib" ]]; then
        "$LINUXDEPLOY" --appdir "$APPDIR" --library "$lib"
    fi
done

# Qt loads its Wayland buffer/decoration integrations at runtime, so the Qt
# plugin cannot discover them from the QML sources. Bundle the client ones.
for dir in wayland-graphics-integration-client wayland-decoration-client wayland-shell-integration; do
    if [[ -d "$QT_PLUGINS_DIR/$dir" ]]; then
        mkdir -p "$APPDIR/usr/plugins/$dir"
        cp -a "$QT_PLUGINS_DIR/$dir/." "$APPDIR/usr/plugins/$dir/"
    fi
done

# Arch's libmpv links to libmujs with an absolute DT_NEEDED path
# (/usr/lib/libmujs.so). Absolute paths bypass the AppImage's bundled
# libraries, so rewrite every absolute dependency to a plain library name.
PATCHELF="$(command -v patchelf || true)"
if [[ -z "$PATCHELF" ]]; then
    PATCHELF_DIR="$TOOLS_DIR/patchelf"
    rm -rf "$PATCHELF_DIR"
    mkdir -p "$PATCHELF_DIR"
    (cd "$PATCHELF_DIR" && env -u APPIMAGE_EXTRACT_AND_RUN "$LINUXDEPLOY" --appimage-extract >/dev/null 2>&1)
    PATCHELF="$PATCHELF_DIR/squashfs-root/usr/bin/patchelf"
fi
if [[ ! -x "$PATCHELF" ]]; then
    echo "patchelf is required to fix absolute library dependencies" >&2
    exit 1
fi

while IFS= read -r -d '' elf; do
    file -b "$elf" 2>/dev/null | grep -q '^ELF' || continue
    needed_list="$("$PATCHELF" --print-needed "$elf" 2>/dev/null || true)"
    while IFS= read -r needed; do
        [[ "$needed" == /* ]] || continue
        base="${needed##*/}"
        if [[ ! -e "$APPDIR/usr/lib/$base" && -f "$needed" ]]; then
            cp -L "$needed" "$APPDIR/usr/lib/$base"
        fi
        if [[ -e "$APPDIR/usr/lib/$base" ]]; then
            "$PATCHELF" --replace-needed "$needed" "$base" "$elf"
        else
            echo "Warning: cannot resolve $needed for $elf" >&2
        fi
    done <<< "$needed_list"
done < <(find "$APPDIR/usr" -type f -print0)

# linuxdeploy only creates an AppRun symlink; use a real launcher so Qt finds
# the bundled plugins/QML modules and the KDE style is active on every desktop.
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<'RUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="$HERE"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QML2_IMPORT_PATH="$HERE/usr/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
export QT_QUICK_CONTROLS_STYLE="${QT_QUICK_CONTROLS_STYLE:-org.kde.desktop}"
# Wayland only: never let Qt pick another platform plugin.
export QT_QPA_PLATFORM=wayland
export LUMA_APPIMAGE=1
export PATH="$HERE/usr/bin:$PATH"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "$HERE/usr/bin/luma" "$@"
RUN
chmod +x "$APPDIR/AppRun"

# yt-dlp is optional. Do not copy the distro wrapper blindly: on most systems it
# is a Python script that imports modules from the system Python installation.

OUTPUT="$DIST_DIR/Luma-$VERSION-$ARCH.AppImage"
rm -f "$OUTPUT"
echo "Packing $OUTPUT..."
ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"

echo
echo "Done: $OUTPUT"
echo "Run it with: ./$OUTPUT"
echo "On systems without FUSE: ./$OUTPUT --appimage-extract-and-run"
