# Vendored: sfizz 1.2.3 (library)

- Upstream: https://github.com/sfztools/sfizz, tag `1.2.3`, commit `4e70dc0bef53b41f2853ed46e26f5911114c92d0`, with its
  submodules (abseil-cpp, filesystem, simde, st_audiofile and its dr_libs / libaiff / wavpack) at the commits that tag pins.
- Licence: BSD-2-Clause (`src/sfizz/LICENSE`); third-party parts keep their own licences in `src/sfizz/external/*`.
- Removed only what the build never uses, to keep the repo small: `tests/`, `benchmarks/`, `demos/`, `devtools/`,
  `Doxyfile`, `external/simde/test/` and its Docker/Meson files, and abseil's `*_test.cc` / `*_benchmark.cc` files.

## Local changes
All in `src/sfizz/src/`, every one marked `[sfizz for MPC OS]` in the code, and all in one patch against the upstream
tag: `patches/0001-sfizz-for-mpc-os.patch` (`patch -p1 -d src/sfizz < patches/0001-sfizz-for-mpc-os.patch` on a clean
1.2.3 tree reproduces `src/sfizz/src` exactly).

### Sample streaming (`FilePool.h/.cpp`, `Synth.cpp`, `Voice.cpp`)
Upstream decodes the whole rest of a sample file on every note-on, at once, on threads it raises to real-time priority
(SCHED_RR), and keeps it until 5 s after the last note: on the RK3288 each key hit spiked the CPU to 100%, and memory had
no limit (a few seconds of chords with Salamander Grand Piano took 200+ MB more). With gentle loading (the engine turns it on
in `create()`; `sfizz_mpc_set_gentle_loading(0)` restores upstream behaviour):
- One normal-priority streamer thread per instance decodes each playing file a little ahead of its furthest voice: first
  every file to 0.35 s ahead (the most behind first), then to 1.25 s ahead, every 8 ms. Voices report how far they read
  (`FileData::noteRead`).
- A budget caps decoded sample memory across all instances (`sfizz_mpc_set_stream_budget`; the engine sets a third of
  MemAvailable, 48 to 384 MB), and the address space reserved for whole decoded files is capped too (3 × budget,
  256 to 768 MB, for a 32-bit process). Files no voice reads are kept 5 s, then freed; when memory is short the longest
  idle go first. If that isn't enough, the file that has played longest is retired: its voices fade out over a few ms (as
  voice stealing does) and its memory goes to the new notes. A new file waits up to 150 ms for room while its preloaded
  start plays.
- Freeing waits until no render block can be reading (`renderBlockBegin/End` around `Synth::renderBlock`).
  `emptyFileLoadingQueues()` (a no-op upstream) stops every stream before files are cleared.
- Allocation failure (`bad_alloc`) is caught where upstream would terminate the host.
- Counters for testing: `sfizz_mpc_stream_stats`, `sfizz_mpc_underruns` (notes cut because their data wasn't decoded in
  time; upstream cuts them the same way), `sfizz_mpc_streams_taken`.

### Memory (`Buffer.h`)
Buffers of 256 kB and more (decoded files) are mapped directly (`mmap`) instead of `calloc`. From the heap, glibc clears
every page of a reused block (CPU, and the whole file committed at once) and freed blocks stay in the process, so memory
only grew as files streamed in and out (x86, 8 s of chords: peak 479 MB → 231 MB, of which 163 MB decoded audio).

### CPU (no change to the sound)
- `simd/HelpersNEON.*`, `SIMDHelpers.cpp`: NEON versions of gain, multiply-add, multiply-multiply, add and subtract,
  dispatched on ARM (upstream's NEON table was empty: ARM ran the scalar loops, which the compiler won't vectorize for the
  in-place calls sfizz makes).
- `Panning.cpp`: pan and width with a constant envelope (the usual case) use one coefficient pair per block; the result
  reproduces upstream's NEON/scalar split exactly.
- `Voice.cpp`: one `pow` per block when the pitch doesn't move; skips the per-sample end-of-sample scan when the block
  can't reach the end; Sinc 8 on stereo samples reads a polyphase table (`Interpolators.*`, 8191 phases as upstream)
  for both channels at once.
- `ADSREnvelope.cpp`: a settled sustain segment is filled in one go.
- `utility/Timing.h`: `ScopedTiming` is a no-op. Its clock reads (about ten per voice per block) only fed sfizz's
  statistics and log files.
- Since 1.0.2 (`Voice.cpp`, `Panning.*`, `MidiState.cpp`): on ARM, a stereo voice's amplitude, pan, width, position and
  make-up gain run as one pass over the audio when pan, width and position are constant for the block (modulated or
  not), and the amplitude envelope's copy and two gains as one pass. Each stage's result is pinned in a register with
  an empty `asm`, because under `-ffast-math` the compiler otherwise folds the constant coefficients of consecutive
  stages together, which changes the rounding. Voices skip the power follower unless the voice stealer reads it, and
  the crossfade stage when the region has no CC crossfades (it multiplied by exactly 1). Sinc 8 sums with NEON pairwise
  adds instead of an emulated `hadd`. `MidiState::flushEvents` skips the ~650 controller lists that are already flushed.

Measured on ARM (Cortex-A7 code under QEMU, instruction counts, Salamander Grand Piano V3, 16 held voices), per voice per
128-frame block, upstream → 1.0.1 → 1.0.2: Polynomial 36 K → 19.8 K → 14.4 K, Sinc 8 55 K → 22.0 K → 15.8 K,
Sinc 24 88 K → 75.1 K → 69.7 K. Bit-identical to upstream (test instruments with pitch-bend and pan sweeps, with and
without pan and amplitude modulation; every quality on x86, Linear to Sinc 24 on ARM) except Sinc 8, which differs by
at most 1e-7 (−130 dB, summation order) and is bit-identical to 1.0.1.

Tried and dropped (1.0.2): skipping the audio of voices that have decayed below −100 dBFS. On Salamander no voice ever
got there: recorded tails reach the recording's noise floor (around −70 to −80 dBFS) and the samples end. Below an
inaudible threshold there was nothing to gain; above one it would cut real tails.

### Gen2 (aarch64)
The same tree builds for aarch64 (`build.sh so64`). In `FilePool.cpp`, behind `#if defined(__aarch64__)`: the address
space reserved for whole decoded files has no 768 MB ceiling (a 64-bit process), and the streamer keeps 2 s ahead
instead of 1.25 s. The engine (`src/sfizz_mpc.cpp`) sets the budget's ceiling to 1 GB and the new-instance defaults to
Sinc 8 / High / 96 voices there. None of this changes the armv7 build. On aarch64 the
output matches upstream to within 3e-8 (1e-7 at Sinc 8) rather than bit for bit: the compiler contracts multiply-adds
differently in upstream's scalar loops than in the port's NEON ones. Per-voice instruction counts for both builds are
in TESTING.md ("CPU per voice").

### Robustness
- `ScopedFTZ.cpp`: the ARM destructor wrote the saved FPSCR with `vmrs` (a read) instead of `vmsr`, so flush-to-zero was
  never restored for the host. Fixed (and `msr fpcr` on aarch64).
- `src/CMakeLists.txt`: release asserts are off unless `SFIZZ_RELEASE_ASSERTS` is set. Upstream keeps them on in release
  builds, and a failed one executes a breakpoint (SIGTRAP), which kills MPC.

## Re-vendor
Copy the new tag's tree (with submodules) over `src/sfizz/`, remove the same folders, apply the patch (fixing it up where
upstream moved on), rebuild, and rerun `tools/test_port.sh vst.json`, the ARM checks and the offline measurements in
TESTING.md.

The LV2 plugin and editor (sfztools/sfizz-ui) are not vendored. The MPC skin was drawn to match the editor's 1.2.3 layout
and Default theme; `art/logo_text_shaded@2x.png`, `art/background@2x.png` and `fonts/Roboto-Regular.ttf` are copied from
sfizz-ui's `plugins/editor/resources` (BSD-2-Clause; Roboto is Apache-2.0).
