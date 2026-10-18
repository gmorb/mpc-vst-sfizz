# Testing

## Offline (done for 1.0.0, 2026-10-14)
1. x86 host test, ASan/UBSan: build the vendored sfizz for x86 (`cmake -S src/sfizz -B build/sfizz-x86 -DSFIZZ_JACK=OFF
   -DSFIZZ_RENDER=OFF -DSFIZZ_SHARED=OFF -DSFIZZ_GIT_SUBMODULE_CHECK=OFF && cmake --build build/sfizz-x86`), merge its
   archives into `build/libsfizz_x86_all.a` (`ar -M`), copy `data/` to `build/sfizz`, then
   `"$MPC_VST/tools/test_port.sh" vst.json`: PASSED. (Its "note 60" line can warn: the test plays right after picking 256
   voices, while the voice change still holds the engine; a real change takes ~1 ms.)
2. The same host test built for armhf and run under qemu-arm: PASSED.
3. The release .so loaded with dlopen under qemu-arm through a minimal VST2 host: both a 24-bit WAV instrument and a
   keyswitch/CC instrument load from a restored chunk and play; the File, Regions, Samples, Memory, key switch badge and
   label, and CC knob names read back as sfizz shows them.
4. `build/sfizz_mpc.so`: ELF32 ARM hard-float, needs glibc 2.27 at most (pthread_create@GLIBC_2.4), exports only
   VSTPluginMain, no libstdc++ dependency.
5. Skin: `tools/studio.py preview` for every mode image, plus `tools/mock_screen.py` (live text filled in).
6. Gentle loading (x86, Salamander Grand Piano V3 FLAC, played in real time): output bit-identical to upstream loading;
   worst 50 ms window of loader CPU 98-99% (upstream) → 18% of a core (patched), over three runs; total loader work about
   10% lower, ~26% lower for short notes. ASan/UBSan stress (fast random notes with pedal, instrument switch while files
   stream, closing mid-stream, three rounds): no errors. One UBSan note in `Buffer.h:188` is upstream's (it also appears
   with upstream loading).

## Offline (done for 1.0.1, 2026-10-14)
All of the 1.0.0 list again (host test PASSED; `tools/test_arm.sh` with Salamander Grand Piano V3 and
`tests/keyswitch.sfz`: both load, play and close cleanly), plus, with the harnesses described below:
1. **Output vs upstream sfizz 1.2.3**, deterministic (samples in RAM), a test instrument with pitch-bend and pan sweeps:
   bit-identical at every sample quality on x86 and at Linear to Sinc 12 on ARM (qemu-arm), except Sinc 8 (max
   difference 1e-7, −130 dB). The `.so`'s engine was built for both from the same tree.
2. **ARM CPU** (qemu-arm with QEMU's `hotblocks` plugin, instructions in `Synth::renderBlock` and below, 16 held
   Salamander voices, 1500 blocks of 128 frames, minus a run of 0 blocks), instructions per voice per block, upstream →
   1.0.1: Polynomial 36 K → 19.8 K, Sinc 8 55 K → 22.0 K, Sinc 24 88 K → 75.1 K.
3. **Chord hammer**, Salamander FLAC, 64 voices, pedal down, a 4–9 key chord every 150 ms for 8 s, in real time:
   - x86, 192 MB budget: output identical to a run where the streamer has all the time it wants; 0 notes cut;
     peak RSS 62 → 251 MB (182 MB decoded audio). 1.0.0 peaked at 479 MB in the same test (memory never given back).
   - x86, 48 MB / 96 MB budgets: 154 / 123 oldest files retired (faded), 46 / 34 notes cut; 1.0.0 cut ~2700 / ~2200.
   - ARM (qemu-arm, slower than real time), 192 MB: 0 cut; 48 MB: 111–124 retired, 69–78 cut. No crash.
   - 1.0.0 crashed the x86 test about one run in four (`std::sort` in the streamer, on values the audio thread moves);
     ten runs after the fix: none.
4. **Sanitizers**: two instances sharing a 24 MB budget, chords and fast notes with the pedal, instrument switches,
   preload and voice-count changes while files stream, instances closed mid-stream, two rounds. ASan/UBSan: no errors.
   TSan: only reads of up to 4 frames past the decoded end by the interpolator at the very edge (upstream does the same;
   the values read are 0 or the final ones) and sfizz's shared random generator (upstream, harmless).

## Offline (done for 1.0.2, 2026-10-16)
1. Output vs upstream sfizz 1.2.3 (`det.sfz`, and `det_fused.sfz`, which has no pan modulation and an amplitude CC so it
   takes the new one-pass paths): bit-identical at every quality on x86 and at Linear, Polynomial, Sinc 12 and Sinc 24
   on ARM; Sinc 8 as in 1.0.1 (−130 dB) and bit-identical to 1.0.1's output on ARM.
2. ARM CPU, instructions per voice per block (same method as 1.0.1): Polynomial 19.8 K → 14.4 K, Sinc 8 22.0 K →
   15.8 K, Sinc 24 75.1 K → 69.7 K.
3. Chord hammer (x86, 192 MB): 0 notes cut, output identical between real time and unhurried runs; with two CPU hogs on
   both cores, 1 note cut. ASan/UBSan stress: no errors; TSan: the same two known reports as 1.0.1.
4. Host test PASSED; `tools/test_arm.sh` with Salamander and `tests/keyswitch.sfz`: loads, plays (same levels as 1.0.1),
   closes cleanly.
5. Tried and dropped: skipping voices decayed below −100 dBFS (see `src/VENDORED.md`); on Salamander none ever got there.

## Offline (done for 1.0.3, 2026-10-16)
1. Piano keys, through the engine API (x86, ASan/UBSan): note-ons for C4 and F#4 report `key_60` and `key_66` changed and
   read On; C4's note-off reports `key_60` again and reads Off; a host set of a key parameter is ignored.
2. Browser: a tap, its echo and a second tap at once on row 1 of the places list opened Plugin Library once.
3. `tools/test_arm.sh` (ARM, the release `.so` through a VST2 host): during note 60 "Key C4 down" reads On, after the
   note-off Off; Salamander and the key-switch instrument load and play as before. Host test PASSED (its known `note 60`
   warn).
4. Skin: 128 pressed-key pictures (only that key's pixels, cut from the strip drawn pressed), each shown by its
   parameter; no new touch overlaps. Screen mock with C4, E4, G4 down: `docs/screen.png`.

## Offline (done for 1.0.4, 2026-10-16)
1. Browser guard, through the engine API (x86, ASan/UBSan): a row tap repeated 450 ms later is ignored; Up pressed twice
   within 300 ms goes up one level; a tap 0.8 s later is taken. Not reproduced on a device: the device's double action
   is assumed to be a second event from the same touch (see `nav_ok` in `src/sfizz_mpc.cpp`).
2. A new instance reports Sample and Oscillator quality Linear. Host test PASSED (its known `note 60` warn); `tools/test_arm.sh`
   with Salamander as before (its chunk sets Sinc 8 explicitly).
3. Tile: `build.sh` writes `Plugin Skins/browser_images/soundsmode.png` (270x110, `art/tile.png`) and
   `Presets/0000-Default.xpl` (the kit's `xpl.py`, an empty chunk: the engine's defaults); both are in the release zip.

## Offline: the Gen2 (aarch64) build (done for 1.0.4, 2026-10-16)
Nothing here ran on a Gen2 device; QEMU 8.2 user mode (`qemu-aarch64`) on x86 throughout.
1. `build.sh so64` → `build/aarch64/sfizz_mpc.so`: ELF64 AArch64, needs glibc 2.27 at most (Gen2's MPC OS 3.x has
   2.39), exports only VSTPluginMain, NEEDED only libc/libm/libpthread/libdl (no libstdc++). Code for Cortex-A55, which
   both RK3588 core types run.
2. `ARCH=aarch64 tools/test_arm.sh tests/keyswitch.sfz "Salamander Grand Piano V3.sfz"`: both load from a restored chunk,
   play (same levels as the armv7 build: 0.1401 / 0.0339 rms), show the key switch, CC names and pressed key, save a
   chunk and close cleanly. The defaults line reads `Polyphony=96 Sample=Sinc 8 Oscillator=High` (armv7: `64 Linear Linear`).
3. Output vs upstream sfizz 1.2.3 built for aarch64 with the same compiler and flags (`det.sfz`, `det_fused.sfz`, plain,
   pitch-bend and pan sweeps, Nearest to Sinc 24 and Sinc 72): max difference 3e-8, and 1.04e-7 at Sinc 8; `det_fused.sfz`'s
   pan sweeps bit-identical at every quality but Sinc 8. Unlike armv7 it isn't bit-identical everywhere: on aarch64 the compiler contracts
   multiply-adds differently in upstream's scalar loops than in the port's NEON ones (rounding order only).
4. Stress (`tests/offline/stress2.cpp` with UBSan in trap mode, two instances, chords and fast notes with the pedal,
   instrument switches, preload and voice changes while streaming, closed mid-stream, two rounds), with the default
   24 MB budget and with `STRESS_BUDGET_MB=1024`: `stress OK`, no traps. Its underrun counts are QEMU's speed (FLAC
   decoding emulated far slower than real time), not a property of the build.
5. The armv7 `.so` built from the same tree was byte-identical to the one in the first 1.0.4 build (sha256
   `d3b1445c…`): every Gen2 change is behind `#if defined(__aarch64__)`. It changed afterwards with the larger browser
   (6 rows), below.
6. Packaging: `release.py` once per `.so` → `sfizz-1.0.4-mpc-armv7.zip` and `sfizz-1.0.4-mpc-aarch64.zip` (manifest
   `arch: aarch64`, `os_compat: ["3.x"]`); `catalog_check.py --catalog` on each: `OK: sfizz 1.0.4 (0 warnings)`.
   The kit's own aarch64 host test (`test_port.sh vst.json aarch64`) needs Docker with an arm64 image; not run here.

## Offline: larger popup lists and the standalone zip (done for 1.0.4, 2026-10-16)
1. `skin_post.py` re-lays and re-draws all 8 option lists (46 px rows, 20 px Roboto, cells at least 120 px or the
   longest option + 36 px), placed by the kit's rule (below the field, else above, else from the top; columns only when
   needed). Checked: every referenced picture exists, panel pictures match their bounds, the kit's old popup pictures are
   gone; `studio.py preview` (its all-lists-open view) and `tools/mock_screen.py` with each list open look right; host
   test PASSED. Seen on the device with 1.0.4 (below).
2. `tools/package.sh` → both installer zips (catalog checks OK, 0 warnings), the standalone
   `sfizz-for-MPC-OS-1.0.4.zip` and the manual. The standalone zip unpacked into a fake `/media/662522/Synths`: the
   README's sed line fills the Default preset's path, and `plugin-meta.xml` with `%payload-path%` replaced (the
   scanner's rule) names the ARM `.so` that exists there. `vstscanner.sh` itself (Force VST distribution) was not run.

## Offline: the larger SFZ browser (done for 1.0.4, 2026-10-16)
1. Six rows of 43 px (were eight of 32), text 34 (was 28), 44 px Home / Up / page buttons, a 40 px path line; the engine
   pages by six (`BR_ROWS`). `br_7`/`br_8` stay in `params.json` (frozen order) and are unused.
2. Engine API (x86): a 14-file folder reads `1-6 of 14`, page down `7-12 of 14`, again `9-14 of 14` (the last page
   ends on the last file), page up `3-8 of 14`; `br_7`/`br_8` read empty and a tap on `br_7` does nothing; a tap on row 6
   loads the file shown there.
3. Host test PASSED (its known `note 60` warn); `tools/test_arm.sh` on both builds: load, play, defaults and close as
   before; both `.so` still need glibc 2.27 at most. Screen mock of the open browser: `docs/browser.png`. No new touch
   overlaps in the skin build.
4. Navigation arrows: the file box's ‹ › ⌄ are 44 × 44 touch targets (were 32 × 36) with 25 px, 2.8 px-wide chevrons
   (the file name readout narrowed from 490 to 444 px to make room); the browser's page arrows use the same chevrons.
   No new touch overlaps; screenshots in `docs/` patched to match (`screen*.png`, `compare.png`, the banner).

## Offline: SoundFont 2 support (done for 1.1.0, 2026-10-17)
Test SoundFonts: the Majora's Mask N64 SoundFont (232 presets, 17 MB) and TimGM6mb (General MIDI, 136 presets with the
drum kit in bank 128, 6 MB, from the pretty_midi package).
1. **Converter vs its Python reference** (`tools/sf2sfz.py`, the converter that made the user's Majora's Mask SFZ set):
   `tests/offline/sf2conv.cpp` (C++, `src/sf2_sfz.cpp`) and `tests/offline/sf2_reference.py` convert every preset of
   both SoundFonts; `diff -r` of the two outputs: identical, SFZ text and WAV bytes (16-bit; the 24-bit path is the
   reference's too, but neither file has `sm24` data).
2. **Damaged files** (`tests/offline/sf2fuzz.cpp`, ASan + UBSan, no recovery): 400 mutations of TimGM6mb (random bytes in
   the header and the preset data, and truncations): 238 still listed, 952 presets converted, no sanitizer report.
   A non-SoundFont `.sf2` shows "Can't read broken.sf2: not a SoundFont 2 file"; SF3 files are refused by version.
3. **Engine API** (`tests/offline/sf2engine.cpp`, ASan + UBSan, x86): the library lists `.sf2` files as folders; a
   SoundFont lists `000:000 Piano 1` ... `1-6 of 136`; a tap converts and loads (33 regions, 9 samples; the row lights;
   C4 plays); ‹ › step Piano 1 → 2 → 3 → 2 (the last from the cache); a program change 40 (CC 0 = 0) loads Violin; bank
   127 program 127 reports "No preset 127:127"; the project line is `file=…/TimGM6mb.sf2#0:40` and reloads; with the path
   changed to a missing folder it finds the SoundFont by name and the browser opens on it; Majora's presets page by six.
4. **Cache**: one folder per SoundFont, named by file name, size and date (a moved card reuses it); with a 5 MB test limit
   (`-DSF2_CACHE_MAX_MB=5`), folders last used 2 and 3 days ago were removed and one used 10 minutes ago kept.
5. **Program changes while playing** (`tests/offline/sf2pcstress.cpp`, real time, notes every 23 ms, a program change
   every 290 ms, bank changes): ASan/UBSan and TSan, no reports.
6. Both builds: host test PASSED (known `note 60` warn); `tools/test_arm.sh` with `TimGM6mb.sf2#0:0`, `#0:40` and
   `#128:0` (the drum kit) under qemu-arm and qemu-aarch64: Piano 1 33 regions, Violin 9, Standard kit 62; each plays
   C4 (rms 0.027 / 0.054 / 0.006, the same on both). `.so` files still need glibc 2.27 at most and export only
   VSTPluginMain.

## Offline: Mono, Legato and Glide (done for 1.1.0, 2026-10-17)
1. **Pitch and envelope** (engine API, the default sine, C4 held, G4 pressed and released over it, x86 with ASan/UBSan),
   measured by FFT in 46 ms windows: Poly plays both (261.6 + 392.0 Hz); Mono with Glide Off jumps to 392.0 Hz with a
   new attack (envelope dips to 0.036 from 0.115) and back to 261.6 Hz on release; Mono with Glide 200 ms passes
   280.8 / 309.6 / 371.4 Hz at +10 / +60 / +150 ms and settles at 392.0; Legato with Glide Off jumps to 392.0 Hz with
   no new attack (envelope stays 0.104-0.127); Legato with Glide 200 ms slides the same way without a new attack. After
   the last release: silence. Mode and Glide are saved in the project.
2. The same scenarios on the ARM builds (armv7 under qemu-arm, aarch64 under qemu-aarch64): within 3-5 int16 steps of
   the x86 render (float rounding).
3. Poly is unchanged: x86 renders of `det.sfz` and `det_fused.sfz` (Linear, Polynomial, Sinc 8, Sinc 24; plain, bend,
   pan) are identical to the previous build, and to upstream sfizz except Sinc 8's known 8e-8.
4. **Stress** (`tests/offline/monostress.cpp`): random overlapping notes, the pedal and all-notes-off on the audio
   thread while another thread changes Mode and Glide every 37 ms, 8 s: ASan/UBSan and TSan, no reports. In Legato
   with `tests/keyswitch.sfz`, pressing the key-switch key C#1 over a held C4 switched to Staccato and C4 kept sounding.
5. Host test PASSED (209 parameters: Mode and Glide appended at the end, indices of the old ones unchanged).

## On the device
1.0.4, 2026-10-17, by the maintainer (`tested.json`): an Akai Force (Gen1) and an MPC Live III (Gen2), both MPC OS 3.9.1.

For 1.1.0, the SoundFont steps to run on a device:
| # | Step | Result |
|---|---|---|
| 1 | Copy an `.sf2` to a card; ⌄ → the card → the SoundFont opens with its presets | |
| 2 | Tap a preset: "Converting…", then it plays; tap it again later: loads at once (cache) | |
| 3 | ‹ › step through the presets | |
| 4 | Program change from a MIDI controller or the sequencer switches presets (if MPC sends them to plugins) | |
| 5 | Save and reload a project with a SoundFont preset | |
| 6 | Mode Mono / Legato with Glide Off and 200 ms: one note at a time, legato without a new attack, the slide | |

## CPU per voice (2026-10-16, both builds, the same method)
Instructions per voice per 128-frame block, Salamander Grand Piano V3 (WAV), 16 held voices, 1500 blocks minus a run
of 0 blocks, QEMU's `hotblocks` plugin with no block limit, counting the render thread only (sfizz's file-loading
threads, which upstream loading uses here, are left out). `docs/images/cpu.png` is drawn from this table
(`tools/make_images.py`).

| Quality | Gen1 upstream | Gen1 port | Gen2 upstream | Gen2 port |
|---|---|---|---|---|
| Linear | 34.2 K | 10.8 K | 31.6 K | 9.3 K |
| Polynomial | 36.5 K | 13.2 K | 33.6 K | 11.2 K |
| Sinc 8 | 55.9 K | 14.5 K | 39.3 K | 12.0 K |
| Sinc 12 | | | 43.1 K | 20.7 K |
| Sinc 24 | 91.2 K | 68.4 K | 83.3 K | 61.0 K |
| Oscillator (`*saw`), Linear / High / Dual-High | | | | 15.6 / 17.7 / 20.1 K |

The 1.0.1/1.0.2 figures above were taken with the plugin's default 4000-block report and include whatever the loading
threads ran; these are lower and steadier. Instruction counts compare work, not time: how fast a core gets through them
(RK3588's A76 and A55 cores, RK3288's A17) still needs a device measurement (`tools/bench.sh` in the kit).

### The offline harnesses (`tests/offline/`)
Built against `build/libsfizz_x86_all.a` (from `tools/build_x86_test_libs.sh`) on x86, or against the armhf archives in
`build/sfizz-armhf` (from `build.sh so`) with `-DAB_MAIN -shared` for ARM, run through `abhost.c` under qemu-arm with
`ABLIB=./lib….so`. For a pristine comparison build, configure upstream sfizz 1.2.3 the same way.
```
g++ -O2 -std=gnu++17 -Isrc/sfizz/src tests/offline/hammer.cpp build/libsfizz_x86_all.a -lpthread -ldl -o hammer
./hammer "Salamander Grand Piano V3.sfz" rt out_rt.f32 192 1     # rt|slow, output, budget MB, gentle 1|0
./hammer "Salamander Grand Piano V3.sfz" slow out_slow.f32 192 1
python3 tests/offline/cmpout.py out_rt.f32 out_slow.f32          # 0 blocks differing: nothing was cut
L=$(find build/sfizz-armhf/ -name '*.a' -not -path '*CMakeFiles*' | sort)
build/toolchain/zc++ -O2 -std=gnu++17 -DAB_MAIN -fPIC -shared -Isrc/sfizz/src tests/offline/hammer.cpp \
  -Wl,--start-group $L -Wl,--end-group -lpthread -ldl -lm -o libhammer.so
arm-linux-gnueabihf-gcc tests/offline/abhost.c -ldl -o abhost
ABLIB=./libhammer.so qemu-arm -L /usr/arm-linux-gnueabihf ./abhost "…/Salamander Grand Piano V3.sfz" rt out.f32 48 1
```
- `cmpd.cpp`: deterministic render of `det.sfz` (`cmpd <sfz> <quality> <out.raw> [bend|pan]`), for comparing builds.
- `stress2.cpp`: the sanitizer stress (link with `src/sfizz_mpc.cpp` and the kit's `wrapper/` includes; `stress2 <data
  folder> <sfz> [another sfz]`); build sfizz with `-fsanitize=address,undefined` or `-fsanitize=thread`.
- `armprof.cpp` + `hotblocks_agg.py`: ARM instruction profile. QEMU must be built with plugins (`contrib/plugins`):
  `ABLIB=./libab_prof.so qemu-arm -plugin libhotblocks.so -d plugin ./abhost render <sfz> <quality> 16 1500 > hot.txt`,
  the same with 0 blocks into `hot0.txt`, then `python3 hotblocks_agg.py libab_prof.so hot.txt hot0.txt 1500 16`. The
  hotblocks plugin keeps 20 blocks by default: raise its limit in `contrib/plugins/hotblocks.c` (4000 here).

## On the device: the full checklist
| # | Step | Result |
|---|---|---|
| 1 | `sh install.sh` from the release zip; MPC restarts | |
| 2 | Insert sfizz on a track: the default sine plays | |
| 3 | ⌄ → browse to an SFZ on a card → loads, name, counts, Memory update | |
| 4 | ‹ › step through the folder | |
| 5 | A keyswitched instrument: badge and label follow the key switches | |
| 6 | Controls: CC knobs appear with the instrument's names, move with MIDI CCs | |
| 7 | Every Settings control; Max voices; frequency presets | |
| 8 | Q-Links on Volume, Pan, CC knobs | |
| 9 | Save and reload a project: same instrument and knob positions | |
| 10 | `tools/bench.sh build/sfizz_mpc.so <ip> -j` with a sampled instrument | |
