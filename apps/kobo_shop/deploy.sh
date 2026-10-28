#!/usr/bin/env bash
# Déploie kobo_shop.app + bundle applications/kobo_shop/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIST="$ROOT/apps/kobo_shop/dist"
BUNDLE="$DIST/kobo_shop"

DEST=""
# Cherche le montage (parfois déjà présent mais pas listé tout de suite)
for _try in 1 2 3; do
  for d in /media/"$USER"/*/applications /run/media/"$USER"/*/applications; do
    [[ -d "$d" ]] || continue
    case "$d" in *Ventoy*) continue ;; esac
    DEST="$d"
    break 2
  done
  # Tentative udisks si le périphérique est branché mais pas monté
  if [[ $_try -eq 1 ]]; then
    for dev in /dev/sdb /dev/sdc /dev/sdd; do
      [[ -b "$dev" ]] || continue
      udisksctl mount -b "$dev" >/dev/null 2>&1 || true
    done
    sleep 1
  else
    sleep 0.5
  fi
done

if [[ -z "$DEST" ]]; then
  echo "Liseuse non montée (pas de .../applications)." >&2
  echo "Branchez en mode USB stockage, attendez le montage, puis relancez." >&2
  lsblk -o NAME,LABEL,SIZE,MOUNTPOINT 2>/dev/null | head -20 >&2 || true
  exit 1
fi

if [[ ! -f "$DIST/kobo_shop.app" || ! -d "$BUNDLE" ]]; then
  echo "Build manquant — lance d'abord ./apps/kobo_shop/build.sh" >&2
  exit 1
fi

echo "→ $DEST"
cp -f "$DIST/kobo_shop.app" "$DEST/"
rm -rf "$DEST/kobo_shop"
cp -a "$BUNDLE" "$DEST/"

# Icône lanceur : lettre K — applications/icons/ + copie à côté du .app (compat)
mkdir -p "$DEST/icons"
if [[ -f "$DIST/icons/kobo_shop.bmp" ]]; then
  cp -f "$DIST/icons/kobo_shop.bmp" "$DIST/icons/kobo_shop_f.bmp" "$DEST/icons/"
  cp -f "$DIST/icons/kobo_shop.bmp" "$DEST/kobo_shop.bmp"
  cp -f "$DIST/icons/kobo_shop_f.bmp" "$DEST/kobo_shop_f.bmp"
fi

# Nettoyage ancien layout à plat (optionnel)
rm -f "$DEST/adept_activate" "$DEST/acsmdownloader" "$DEST/ca-bundle.crt"
rm -rf "$DEST/adept_libs"
# hello test
rm -f "$DEST/hello_kobo.app"

cmp "$DIST/kobo_shop.app" "$DEST/kobo_shop.app"
echo "OK — apps visibles : kobo_shop.app (+ icône lanceur)"
ls -la "$DEST/kobo_shop.app" "$DEST/kobo_shop" | head -20
ls -la "$DEST/icons/kobo_shop"*.bmp 2>/dev/null || true
