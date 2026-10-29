#!/usr/bin/env bash
# Native x86_64 acsmdownloader for command-line tests on the PC.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/third_party/adept_arm/src"
WORK="$ROOT/third_party/adept_x86"
PREFIX="$WORK/prefix"
OBJ="$WORK/obj"
OUT="$ROOT/tools/acsm_fulfill/dist_x86"
JOBS="$(nproc)"

if [[ ! -f "$SRC/libgourou/utils/acsmdownloader.cpp" ]]; then
  echo "Sources manquantes: $SRC/libgourou (lance d'abord tools/acsm_fulfill/build_arm.sh)" >&2
  exit 1
fi

mkdir -p "$PREFIX/include" "$PREFIX/lib" "$OBJ" "$OUT" "$WORK/debs"

# Headers only: runtime libcurl/libzip are already on the system, -dev is not.
if [[ ! -f "$WORK/sysinc/usr/include/x86_64-linux-gnu/curl/curl.h" || ! -f "$WORK/sysinc/usr/include/zip.h" ]]; then
  echo "==> headers curl + libzip (apt download, pas d'install root)"
  (
    cd "$WORK/debs"
    apt-get download libcurl4-openssl-dev libzip-dev
  )
  rm -rf "$WORK/sysinc"
  mkdir -p "$WORK/sysinc"
  for deb in "$WORK/debs"/*.deb; do
    dpkg-deb -x "$deb" "$WORK/sysinc"
  done
fi

if [[ ! -f "$PREFIX/lib/libpugixml.a" ]]; then
  echo "==> pugixml"
  g++ -O2 -fPIC -I"$SRC/pugixml/src" -c "$SRC/pugixml/src/pugixml.cpp" -o "$OBJ/pugixml.o"
  ar rcs "$PREFIX/lib/libpugixml.a" "$OBJ/pugixml.o"
fi
cp -f "$SRC/pugixml/src/pugixml.hpp" "$SRC/pugixml/src/pugiconfig.hpp" "$PREFIX/include/"

echo "==> updfparser"
rm -f "$OBJ"/uPDF*.o "$PREFIX/lib/libupdfparser.a"
g++ -O2 -fPIC -Wall -I"$SRC/updfparser/include" -c \
  "$SRC/updfparser/src/uPDFParser.cpp" -o "$OBJ/uPDFParser.o"
g++ -O2 -fPIC -Wall -I"$SRC/updfparser/include" -c \
  "$SRC/updfparser/src/uPDFTypes.cpp" -o "$OBJ/uPDFTypes.o"
ar rcs "$PREFIX/lib/libupdfparser.a" "$OBJ/uPDFParser.o" "$OBJ/uPDFTypes.o"

INC=(
  -I"$SRC/libgourou/include"
  -I"$SRC/updfparser/include"
  -I"$PREFIX/include"
  -I"$WORK/sysinc/usr/include"
  -I"$WORK/sysinc/usr/include/x86_64-linux-gnu"
)
CXXFLAGS=(-O2 -fPIC -Wall "${INC[@]}")

echo "==> libgourou"
rm -f "$OBJ"/lg_*.o "$PREFIX/lib/libgourou.a"
for src in libgourou user device fulfillment_item loan_token bytearray; do
  g++ "${CXXFLAGS[@]}" -c "$SRC/libgourou/src/${src}.cpp" -o "$OBJ/lg_${src}.o"
done
ar rcs "$PREFIX/lib/libgourou.a" "$OBJ"/lg_*.o

echo "==> acsmdownloader"
rm -f "$OBJ"/utils_*.o "$OUT/acsmdownloader"
g++ "${CXXFLAGS[@]}" -c "$SRC/libgourou/utils/drmprocessorclientimpl.cpp" -o "$OBJ/utils_client.o"
g++ "${CXXFLAGS[@]}" -c "$SRC/libgourou/utils/utils_common.cpp" -o "$OBJ/utils_common.o"
g++ "${CXXFLAGS[@]}" -c "$SRC/libgourou/utils/acsmdownloader.cpp" -o "$OBJ/acsmdownloader.o"
g++ -O2 -o "$OUT/acsmdownloader" \
  "$OBJ/acsmdownloader.o" "$OBJ/utils_client.o" "$OBJ/utils_common.o" \
  "$PREFIX/lib/libgourou.a" \
  "$PREFIX/lib/libupdfparser.a" \
  "$PREFIX/lib/libpugixml.a" \
  -L/usr/lib/x86_64-linux-gnu -l:libcurl.so.4 -l:libzip.so.5 \
  -lssl -lcrypto -lz -ldl -lpthread

# Build adept_remove
rm -f "$OBJ"/adept_remove.o "$OUT/adept_remove"
g++ "${CXXFLAGS[@]}" -c "$SRC/libgourou/utils/adept_remove.cpp" -o "$OBJ/adept_remove.o"
g++ -O2 -o "$OUT/adept_remove" \
  "$OBJ/adept_remove.o" "$OBJ/utils_client.o" "$OBJ/utils_common.o" \
  "$PREFIX/lib/libgourou.a" \
  "$PREFIX/lib/libupdfparser.a" \
  "$PREFIX/lib/libpugixml.a" \
  -L/usr/lib/x86_64-linux-gnu -l:libcurl.so.4 -l:libzip.so.5 \
  -lssl -lcrypto -lz -ldl -lpthread

echo
echo "OK"
file "$OUT/acsmdownloader"
file "$OUT/adept_remove"
ls -la "$OUT/acsmdownloader"
ls -la "$OUT/adept_remove"
echo
echo "Essai:"
echo "  $OUT/acsmdownloader --help"
