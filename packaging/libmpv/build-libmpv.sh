#!/usr/bin/env bash
# Build the libmpv used by the AppImage: libplacebo and mpv from pinned
# releases, installed into a private prefix. libplacebo is shared here (the
# Arch package in this directory deliberately links it statically instead).
#
# This exists because the libmpv in distributions can lag behind the release
# Luma needs. Both libraries are bundled into the AppImage.
set -Eeuo pipefail

MPV_VERSION=0.42.0
LIBPLACEBO_VERSION=7.387.0
MPV_SHA256=bcfebbfa2b65a6d88dc97ea18117831072373e38b5785a2afb874ed4df5070dd
LIBPLACEBO_SHA256=f9e62d3774a9c4cfd198ec3abaa88294aaade2e31c04d5ab02bcf4abc98b1df5
MPV_URL="https://github.com/mpv-player/mpv/archive/refs/tags/v${MPV_VERSION}.tar.gz"
LIBPLACEBO_URL="https://github.com/haasn/libplacebo/archive/refs/tags/v${LIBPLACEBO_VERSION}.tar.gz"

PREFIX=""
BUILD_DIR=""
SOURCES_DIR=""
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

usage() {
    cat <<'USAGE'
Usage: build-libmpv.sh --prefix DIR [options]

Builds libplacebo 7.387.0 (shared) and mpv 0.42.0 (libmpv) into DIR.

Options:
  --prefix DIR      Installation prefix (required)
  --build-dir DIR   Build and source directory (default: DIR/build)
  --sources DIR     Directory with mpv-0.42.0.tar.gz and
                    libplacebo-7.387.0.tar.gz; downloads when omitted
  --jobs N          Parallel build jobs (default: nproc)
  -h, --help        Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="${2:?}"; shift 2 ;;
        --build-dir) BUILD_DIR="${2:?}"; shift 2 ;;
        --sources) SOURCES_DIR="${2:?}"; shift 2 ;;
        --jobs) JOBS="${2:?}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -n "$PREFIX" ]] || { usage >&2; exit 2; }
PREFIX="$(realpath -m "$PREFIX")"
BUILD_DIR="${BUILD_DIR:-$PREFIX/build}"
BUILD_DIR="$(realpath -m "$BUILD_DIR")"
SOURCES_DIR="${SOURCES_DIR:-$BUILD_DIR/sources}"
SOURCES_DIR="$(realpath -m "$SOURCES_DIR")"

for tool in meson ninja pkg-config tar sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing required tool: $tool" >&2
        exit 1
    }
done
if [[ -z "$SOURCES_DIR" || ! -f "$SOURCES_DIR/mpv-${MPV_VERSION}.tar.gz" ]]; then
    command -v curl >/dev/null 2>&1 || {
        echo "curl is required to download the sources" >&2
        exit 1
    }
fi

mkdir -p "$SOURCES_DIR" "$BUILD_DIR"

fetch() {
    local url="$1" dest="$2" sha="$3"
    if [[ ! -f "$dest" ]]; then
        echo "Downloading $(basename "$dest")..."
        curl -fL --retry 3 --retry-delay 2 --connect-timeout 20 \
            -o "$dest.part" "$url"
        mv "$dest.part" "$dest"
    fi
    printf '%s  %s\n' "$sha" "$dest" | sha256sum -c - >/dev/null || {
        echo "Checksum mismatch: $dest" >&2
        exit 1
    }
}

MPV_TARBALL="$SOURCES_DIR/mpv-${MPV_VERSION}.tar.gz"
LIBPLACEBO_TARBALL="$SOURCES_DIR/libplacebo-${LIBPLACEBO_VERSION}.tar.gz"
fetch "$MPV_URL" "$MPV_TARBALL" "$MPV_SHA256"
fetch "$LIBPLACEBO_URL" "$LIBPLACEBO_TARBALL" "$LIBPLACEBO_SHA256"

SRC_DIR="$BUILD_DIR/src"
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"
tar -xf "$MPV_TARBALL" -C "$SRC_DIR"
tar -xf "$LIBPLACEBO_TARBALL" -C "$SRC_DIR"

LIBPLACEBO_SRC="$SRC_DIR/libplacebo-${LIBPLACEBO_VERSION}"
MPV_SRC="$SRC_DIR/mpv-${MPV_VERSION}"
[[ -d "$LIBPLACEBO_SRC" && -d "$MPV_SRC" ]] || {
    echo "Unexpected source tree layout in $SRC_DIR" >&2
    exit 1
}

echo "Building libplacebo ${LIBPLACEBO_VERSION} (shared)..."
rm -rf "$BUILD_DIR/libplacebo"
meson setup "$BUILD_DIR/libplacebo" "$LIBPLACEBO_SRC" \
    --prefix="$PREFIX" \
    --libdir=lib \
    --buildtype=release \
    -Ddefault_library=shared \
    -Ddemos=false \
    -Dtests=false \
    -Dbench=false \
    -Dfuzz=false \
    -Dvulkan=enabled \
    -Dopengl=enabled \
    -Dglslang=enabled \
    -Dshaderc=enabled \
    -Dlcms=enabled \
    -Ddovi=enabled \
    -Dlibdovi=enabled \
    -Dunwind=enabled \
    -Dxxhash=enabled
meson compile -C "$BUILD_DIR/libplacebo" -j "$JOBS"
meson install -C "$BUILD_DIR/libplacebo" --no-rebuild

echo "Building mpv ${MPV_VERSION} (libmpv)..."
rm -rf "$BUILD_DIR/mpv"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
meson setup "$BUILD_DIR/mpv" "$MPV_SRC" \
    --prefix="$PREFIX" \
    --libdir=lib \
    --buildtype=release \
    --auto-features=auto \
    -Dgpl=true \
    -Dcplayer=false \
    -Dlibmpv=true \
    -Dbuild-date=false \
    -Dtests=false \
    -Dfuzzers=false \
    -Dgl-x11=enabled \
    -Dcaca=disabled \
    -Dcdda=enabled \
    -Ddvbin=enabled \
    -Ddvdnav=enabled \
    -Dlibarchive=enabled \
    -Dopenal=enabled \
    -Dhtml-build=disabled \
    -Dmanpage-build=disabled \
    -Dpdf-build=disabled
meson compile -C "$BUILD_DIR/mpv" -j "$JOBS"
meson install -C "$BUILD_DIR/mpv" --no-rebuild

# libmpv is shared here, so private static dependencies are irrelevant and can
# confuse consumers that use pkg-config --static.
sed -i -e '/Requires\.private/d' -e '/Libs\.private/d' \
    "$PREFIX/lib/pkgconfig/mpv.pc"

[[ -f "$PREFIX/lib/libmpv.so" ]] || {
    echo "libmpv was not installed into $PREFIX/lib" >&2
    exit 1
}
[[ -f "$PREFIX/lib/libplacebo.so" ]] || {
    echo "libplacebo was not installed into $PREFIX/lib" >&2
    exit 1
}

echo "libmpv $(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --modversion mpv) installed in $PREFIX"
echo "libplacebo $(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --modversion libplacebo) installed in $PREFIX"
