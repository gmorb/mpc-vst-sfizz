#!/usr/bin/env bash
# Package a release (run ./build.sh first). Writes dist/:
#   sfizz-<v>-mpc-armv7.zip      Gen1 catalog zip (release.py: install.sh, uninstall.sh, the portable plugin folder)
#   sfizz-<v>-mpc-aarch64.zip    Gen2 catalog zip (the same, with the aarch64 .so; install.sh registers it for Gen2)
#   sfizz-for-MPC-OS-<v>.zip     Gen1 standalone zip for vstscanner.sh (Force VST distribution layout: the plugin folder,
#                                README.md from packaging/standalone-README.md, LICENSE, NOTICE.md, licenses/)
#   sfizz-for-MPC-OS-<v>-manual.pdf, SHA256SUMS
#   MPC_VST=/path/to/mpc-vst-plugins tools/package.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MV="$(cd "${MPC_VST:?set MPC_VST to a checkout of sd88me/mpc-vst-plugins}" && pwd)"
V=$(python3 -c "import json; v=json.load(open('vst.json'))['version']; print('%d.%d.%d' % (v // 1000, v // 100 % 10, v % 100))")
REPO=gmorb/mpc-vst-sfizz
SKIN="build/skin/sfztools - VST - sfizz"
FOLDER="sfztools - VST - sfizz"
COLL="sfizz for MPC OS"
for f in build/sfizz_mpc.so build/aarch64/sfizz_mpc.so "$SKIN/Plugin Skins/TUI.json" docs/MANUAL.pdf; do
  [ -f "$f" ] || { echo "missing $f: run ./build.sh (and tools/make_manual.py) first" >&2; exit 1; }
done
rm -rf dist && mkdir -p dist/work

# 1. the two catalog zips, one per .so, with the same arguments
for so in build/sfizz_mpc.so build/aarch64/sfizz_mpc.so; do
  python3 "$MV/tools/release.py" --so "$so" --skin "$SKIN" --entry build/pluginlist-entry.xml --version "$V" \
    --extra data:sfizz --user-data sfizz/Instruments --repo "$REPO" --license BSD-2-Clause --id sfizz \
    --about "The sfizz 1.2.3 SFZ sampler (disk streaming, key switches, CC controls, Scala tuning) with a one-page screen modelled on sfizz's own editor." \
    -o dist >/dev/null
done
for z in "dist/sfizz-$V-mpc-armv7.zip" "dist/sfizz-$V-mpc-aarch64.zip"; do
  python3 "$MV/tools/catalog_check.py" "$z" --catalog --expect-id sfizz --expect-repo "$REPO" | tail -1
done

# 2. the standalone zip: the Gen1 zip's plugin folder, as release.py made it, with the collection's files
(cd dist/work && unzip -q "../sfizz-$V-mpc-armv7.zip")
C="dist/stage/$COLL"
mkdir -p "$C/licenses"
cp -r "dist/work/sfizz-$V/portable/$FOLDER" "$C/"
cp LICENSE NOTICE.md "$C/"
X=src/sfizz/external; I=src/sfizz/src/external
cp src/sfizz/LICENSE "$C/licenses/sfizz-BSD-2-Clause.txt"
cp "$MV/LICENSE" "$C/licenses/mpc-vst-plugins-MIT.txt"
cp $X/abseil-cpp/LICENSE "$C/licenses/abseil-Apache-2.0.txt"
cp $X/atomic_queue/LICENSE "$C/licenses/atomic_queue-MIT.txt"
cp $X/cephes/LICENSE.txt "$C/licenses/cephes.txt"
cp $X/filesystem/LICENSE "$C/licenses/ghc-filesystem-MIT.txt"
cp $X/jsl/LICENSE.md "$C/licenses/jsl-BSL-1.0.txt"
cp $X/simde/COPYING "$C/licenses/simde-MIT.txt"
cp $X/st_audiofile/LICENSE.md "$C/licenses/st_audiofile-BSD-2-Clause.txt"
cp $X/st_audiofile/thirdparty/dr_libs/LICENSE "$C/licenses/dr_libs.txt"
cp $X/st_audiofile/thirdparty/stb_vorbis/LICENSE "$C/licenses/stb_vorbis.txt"
cp $X/st_audiofile/thirdparty/libaiff/LICENSE "$C/licenses/libaiff.txt"
cp $X/st_audiofile/thirdparty/wavpack/COPYING "$C/licenses/wavpack-BSD-3-Clause.txt"
cp $I/cpuid/LICENSE.rst "$C/licenses/cpuid-BSD-3-Clause.txt"
cp $I/hiir/license.txt "$C/licenses/hiir-WTFPL.txt"
cp $I/kiss_fft/COPYING "$C/licenses/kiss_fft-BSD-3-Clause.txt"
cp $I/pugixml/LICENSE.md "$C/licenses/pugixml-MIT.txt"
cp $I/spline/LICENSE "$C/licenses/spline-BSD-3-Clause.txt"
cp $I/tunings/LICENSE.md "$C/licenses/tuning-library-MIT.txt"
{ printf 'Roboto (the screen font), by Google, is licensed under the Apache License 2.0:\n\n'
  cat $X/abseil-cpp/LICENSE; } > "$C/licenses/Roboto-Apache-2.0.txt"
sed -e "s|@VERSION@|$V|g" -e "s|@SOURCE@|https://github.com/$REPO (tag v$V)|g" packaging/standalone-README.md > "$C/README.md"
! grep -q "@[A-Z]*@" "$C/README.md" || { echo "unfilled placeholder in README.md" >&2; exit 1; }
find dist/stage -exec touch -d "2026-01-01 00:00:00" {} +
(cd dist/stage && find "$COLL" -type f | LC_ALL=C sort | zip -q -X -@ "../sfizz-for-MPC-OS-$V.zip")
rm -rf dist/stage dist/work

# 3. the manual and the checksums
cp docs/MANUAL.pdf "dist/sfizz-for-MPC-OS-$V-manual.pdf"
(cd dist && sha256sum "sfizz-$V-mpc-armv7.zip" "sfizz-$V-mpc-aarch64.zip" "sfizz-for-MPC-OS-$V.zip" \
  "sfizz-for-MPC-OS-$V-manual.pdf" > SHA256SUMS)
ls -l dist
