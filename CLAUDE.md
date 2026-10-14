# sfizz for MPC OS: agent guide

The sfizz 1.2.3 SFZ engine as a VST2 instrument for Gen1 MPC OS hardware (armv7), built on
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (the "kit"). This file is the working guide for anyone, agent
or person, who builds, tests, releases or lists it. Read the kit's own `CLAUDE.md`, `docs/PORTING.md`, `docs/RELEASING.md`
and `catalog/README.md` too; this file adds what is specific to this port.

## Fixed facts (don't change them)
| | |
|---|---|
| Repo | `gmorb/mpc-vst-sfizz` (if renamed: update `packaging/catalog/sfizz.json`, README, the `--repo` below) |
| Plugin name / vendor | `sfizz` / `sfztools` → plugin folder `sfztools - VST - sfizz` |
| `.so` / uid | `sfizz_mpc.so` / `SfzM` (0x53667a4d). Never change: projects find the plugin by uid |
| Catalog id | `sfizz`; entry `packaging/catalog/sfizz.json`; asset `sfizz-<version>-mpc-armv7.zip` |
| Licence | BSD-2-Clause |
| Kit commit | `e18a0a1da84515096d354f399dd4cd7f9c079fb5` (build and check against this one) |
| Engine | sfizz 1.2.3 vendored in `src/sfizz` (`src/VENDORED.md`), one local patch: `patches/0001-gentle-background-loading.patch` |
| MPC OS | 3.x only (the skin uses the 3.x format; the `.so` itself needs glibc ≤ 2.27) |

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
- `tests/vsthost.c` + `tools/test_arm.sh`: the release `.so` through a VST2 host under qemu-arm.
  `tools/build_x86_test_libs.sh`: x86 sfizz for the kit's host test. `tools/mock_screen.py`: screen mock with live text.

## Build
```
pip install ziglang==0.13.0                                  # armhf cross compiler, targets glibc 2.28
git clone https://github.com/sd88me/mpc-vst-plugins kit && git -C kit checkout e18a0a1da84515096d354f399dd4cd7f9c079fb5
export MPC_VST=$PWD/kit HTML_ART_PYTHON=<python with playwright + pillow and a Chromium>
./build.sh            # skin (design.py -> gen_vst.py -> skin_post.py) and build/sfizz_mpc.so
```
Checks after a build (all must hold; show the output):
```
arm-linux-gnueabihf-readelf -h build/sfizz_mpc.so | grep -E "ARM|hard-float"
arm-linux-gnueabihf-objdump -T build/sfizz_mpc.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1    # <= GLIBC_2.31
arm-linux-gnueabihf-nm -D --defined-only build/sfizz_mpc.so                                     # only VSTPluginMain
arm-linux-gnueabihf-readelf -d build/sfizz_mpc.so | grep NEEDED                                 # no libstdc++
```

## Test (offline first, always)
```
tools/build_x86_test_libs.sh && "$MPC_VST/tools/test_port.sh" vst.json      # must print PASSED
tools/test_arm.sh [some.sfz ...]                                               # loads, plays, prints the screen's text
"$MPC_VST/tools/studio.py" preview "build/skin/sfztools - VST - sfizz/Plugin Skins" -o build/preview/page_%d.png
```
The host test's `note 60` line may say `warn` (it plays during a voice-count change; see TESTING.md). Anything `FAIL` is a
failure. Look at every preview image, and at a `tools/mock_screen.py` render, after any skin change.

## Release
1. Bump `version` in `vst.json` (e.g. 1001 for 1.0.1) and the version below. Update `RELEASE_NOTES.md`.
2. Build and test as above.
3. Package with the catalog fields (no other way of making the zip):
   ```
   python3 "$MPC_VST/tools/release.py" --so build/sfizz_mpc.so --skin "build/skin/sfztools - VST - sfizz" \
     --entry build/pluginlist-entry.xml --version 1.0.0 --extra data:sfizz --user-data sfizz/Instruments \
     --repo gmorb/mpc-vst-sfizz --license BSD-2-Clause --id sfizz [--bench build/bench.txt] \
     --about "The sfizz 1.2.3 SFZ sampler (disk streaming, key switches, CC controls, Scala tuning) with a one-page screen modelled on sfizz's own editor." -o dist
   python3 "$MPC_VST/tools/catalog_check.py" dist/sfizz-1.0.0-mpc-armv7.zip --catalog --expect-id sfizz --expect-repo gmorb/mpc-vst-sfizz
   ```
   The check must end `OK: sfizz 1.0.0`.
4. Device smoke test with that exact zip (TESTING.md, "On the device"). Ask before installing on someone's device: it
   restarts MPC. CPU: `"$MPC_VST/tools/bench.sh" build/sfizz_mpc.so <ip> -j | tee build/bench.txt` with a sampled
   instrument; re-package with `--bench` if you ran it.
5. Tag and publish with `gh` (every claim backed by the command's output):
   ```
   git tag v1.0.0 && git push origin main v1.0.0
   gh release create v1.0.0 --repo gmorb/mpc-vst-sfizz --draft --title "sfizz for MPC OS 1.0.0" \
     --notes-file RELEASE_NOTES.md dist/sfizz-1.0.0-mpc-armv7.zip
   gh release view v1.0.0 --repo gmorb/mpc-vst-sfizz --json isDraft,assets,url     # check
   gh release edit v1.0.0 --repo gmorb/mpc-vst-sfizz --draft=false --latest        # only after the device test
   ```
6. After a device test, add it to `tested.json` (root, default branch), truthfully:
   `[{"version": "1.0.0", "device": "MPC Live II", "firmware": "3.x.y", "date": "YYYY-MM-DD"}]`.

## List it in the catalog (first release only)
```
gh repo fork sd88me/mpc-vst-plugins --clone --remote && cd mpc-vst-plugins && git checkout -b add-sfizz
cp ../mpc-vst-sfizz/packaging/catalog/sfizz.json catalog/plugins/sfizz.json
python3 tools/catalog_build.py --check-registry                                  # entry valid
curl -sI https://raw.githubusercontent.com/gmorb/mpc-vst-sfizz/v1.0.0/docs/screen.png | head -1    # 200
git add catalog/plugins/sfizz.json && git commit -m "Add sfizz" && git push -u origin add-sfizz
gh pr create --repo sd88me/mpc-vst-plugins --head gmorb:add-sfizz --title "Add sfizz" \
  --body "sfizz 1.0.0: the sfizz 1.2.3 SFZ engine for Gen1 MPC / Force. Release: <the real release URL>"
gh pr view --repo sd88me/mpc-vst-plugins --json url,state,number                # check
```
No versions or checksums in the entry: the catalog reads them from the release. Later releases need no PR.
The screenshot link is pinned to the tag: it must exist in that tag (`docs/screen.png`).

## Gotchas (learned building it)
- **Per-note CPU spikes** came from sfizz's file streaming, not from rendering: upstream decodes a played sample's whole
  file at once on a real-time thread. The vendored patch paces it (see `src/VENDORED.md`). Keep it when re-vendoring, and
  re-measure with a large FLAC piano before a release. Don't "optimise" by lowering quality: rendering costs ~0.2 ms of CPU
  per second of a voice on x86; the loader was the problem.
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

## Ground rules
- Never claim a device result you didn't see; say "offline" for QEMU/x86 results. Record device facts in TESTING.md.
- Ask before restarting MPC or installing on a device; back up `MPC.settings` (the installer does).
- No inMusic/Akai names or logos in the skin, plugin name or screenshot.
- Commits: one concern each, with the trailers your session requires.
