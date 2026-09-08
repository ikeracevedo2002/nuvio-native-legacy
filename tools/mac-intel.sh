#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BUILD_KIND=Debug
RUN_APP=1
RUN_SMOKE=0
VIDEO=""
APP_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --release) BUILD_KIND=Release; shift ;;
    --no-run) RUN_APP=0; shift ;;
    --smoke) RUN_SMOKE=1; shift ;;
    --video) [[ $# -ge 2 ]] || { echo "--video requiere una ruta" >&2; exit 2; }; VIDEO=$2; shift 2 ;;
    --) shift; APP_ARGS+=("$@"); break ;;
    *) APP_ARGS+=("$1"); shift ;;
  esac
done
[[ "$(uname -s)" == Darwin ]] || { echo "mac-intel.sh: requiere macOS" >&2; exit 2; }
[[ "$(uname -m)" == x86_64 ]] || { echo "mac-intel.sh: requiere Intel x86_64" >&2; exit 2; }
command -v xcrun >/dev/null || { echo "faltan Xcode Command Line Tools" >&2; exit 1; }
command -v pkg-config >/dev/null || { echo "falta pkg-config" >&2; exit 1; }
CC=${CC:-$(xcrun --find clang)}
PKG_MODULES=(sdl2 SDL2_image SDL2_ttf)
if pkg-config --exists libmpv; then PKG_MODULES+=(libmpv)
elif pkg-config --exists mpv; then PKG_MODULES+=(mpv)
else echo "libmpv nao foi encontrado por pkg-config" >&2; exit 1; fi
for module in "${PKG_MODULES[@]}"; do
  pkg-config --exists "$module" || { echo "modulo ausente: $module" >&2; exit 1; }
done
read -r -a PKG_CFLAGS <<< "$(pkg-config --cflags "${PKG_MODULES[@]}")"
read -r -a PKG_LIBS <<< "$(pkg-config --libs "${PKG_MODULES[@]}")"
ENV_FILE=$(mktemp "${TMPDIR:-/tmp}/nuvio-env.XXXXXX")
trap 'rm -f "$ENV_FILE"' EXIT
tools/env.sh --env-file "$ENV_FILE" >/dev/null
# shellcheck disable=SC1090
set -a; . "$ENV_FILE"; set +a
DEFINE_ARGS=(
  "-DNV_SUPABASE_URL=\"${NV_SUPABASE_URL:-}\""
  "-DNV_SUPABASE_ANON_KEY=\"${NV_SUPABASE_ANON_KEY:-}\""
  "-DNV_TV_LOGIN_BASE=\"${NV_TV_LOGIN_BASE:-}\""
  "-DNV_TRAKT_CLIENT_ID=\"${NV_TRAKT_CLIENT_ID:-}\""
  "-DNV_TRAKT_CLIENT_SECRET=\"${NV_TRAKT_CLIENT_SECRET:-}\""
  "-DNV_SIMKL_CLIENT_ID=\"${NV_SIMKL_CLIENT_ID:-}\""
  "-DNV_SIMKL_APP=\"${NV_SIMKL_APP:-}\""
  "-DNV_TMDB_API_KEY=\"${NV_TMDB_API_KEY:-}\""
)
OUT_DIR="$ROOT/build/macos-x86_64/$BUILD_KIND"
OUT="$OUT_DIR/Nuvio"
mkdir -p "$OUT_DIR"
if [[ "$BUILD_KIND" == Release ]]; then OPT=(-O2 -DNDEBUG)
else OPT=(-O0 -g3 -fno-omit-frame-pointer); fi
CFLAGS=(-std=c99 -arch x86_64 -I"$ROOT/src" -DNV_HAS_LIBMPV=1 "${OPT[@]}")
echo "[mac] compilando $OUT"
"$CC" "${CFLAGS[@]}" "${DEFINE_ARGS[@]}" "${PKG_CFLAGS[@]}" \
  src/*.c -o "$OUT" "${PKG_LIBS[@]}" -framework OpenGL -lm -pthread \
  -Wno-deprecated-declarations
file "$OUT"
file "$OUT" | grep -q 'x86_64' || { echo "binario nao e x86_64" >&2; exit 1; }
otool -L "$OUT"
ART="$ROOT/deploy/app/art"
if [[ "$RUN_SMOKE" == 1 ]]; then
  [[ -n "$VIDEO" ]] || { echo "--smoke requer --video /caminho/video" >&2; exit 2; }
  tools/macos-libmpv-smoke.sh "$VIDEO"
fi
if [[ "$RUN_APP" == 1 ]]; then exec "$OUT" "$ART" "${APP_ARGS[@]}"; fi
