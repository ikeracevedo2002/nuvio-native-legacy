#!/usr/bin/env bash
set -euo pipefail
APP=${1:-build/Nuvio.app}
fail() { echo "validate-macos-bundle: $*" >&2; exit 1; }
[[ "$(uname -s)" == Darwin ]] || { echo "validate-macos-bundle: requer macOS" >&2; exit 2; }
[[ -d "$APP/Contents" ]] || fail "bundle ausente: $APP"
EXE="$APP/Contents/MacOS/Nuvio"
FRAMEWORKS="$APP/Contents/Frameworks"
[[ -x "$EXE" ]] || fail "executavel ausente"
[[ -d "$FRAMEWORKS" ]] || fail "Frameworks ausente"
SDL3="$FRAMEWORKS/libSDL3.dylib"
[[ -e "$SDL3" ]] || fail "SDL3 ausente: $SDL3"
if [[ -e "$FRAMEWORKS/libSDL3.0.dylib" ]]; then
  [[ -L "$SDL3" ]] || fail "libSDL3.dylib deve ser um symlink para libSDL3.0.dylib"
  [[ "$(readlink "$SDL3")" == "libSDL3.0.dylib" ]] ||
    fail "symlink SDL3 inesperado: $(readlink "$SDL3")"
fi
otool -D "$SDL3" | tail -n 1 |
  grep -E -q '@rpath/libSDL3(\.0)?\.dylib' ||
  fail "install name de SDL3 nao e relocavel"
[[ -d "$APP/Contents/Resources/app/art" ]] || fail "art ausente"
[[ -d "$APP/Contents/Resources/app/fonts" ]] || fail "fonts ausente"
plutil -lint "$APP/Contents/Info.plist" >/dev/null || fail "Info.plist invalido"
while IFS= read -r item; do
  file "$item" | grep -q 'x86_64' || fail "nao e Intel x86_64: $item"
done < <(find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" -type f \( -perm -111 -o -name '*.dylib' \) -print 2>/dev/null)
while IFS= read -r item; do
  deps=$(otool -L "$item" 2>/dev/null || true)
  if echo "$deps" | grep -E '/(opt/local|opt/homebrew|usr/local)/' >/dev/null; then
    fail "dependencia do gerenciador ficou no bundle: $item"
  fi
done < <(find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" -type f \( -perm -111 -o -name '*.dylib' \) -print 2>/dev/null)
while IFS= read -r item; do
  while IFS= read -r rpath; do
    case "$rpath" in
      /opt/local/*|/opt/homebrew/*|/usr/local/*|@loader_path/*/opt/*|@loader_path/*/usr/local/*)
        fail "RPATH externo ficou no bundle: $item -> $rpath"
        ;;
    esac
  done < <(
    otool -l "$item" |
      awk '/cmd LC_RPATH/{getline; getline; sub(/^[[:space:]]*path /, ""); sub(/ \(offset .*/, ""); print}'
  )
done < <(find "$APP/Contents/MacOS" "$FRAMEWORKS" -type f \( -perm -111 -o -name '*.dylib' \) -print 2>/dev/null)
otool -l "$EXE" | grep -A2 LC_RPATH |
  grep -Fq '@executable_path/../Frameworks' ||
  fail "RPATH interno ausente no executavel"
find "$APP" -type f \( -name 'local.properties' -o -name 'trakt.txt' -o -name 'addons.txt' \
  -o -name 'tmdb.txt' -o -name 'mdblist.txt' -o -name 'sessao.txt' -o -name 'perfil.txt' \
  -o -name 'cliente.txt' -o -name 'nuvem.txt' -o -name 'ajustes.txt' -o -name 'progresso.txt' \
  -o -name 'collections.json' \) -print -quit | grep -q . && fail "dados/segredos do usuario no bundle"
find "$APP" -type d -name 'collections' -print -quit | grep -q . && fail "colecoes do usuario no bundle"
if [[ -d "$APP/_CodeSignature" ]]; then
  codesign --verify --deep --strict --verbose=4 "$APP" || fail "assinatura invalida"
fi
echo "bundle valido: $APP"
