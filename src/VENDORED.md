# Vendored: sfizz 1.2.3 (library)

- Upstream: https://github.com/sfztools/sfizz, tag `1.2.3`, commit `4e70dc0bef53b41f2853ed46e26f5911114c92d0`, with its
  submodules (abseil-cpp, filesystem, simde, st_audiofile and its dr_libs / libaiff / wavpack) at the commits that tag pins.
- Licence: BSD-2-Clause (`src/sfizz/LICENSE`); third-party parts keep their own licences in `src/sfizz/external/*`.
- Local changes: one, `patches/0001-gentle-background-loading.patch` (`src/sfizz/FilePool.h` and `.cpp`, every change
  marked `[sfizz for MPC OS]`). Upstream streams the whole rest of a sample file on every note-on, at once, on a
  real-time-priority (SCHED_RR) thread; on the RK3288 each key hit spiked the CPU to 100%. With gentle loading (the engine
  turns it on in `create()`), a note's first second loads in the pool job at normal priority, then one ordinary-priority
  thread per sfizz instance tops each playing file up every 15 ms, at most 2× real time ahead, and lets go of a file
  (back to its preloaded start, as sfizz's own garbage collector does) once no voice reads it. `emptyFileLoadingQueues()`,
  a no-op upstream, now stops and waits for those streams before files are cleared. The audio is bit-identical; measured
  on x86 (Salamander Grand Piano V3, chords in real time): worst 50 ms of loader CPU 98% → 18% of a core, total −10%.
  `sfizz_mpc_set_gentle_loading(0)` restores upstream behaviour.
- Removed only what the build never uses, to keep the repo small: `tests/`,
  `benchmarks/`, `demos/`, `devtools/`, `Doxyfile`, `external/simde/test/` and its Docker/Meson files, and abseil's
  `*_test.cc` / `*_benchmark.cc` files.
- Re-vendor: copy the new tag's tree (with submodules) over `src/sfizz/`, remove the same folders, re-apply the patch
  (`patch -p1 -d src/sfizz < patches/0001-gentle-background-loading.patch`, fixing it up if FilePool moved on), rebuild, and rerun
  `tools/test_port.sh vst.json` and the ARM checks in TESTING.md.

The LV2 plugin and editor (sfztools/sfizz-ui) are not vendored. The MPC skin was drawn to match the editor's 1.2.3 layout
and Default theme; `art/logo_text_shaded@2x.png`, `art/background@2x.png` and `fonts/Roboto-Regular.ttf` are copied from
sfizz-ui's `plugins/editor/resources` (BSD-2-Clause; Roboto is Apache-2.0).
