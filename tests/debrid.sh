#!/bin/bash
set -eu
cd "$(dirname "$0")/.."
cc -O1 -g -Wall -Wextra -Isrc -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -Wno-deprecated-declarations \
  src/debrid.c src/stream_parse.c src/js.c tests/debrid.c -o /tmp/nuvio-debrid-tests
/tmp/nuvio-debrid-tests
