#!/usr/bin/env bash
# Compile kobo_shop.app + bundle (même rôle que build.sh).
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/build.sh" "$@"
