The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as a native instrument plugin for MPC OS standalone
hardware: SFZ libraries and SoundFont 2 files streamed from SD cards and USB drives, key switches, the instrument's own CC controls and Scala
tuning, on a one-page screen modelled on sfizz's own editor.

## Download the zip for your device
| Device | Zip |
|---|---|
| **Gen1**: MPC Live / Live II, One, X, Key 61, Force (RK3288, 32-bit) | `sfizz-1.1.0-mpc-armv7.zip` |
| **Gen2**: MPC Live III and other RK3588 units (64-bit) | `sfizz-1.1.0-mpc-aarch64.zip` |
| **Gen1** with `vstscanner.sh` (Force VST distribution, e.g. MockbaMod) | `sfizz-for-MPC-OS-1.1.0.zip` |

Same plugin, screen, parameters and ID on both: projects move between generations. Needs MPC OS 3.x and root SSH access.
The installer zips check that they match the device. The manual is attached (`sfizz-for-MPC-OS-1.1.0-manual.pdf`).

## Install
**Installer zips** (`-mpc-armv7` / `-mpc-aarch64`):
```
scp -r sfizz-1.1.0 root@<device-ip>:/tmp/
ssh root@<device-ip> sh /tmp/sfizz-1.1.0/install.sh      # stops and restarts MPC: save first
```
**Standalone zip with `vstscanner`** (Gen1): copy the `sfztools - VST - sfizz` folder into your card's `Synths` folder
next to `vstscanner.sh`, run the one-line preset fix from its README, then `vstscanner`. The README covers MockbaMod and
other firmware. Use one method per device: the scanner rebuilds MPC's whole plugin list.

Then add **sfizz** from the plugin browser (manufacturer "sfztools"), or from Sounds › INSTRUMENTS (the sfizz tile,
then Default).

## New in 1.1.0: SoundFont 2, Mono / Legato with Glide
- **SoundFonts in the browser**: an `.sf2` file opens like a folder and lists its presets as `bank:program name`
  (`000:000 Piano 1`; General MIDI drum kits are in bank 128). Tap a preset to load it.
- **Converted once, then cached**: the first load of a preset converts it into an SFZ instrument and WAV samples in the
  plugin's `sfizz/SF2 Cache` folder (a moment for big presets; the current instrument keeps playing meanwhile); later
  loads are instant. The cache keeps up to 1 GB, dropping the least recently used SoundFonts first, and is safe to delete.
- **‹ › step through presets**, and a **MIDI program change** (bank from CC 0) switches to that preset of the loaded
  SoundFont.
- **Projects** remember the SoundFont and preset, and find the file by name if the card moved.
- **Mono, Legato and Glide** (Settings › Playing, also on Q-Links): Mono plays one note at a time and retriggers;
  Legato moves the sounding note to each new pitch without a new attack; Glide (Off to 2 s) slides between notes.
  Letting go of the newest note returns to the one still held. Poly, the default, plays exactly as before.
- The conversion follows the SoundFont 2.04 rules (key/velocity splits, loops, tuning, attenuation, pan, envelopes,
  filter, LFOs, exclusive classes, 24-bit samples). Custom SF2 modulators beyond the standard ones aren't converted;
  compressed SF3 files aren't supported.

Tested offline on both builds (emulated Gen1 and Gen2, sanitizers, damaged SoundFonts, pitch measurements of Mono,
Legato and Glide); see TESTING.md.

Full history: [RELEASE_NOTES.md](https://github.com/gmorb/mpc-vst-sfizz/blob/main/RELEASE_NOTES.md). Tests:
[TESTING.md](https://github.com/gmorb/mpc-vst-sfizz/blob/main/TESTING.md).

## SHA-256
```
d8f7ca0ffac31aced2e06c2845b5796cdbb58add8a0c6247b60e94f08293df0e  sfizz-1.1.0-mpc-armv7.zip
64d7b78a1d5b036ccd2d2776f226d76dc96dd01a46fa6cc0edd9d23b53443a67  sfizz-1.1.0-mpc-aarch64.zip
6e1a0531226436cc0ad4b17f3a2e20e2e7c369bf828a2a6294ea193bab11f2ea  sfizz-for-MPC-OS-1.1.0.zip
```

sfizz is by the sfztools contributors; this is an unofficial port (BSD-2-Clause), not affiliated with inMusic or Akai.
