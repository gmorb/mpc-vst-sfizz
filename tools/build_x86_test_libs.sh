#!/usr/bin/env bash
# Build the vendored sfizz for x86 and merge its archives into build/libsfizz_x86_all.a, the library vst.json's "build"
# section links into mpc-vst-plugins' offline host test (tools/test_port.sh). Also stages build/sfizz, the data folder the
# host test's engine finds next to its binary.
#   tools/build_x86_test_libs.sh
set -euo pipefail
PORT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PORT"
cmake -S src/sfizz -B build/sfizz-x86 -DCMAKE_BUILD_TYPE=Release -DSFIZZ_JACK=OFF -DSFIZZ_RENDER=OFF -DSFIZZ_SHARED=OFF \
  -DENABLE_LTO=OFF -DSFIZZ_GIT_SUBMODULE_CHECK=OFF >/dev/null
cmake --build build/sfizz-x86 -j"$(nproc)"
{ echo "create build/libsfizz_x86_all.a"
  find build/sfizz-x86/ -name '*.a' -not -path '*CMakeFiles*' | sort | sed 's/^/addlib /'
  echo save; echo end; } | ar -M
rm -rf build/sfizz && cp -r data build/sfizz
echo "built build/libsfizz_x86_all.a; now: \"\$MPC_VST/tools/test_port.sh\" vst.json"
