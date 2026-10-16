#!/usr/bin/env bash
# Load build/sfizz_mpc.so the way MPC does (dlopen, VSTPluginMain) under qemu-arm, restore a project chunk per SFZ given,
# play a note and print what the screen would show. Needs qemu-user and arm-linux-gnueabihf-gcc (Debian/Ubuntu:
# apt install qemu-user gcc-arm-linux-gnueabihf); for the Gen2 build, aarch64-linux-gnu-gcc (gcc-aarch64-linux-gnu).
#   tools/test_arm.sh [file.sfz ...]              Gen1: build/sfizz_mpc.so under qemu-arm (default: tests/keyswitch.sfz)
#   ARCH=aarch64 tools/test_arm.sh [file.sfz ...] Gen2: build/aarch64/sfizz_mpc.so under qemu-aarch64
set -euo pipefail
PORT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PORT"
STAGE="build/stage/sfztools - VST - sfizz"
if [ "${ARCH:-armv7}" = aarch64 ]; then
  SO=build/aarch64/sfizz_mpc.so CC=aarch64-linux-gnu-gcc QEMU=qemu-aarch64 SYSROOT=/usr/aarch64-linux-gnu
else
  SO=build/sfizz_mpc.so CC=arm-linux-gnueabihf-gcc QEMU=qemu-arm SYSROOT=/usr/arm-linux-gnueabihf
fi
rm -rf build/stage && mkdir -p "$STAGE" && cp "$SO" "$STAGE/" && cp -r data "$STAGE/sfizz"
"$CC" -O1 tests/vsthost.c -o build/vsthost -ldl -lm
[ $# -gt 0 ] || set -- "$PORT/tests/keyswitch.sfz"
ARGS=(); for f in "$@"; do ARGS+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"); done
"$QEMU" -L "$SYSROOT" build/vsthost "$PORT/$STAGE/sfizz_mpc.so" "${ARGS[@]}" 2>&1 | grep -v '^\[sfizz\]'
