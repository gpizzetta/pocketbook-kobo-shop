#!/usr/bin/env bash
# Déploie kobo_shop.app + bundle applications/kobo_shop/
# Données runtime = /mnt/ext1/kobo_shop_data/ (créé par l'app, pas ici)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIST="$ROOT/apps/kobo_shop/dist"
BUNDLE="$DIST/kobo_shop"

DEST=""
for _try in 1 2 3; do
  for d in /media/"$USER"/*/applications /run/media/"$USER"/*/applications; do
    [[ -d "$d" ]] || continue
    case "$d" in *Ventoy*) continue ;; esac
    DEST="$d"
    break 2
  done
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
  echo "Build manquant — lance d'abord ./apps/kobo_shop/compile.sh" >&2
  exit 1
fi

ARM="$ROOT/tools/acsm_fulfill/dist_arm"
for bin in adept_activate acsmdownloader adept_remove; do
  if [[ ! -f "$ARM/$bin" ]]; then
    echo "Binaire manquant: $ARM/$bin" >&2
    echo "Lance d'abord ./tools/acsm_fulfill/build_arm.sh" >&2
    exit 1
  fi
done

echo "→ $DEST"
cp -f "$DIST/kobo_shop.app" "$DEST/"
rm -rf "$DEST/kobo_shop"
cp -a "$BUNDLE" "$DEST/"
# Dernière version ARM (écrase ce que build.sh a mis dans le bundle)
cp -f "$ARM/adept_activate" "$ARM/acsmdownloader" "$ARM/adept_remove" \
  "$DEST/kobo_shop/"

# Lanceur Era : BMP 8-bit dans applications/icons/ + clé @kobo_shop dans la langue.
# Le titre de view.json seul laisse le nom « @kobo_shop » (clé de traduction manquante).
MNT="$(cd "$DEST/.." && pwd)"
VIEW_DIR="$MNT/system/config/desktop"
VIEW_JSON="$VIEW_DIR/view.json"
mkdir -p "$VIEW_DIR" "$DEST/icons"
if [[ ! -f "$BUNDLE/icons/kobo_shop.bmp" || ! -f "$BUNDLE/icons/kobo_shop_f.bmp" ]]; then
  echo "ERREUR: icône absente dans le bundle ($BUNDLE/icons/)" >&2
  exit 1
fi
cp -f "$BUNDLE/icons/kobo_shop.bmp" "$BUNDLE/icons/kobo_shop_f.bmp" "$DEST/icons/"
python3 - "$VIEW_JSON" <<'PY'
import json, sys, os, shutil

path = sys.argv[1]
# Exemple firmware : icon = { "path": "applications/icons/...." } (relatif à /mnt/ext1)
entry = {
    "path": "/mnt/ext1/applications/kobo_shop.app",
    "title": "Kobo Shop",
    "icon": {"path": "applications/icons/kobo_shop.bmp"},
    "focused_icon": {"path": "applications/icons/kobo_shop_f.bmp"},
    "visible": "1",
}

DEFAULT_GENERAL = [
    "PB_Dictionary",
    "PB_Library",
    "PB_Settings",
    "PB_Music",
    "PB_Browser",
    "PB_Notes",
    "PB_Calculator",
    "PB_Gallery",
    "U_kobo_shop",
]

def ensure_groups(data):
    view = data.setdefault("view", {})
    if not isinstance(view, dict):
        view = {}
        data["view"] = view
    groups = view.get("groups")
    if not isinstance(groups, list) or not groups:
        # Structure type firmware 6.x — ne pas laisser @General vide
        groups = [
            {"title": "@General", "sort": "title", "apps": list(DEFAULT_GENERAL)},
            {"title": "@Services", "sort": "title", "apps": []},
            {"title": "@Games", "sort": "title", "apps": []},
        ]
        view["groups"] = groups
    return groups

data = {}
if os.path.isfile(path) and os.path.getsize(path) > 0:
    shutil.copy2(path, path + ".bak")
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except json.JSONDecodeError:
        data = {}

if not isinstance(data, dict):
    data = {}
data.setdefault("version", "5")

apps = data.setdefault("applications", {})
if not isinstance(apps, dict):
    apps = {}
    data["applications"] = apps
apps["U_kobo_shop"] = entry

groups = ensure_groups(data)
placed = False
for g in groups:
    if not isinstance(g, dict):
        continue
    arr = g.setdefault("apps", [])
    if not isinstance(arr, list):
        continue
    if "U_kobo_shop" in arr:
        placed = True
        break
for g in groups:
    if placed:
        break
    if not isinstance(g, dict):
        continue
    title = str(g.get("title") or "")
    arr = g.setdefault("apps", [])
    if not isinstance(arr, list):
        continue
    if "General" in title or title == "@General" or "Application" in title:
        if "U_kobo_shop" not in arr:
            arr.append("U_kobo_shop")
        placed = True
        break
if not placed and groups:
    arr = groups[0].setdefault("apps", [])
    if isinstance(arr, list) and "U_kobo_shop" not in arr:
        arr.append("U_kobo_shop")

with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write("\n")
print("view.json OK — U_kobo_shop (icônes: applications/icons/)")
PY

# @kobo_shop → « Kobo Shop » (system/language/<langue>.txt, langue = global.cfg)
LANG_CODE="fr"
CFG="$MNT/system/profiles/default/config/global.cfg"
if [[ -f "$CFG" ]]; then
  _code="$(sed -n 's/^language=//p' "$CFG" | head -1 | tr -d '[:space:]')"
  [[ -n "$_code" ]] && LANG_CODE="$_code"
fi
python3 - "$MNT/system/language/${LANG_CODE}.txt" "$LANG_CODE" <<'PY'
import sys
path, code = sys.argv[1], sys.argv[2]
headers = {
    "fr": "French", "en": "English", "de": "German", "es": "Spanish",
    "it": "Italian", "ru": "Russian", "uk": "Ukrainian", "pl": "Polish",
    "nl": "Dutch", "pt": "Portuguese", "cs": "Czech", "hu": "Hungarian",
}
header = headers.get(code, "French")
key = "@kobo_shop=Kobo Shop"
try:
    lines = open(path, encoding="utf-8").read().splitlines()
except FileNotFoundError:
    lines = []
if not lines:
    lines = [header]
out = []
found = False
for line in lines:
    if line.startswith("@kobo_shop="):
        out.append(key)
        found = True
    else:
        out.append(line)
if not found:
    out.append(key)
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out) + "\n")
print(f"langue OK — {path} ({key})")
PY

# Nettoyage anciens emplacements
rm -f "$DEST/adept_activate" "$DEST/acsmdownloader" "$DEST/ca-bundle.crt"
rm -rf "$DEST/adept_libs"
rm -f "$DEST/hello_kobo.app"
# Anciens BMP à côté du .app (le lanceur lit applications/icons/)
rm -f "$DEST/kobo_shop.bmp" "$DEST/kobo_shop_f.bmp"
# Ancien data sous applications — ne pas toucher kobo_shop_data à la racine
rm -rf "$DEST/kobo_shop_data"

cmp "$DIST/kobo_shop.app" "$DEST/kobo_shop.app"
echo "OK — install : kobo_shop.app + applications/kobo_shop/"
echo "    ADEPT    : adept_activate, acsmdownloader, adept_remove (dist_arm)"
echo "    données  : /mnt/ext1/kobo_shop_data/ (géré par l'app)"
ls -la "$DEST/kobo_shop.app" "$DEST/kobo_shop" | head -25
ls -la "$DEST/kobo_shop/adept_activate" "$DEST/kobo_shop/acsmdownloader" \
  "$DEST/kobo_shop/adept_remove"
ls -la "$DEST/icons/kobo_shop"*.bmp 2>/dev/null || true
