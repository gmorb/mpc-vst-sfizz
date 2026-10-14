# sfizz for MPC OS 1.0.0

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
- No CPU spike per key hit: sfizz's sample streaming is paced and runs at normal priority (upstream reads the whole
  file at once on a real-time thread); identical sound, about 5× lower worst-case loading burst.
- Sample quality defaults to Sinc 8 (cleaner than sfizz's Polynomial default for about 4% more CPU per voice).

## Requirements
- Gen1 MPC OS device (armv7: Force, MPC Live / Live II, One, X, Key 61), **MPC OS 3.x**.
- Root shell access (a modded unit): the installer edits `MPC.settings`.

## Install
Copy the unzipped `sfizz-1.0.0` folder to the device and run `sh install.sh` as root (it stops and restarts MPC: save your
project first). Full steps, including a manual install, are in the zip's `INSTALL.md` and in `docs/MANUAL.pdf`.

## Tested
Offline only for this release: mpc-vst-plugins' host test (x86 ASan/UBSan and armhf under QEMU) and the release `.so`
loaded through a VST2 host under QEMU with a 24-bit WAV instrument, a key-switch instrument and Salamander Grand Piano V3.
Device results will be recorded in `tested.json`.

## Credits
sfizz by the sfztools contributors (BSD-2-Clause). VST2 wrapper, skin tools and installer from
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (MIT).
