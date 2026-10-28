#!/usr/bin/env bash
# Cross-compile libgourou utils (adept_activate, acsmdownloader) for PocketBook B288.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK="${SDK_ROOT:-$ROOT/third_party/SDK-B288-6.10}"
SYSROOT="$SDK/usr/arm-obreey-linux-gnueabi/sysroot"
WORK="$ROOT/third_party/adept_arm"
SRC="$WORK/src"
PREFIX="$WORK/prefix"
OUT="$ROOT/tools/acsm_fulfill/dist_arm"
APP_DIST="$ROOT/apps/kobo_shop/dist"
JOBS="$(nproc)"

export PATH="$SDK/usr/bin:$PATH"
TRIPLE=arm-obreey-linux-gnueabi
CC="${TRIPLE}-clang"
CXX_BUILD="${TRIPLE}-clang++"
CXX_LINK="${TRIPLE}-g++"
AR="${TRIPLE}-ar"

COMMON_FLAGS="--sysroot=${SYSROOT} -O2 -fPIC"

echo "==> Cross-build ADEPT ARM → $OUT"

mkdir -p "$SRC" "$PREFIX"/{lib,include} "$OUT"

cd "$SRC"

# --- sources ---
[[ -d libgourou/.git ]] || git clone --depth 1 https://git.mopalinski.com/soutade/libgourou.git
[[ -d updfparser/.git ]] || git clone --depth 1 https://git.mopalinski.com/soutade/uPDFParser.git updfparser
[[ -d pugixml ]] || git clone --depth 1 --branch v1.14 https://github.com/zeux/pugixml.git
if [[ ! -d libzip ]]; then
  curl -fL -o libzip.tgz https://github.com/nih-at/libzip/archive/refs/tags/v1.10.1.tar.gz
  tar xzf libzip.tgz && mv libzip-1.10.1 libzip
fi

cmake_cross() {
  local srcdir="$1"; shift
  cmake "$srcdir" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX_BUILD" \
    -DCMAKE_SYSROOT="$SYSROOT" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    "$@"
}

# --- pugixml ---
if [[ ! -f "$PREFIX/lib/libpugixml.a" ]]; then
  echo "==> pugixml"
  mkdir -p pugixml/build_arm && cd pugixml/build_arm
  cmake_cross ..
  cmake --build . -j"$JOBS" && cmake --install .
  cd "$SRC"
fi

# --- libzip ---
if [[ ! -f "$PREFIX/lib/libzip.a" ]]; then
  echo "==> libzip"
  mkdir -p libzip/build_arm && cd libzip/build_arm
  cmake_cross .. \
    -DBUILD_TOOLS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF \
    -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_OPENSSL=ON -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF
  cmake --build . -j"$JOBS" && cmake --install .
  cd "$SRC"
fi

# --- updfparser ---
if [[ ! -f updfparser/libupdfparser.a ]]; then
  echo "==> updfparser"
  cd updfparser
  make clean || true
  make CROSS="${TRIPLE}-" BUILD_STATIC=1 BUILD_SHARED=0 \
    CXXFLAGS="${COMMON_FLAGS} -I./include" -j"$JOBS"
  cd "$SRC"
fi

# --- libgourou ---
echo "==> libgourou"
cd libgourou
make clean || true
rm -rf lib obj
mkdir -p lib
ln -sfn "$SRC/updfparser" lib/updfparser
EXTRA_INC="-I${PREFIX}/include -I./lib/updfparser/include -I./include"
make CROSS="${TRIPLE}-" BUILD_STATIC=1 BUILD_SHARED=0 BUILD_UTILS=0 \
  CXXFLAGS="${COMMON_FLAGS} ${EXTRA_INC}" -j"$JOBS"
# Rebuild archive without nested .a
rm -f libgourou.a
"$AR" rcs libgourou.a obj/*.o

# --- utils ---
echo "==> utils (adept_activate, acsmdownloader)"
cd utils
UTIL_INC="-I${PREFIX}/include -I${SRC}/libgourou/include -I${SRC}/updfparser/include"
rm -f *.o utils.a adept_activate acsmdownloader
"$CXX_BUILD" $COMMON_FLAGS $UTIL_INC -Wall -c \
  drmprocessorclientimpl.cpp utils_common.cpp
"$AR" crs utils.a drmprocessorclientimpl.o utils_common.o

link_one() {
  local bin="$1"
  "$CXX_LINK" $COMMON_FLAGS $UTIL_INC -Wall \
    "${bin}.cpp" utils.a \
    ../libgourou.a \
    "$SRC/updfparser/libupdfparser.a" \
    "$PREFIX/lib/libzip.a" \
    "$PREFIX/lib/libpugixml.a" \
    -lcurl -lssl -lcrypto -lcares -lz -latomic -ldl -lpthread \
    "$SYSROOT/usr/lib/libm.a" \
    -o "$bin"
}

link_one adept_activate
link_one acsmdownloader

mkdir -p "$OUT" "$APP_DIST"
cp -f adept_activate acsmdownloader "$OUT/"
cp -f adept_activate acsmdownloader "$APP_DIST/"
"${TRIPLE}-strip" "$OUT/adept_activate" "$OUT/acsmdownloader" \
  "$APP_DIST/adept_activate" "$APP_DIST/acsmdownloader"

echo
echo "OK"
file "$OUT/adept_activate" "$OUT/acsmdownloader"
ls -la "$OUT"
echo
echo "Copie sur liseuse:"
echo "  cp $OUT/adept_activate $OUT/acsmdownloader $APP_DIST/kobo_shop.app /media/\$USER/*/applications/"
