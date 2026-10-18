# sfizz for MPC OS: agent guide

The sfizz 1.2.3 SFZ engine as a VST2 instrument for MPC OS hardware, Gen1 (armv7, RK3288) and Gen2 (aarch64, RK3588),
built on [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (the "kit"). This file is the working guide for anyone, agent
or person, who builds, tests, releases or lists it. Read the kit's own `CLAUDE.md`, `docs/PORTING.md`, `docs/RELEASING.md`
and `catalog/README.md` too; this file adds what is specific to this port.

## Fixed facts (don't change them)
| | |
|---|---|
| Repo | `gmorb/mpc-vst-sfizz` (if renamed: update `packaging/catalog/sfizz.json`, README, the `--repo` below) |
| Plugin name / vendor | `sfizz` / `sfztools` → plugin folder `sfztools - VST - sfizz` |
| `.so` / uid | `sfizz_mpc.so` / `SfzM` (0x53667a4d). Never change: projects find the plugin by uid |
| Catalog id | `sfizz`; entry `packaging/catalog/sfizz.json`; assets `sfizz-<version>-mpc-armv7.zip` (Gen1) and `sfizz-<version>-mpc-aarch64.zip` (Gen2), both on the same release |
| Other assets | `sfizz-for-MPC-OS-<version>.zip` (Gen1 standalone folder for `vstscanner.sh`, README from `packaging/standalone-README.md`), `sfizz-for-MPC-OS-<version>-manual.pdf`, `SHA256SUMS`: all made by `tools/package.sh` |
| Licence | BSD-2-Clause |
| Kit commit | `e18a0a1da84515096d354f399dd4cd7f9c079fb5` (build and check against this one) |
| Engine | sfizz 1.2.3 vendored in `src/sfizz` (`src/VENDORED.md`), local changes in one patch: `patches/0001-sfizz-for-mpc-os.patch` |
| MPC OS | 3.x only (the skin uses the 3.x format; each `.so` itself needs glibc ≤ 2.27) |
| Release guide | `docs/RELEASE_AGENT.md`: the step-by-step GitHub release and catalog listing, with checks |

**Parameter order is frozen** (`params.json`, VST index = position). MPC stores values by index: append new parameters at
the end, never reorder or remove, and bump the major version if indices ever must change. Index 0 is `stretch` on purpose
(see Gotchas).

## Layout
- `src/sfizz_mpc.cpp`: the engine (`mpc_engine()`): sfizz behind a per-instance worker thread. Loads, voice count,
  oversampling, preload and Scala files run on the worker holding `synth_m`; `render()` only try-locks it and outputs silence
  while a load holds it; cheap settings are atomics applied by `render()`. State is the `"state"` text chunk.
- `params.json`, `vst.json` (defines: `MODULE_SUBDIR "sfizz"`, `HAS_DISPLAY_REV`, `PARAM_TEXT_MAX 64`; `cc`/`nrpn` off so every
  CC reaches sfizz).
- `design.py` draws `art/` and writes `layout.conf` from one set of coordinates. Edit `design.py`, never `layout.conf`.
- `skin_post.py` patches the built `TUI.json` (Roboto Regular everywhere, values inside knobs, CC names in the pill above
  the knob, the invisible frequency knob, popup values centred). It exits non-zero if what it patches is missing.
- `data/` ships as the plugin's `sfizz/` folder: `DefaultInstrument.sfz`, `DefaultScale.scl`, `Instruments/` (user data,
  kept on upgrade), `Scales/`.
- `tests/vsthost.c` + `tools/test_arm.sh`: the release `.so` through a VST2 host under qemu-arm (`ARCH=aarch64`:
  the Gen2 `.so` under qemu-aarch64). `tools/make_images.py`: the README's banner and CPU chart.
  `tools/build_x86_test_libs.sh`: x86 sfizz for the kit's host test. `tools/mock_screen.py`: screen mock with live text.

## Build
```
pip install ziglang==0.13.0                                  # cross compiler for armhf and aarch64, targets glibc 2.28
git clone https://github.com/sd88me/mpc-vst-plugins kit && git -C kit checkout e18a0a1da84515096d354f399dd4cd7f9c079fb5
export MPC_VST=$PWD/kit HTML_ART_PYTHON=<python with playwright + pillow and a Chromium>
./build.sh            # skin (design.py -> gen_vst.py -> skin_post.py), build/sfizz_mpc.so and build/aarch64/sfizz_mpc.so
```
(`./build.sh skin|so|so64` builds one part.)
Checks after a build (all must hold; show the output):
```
arm-linux-gnueabihf-readelf -h build/sfizz_mpc.so | grep -E "ARM|hard-float"
arm-linux-gnueabihf-objdump -T build/sfizz_mpc.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1    # <= GLIBC_2.31
arm-linux-gnueabihf-nm -D --defined-only build/sfizz_mpc.so                                     # only VSTPluginMain
arm-linux-gnueabihf-readelf -d build/sfizz_mpc.so | grep NEEDED                                 # no libstdc++
aarch64-linux-gnu-readelf -h build/aarch64/sfizz_mpc.so | grep -E "ELF64|AArch64"
aarch64-linux-gnu-objdump -T build/aarch64/sfizz_mpc.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1  # <= GLIBC_2.39
aarch64-linux-gnu-nm -D --defined-only build/aarch64/sfizz_mpc.so                              # only VSTPluginMain
aarch64-linux-gnu-readelf -d build/aarch64/sfizz_mpc.so | grep NEEDED                          # no libstdc++
```

## Test (offline first, always)
```
tools/build_x86_test_libs.sh && "$MPC_VST/tools/test_port.sh" vst.json      # must print PASSED
tools/test_arm.sh [some.sfz ...]                                               # loads, plays, prints the screen's text
ARCH=aarch64 tools/test_arm.sh [some.sfz ...]                                  # the same for the Gen2 .so (qemu-aarch64)
"$MPC_VST/tools/studio.py" preview "build/skin/sfztools - VST - sfizz/Plugin Skins" -o build/preview/page_%d.png
```
The host test's `note 60` line may say `warn` (it plays during a voice-count change; see TESTING.md). Anything `FAIL` is a
failure. Look at every preview image, and at a `tools/mock_screen.py` render, after any skin change.

## Release
1. Bump `version` in `vst.json` (e.g. 1005 for 1.0.5) and the version below. Update `RELEASE_NOTES.md`.
2. Build and test as above.
3. Package: `tools/package.sh` does all of the below and also the standalone zip, the manual copy and `SHA256SUMS`
   (run `tools/make_manual.py` first if the manual changed). By hand, the catalog zips are, once per `.so`:
   ```
   for so in build/sfizz_mpc.so build/aarch64/sfizz_mpc.so; do
     python3 "$MPC_VST/tools/release.py" --so $so --skin "build/skin/sfztools - VST - sfizz" \
       --entry build/pluginlist-entry.xml --version 1.1.0 --extra data:sfizz --user-data sfizz/Instruments \
       --repo gmorb/mpc-vst-sfizz --license BSD-2-Clause --id sfizz \
       --about "The sfizz 1.2.3 SFZ sampler, now also playing SoundFont 2 (.sf2) files (disk streaming, key switches, CC controls, Scala tuning), with a one-page screen modelled on sfizz's own editor." -o dist
   done          # add --bench build/bench.txt to the armv7 one only, if you ran the bench (a Gen1 result)
   for z in dist/sfizz-1.1.0-mpc-armv7.zip dist/sfizz-1.1.0-mpc-aarch64.zip; do
     python3 "$MPC_VST/tools/catalog_check.py" $z --catalog --expect-id sfizz --expect-repo gmorb/mpc-vst-sfizz; done
   (cd dist && sha256sum sfizz-1.1.0-mpc-*.zip > SHA256SUMS)
   ```
   Each check must end `OK: sfizz 1.1.0`.
4. Device smoke test with that exact zip (TESTING.md, "On the device"). Ask before installing on someone's device: it
   restarts MPC. CPU: `"$MPC_VST/tools/bench.sh" build/sfizz_mpc.so <ip> -j | tee build/bench.txt` with a sampled
   instrument; re-package with `--bench` if you ran it.
5. Tag and publish with `gh` (every claim backed by the command's output):
   ```
   git tag v1.1.0 && git push origin main v1.1.0
   gh release create v1.1.0 --repo gmorb/mpc-vst-sfizz --draft --title "sfizz for MPC OS 1.1.0" \
     --notes-file packaging/release-notes-v1.1.0.md \
     dist/sfizz-1.1.0-mpc-armv7.zip dist/sfizz-1.1.0-mpc-aarch64.zip dist/sfizz-for-MPC-OS-1.1.0.zip \
     dist/sfizz-for-MPC-OS-1.1.0-manual.pdf dist/SHA256SUMS
   gh release view v1.1.0 --repo gmorb/mpc-vst-sfizz --json isDraft,assets,url     # check
   gh release edit v1.1.0 --repo gmorb/mpc-vst-sfizz --draft=false --latest        # only after the device test
   ```
6. After a device test, add it to `tested.json` (root, default branch), truthfully:
   `[{"version": "1.1.0", "device": "MPC Live II", "firmware": "3.x.y", "date": "YYYY-MM-DD"}]`. A test of the Gen2 zip
   needs "Gen2" in `device` (e.g. `"MPC Live III (Gen2)"`): that is what gives the Verified Gen2 badge.

## List it in the catalog (first release only)
```
gh repo fork sd88me/mpc-vst-plugins --clone --remote && cd mpc-vst-plugins && git checkout -b add-sfizz
cp ../mpc-vst-sfizz/packaging/catalog/sfizz.json catalog/plugins/sfizz.json
python3 tools/catalog_build.py --check-registry                                  # entry valid
curl -sI https://raw.githubusercontent.com/gmorb/mpc-vst-sfizz/v1.1.0/docs/screen.png | head -1    # 200
git add catalog/plugins/sfizz.json && git commit -m "Add sfizz" && git push -u origin add-sfizz
gh pr create --repo sd88me/mpc-vst-plugins --head gmorb:add-sfizz --title "Add sfizz" \
  --body "sfizz 1.1.0: the sfizz 1.2.3 SFZ engine for MPC / Force, Gen1 (armv7) and Gen2 (aarch64) zips on one release. Release: <the real release URL>"
gh pr view --repo sd88me/mpc-vst-plugins --json url,state,number                # check
```
No versions or checksums in the entry: the catalog reads them from the release. Later releases need no PR. One entry
covers both builds: the catalog finds the Gen2 zip by swapping `armv7` for `aarch64` in `asset_pattern`. Never add a
second entry for Gen2.
The screenshot link is pinned to the tag: it must exist in that tag (`docs/screen.png`).

## Gotchas (learned building it)
- **Per-note CPU spikes** came from sfizz's file streaming: upstream decodes a played sample's whole file at once on a
  real-time thread, with no memory limit. The vendored patch streams it within a budget (see `src/VENDORED.md`). Keep it
  when re-vendoring, and re-measure with a large FLAC piano before a release. Don't "optimise" by lowering quality.
- **The streamer must never sort or decide on live values** the audio thread moves (`readHead`): snapshot them first.
  Sorting on them made `std::sort` read past its array and crashed MPC with many keys held (fixed in 1.0.1).
- **Measure ARM CPU as instructions**, not x86 time: `qemu-arm` with the hotblocks plugin (TESTING.md). In 1.0.1 the
  NEON helpers were silently compiled as scalar because `HelpersNEON.cpp` didn't include `SIMDConfig.h`; only the
  disassembly / instruction counts showed it.
- **`-ffast-math` reassociates across merged stages**: folding two stages' NEON loops into one let the compiler multiply
  their constant coefficients together first (fewer multiplies, different rounding). `#pragma clang fp reassociate(off)`
  didn't stop it; an empty `asm` on each stage's result does. Check every merged path against upstream on ARM.
- **Identity tests must take the path you changed**: `det.sfz` modulates pan, so it never reached the constant-pan paths;
  `tests/offline/det_fused.sfz` does. Disk-heavy work running beside the real-time hammer test (a compile) caused note
  cuts that were not the engine's; rerun on a quiet machine before believing a regression.
- **Pressed keys on the piano**: parameters `key_0`..`key_127` (Off/On, appended after 1.0.2's), set by the engine from MIDI
  note on/off and reported through `mpc_engine_changed`; each shows a picture over the strip (`when=key_N:On`). Not
  string tiles: polling 128 `<key>_on` every 10 ms would cost real audio-thread time. Screen touches report no
  release, which is why a playable keyboard (tried in a 1.0.3 draft) was dropped.
- **List-row taps**: ignore value 0 (the echo of a row switching off), and drop taps made on a listing that has changed
  since. One touch still acted twice on the device with a 350 ms guard (1.0.3); 1.0.4 guards every browsing action for
  700 ms after the last one or a new listing (`nav_ok`). If it persists, log the setParameter calls on the device.
- **SFZ browser size**: `design.py` `BR_ROWS`/`BR_TH`/`BR_BTN` and the engine's `BR_ROWS` must agree (6 rows since 1.0.4;
  paging and `br_page` use the engine's). Params `br_7`/`br_8` are unused but stay (frozen order).
- **Popup lists**: `skin_post.py` re-lays and re-draws every popup's option list larger than the kit does (46 px rows,
  20 px text; `POP_*` at its end), from `layout.conf`'s field positions and `params.json`'s option names. A new popup
  needs nothing extra; it exits non-zero if a list doesn't match. The kit's own `sh_po_*`/`sh_pop_*` pictures are removed.
- **Standalone zip and `vstscanner.sh`**: the scanner fills `%payload-path%` in `plugin-meta.xml` only, not in
  `Presets/0000-Default.xpl` (`install.sh` does both), so the standalone README has a one-line `sed` for the preset.
  The scanner rebuilds the whole plugin list (Gen1 `pluginList-arm` only): one install method per device.
- **SoundFonts**: `src/sf2_sfz.cpp` converts one preset at a time into `<data>/SF2 Cache/<name>-<hash>/<bank>-<prog>.sfz`
  + `Samples/s<N>.wav`; the engine names a preset `<file>.sf2#<bank>:<program>` (browser rows, `file_path`, projects).
  Its output must stay identical to `tools/sf2sfz.py` (`tests/offline/sf2_reference.py` + `sf2conv.cpp`, `diff -r`):
  change both together. Conversion runs on the worker without `synth_m`, so the old instrument plays meanwhile. Program
  changes reach the worker through an atomic (the worker wakes every 50 ms), never a lock on the audio thread. Rows show
  `.sfz` files by stem only: preset names contain dots ("E.Piano 1").
- **Mono / Legato**: the note stack lives in the engine (`mono_*` in `src/sfizz_mpc.cpp`, audio thread only, fixed
  arrays); sfizz only gets note-ons/offs and `glideNote` (vendored: a per-voice cents offset, skipped while zero so Poly
  output stays identical). Legato keeps the first note's voices and glides them; the final note-off goes to that first
  note (`base`). Key-switch keys (`sw_mask`, set in `describe`) bypass the stack. A mode change flushes the stack.
  The glide sentinel is a finite 1e30, not NaN: sfizz builds with `-ffast-math`.
- **Instruments tile**: `vst.json` `"tile": "art/tile.png"` (drawn by `design.py`), as the kit documents; the tile only
  opens the plugin through the Default preset the kit writes beside it.
- **Parameter 0**: MPC toggles parameter 0 on insert and on every STOP; the kit's wrapper defers host sets of it, which
  breaks stepping tests for an option or whole-number parameter there. Keep a continuous knob (`stretch`) at index 0.
- **0-127 knobs** (Volume, Pan, CC slots) are continuous knobs with `dynamic_display`, not `"display": "int"`: with int
  display a wheel click is 1.27 steps and lands two; `nudge_pct` fixes that but fails the host test's sweep checks. The
  engine rounds, so a click or Q-Link event is one step either way.
- **Live text sizes are MPC font heights**, about 1.52 × CSS px. `tsize`/`skin_post` sizes are in those units.
- A popup's value label is `w − 44` wide: popups narrower than 46 px make a zero-width label (the preview tool crashes).
- `find "$SF"` doesn't descend a symlinked build folder: `build.sh` uses `"$SF/"`.
- `setOversamplingFactor` and `setPreloadSize` reload samples: the engine applies only changed values, coalesced.
- MIDI during a busy synth: kept through ~90 ms (settings changes), then late note-ons are dropped, note-offs kept.
- The skin build needs Chromium for Playwright. Where Playwright's CDN is blocked, an npm-shipped Chromium works through a
  `sitecustomize.py` that sets `executable_path` (and `--no-sandbox --single-process --headless=shell`).

- **Gen2 (aarch64)**: every Gen2 difference is behind `#if defined(__aarch64__)` (defaults `DEF_*` and
  `STREAM_BUDGET_MAX` in `src/sfizz_mpc.cpp`; `reserveLimit` and `kAheadSeconds` in `FilePool.cpp`), so the armv7 `.so`
  stays byte-identical when only Gen2 changes (check its sha256 against the last build when that is the intent). Keep
  `DEF_VOICES == POLYPHONY[DEF_POLY_IDX]` (a literal on purpose: indexing the table there changed the armv7 layout).
  On aarch64 the output is within 3e-8 of upstream, not bit-identical (FMA contraction). Tested on an MPC Live III (Gen2)
  with 1.0.4; there is no root route on Gen2 that the kit provides, so testers need their own.

## Ground rules
- Never claim a device result you didn't see; say "offline" for QEMU/x86 results. Record device facts in TESTING.md.
- Ask before restarting MPC or installing on a device; back up `MPC.settings` (the installer does).
- No inMusic/Akai names or logos in the skin, plugin name or screenshot.
- Commits: one concern each, with the trailers your session requires.
