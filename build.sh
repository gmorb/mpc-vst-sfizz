#!/usr/bin/env bash
# Build sfizz for MPC OS: the skin (design.py -> layout.conf -> build/skin) and build/sfizz_mpc.so (armhf VST2).
#   MPC_VST=/path/to/mpc-vst-plugins ./build.sh [skin|so|so64|all]
#     so   -> build/sfizz_mpc.so          Gen1 (RK3288, armv7 hard-float)
#     so64 -> build/aarch64/sfizz_mpc.so  Gen2 (RK3588, aarch64)
#     all  -> the skin and both
# Needs: Python 3 with Pillow and Playwright (HTML_ART_PYTHON, for the skin), CMake, and Zig (python3 -m ziglang, from
# `pip install ziglang==0.13.0`) as the cross compiler: it targets glibc 2.28 for both (MPC OS 2.x has 2.31, Gen2's 3.x 2.39).
set -euo pipefail
PORT="$(cd "$(dirname "$0")" && pwd)"
MV="$(cd "${MPC_VST:?set MPC_VST to a checkout of sd88me/mpc-vst-plugins}" && pwd)"
WHAT="${1:-all}"
PY="${HTML_ART_PYTHON:-python3}"
cd "$PORT"
if [ "$WHAT" = skin ] || [ "$WHAT" = all ]; then
  "$PY" design.py
  PATH="$(dirname "$(command -v "$PY")"):$PATH" SHADOW_ART="$MV/tools/html_art.py" "$PY" "$MV/tools/gen_vst.py" vst.json
fi
[ "$WHAT" = skin ] && exit 0
[ -f build/params.h ] || "$PY" "$MV/tools/gen_vst.py" vst.json --params-h

build_so() {   # build_so <armv7|aarch64>
  local ARCH="$1" TC SF OBJ OUT FLAGS PROC
  if [ "$ARCH" = aarch64 ]; then
    # Cortex-A55 code runs on both of the RK3588's core types (A76 and A55: ARMv8.2-A with dot product and fp16)
    FLAGS="-target aarch64-linux-gnu.2.28 -mcpu=cortex_a55"; PROC=aarch64
    TC="$PORT/build/toolchain64"; SF="$PORT/build/sfizz-aarch64"; OBJ="$PORT/build/obj64"; OUT="$PORT/build/aarch64/sfizz_mpc.so"
  else
    FLAGS="-target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 -D_FILE_OFFSET_BITS=64"; PROC=armv7l   # Cortex-A17 (RK3288) runs A7 code
    TC="$PORT/build/toolchain"; SF="$PORT/build/sfizz-armhf"; OBJ="$PORT/build/obj"; OUT="$PORT/build/sfizz_mpc.so"
  fi
  mkdir -p "$TC" "$OBJ" "$(dirname "$OUT")"
  for t in cc c++; do printf '#!/bin/sh\nexec python3 -m ziglang %s %s "$@"\n' "$t" "$FLAGS" > "$TC/z$t"; done
  printf '#!/bin/sh\nexec python3 -m ziglang ar "$@"\n' > "$TC/zar"
  printf '#!/bin/sh\nexec python3 -m ziglang ranlib "$@"\n' > "$TC/zranlib"
  chmod +x "$TC"/z*
  cat > "$TC/cross.cmake" <<CM
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $PROC)
set(CMAKE_C_COMPILER $TC/zcc)
set(CMAKE_CXX_COMPILER $TC/zc++)
set(CMAKE_AR $TC/zar CACHE FILEPATH "")
set(CMAKE_RANLIB $TC/zranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
CM
  local HID="-fvisibility=hidden -ffunction-sections -fdata-sections"
  cmake -S src/sfizz -B "$SF" -DCMAKE_TOOLCHAIN_FILE="$TC/cross.cmake" -DCMAKE_BUILD_TYPE=Release -DSFIZZ_JACK=OFF \
    -DSFIZZ_RENDER=OFF -DSFIZZ_SHARED=OFF -DENABLE_LTO=OFF -DSFIZZ_GIT_SUBMODULE_CHECK=OFF \
    -DCMAKE_CXX_FLAGS="$HID -fvisibility-inlines-hidden" -DCMAKE_C_FLAGS="$HID" >/dev/null
  cmake --build "$SF" -j"$(nproc)"
  "$TC/zc++" -O2 -std=gnu++17 $HID -fPIC -Isrc/sfizz/src -I"$MV/wrapper" -c src/sfizz_mpc.cpp -o "$OBJ/sfizz_mpc.o"
  "$TC/zc++" -O2 -std=gnu++17 $HID -fPIC -c src/sf2_sfz.cpp -o "$OBJ/sf2_sfz.o"
  "$TC/zcc" -O2 -std=gnu11 $HID -fPIC -I"$PORT/build" -I"$MV/wrapper" -c "$MV/wrapper/vst2_wrap.c" -o "$OBJ/vst2_wrap.o"
  echo '{ global: VSTPluginMain; local: *; };' > "$OBJ/exports.map"
  local LIBS; LIBS=$(find "$SF/" -name '*.a' -not -path '*CMakeFiles*' | sort)
  "$TC/zc++" -shared -fPIC -Wl,--no-undefined -Wl,--gc-sections -Wl,--version-script="$OBJ/exports.map" -s \
    "$OBJ/sfizz_mpc.o" "$OBJ/sf2_sfz.o" "$OBJ/vst2_wrap.o" -Wl,--start-group $LIBS -Wl,--end-group -lpthread -ldl -lm -o "$OUT"
  echo "built ${OUT#$PORT/} ($ARCH)"
}
case "$WHAT" in
  so) build_so armv7 ;;
  so64) build_so aarch64 ;;
  all) build_so armv7; build_so aarch64 ;;
  *) echo "usage: build.sh [skin|so|so64|all]" >&2; exit 2 ;;
esac
