#!/usr/bin/env bash
set -euo pipefail

CC=i686-w64-mingw32-g++
WINDRES=i686-w64-mingw32-windres
SRC=src
OUT=build-win32
CFLAGS="-std=c++17 -O2 -DUNICODE -D_UNICODE -DNOMINMAX -I$SRC"
LIBS="-lshell32 -lcomctl32 -lcomdlg32"
# mcfgthread (threading runtime used by nixpkgs' mingw gcc)
MCFG=/nix/store/qic29l8an3j0p18s5ywnblnzsah2havf-mcfgthread-i686-w64-mingw32-2.3.1/lib

rm -rf "$OUT"
mkdir -p "$OUT"

echo "== resource =="
$WINDRES -I res res/app.rc "$OUT/app.res.o"

echo "== compile =="
$CC $CFLAGS -c $SRC/main.cpp       -o "$OUT/main.o"
$CC $CFLAGS -c $SRC/strutil.cpp    -o "$OUT/strutil.o"
$CC $CFLAGS -c $SRC/storage.cpp    -o "$OUT/storage.o"
$CC $CFLAGS -c $SRC/winapi.cpp     -o "$OUT/winapi.o"
$CC $CFLAGS -c $SRC/ataregs.cpp    -o "$OUT/ataregs.o"
$CC $CFLAGS -c $SRC/directregs.cpp -o "$OUT/directregs.o"
$CC $CFLAGS -c $SRC/nvme.cpp       -o "$OUT/nvme.o"
$CC $CFLAGS -c $SRC/comparison.cpp -o "$OUT/comparison.o"
$CC $CFLAGS -c $SRC/report.cpp     -o "$OUT/report.o"
$CC $CFLAGS -c $SRC/appwindow.cpp  -o "$OUT/appwindow.o"

echo "== link =="
$CC -mwindows -static -L"$MCFG" \
  "$OUT"/*.o \
  -o hdssd-analyzer.exe \
  $LIBS

echo "== done =="
ls -lh hdssd-analyzer.exe