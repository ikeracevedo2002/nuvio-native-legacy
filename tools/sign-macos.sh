#!/usr/bin/env bash
set -euo pipefail
APP=${1:-build/Nuvio.app}
IDENTITY=${MACOS_SIGN_IDENTITY:--}
[[ "$(uname -s)" == Darwin ]] || { echo "sign-macos.sh: requiere macOS" >&2; exit 2; }
[[ -d "$APP" ]] || { echo "bundle ausente: $APP" >&2; exit 1; }
ARGS=(--force --sign "$IDENTITY")
if [[ "$IDENTITY" != - ]]; then ARGS+=(--timestamp); fi
if [[ -n "${MACOS_ENTITLEMENTS:-}" ]]; then ARGS+=(--entitlements "$MACOS_ENTITLEMENTS"); fi
if [[ -d "$APP/Contents/Frameworks" ]]; then
  while IFS= read -r lib; do codesign "${ARGS[@]}" "$lib"; done < <(find "$APP/Contents/Frameworks" -type f \( -name '*.dylib' -o -name '*.framework' \) -print | sort -r)
fi
codesign "${ARGS[@]}" "$APP/Contents/MacOS/Nuvio"
codesign "${ARGS[@]}" "$APP"
codesign --verify --deep --strict --verbose=4 "$APP"
echo "bundle assinado: $APP (identidade $IDENTITY)"
