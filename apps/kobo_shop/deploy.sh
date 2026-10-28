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

echo "→ $DEST"
cp -f "$DIST/kobo_shop.app" "$DEST/"
rm -rf "$DEST/kobo_shop"
cp -a "$BUNDLE" "$DEST/"

# Icône menu Applications : view.json au format Era (version + view.groups)
# Les BMP restent dans applications/kobo_shop/icons/ (pas applications/icons/)
MNT="$(cd "$DEST/.." && pwd)"
VIEW_DIR="$MNT/system/config/desktop"
VIEW_JSON="$VIEW_DIR/view.json"
mkdir -p "$VIEW_DIR"
if [[ ! -f "$DEST/kobo_shop/icons/kobo_shop.bmp" ]]; then
  echo "ERREUR: icône absente dans bundle ($DEST/kobo_shop/icons/)" >&2
  exit 1
fi
python3 - "$VIEW_JSON" <<'PY'
import json, sys, os, shutil

path = sys.argv[1]
# Chemins relatifs à /mnt/ext1 — format objet {"path":...} accepté par le FW
entry = {
    "path": "/mnt/ext1/applications/kobo_shop.app",
    "title": "Kobo Shop",
    "icon": {"path": "applications/kobo_shop/icons/kobo_shop.bmp"},
    "focused_icon": {"path": "applications/kobo_shop/icons/kobo_shop_f.bmp"},
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
print("view.json OK — U_kobo_shop + view.groups (icônes: applications/kobo_shop/icons/)")
PY

# Nettoyage anciens emplacements
rm -f "$DEST/adept_activate" "$DEST/acsmdownloader" "$DEST/ca-bundle.crt"
rm -rf "$DEST/adept_libs"
rm -f "$DEST/hello_kobo.app"
rm -f "$DEST/kobo_shop.bmp" "$DEST/kobo_shop_f.bmp"
# Ancien dossier applications/icons (icônes app uniquement)
rm -f "$DEST/icons/kobo_shop.bmp" "$DEST/icons/kobo_shop_f.bmp"
# Ancien data sous applications — ne pas toucher kobo_shop_data à la racine
rm -rf "$DEST/kobo_shop_data"

cmp "$DIST/kobo_shop.app" "$DEST/kobo_shop.app"
echo "OK — install : kobo_shop.app + applications/kobo_shop/"
echo "    données  : /mnt/ext1/kobo_shop_data/ (géré par l'app)"
ls -la "$DEST/kobo_shop.app" "$DEST/kobo_shop" | head -25
ls -la "$DEST/kobo_shop/icons/kobo_shop"*.bmp 2>/dev/null || true
