#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
grep -q 'video_render' src/video.h
grep -q 'mpv_render_context_render' src/video_macos.c
grep -q 'SDL_GL_GetProcAddress' tests/macos_libmpv_smoke.c
grep -q 'Contents/Resources/app/art' src/main.c tools/package-macos-intel.sh
grep -q -- '-arch x86_64' tools/mac-intel.sh
grep -q -- '-isysroot' tools/mac-intel.sh
grep -q -- '-mmacosx-version-min' tools/mac-intel.sh
test -f tools/macos-compat/AvailabilityMacros.h
grep -q 'install_name_tool' tools/package-macos-intel.sh
grep -q 'video_encerrar' src/main.c
if grep -nE '\b(system|popen)\s*\(' src/video_macos.c; then
  echo "backend macOS nao pode iniciar processos externos" >&2
  exit 1
fi
for script in tools/bootstrap-macos-intel.sh tools/mac-intel.sh \
  tools/macos-libmpv-smoke.sh tools/package-macos-intel.sh \
  tools/sign-macos.sh tools/validate-macos-bundle.sh; do
  bash -n "$script"
done
echo "macOS native contract: OK (static; runtime exige Mac Intel)"
