# Maintainer: B612S

pkgname=luma
# VERSION is the single source of the application version (MAJOR.MINOR.PATCH).
pkgver="$(tr -d '[:space:]' < "${startdir:-$PWD}/VERSION" 2>/dev/null || true)"
pkgver="${pkgver:-1.0.0}"
# Increment for a packaging-only rebuild; reset to 1 when pkgver changes.
pkgrel=1
# Keep the package small; debug packages are not published.
options=('strip' '!debug')
pkgdesc="Lightweight audio and video player based on mpv and Qt/QML"
arch=('x86_64' 'aarch64')
url="https://github.com/B612S/luma"
license=('GPL-3.0-or-later')

depends=(
    'qt6-base>=6.9.0'
    'qt6-declarative>=6.9.0'
    'qt6-svg>=6.9.0'
    'qt6-wayland>=6.9.0'
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
    'qt6-tools>=6.9.0'
)

optdepends=(
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

    # prepare() copies the tree without .git, so pass the revision explicitly.
    local git_hash
    git_hash="$(git -C "${startdir:-$PWD}" rev-parse --short=7 HEAD 2>/dev/null)"
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DLUMA_BUILD_TESTS=OFF \
        -DLUMA_GIT_HASH="$git_hash" \
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
