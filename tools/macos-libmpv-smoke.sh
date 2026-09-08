#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
VIDEO=${1:-${NUVIO_SMOKE_VIDEO:-}}
[[ -n "$VIDEO" ]] || { echo "uso: $0 /caminho/a/video" >&2; exit 2; }
[[ "$(uname -s)" == Darwin ]] || { echo "smoke requer macOS" >&2; exit 2; }
[[ "$(uname -m)" == x86_64 ]] || { echo "smoke requer Intel x86_64" >&2; exit 2; }
command -v pkg-config >/dev/null || { echo "falta pkg-config" >&2; exit 1; }
MODULES=(sdl2 SDL2_image SDL2_ttf)
if pkg-config --exists libmpv; then MODULES+=(libmpv)
elif pkg-config --exists mpv; then MODULES+=(mpv)
else echo "libmpv ausente por pkg-config" >&2; exit 1; fi
read -r -a CFLAGS <<< "$(pkg-config --cflags "${MODULES[@]}")"
read -r -a LIBS <<< "$(pkg-config --libs "${MODULES[@]}")"
OUT="$ROOT/build/macos-x86_64/macos_libmpv_smoke"
"${CC:-$(xcrun --find clang)}" -std=c99 -arch x86_64 -O0 -g3 \
  "${CFLAGS[@]}" tests/macos_libmpv_smoke.c -o "$OUT" \
  "${LIBS[@]}" -framework OpenGL -lm -pthread -Wno-deprecated-declarations
file "$OUT" | grep -q x86_64
echo "[smoke] $VIDEO"
"$OUT" "$VIDEO" &
PID=$!
(
  sleep 20
  kill "$PID" 2>/dev/null || true
) &
WATCHDOG=$!
set +e
wait "$PID"
STATUS=$?
set -e
kill "$WATCHDOG" 2>/dev/null || true
wait "$WATCHDOG" 2>/dev/null || true
exit "$STATUS"
