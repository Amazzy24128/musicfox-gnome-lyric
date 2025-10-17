#!/usr/bin/env bash
set -euo pipefail

CFLAGS="$(pkg-config --cflags gio-2.0 gobject-2.0 glib-2.0)"
LIBS="$(pkg-config --libs gio-2.0 gobject-2.0 glib-2.0)"
GEN_C="music-info-service-generated.c"
GEN_O="music-info-service-generated.o"
SRC="dbus_service.cpp"
OUT="music-info-service"

echo "Using CFLAGS: $CFLAGS"
echo "Using LIBS: $LIBS"

if [ ! -f "$GEN_C" ]; then
  echo "Error: $GEN_C not found in current directory."
  exit 1
fi

echo "Compiling $GEN_C -> $GEN_O"
gcc -std=gnu11 -O2 -Wall $CFLAGS -c "$GEN_C" -o "$GEN_O"

echo "Compiling and linking $SRC + $GEN_O -> $OUT"
g++ -std=c++17 -O2 -Wall $CFLAGS "$SRC" "$GEN_O" -o "$OUT" $LIBS -pthread

echo "Build finished: ./$OUT"