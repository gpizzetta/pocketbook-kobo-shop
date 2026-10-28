#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK_ROOT="${SDK_ROOT:-$ROOT/third_party/SDK-B288-6.10}"
BUILD_DIR="$(cd "$(dirname "$0")" && pwd)/build"

if [[ ! -d "$SDK_ROOT" ]]; then
  echo "SDK introuvable: $SDK_ROOT" >&2
  exit 1
fi

export PATH="$SDK_ROOT/usr/bin:$PATH"

cmake -S "$(dirname "$0")" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$SDK_ROOT/share/cmake/arm_conf.cmake" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "Built: $BUILD_DIR/hello_kobo.app"
file "$BUILD_DIR/hello_kobo.app"
