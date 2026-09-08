#!/usr/bin/env bash
# Compatibilidade com o comando historico; o build Intel real fica em
# mac-intel.sh e usa SDL2/libmpv descobertos por pkg-config.
set -euo pipefail
exec "$(dirname "$0")/mac-intel.sh" "$@"
