#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
APP="$ROOT/build/Nuvio.app"
BINARY=""
DO_SIGN=1
VERSION=${MACOS_VERSION:-0.1.0}
MIN_MACOS=${MACOS_MIN_VERSION:-10.13}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) BINARY=$2; shift 2 ;;
    --no-sign) DO_SIGN=0; shift ;;
    --version) VERSION=$2; shift 2 ;;
    *) echo "uso: $0 [--binary path] [--no-sign] [--version x.y.z]" >&2; exit 2 ;;
  esac
done
[[ "$(uname -s)" == Darwin ]] || { echo "package-macos-intel.sh: requiere macOS" >&2; exit 2; }
[[ "$(uname -m)" == x86_64 ]] || { echo "package-macos-intel.sh: requiere Intel x86_64" >&2; exit 2; }
command -v otool >/dev/null || { echo "falta otool" >&2; exit 1; }
if [[ -z "$BINARY" ]]; then
  tools/mac-intel.sh --release --no-run
  BINARY="$ROOT/build/macos-x86_64/Release/Nuvio"
fi
[[ -x "$BINARY" ]] || { echo "binario ausente: $BINARY" >&2; exit 1; }
file "$BINARY" | grep -q x86_64 || { echo "binario nao e x86_64" >&2; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/app" "$APP/Contents/Frameworks"
cp "$BINARY" "$APP/Contents/MacOS/Nuvio"
cp -R deploy/app/art "$APP/Contents/Resources/app/"
if [[ -d deploy/app/fonts ]]; then cp -R deploy/app/fonts "$APP/Contents/Resources/app/"; fi
if [[ -f deploy/app/icon.png ]]; then cp deploy/app/icon.png "$APP/Contents/Resources/"; fi
sed -e "s/@VERSION@/$VERSION/g" -e "s/@MIN_MACOS@/$MIN_MACOS/g" \
  deploy/macos/Info.plist.in > "$APP/Contents/Info.plist"

LIB_DIRS=()
for module in sdl2 sdl3 SDL2_image SDL2_ttf libmpv mpv libcurl libwebp; do
  if pkg-config --exists "$module" 2>/dev/null; then
    libdir=$(pkg-config --variable=libdir "$module" 2>/dev/null || true)
    [[ -n "$libdir" ]] && LIB_DIRS+=("$libdir")
  fi
done
LIB_DIRS+=(/opt/local/lib /opt/homebrew/lib /usr/local/lib)
SEEN=()
already_seen() {
  local want=$1 item
  for item in "${SEEN[@]-}"; do [[ "$item" == "$want" ]] && return 0; done
  return 1
}
find_library() {
  local dep=$1 base d candidate
  base=$(basename "$dep")
  for d in "${LIB_DIRS[@]}"; do
    candidate="$d/$base"
    [[ -f "$candidate" ]] && { echo "$candidate"; return 0; }
  done
  return 1
}
strip_rpaths() {
  local item=$1 rpath
  while IFS= read -r rpath; do
    [[ -n "$rpath" ]] || continue
    install_name_tool -delete_rpath "$rpath" "$item"
  done < <(
    otool -l "$item" |
      awk '/cmd LC_RPATH/{getline; getline; sub(/^[[:space:]]*path /, ""); sub(/ \(offset .*/, ""); print}'
  )
}
bundle_deps() {
  local item=$1 dep src base dest
  while IFS= read -r dep; do
    [[ -n "$dep" ]] || continue
    case "$dep" in
      /System/Library/*|/System/Library/Frameworks/*|/usr/lib/*) continue ;;
    esac
    base=$(basename "$dep")
    [[ "$base" == "Nuvio" ]] && continue
    src=""
    if [[ "$dep" == /* && -f "$dep" ]]; then src=$dep
    elif [[ "$dep" == @rpath/* ]]; then src=$(find_library "$base" || true)
    fi
    [[ -n "$src" ]] || continue
    dest="$APP/Contents/Frameworks/$base"
    if ! already_seen "$base"; then
      SEEN+=("$base")
      cp -L "$src" "$dest"
      chmod +x "$dest"
      strip_rpaths "$dest"
      install_name_tool -id "@rpath/$base" "$dest" 2>/dev/null || true
      bundle_deps "$dest"
    fi
    install_name_tool -change "$dep" "@rpath/$base" "$item" 2>/dev/null || true
  done < <(otool -L "$item" | tail -n +2 | awk '{print $1}')
}
bundle_file() {
  local src=$1 base=$2 dest="$APP/Contents/Frameworks/$2"
  if ! already_seen "$base"; then
    SEEN+=("$base")
    cp -L "$src" "$dest"
    chmod +x "$dest"
    strip_rpaths "$dest"
    install_name_tool -id "@rpath/$base" "$dest" 2>/dev/null || true
    bundle_deps "$dest"
  fi
}
MAIN="$APP/Contents/MacOS/Nuvio"
strip_rpaths "$MAIN"
install_name_tool -add_rpath '@executable_path/../Frameworks' "$MAIN" 2>/dev/null || true
bundle_deps "$MAIN"

SDL3_SOURCE=""
SDL3_NAME=""
for base in libSDL3.0.dylib libSDL3.dylib; do
  src=$(find_library "$base" || true)
  if [[ -n "$src" ]]; then
    SDL3_SOURCE=$src
    SDL3_NAME=$base
    break
  fi
done
[[ -n "$SDL3_SOURCE" ]] || {
  echo "SDL3 no encontrado en las rutas de pkg-config/SDK del build" >&2
  exit 1
}
bundle_file "$SDL3_SOURCE" "$SDL3_NAME"
if [[ "$SDL3_NAME" == libSDL3.0.dylib ]]; then
  ln -s "$SDL3_NAME" "$APP/Contents/Frameworks/libSDL3.dylib"
fi

# curl e webp sao carregadas por dlopen no legado e, por isso, nao aparecem em
# otool -L do executavel. Se existirem no ambiente de build, entram no bundle
# explicitamente e sao descobertas por @rpath no runtime.
for base in libcurl.4.dylib libcurl.dylib libwebp.7.dylib libwebp.dylib; do
  src=$(find_library "$base" || true)
  [[ -n "$src" ]] || continue
  bundle_file "$src" "$base"
done
if [[ "$DO_SIGN" == 1 ]]; then tools/sign-macos.sh "$APP"; fi
tools/validate-macos-bundle.sh "$APP"
echo "bundle pronto: $APP"
