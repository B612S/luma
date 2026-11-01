# Maintainer: B612S

pkgname=luma
# VERSION is the single source of the application version (MAJOR.MINOR.PATCH).
pkgver="$(tr -d '[:space:]' < "${startdir:-$PWD}/VERSION" 2>/dev/null || true)"
pkgver="${pkgver:-1.0.0}"
# Increment for a packaging-only rebuild; reset to 1 when pkgver changes.
pkgrel=1
pkgdesc="Lightweight audio and video player based on mpv and Qt/QML"
arch=('x86_64' 'aarch64')
url="https://github.com/B612S/luma"
license=('GPL-3.0-or-later')

depends=(
    'qt6-base'
    'qt6-declarative'
    'qt6-svg'
    'qt6-wayland'
    'wayland'
    'libxkbcommon'
    'mpv'
    'ffmpeg'
    'taglib'
    'kirigami'
    'hicolor-icon-theme'
    'plasma-integration'
    'qqc2-desktop-style'
)

makedepends=(
    'cmake'
    'ninja'
    'pkgconf'
    'qt6-tools'
)

optdepends=(
    'yt-dlp: playback of links from video sites'
    'avahi: Chromecast discovery'
)

# This PKGBUILD packages the working tree, so there is no remote source.
source=()
sha256sums=()

prepare() {
    # Copy the checkout into $srcdir without local build, VCS or packaging data.
    rm -rf "$srcdir/luma"
    mkdir -p "$srcdir/luma"
    tar -C "${startdir:-$PWD}" \
        --exclude='./.git' \
        --exclude='./src' \
        --exclude='./pkg' \
        --exclude='./build' \
        --exclude='./build-appimage' \
        --exclude='./dist' \
        --exclude='./AppDir' \
        --exclude='./squashfs-root' \
        --exclude='./.appimage-tools' \
        -cf - . | tar -C "$srcdir/luma" -xf -
}

build() {
    cd "$srcdir/luma"

    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DLUMA_BUILD_TESTS=OFF \
        -G Ninja

    cmake --build build
}

package() {
    cd "$srcdir/luma"

    # Installs the binary, QML module, translations, desktop file and icon.
    DESTDIR="$pkgdir" cmake --install build

    # This deployment map is not needed by the installed QML module.
    rm -f "$pkgdir/usr/lib/qt6/qml/luma/luma_qml_module_dir_map.qrc"
}
