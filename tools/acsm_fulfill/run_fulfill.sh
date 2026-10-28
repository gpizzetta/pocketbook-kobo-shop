#!/usr/bin/env bash
# Fulfil ACSM via libgourou — activation + download UNIQUEMENT (pas de adept_remove).
# Préfère l'AppImage 0.8.10 locale ; fallback Docker avec CA de l'hôte montés.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL="$(cd "$(dirname "$0")" && pwd)"
ADEPT_DIR="${ADEPT_DIR:-$TOOL/adept}"
OUT_DIR="${OUT_DIR:-$TOOL/out}"
LG_DIR="${LIBGOUROU_DIR:-$ROOT/third_party/libgourou-0.8.10}"
IMAGE="${LIBGOUROU_IMAGE:-bcliang/docker-libgourou:ubuntu}"
CA_BUNDLE="${SSL_CERT_FILE:-/etc/ssl/certs/ca-certificates.crt}"

mkdir -p "$ADEPT_DIR" "$OUT_DIR"
export SSL_CERT_FILE="$CA_BUNDLE"
export CURL_CA_BUNDLE="$CA_BUNDLE"

usage() {
  cat <<EOF
Usage:
  $0 activate              # adept_activate (ByteBooks ID)
  $0 download <file.acsm>  # acsmdownloader only (garde le DRM Adobe)
  $0 shell                 # bash dans Docker (debug)

Env:
  ADEPT_DIR, OUT_DIR, LIBGOUROU_DIR, LIBGOUROU_IMAGE, SSL_CERT_FILE
EOF
}

have_appimage() {
  [[ -x "$LG_DIR/adept_activate" && -x "$LG_DIR/acsmdownloader" ]]
}

run_docker() {
  local ca_args=()
  if [[ -f "$CA_BUNDLE" ]]; then
    ca_args+=(
      -v "$CA_BUNDLE:/etc/ssl/certs/ca-certificates.crt:ro"
      -e SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
      -e CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
    )
  fi
  docker run --rm -it \
    "${ca_args[@]}" \
    -v "$ADEPT_DIR:/home/libgourou/.adept" \
    -v "$ROOT:/home/libgourou/files" \
    -v "$OUT_DIR:/home/libgourou/out" \
    -w /home/libgourou \
    --entrypoint /bin/bash \
    "$IMAGE" \
    -lc "$*"
}

cmd="${1:-}"
case "$cmd" in
  activate)
    echo "Activation ADEPT avec compte ByteBooks (pas Adobe ID legacy)."
    echo "Identifiants saisis interactivement — non stockés dans le script."
    echo
    if have_appimage; then
      echo "Moteur: AppImage libgourou 0.8.10 ($LG_DIR)"
      # adept_activate exige que le dossier de sortie N'EXISTE PAS
      TMP_ADEPT="$(mktemp -d "$TOOL/adept.new.XXXXXX")"
      read -r -p "ByteBooks email: " USER
      read -r -s -p "Password: " PASS; echo
      if ! "$LG_DIR/adept_activate" --random-serial \
          -u "$USER" -p "$PASS" \
          --output-dir "$TMP_ADEPT"; then
        rm -rf "$TMP_ADEPT"
        exit 1
      fi
      rm -rf "$ADEPT_DIR"
      mv "$TMP_ADEPT" "$ADEPT_DIR"
      echo
      echo "Activation OK:"
      ls -la "$ADEPT_DIR"
    else
      echo "AppImage absente — Docker + CA hôte"
      docker pull "$IMAGE"
      run_docker 'set -e
        read -r -p "ByteBooks email: " USER
        read -r -s -p "Password: " PASS; echo
        rm -rf /tmp/adept.new
        adept_activate --random-serial \
          --username "$USER" --password "$PASS" \
          --output-dir /tmp/adept.new
        rm -rf /home/libgourou/.adept/*
        cp -a /tmp/adept.new/. /home/libgourou/.adept/
        ls -la /home/libgourou/.adept
      '
    fi
    ;;
  download)
    shift
    ACSM="${1:-}"
    [[ -n "$ACSM" ]] || { usage; exit 1; }
    if [[ ! -f "$ACSM" ]]; then
      [[ -f "$ROOT/$ACSM" ]] && ACSM="$ROOT/$ACSM" || { echo "Introuvable: $ACSM" >&2; exit 1; }
    fi
    ACSM_ABS="$(cd "$(dirname "$ACSM")" && pwd)/$(basename "$ACSM")"
    if [[ ! -f "$ADEPT_DIR/activation.xml" ]]; then
      echo "Pas d'activation. Lance: $0 activate" >&2
      exit 1
    fi
    echo "Fulfil SANS retrait DRM: $ACSM_ABS"
    if have_appimage; then
      cd "$OUT_DIR"
      cp -f "$ACSM_ABS" ./input.acsm
      "$LG_DIR/acsmdownloader" \
        --adept-directory "$ADEPT_DIR" \
        -o book.drm \
        ./input.acsm
      file book.drm || true
      ls -la
      echo "OK → $OUT_DIR (DRM Adobe conservé)"
    else
      REL="${ACSM_ABS#"$ROOT"/}"
      run_docker "set -e
        cd /home/libgourou/out
        cp \"/home/libgourou/files/$REL\" ./input.acsm
        acsmdownloader --adept-directory /home/libgourou/.adept -o book.drm ./input.acsm
        file book.drm || true
        ls -la
      "
    fi
    ;;
  shell)
    docker pull "$IMAGE" >/dev/null
    run_docker 'exec bash'
    ;;
  *)
    usage
    exit 1
    ;;
esac
