#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "bootstrap-macos-intel.sh: execute este script no macOS" >&2
  exit 2
fi
if [[ "$(uname -m)" != "x86_64" ]]; then
  echo "bootstrap-macos-intel.sh: o alvo desta variante e Intel x86_64" >&2
  exit 2
fi
command -v xcrun >/dev/null || { echo "Xcode Command Line Tools ausentes" >&2; exit 1; }
command -v pkg-config >/dev/null || {
  echo "pkg-config ausente; instale-o pelo MacPorts ou Homebrew apenas para desenvolvimento" >&2
  exit 1
}
echo "clang: $(xcrun --find clang)"
echo "arquitetura do host: $(uname -m)"
if command -v port >/dev/null; then
  echo "MacPorts: $(port version | head -1)"
  if port installed mpv 2>/dev/null | grep -q 'active'; then
    echo "libmpv: MacPorts mpv ativo"
  else
    echo "libmpv: instale o port mpv com suporte libmpv antes do build" >&2
  fi
elif command -v brew >/dev/null; then
  echo "Homebrew detectado: $(brew --prefix)"
  echo "libmpv: confirme que os headers/bibliotecas de libmpv estao instalados"
else
  echo "Nenhum gerenciador detectado; forneca SDL2, SDL2_image, SDL2_ttf e libmpv via pkg-config" >&2
fi
for module in sdl2 sdl3 SDL2_image SDL2_ttf; do
  pkg-config --exists "$module" || { echo "pkg-config nao encontra $module" >&2; exit 1; }
done
if ! pkg-config --exists libmpv && ! pkg-config --exists mpv; then
  echo "pkg-config nao encontra libmpv (modulos tentados: libmpv, mpv)" >&2
  exit 1
fi
echo "Dependencias de compilacao/empacotamento encontradas. Nenhum caminho do gerenciador sera gravado no bundle."
