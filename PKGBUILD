# Maintainer: B612S
pkgname=luma
pkgver=1.0.0
pkgrel=1
pkgdesc="Lightweight video player based on mpv and Qt/QML"
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
    'taglib'
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

optdepends=(
    'yt-dlp: playback of links from video sites'
)

# Оставляем пустым. Мы скопируем файлы вручную в prepare()
source=()
sha256sums=()

options=(!strip !debug)

prepare() {
    # Создаем рабочую директорию и копируем туда всё из папки с PKGBUILD
    mkdir -p "$srcdir/luma"

    # Копируем ВСЁ, кроме папки src (чтобы не зациклиться) и build
    cp -r "$startdir"/* "$srcdir/luma/" 2>/dev/null || true
    rm -rf "$srcdir/luma/src" "$srcdir/luma/build"

    # Если вы используете rsync, можно сделать так (он аккуратнее):
    # rsync -a --exclude 'src' --exclude 'build' "$startdir/" "$srcdir/luma/"
}

build() {
    cd "$srcdir/luma"

    rm -rf build

    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -G Ninja

    cmake --build build
}

package() {
    cd "$srcdir/luma"

    DESTDIR="$pkgdir" cmake --install build

    # Убираем лишний файл
    rm -f "$pkgdir/usr/lib/qt6/qml/luma/luma_qml_module_dir_map.qrc"

    # Устанавливаем .desktop
    install -Dm644 "luma.desktop" \
        "$pkgdir/usr/share/applications/luma.desktop"

    # Устанавливаем иконку
    install -Dm644 "assets/luma.png" \
        "$pkgdir/usr/share/icons/hicolor/256x256/apps/luma.png"
}
