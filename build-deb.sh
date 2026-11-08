#!/usr/bin/env bash
# Build a Debian package from the current checkout.
#
# This is a best-effort portable package, not a Debian-native source package:
# the binary needs a Qt 6.9 + Kirigami runtime on the target system.
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VERSION="$(tr -d '[:space:]' < VERSION 2>/dev/null || true)"
VERSION="${DEB_VERSION:-${VERSION:-1.0.0}}"
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64) DEB_ARCH=amd64 ;;
    aarch64) DEB_ARCH=arm64 ;;
    *) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac

BUILD_DIR="${BUILD_DIR:-build-deb}"
DIST_DIR="${DIST_DIR:-dist}"
ROOT="$SCRIPT_DIR/$BUILD_DIR/deb-root"
OUTPUT="$SCRIPT_DIR/$DIST_DIR/luma_${VERSION}_${DEB_ARCH}.deb"

command -v dpkg-deb >/dev/null 2>&1 || {
    echo "dpkg-deb is required to build the Debian package" >&2
    exit 1
}

rm -rf "$BUILD_DIR" "$ROOT"
mkdir -p "$DIST_DIR"

cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLUMA_BUILD_TESTS=OFF \
    -G Ninja
cmake --build "$BUILD_DIR"

DESTDIR="$ROOT" cmake --install "$BUILD_DIR"
rm -f "$ROOT/usr/lib/qt6/qml/luma/luma_qml_module_dir_map.qrc"

mkdir -p "$ROOT/DEBIAN"
INSTALLED_SIZE="$(du -sk "$ROOT/usr" | cut -f1)"
cat > "$ROOT/DEBIAN/control" <<DEB_CONTROL
Package: luma
Version: $VERSION
Architecture: $DEB_ARCH
Maintainer: B612S <B612S@users.noreply.github.com>
Section: video
Priority: optional
Homepage: https://github.com/B612S/luma
Installed-Size: $INSTALLED_SIZE
Depends: libc6, libstdc++6
Recommends: libqt6core6, libqt6gui6, libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, libqt6svg6, libqt6waylandclient6, libmpv2, libavformat61, libavcodec61, libswscale8, libavutil59, libtag1v5, libwayland-client0, libxkbcommon0, qml6-module-org-kde-kirigami, qml6-module-org-kde-qqc2-desktop-style, plasma-integration
Description: Lightweight audio and video player based on mpv and Qt/QML
 Luma is a Wayland-only video and audio player built with Qt 6, QML,
 Kirigami and libmpv.
DEB_CONTROL

dpkg-deb --build --root-owner-group "$ROOT" "$OUTPUT"
echo "Built $OUTPUT"
