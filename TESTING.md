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
