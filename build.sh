#!/usr/bin/env bash
# Build sfizz for MPC OS: the skin (design.py -> layout.conf -> build/skin) and build/sfizz_mpc.so (armhf VST2).
#   MPC_VST=/path/to/mpc-vst-plugins ./build.sh [skin|so|all]
# Needs: Python 3 with Pillow and Playwright (HTML_ART_PYTHON, for the skin), CMake, and Zig (python3 -m ziglang, from
# `pip install ziglang==0.13.0`) as the armhf cross compiler: it targets glibc 2.28, under MPC OS 2.x's 2.31.
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
TC="$PORT/build/toolchain"
mkdir -p "$TC"
FLAGS="-target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 -D_FILE_OFFSET_BITS=64"   # Cortex-A17 (RK3288) runs A7 code
for t in cc c++; do printf '#!/bin/sh\nexec python3 -m ziglang %s %s "$@"\n' "$t" "$FLAGS" > "$TC/z$t"; done
printf '#!/bin/sh\nexec python3 -m ziglang ar "$@"\n' > "$TC/zar"
printf '#!/bin/sh\nexec python3 -m ziglang ranlib "$@"\n' > "$TC/zranlib"
chmod +x "$TC"/z*
cat > "$TC/armhf.cmake" <<CM
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)
set(CMAKE_C_COMPILER $TC/zcc)
set(CMAKE_CXX_COMPILER $TC/zc++)
set(CMAKE_AR $TC/zar CACHE FILEPATH "")
set(CMAKE_RANLIB $TC/zranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
CM
SF="$PORT/build/sfizz-armhf"
HID="-fvisibility=hidden -ffunction-sections -fdata-sections"
cmake -S src/sfizz -B "$SF" -DCMAKE_TOOLCHAIN_FILE="$TC/armhf.cmake" -DCMAKE_BUILD_TYPE=Release -DSFIZZ_JACK=OFF \
  -DSFIZZ_RENDER=OFF -DSFIZZ_SHARED=OFF -DENABLE_LTO=OFF -DSFIZZ_GIT_SUBMODULE_CHECK=OFF \
  -DCMAKE_CXX_FLAGS="$HID -fvisibility-inlines-hidden" -DCMAKE_C_FLAGS="$HID" >/dev/null
cmake --build "$SF" -j"$(nproc)"
[ -f build/params.h ] || "$PY" "$MV/tools/gen_vst.py" vst.json --params-h
OBJ="$PORT/build/obj"; mkdir -p "$OBJ"
"$TC/zc++" -O2 -std=gnu++17 $HID -fPIC -Isrc/sfizz/src -I"$MV/wrapper" -c src/sfizz_mpc.cpp -o "$OBJ/sfizz_mpc.o"
"$TC/zcc" -O2 -std=gnu11 $HID -fPIC -I"$PORT/build" -I"$MV/wrapper" -c "$MV/wrapper/vst2_wrap.c" -o "$OBJ/vst2_wrap.o"
echo '{ global: VSTPluginMain; local: *; };' > "$OBJ/exports.map"
LIBS=$(find "$SF/" -name '*.a' -not -path '*CMakeFiles*' | sort)
"$TC/zc++" -shared -fPIC -Wl,--no-undefined -Wl,--gc-sections -Wl,--version-script="$OBJ/exports.map" -s \
  "$OBJ/sfizz_mpc.o" "$OBJ/vst2_wrap.o" -Wl,--start-group $LIBS -Wl,--end-group -lpthread -ldl -lm -o build/sfizz_mpc.so
echo "built build/sfizz_mpc.so"
