# sfizz for MPC OS 1.0.4

**Now for Gen2 too.** This release has two zips: `sfizz-1.0.4-mpc-armv7.zip` for Gen1 (MPC Live / Live II, One, X, Key 61,
Force; RK3288) and `sfizz-1.0.4-mpc-aarch64.zip` for Gen2 (MPC Live III and other RK3588 units). Same plugin, screen,
parameters and ID on both: projects move between generations.

## Changes since 1.0.3
- **Gen2 (aarch64) build**: a native 64-bit build of the same engine. A new sfizz on Gen2 starts with Sample quality
  Sinc 8, Oscillator quality High and 96 voices (Gen1: Linear, Linear, 64), may use up to 1 GB for decoded samples
  (Gen1: 384 MB) and streams 2 s ahead of each note (Gen1: 1.25 s). Measured offline, Sinc 8 costs about 1.3× Linear's
  CPU per voice there, and about a third of what upstream sfizz needs on the same CPU. Not yet tested on a Gen2 device.
- **Browser**: a tap still acted twice on the device. Every browsing action (a row, Up, Home, the page arrows, ‹ › in the
  file box) is now taken only if none was taken, and no new folder listing appeared, in the last 0.7 s.
- **Lighter defaults on Gen1**: a new sfizz starts with Sample and Oscillator quality at Linear. Raise Sample quality
  when there is CPU to spare. Projects keep the quality they were saved with.
- **Instruments tile**: sfizz shows as an artwork tile in Sounds › INSTRUMENTS (tap it, then Default) instead of a plain
  folder.

# sfizz for MPC OS 1.0.3

The piano strip at the bottom of the screen now shows the keys being played, and the SFZ browser no longer acts twice on
one tap.

## Changes since 1.0.2
- **Piano shows played keys**: a key shows pressed while its note is held (from pads, a MIDI controller or the
  sequencer), drawn as sfizz's editor draws it: a pressed white key turns grey, a black key lighter. The strip is
  otherwise unchanged; touching it doesn't play.
- **Browser**: one tap, one action. A tap that landed as a new folder appeared could act again on the new folder's row
  (open a folder and then whatever was under the finger). Taps within a third of a second of a new listing, taps made on
  a listing that has since changed, and a row's switch-off echo are now ignored.

# sfizz for MPC OS 1.0.2

About 28% less CPU per voice than 1.0.1, with the same output: bit-identical to 1.0.1 at every sample quality (and to
sfizz itself at every quality but Sinc 8, as before).

## Changes since 1.0.1
- A stereo voice's volume, pan, stereo width and position are applied in one pass over the audio instead of five, and
  the amplitude envelope's gains in one pass instead of three (ARM).
- Work nothing used is skipped: per-voice loudness tracking (only one voice-stealing mode reads it), the crossfade stage
  of regions without CC crossfades, and re-flushing ~650 MIDI controller lists every block that hadn't changed.
- Sinc 8's final sums use the ARM's pairwise adds.
- Measured as ARM instructions per voice per block (emulated): Polynomial 19.8 K → 14.4 K, Sinc 8 22.0 K → 15.8 K,
  Sinc 24 75.1 K → 69.7 K.

## Changes in 1.0.1
- **Crash fixed**: the sample streamer could read past the end of its own list while many notes were streaming (it sorted
  on positions the audio thread was still moving), which crashed MPC when several keys were played. Also: failed
  allocations no longer end the process, and sfizz's internal checks no longer stop MPC (SIGTRAP) in release builds.
- **Memory stays bounded**: decoded samples have a budget (a third of the free memory at start, 48 to 384 MB). Large
  sample buffers are given back to the system when freed; before, memory only grew as files streamed in and out. If the
  budget runs out, the notes that have played longest fade out quickly (as voice stealing does) so new notes still sound.
- **Less CPU per voice**: about 55 K → 22 K ARM instructions per voice per block at Sinc 8 (measured under emulation):
  sfizz's NEON code paths are now used on ARM, Sinc 8 reads both channels at once, constant pan, pitch and sustain are
  computed once per block, and per-voice timing statistics (clock reads nobody used) are off.
- Streaming runs on a normal-priority thread, never ahead of MPC's own work; sfizz restored the wrong CPU flag register
  after each block on ARM (flush-to-zero leaked to the host); fixed.

The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as a native instrument for Gen1 MPC OS hardware
(MPC Live / Live II / One / X / Key 61, Force), on a one-page screen modelled on sfizz's own editor.

## Highlights
- sfizz 1.2.3's engine (one change, to sample streaming; the sound is identical): SFZ v2 with ARIA extensions, key switches, CC-driven regions and crossfades,
  24-bit WAV / FLAC / Ogg / AIFF / WavPack samples, kept in 32-bit float.
- Samples stream from disk: libraries larger than the MPC's memory play in full.
- One page: file box (‹ › ⌄), key switch badge, Voices / Max / Memory, Volume and Pan, Information, the instrument's
  CC knobs with its own names, Settings (engine, rendering quality, Scala tuning), piano.
- SFZ browser for SD cards, USB drives and the plugin's own library. Projects keep the instrument by path and find it by
  name if it moved.
- No CPU spike per key hit: samples stream a little ahead of each note at normal priority, within a memory budget
  (upstream reads the whole file at once on a real-time thread, with no limit); identical sound.
- Sample and Oscillator quality default to Linear, the lightest on the CPU; Sinc 8 and up are there when there's room.

## Requirements
- Gen1 MPC OS device (armv7: Force, MPC Live / Live II, One, X, Key 61), **MPC OS 3.x**.
- Root shell access (a modded unit): the installer edits `MPC.settings`.

## Install
Copy the unzipped `sfizz-1.0.4` folder to the device and run `sh install.sh` as root (it stops and restarts MPC: save your
project first). Full steps, including a manual install, are in the zip's `INSTALL.md` and in `docs/MANUAL.pdf`.

## Tested
Offline only for this release: mpc-vst-plugins' host test, the release `.so` through a VST2 host under QEMU (ARM),
output compared with upstream sfizz on x86 and ARM, and Salamander Grand Piano V3 hammered with chords and the pedal in
real time (x86) and under QEMU (ARM), with ASan/UBSan and TSan stress runs. Not yet tested on a device.
Device results will be recorded in `tested.json`.

## Credits
sfizz by the sfztools contributors (BSD-2-Clause). VST2 wrapper, skin tools and installer from
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (MIT).
