#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
sources=()
for source in src/*.c; do
  if [ "$source" != src/main.c ]; then sources+=("$source"); fi
done
flags=()
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
pkg_cflags=(); pkg_libs=()
if [ "$(uname -s)" = Darwin ]; then
  command -v pkg-config >/dev/null
  modules=(sdl2 SDL2_image SDL2_ttf)
  if pkg-config --exists libmpv; then modules+=(libmpv)
  else modules+=(mpv); fi
  read -r -a pkg_cflags <<< "$(pkg-config --cflags "${modules[@]}")"
  read -r -a pkg_libs <<< "$(pkg-config --libs "${modules[@]}")"
fi
cc "${flags[@]}" "${sources[@]}" tests/player_regression.c -Isrc \
  "${pkg_cflags[@]}" -o /tmp/nuvio-player-tests -O1 -g \
  "${pkg_libs[@]}" -framework OpenGL \
  -Wno-deprecated-declarations -Wno-macro-redefined
/tmp/nuvio-player-tests "$@"
