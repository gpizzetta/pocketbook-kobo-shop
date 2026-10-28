#!/usr/bin/env bash
# Raccourci racine → compile l'app PocketBook kobo_shop.
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/apps/kobo_shop/build.sh" "$@"
