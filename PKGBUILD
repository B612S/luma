pkgname=luma
pkgver=1.0.0
pkgrel=1
pkgdesc="Lightweight video player"
arch=('x86_64')
url="https://github.com/B612S/luma"
license=('GPL-3.0-or-later')

depends=(
    'qt6-base'
    'qt6-declarative'
    'qt6-svg'
    'qt6-wayland'
    'mpv'
    'ffmpeg'
    'kirigami'
    'hicolor-icon-theme'
    'plasma-integration'
    'qqc2-desktop-style'
)

makedepends=(
    'cmake'
    'ninja'
    'extra-cmake-modules'
    'qt6-tools'
)

source=()
sha256sums=()

options=(!strip !debug)

prepare() {
    mkdir -p "$srcdir/luma_qml_src"
    rsync -a --exclude 'build' "$startdir/luma_qml_src/" "$srcdir/luma_qml_src/"
}

build() {
    cd "$srcdir/luma_qml_src"

    rm -rf build

    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -G Ninja
    cmake --build build
}

package() {
    cd "$srcdir/luma_qml_src"
    DESTDIR="$pkgdir" cmake --install build
    rm -f "$pkgdir/usr/lib/qt6/qml/luma/luma_qml_module_dir_map.qrc"
    install -Dm644 "$srcdir/luma_qml_src/luma.desktop" \
        "$pkgdir/usr/share/applications/luma.desktop"
    install -Dm644 "$srcdir/luma_qml_src/assets/luma.png" \
        "$pkgdir/usr/share/icons/hicolor/256x256/apps/luma.png"
}
