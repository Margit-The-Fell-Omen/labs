#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
MKISOFS="/nix/store/jk46d9xgyphz487vylfl0rjkz10av34p-cdrtools-3.02a09/bin/mkisofs"
OUT="/home/ushki/vm/lab4.iso"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/lab4"

cp "$ROOT/webcam-app.exe" "$STAGE/lab4/"
cp -r "$ROOT/src" "$ROOT/res" "$STAGE/lab4/"
cp "$ROOT/build-win32.sh" "$ROOT/third_party/opencv-build.sh" "$ROOT/CMakeLists.txt" "$STAGE/lab4/"
cp "$ROOT/third_party/win32_i686.toolchain.cmake" "$STAGE/lab4/"
mkdir -p "$STAGE/lab4/third_party"
mkdir -p "$STAGE/lab4/third_party/opencv-libs" "$STAGE/lab4/third_party/build-3rdparty"
cp -r "$ROOT/third_party/opencv-libs/lib" "$ROOT/third_party/opencv-libs/include" \
      "$STAGE/lab4/third_party/opencv-libs/"
cp -r "$ROOT/third_party/build-opencv/3rdparty/lib" "$STAGE/lab4/third_party/build-3rdparty/"

find "$STAGE/lab4" -type f -name "*.o" -delete

"$MKISOFS" -quiet -R -J -V "LAB4_WEBCAM" -o "$OUT" "$STAGE/lab4"

echo "== done =="
ls -lh "$OUT"