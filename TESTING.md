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
1. Keyboard, through the engine API (x86, ASan/UBSan): the C keys read C3, C4, C5; a tap on C3 lights the key and plays
   (one voice) and goes dark after 0.6 s; a switch-off echo on an unlit key does nothing; a MIDI note lights its black
   key and its note-off darkens it; ‹ › shift by octaves and clamp; the octave is in the saved state.
2. Browser: a tap, its echo and a second tap at once on row 1 of the places list opened Plugin Library once.
3. `tools/test_arm.sh` (ARM, the release `.so` through a VST2 host): tapping "Key C+1" (C4) plays (rms 0.023) and is
   silent again 1.4 s later; Salamander and the key-switch instrument load and play as before. Host test PASSED (its known
   `note 60` warn).
4. Skin: built without new touch overlaps except the Frequency presets' open list over two white keys (that list already
   overlaps other controls the same way); screen mock with lit keys: `docs/screen.png`.

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

## On the device (to do)
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
