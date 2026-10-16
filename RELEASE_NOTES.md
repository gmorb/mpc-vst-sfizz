# sfizz for MPC OS 1.0.3

The keyboard at the bottom of the screen is now playable, and the SFZ browser no longer acts twice on one tap.

## Changes since 1.0.2
- **Playable keyboard**: two octaves and a C, shifted by octave with ‹ ›. Keys light while they sound, from MIDI or a
  tap; a tap plays the note at velocity 100 for 0.6 s (MPC reports a screen touch, not the finger lifting). Taps play
  sfizz directly and aren't recorded by MPC's sequencer. The octave is saved with the project. It replaces the full
  128-key picture, whose keys were too narrow to touch.
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
- Sample quality defaults to Sinc 8 (cleaner than sfizz's Polynomial default, for a little more CPU per voice).

## Requirements
- Gen1 MPC OS device (armv7: Force, MPC Live / Live II, One, X, Key 61), **MPC OS 3.x**.
- Root shell access (a modded unit): the installer edits `MPC.settings`.

## Install
Copy the unzipped `sfizz-1.0.3` folder to the device and run `sh install.sh` as root (it stops and restarts MPC: save your
project first). Full steps, including a manual install, are in the zip's `INSTALL.md` and in `docs/MANUAL.pdf`.

## Tested
Offline only for this release: mpc-vst-plugins' host test, the release `.so` through a VST2 host under QEMU (ARM),
output compared with upstream sfizz on x86 and ARM, and Salamander Grand Piano V3 hammered with chords and the pedal in
real time (x86) and under QEMU (ARM), with ASan/UBSan and TSan stress runs. Not yet tested on a device.
Device results will be recorded in `tested.json`.

## Credits
sfizz by the sfztools contributors (BSD-2-Clause). VST2 wrapper, skin tools and installer from
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (MIT).
