#!/usr/bin/env bash
set -euo pipefail
APP=${1:-build/Nuvio.app}
fail() { echo "validate-macos-bundle: $*" >&2; exit 1; }
[[ "$(uname -s)" == Darwin ]] || { echo "validate-macos-bundle: requer macOS" >&2; exit 2; }
[[ -d "$APP/Contents" ]] || fail "bundle ausente: $APP"
EXE="$APP/Contents/MacOS/Nuvio"
[[ -x "$EXE" ]] || fail "executavel ausente"
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
find "$APP" -type f \( -name 'local.properties' -o -name 'trakt.txt' -o -name 'addons.txt' \
  -o -name 'tmdb.txt' -o -name 'mdblist.txt' -o -name 'sessao.txt' -o -name 'perfil.txt' \
  -o -name 'cliente.txt' -o -name 'nuvem.txt' -o -name 'ajustes.txt' -o -name 'progresso.txt' \
  -o -name 'collections.json' \) -print -quit | grep -q . && fail "dados/segredos do usuario no bundle"
find "$APP" -type d -name 'collections' -print -quit | grep -q . && fail "colecoes do usuario no bundle"
if [[ -d "$APP/_CodeSignature" ]]; then
  codesign --verify --deep --strict "$APP" || fail "assinatura invalida"
fi
echo "bundle valido: $APP"
