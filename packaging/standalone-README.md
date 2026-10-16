# sfizz for MPC OS

*The sfizz SFZ sampler on your MPC: play SFZ libraries straight from SD cards and USB drives.*

The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as an instrument plugin for Gen 1 Akai Force and MPC
units, on a one-page screen modelled on sfizz's own editor: samples streamed from disk (libraries larger than the
MPC's memory play in full), key switches, the instrument's own controls on knobs and Q-Links, Scala tunings, and up to
Sinc 72 interpolation. Version @VERSION@.
Not affiliated with or endorsed by sfztools, inMusic or Akai.

## What's in the folder
`sfztools - VST - sfizz/` is the whole plugin: `sfizz_mpc.so`, its page (`Plugin Skins/`, with its Sounds ›
INSTRUMENTS tile in `browser_images/`), `plugin-meta.xml`, `version.xml`, the Default preset (`Presets/`), and its
data folder `sfizz/`: the default instrument and scale, an empty `Instruments/` folder for your SFZ libraries and a
`Scales/` folder for Scala files. `README.md` (this file), `LICENSE`, `NOTICE.md` and `licenses/` are the licences.

## Requirements
- A **Gen 1** Akai Force or MPC (Live, Live II, One, X, Key 61) on MPC OS 3.x, with SSH access (modded firmware,
  e.g. MockbaMod). Gen 2 units (MPC Live III and other RK3588 units) need the other zip from the same release,
  `sfizz-@VERSION@-mpc-aarch64.zip`, installed with its `install.sh` (see its INSTALL.md) or from the MPC OS Plugin
  Catalog.
- The `Synths` folder with `vstscanner.sh` from the Force VST plugins distribution.
- SFZ instruments: an `.sfz` file next to its samples folder (WAV, FLAC, Ogg Vorbis, AIFF or WavPack). A good free one:
  Salamander Grand Piano V3 (github.com/sfzinstruments/SalamanderGrandPiano).

## Install
**Assumption: you use MockbaMod and its memory card is `/media/662522`** (for anything else see Other firmware).

1. Copy the `sfztools - VST - sfizz` folder into the `Synths` folder on the card (`/media/662522/Synths`, next to
   `vstscanner.sh`). Copy the folder itself, directly into `Synths`.
2. Point the Instruments tile's Default preset at that folder (one line; the zip can't know your card's name):
   ```
   ssh ip-of-Force "sed -i 's|%payload-path%|/media/662522/Synths|' '/media/662522/Synths/sfztools - VST - sfizz/Presets/0000-Default.xpl'"
   ```
3. On the device, run the scanner: `ssh ip-of-Force 'sh /media/662522/Synths/vstscanner.sh'`
   (after its first run it is installed as the command `vstscanner`).
4. MPC restarts. sfizz is under the VST category (Instrument plugins), manufacturer "sfztools", as "sfizz"; it also
   has its own tile in Sounds › INSTRUMENTS (tap it, then **Default**).

To remove it, delete the folder and run the scanner again. To update, delete the old folder (keep your
`sfizz/Instruments` and `sfizz/Scales` if you put files there: copy them into the new folder), copy the new one, repeat
step 2 and run the scanner. Re-insert sfizz on your tracks after an update.

If sfizz was installed before with a release zip's `install.sh`, remove that copy first
(`sh /tmp/sfizz-<version>/uninstall.sh`, or delete `/sdcard/Synths/sfztools - VST - sfizz`), so there is only one.

## Your instruments
Tap **⌄** at the top right of the file box: the SFZ browser lists the plugin's own library
(`sfztools - VST - sfizz/sfizz/Instruments/`), every card and USB drive under `/media`, and `/sdcard`. Tap folders,
then an `.sfz` file to load it; ‹ › in the file box step through the SFZ files in the same folder. Keep each instrument
as its author ships it: the `.sfz` file finds its samples by their path relative to it. A project saves the instrument
by path and finds it by name if the card or folder moved. Scala files (`.scl`) go in `sfizz/Scales/`.

## The page
One page, the four tabs of sfizz's editor side by side: the header (file box with ‹ › ⌄, key switch, Voices / Max /
Memory, Volume and Pan), **Information** (curves, masters, groups, regions, samples), **Controls** (the instrument's
own CC knobs with its names, up to 8) and **Settings** (preload size, oversampling, sustain cancels release, sample and
oscillator rendering quality, Scala file, root key, tuning frequency with historical presets, stretch tuning), over a
piano that shows the keys being played. The drop-down lists and the SFZ browser are sized for the touchscreen.
**Q-Links**: Volume, Pan, the 8 CC knobs, Frequency, Stretch, Sample and Oscillator quality.

## Notes
- **CPU:** a new sfizz starts with Sample and Oscillator quality at **Linear**, the lightest setting that still
  interpolates. Raise Sample quality (Polynomial, Sinc 8) when there is CPU to spare: it is cleaner on samples played
  far from their own pitch. Lower **Max** voices if dense playing with the pedal overloads the CPU.
- **Memory:** samples stream from disk within a budget of a third of the free memory (48 to 384 MB); when it is full,
  the notes that have played longest fade out first. Raise **Preload size** if a slow card drops note starts.
- **One install method per device:** the scanner rebuilds MPC's whole plugin list from the plugin folders it finds,
  so don't mix this with the catalog's `install.sh` zips.

## Other firmware
The scanner registers every plugin folder in a `Synths` folder on any drive under `/media`. It needs MPC's
settings file; it looks at MockbaMod's `/data/Settings/MPC/MPC.settings` unless given another as its first argument.

1. SSH into the device. `ls /media` lists the drives; pick one and create `/media/<drive>/Synths` if needed.
2. Copy `vstscanner.sh` and the `sfztools - VST - sfizz` folder into it.
3. Run step 2 of Install with `/media/<drive>/Synths` in place of `/media/662522/Synths` (both places).
4. Find MPC's settings file: `find / -name MPC.settings 2>/dev/null` (usually
   `/media/az01-internal/Settings/MPC/MPC.settings`) and back it up: `cp <that path> <that path>.bak`
5. Run the scanner with it: `sh /media/<drive>/Synths/vstscanner.sh <that path>`
6. If the plugin shows MPC's plain parameter list instead of its page, MPC doesn't look for pages on that drive:
   check `grep SynthContentLocations <settings path>` and use a drive whose `Synths` folder is listed there.

## Licence and source
BSD-2-Clause (`LICENSE`), as sfizz; `NOTICE.md` lists every component (sfizz and its libraries, the mpc-vst-plugins
wrapper, Roboto) with its licence, and `licenses/` holds the texts. The full manual is on the release page
(`sfizz-for-MPC-OS-@VERSION@-manual.pdf`). Source code: @SOURCE@
