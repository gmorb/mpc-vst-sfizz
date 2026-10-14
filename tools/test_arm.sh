#!/usr/bin/env bash
# Load build/sfizz_mpc.so the way MPC does (dlopen, VSTPluginMain) under qemu-arm, restore a project chunk per SFZ given,
# play a note and print what the screen would show. Needs qemu-user and arm-linux-gnueabihf-gcc (Debian/Ubuntu:
# apt install qemu-user gcc-arm-linux-gnueabihf).
#   tools/test_arm.sh [file.sfz ...]        (default: tests/keyswitch.sfz)
set -euo pipefail
PORT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PORT"
STAGE="build/stage/sfztools - VST - sfizz"
rm -rf build/stage && mkdir -p "$STAGE" && cp build/sfizz_mpc.so "$STAGE/" && cp -r data "$STAGE/sfizz"
arm-linux-gnueabihf-gcc -O1 tests/vsthost.c -o build/vsthost -ldl -lm
[ $# -gt 0 ] || set -- "$PORT/tests/keyswitch.sfz"
ARGS=(); for f in "$@"; do ARGS+=("$(cd "$(dirname "$f")" && pwd)/$(basename "$f")"); done
qemu-arm -L /usr/arm-linux-gnueabihf build/vsthost "$PORT/$STAGE/sfizz_mpc.so" "${ARGS[@]}" 2>&1 | grep -v '^\[sfizz\]'
