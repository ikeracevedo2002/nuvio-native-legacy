#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
cc -Isrc tests/mkv_caps.c src/rede.c -o /tmp/nuvio-mkv-caps-tests \
  -I/opt/homebrew/include -Wno-deprecated-declarations 2>/dev/null || \
cc -Isrc tests/mkv_caps.c src/rede.c -o /tmp/nuvio-mkv-caps-tests -I/opt/homebrew/include
/tmp/nuvio-mkv-caps-tests
