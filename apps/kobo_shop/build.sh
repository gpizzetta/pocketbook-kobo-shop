#!/usr/bin/env bash
# Build kobo_shop.app + bundle helpers sous dist/kobo_shop/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
SDK_ROOT="${SDK_ROOT:-$ROOT/third_party/SDK-B288-6.10}"
SYSROOT="$SDK_ROOT/usr/arm-obreey-linux-gnueabi/sysroot"
BUILD_DIR="$APP_DIR/build"
DIST_DIR="$APP_DIR/dist"
BUNDLE="$DIST_DIR/kobo_shop"

if [[ ! -d "$SDK_ROOT" ]]; then
  echo "SDK introuvable: $SDK_ROOT" >&2
  exit 1
fi

export PATH="$SDK_ROOT/usr/bin:$PATH"

cmake -S "$APP_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDK_ROOT/share/cmake/arm_conf.cmake" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j"$(nproc)"

# Nettoyer l'ancien layout à plat dans dist/
rm -f "$DIST_DIR/adept_activate" "$DIST_DIR/acsmdownloader" "$DIST_DIR/ca-bundle.crt"
rm -rf "$DIST_DIR/adept_libs"

mkdir -p "$BUNDLE/adept_libs"
cp -f "$BUILD_DIR/kobo_shop.app" "$DIST_DIR/kobo_shop.app"

# Helpers ADEPT ARM (libgourou)
if [[ -x "$ROOT/tools/acsm_fulfill/dist_arm/adept_activate" ]]; then
  cp -f "$ROOT/tools/acsm_fulfill/dist_arm/adept_activate" \
        "$ROOT/tools/acsm_fulfill/dist_arm/acsmdownloader" "$BUNDLE/"
fi

# Libs runtime OpenSSL3 / curl (absentes du firmware)
for lib in libssl.so.3 libcrypto.so.3 libcurl.so.4 libcares.so.2 \
           libz.so.1 libatomic.so.1 libstdc++.so.6 libgcc_s.so.1; do
  f=$(find "$SYSROOT" -name "$lib" 2>/dev/null | head -1)
  if [[ -n "$f" ]]; then
    cp -aL "$f" "$BUNDLE/adept_libs/"
  fi
done

# CA bundle
CACERTS="$SYSROOT/usr/share/ca-certificates/mozilla"
if [[ -d "$CACERTS" ]]; then
  cat "$CACERTS"/*.crt > "$BUNDLE/ca-bundle.crt"
fi

# Icônes lanceur + UI → bundle applications/kobo_shop/icons/
ICONS_SRC="$APP_DIR/assets/icons"
if [[ -f "$ICONS_SRC/kobo_shop.bmp" ]]; then
  mkdir -p "$BUNDLE/icons"
  cp -f "$ICONS_SRC/kobo_shop.bmp" "$ICONS_SRC/kobo_shop_f.bmp" "$BUNDLE/icons/"
  if [[ -d "$ICONS_SRC/ui" ]]; then
    cp -f "$ICONS_SRC/ui/"*.bmp "$BUNDLE/icons/" 2>/dev/null || true
  fi
else
  echo "Note: pas d'icônes dans assets/icons/ — lance scripts/generate_icons.py" >&2
fi
# Plus de dist/icons séparé
rm -rf "$DIST_DIR/icons"

echo "Built: $DIST_DIR/kobo_shop.app + bundle $BUNDLE/"
file "$DIST_DIR/kobo_shop.app"
ls -la "$DIST_DIR" "$BUNDLE"
echo
echo "Déploiement USB :"
echo "  ./apps/kobo_shop/deploy.sh"
echo "  # → applications/kobo_shop.app + applications/kobo_shop/{libs,icons,…}"
echo "  # données runtime : /mnt/ext1/kobo_shop_data/ (créé par l'app)"
