#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
OUT="$ROOT/build-win32"
DEST="$ROOT/webcam-app.exe"

TCHAIN_ROOT="/nix/store/3fr3xdg8jwyzr5a5y13fzvv9g47yw926-i686-w64-mingw32-gcc-15.2.0"
BINUTILS="/nix/store/6zigdbx74ya8b85aj4vswpbgazpkfaxv-i686-w64-mingw32-binutils-wrapper-2.44"
MCFG_DEV="/nix/store/l7xspmfvr51mj9g44qdj2cpq6cs403iw-mcfgthread-i686-w64-mingw32-2.3.1-dev"
MCFG_LIB="/nix/store/qic29l8an3j0p18s5ywnblnzsah2havf-mcfgthread-i686-w64-mingw32-2.3.1"
MINGW_LIB="/nix/store/2m5s2w1hc3kf56i0mryri2fnkyvy5pa5-mingw-w64-i686-w64-mingw32-13.0.0/lib"
OPENCV="$ROOT/third_party/opencv-libs"
OPENCV3RD="$ROOT/third_party/build-opencv/3rdparty/lib"

CC="$TCHAIN_ROOT/bin/i686-w64-mingw32-g++"
WINDRES="$BINUTILS/bin/i686-w64-mingw32-windres"

export PATH="$TCHAIN_ROOT/bin:$BINUTILS/bin:$PATH"

CFLAGS="-std=c++17 -O2 -fno-omit-frame-pointer -DUNICODE -D_UNICODE -DNOMINMAX -mwindows"
CFLAGS="$CFLAGS -I$SRC -I$OPENCV/include/opencv4 -I$MCFG_DEV/include"

# DirectShow/COM + shell UI libs
DS_LIBS="-lole32 -loleaut32 -luuid -lstrmiids -lwinmm -lcomdlg32 -lshell32"

rm -rf "$OUT"
mkdir -p "$OUT"

echo "== resource =="
$WINDRES -I res res/app.rc "$OUT/app.res.o"

echo "== compile =="
for f in main strutil reportlog bmpwrite snapframe aviwriter dscamera appwindow; do
    echo "  $f.cpp"
    $CC $CFLAGS -c "$SRC/$f.cpp" -o "$OUT/$f.o"
done

echo "== link =="
$CC $CFLAGS \
  -static \
  -B"$MINGW_LIB" \
  -L"$MCFG_LIB/lib" \
  -L"$OPENCV/lib" \
  -L"$OPENCV3RD" \
  -L"$MINGW_LIB" \
  "$OUT"/main.o "$OUT"/strutil.o "$OUT"/reportlog.o "$OUT"/bmpwrite.o \
  "$OUT"/snapframe.o "$OUT"/aviwriter.o "$OUT"/dscamera.o "$OUT"/appwindow.o "$OUT"/app.res.o \
  -o "$DEST" \
  -lopencv_imgcodecs4100 -lopencv_imgproc4100 -lopencv_core4100 \
  -llibjpeg-turbo -llibopenjp2 -lzlib -lcomctl32 -lmcfgthread \
  $DS_LIBS

echo "== done =="
ls -lh "$DEST"