#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/opencv-4.10.0"
BUILD="$ROOT/build-opencv"
DEST="$ROOT/opencv-libs"

TCHAIN_ROOT="/nix/store/3fr3xdg8jwyzr5a5y13fzvv9g47yw926-i686-w64-mingw32-gcc-15.2.0"
BINUTILS="/nix/store/6zigdbx74ya8b85aj4vswpbgazpkfaxv-i686-w64-mingw32-binutils-wrapper-2.44"
MAKE="/nix/store/5lyy21zm97pj8l67r2xapdkxpxb6v6y0-gnumake-4.4.1/bin/make"
NINJA="/nix/store/wc7a9w61k9bqvdnksx7mfj49x55nl8i8-ninja-1.13.2/bin/ninja"

export PATH="$BINUTILS/bin:$TCHAIN_ROOT/bin:$PATH"
export CC="$TCHAIN_ROOT/bin/i686-w64-mingw32-gcc"
export CXX="$TCHAIN_ROOT/bin/i686-w64-mingw32-g++"

rm -rf "$BUILD" "$DEST"
mkdir -p "$BUILD"

cmake -S "$SRC" -B "$BUILD" \
    -G "Unix Makefiles" \
    -DCMAKE_MAKE_PROGRAM="$MAKE" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/win32_i686.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$DEST" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_LIST=core,imgproc,imgcodecs \
    -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF \
    -DBUILD_opencv_apps=OFF \
    -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_V4L=OFF -DWITH_LIBV4L=OFF \
    -DWITH_DSHOW=OFF -DWITH_MSMF=OFF \
    -DWITH_OPENCL=OFF -DWITH_OPENCL_SVM=OFF -DWITH_OPENCLAMDFFT=OFF -DWITH_OPENCLAMDBLAS=OFF \
    -DWITH_TBB=OFF -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_EIGEN=OFF -DWITH_QUIRC=OFF \
    -DWITH_VA=OFF -DWITH_VA_INTEL=OFF -DWITH_XINE=OFF -DWITH_GDCM=OFF \
    -DWITH_WEBP=OFF -DWITH_PNG=OFF -DWITH_TIFF=OFF -DWITH_OPENEXR=OFF -DWITH_JASPER=OFF \
    -DWITH_JPEG=ON -DBUILD_JPEG=ON \
    -DWITH_HALIDE=OFF \
    -DENABLE_PRECOMPILED_HEADERS=OFF

echo "== build =="
"$MAKE" -C "$BUILD" -j"$(nproc)"

echo "== install =="
"$MAKE" -C "$BUILD" install

echo "== done =="
ls -la "$DEST"