The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as a native instrument plugin for MPC OS standalone
hardware: SFZ libraries streamed from SD cards and USB drives, key switches, the instrument's own CC controls and Scala
tuning, on a one-page screen modelled on sfizz's own editor.

## Download the zip for your device
| Device | Zip |
|---|---|
| **Gen1**: MPC Live / Live II, One, X, Key 61, Force (RK3288, 32-bit) | `sfizz-1.0.4-mpc-armv7.zip` |
| **Gen2**: MPC Live III and other RK3588 units (64-bit) | `sfizz-1.0.4-mpc-aarch64.zip` |
| **Gen1** with `vstscanner.sh` (Force VST distribution, e.g. MockbaMod) | `sfizz-for-MPC-OS-1.0.4.zip` |

Same plugin, screen, parameters and ID on both: projects move between generations. Needs MPC OS 3.x and root SSH access.
The installer zips check that they match the device. The manual is attached (`sfizz-for-MPC-OS-1.0.4-manual.pdf`).

## Install
**Installer zips** (`-mpc-armv7` / `-mpc-aarch64`):
```
scp -r sfizz-1.0.4 root@<device-ip>:/tmp/
ssh root@<device-ip> sh /tmp/sfizz-1.0.4/install.sh      # stops and restarts MPC: save first
```
**Standalone zip with `vstscanner`** (Gen1): copy the `sfztools - VST - sfizz` folder into your card's `Synths` folder
next to `vstscanner.sh`, run the one-line preset fix from its README, then `vstscanner`. The README covers MockbaMod and
other firmware. Use one method per device: the scanner rebuilds MPC's whole plugin list.

Then add **sfizz** from the plugin browser (manufacturer "sfztools"), or from Sounds › INSTRUMENTS (the sfizz tile,
then Default).

## New in 1.0.4
- **Gen2 build** (aarch64): a native 64-bit build of the same engine. A new sfizz on Gen2 starts with Sample quality
  Sinc 8, Oscillator quality High and 96 voices (Gen1: Linear, Linear, 64), may keep up to 1 GB of decoded samples
  (Gen1: 384 MB) and streams 2 s ahead of each note (Gen1: 1.25 s). Measured offline, Sinc 8 costs about 1.3× Linear's
  CPU per voice there, about a third of what upstream sfizz needs on the same CPU. The Gen2 build has been tested
  on an MPC Live III (Gen2).
- **Browser**: one touch now acts once. Every browsing action is ignored for 0.7 s after the previous one or after a new
  folder listing appears.
- **Lighter defaults on Gen1**: Sample and Oscillator quality start at Linear. Raise Sample quality when there is CPU
  to spare; projects keep the quality they were saved with.
- **Instruments tile**: sfizz shows as an artwork tile in Sounds › INSTRUMENTS (tap it, then Default).
- **Larger drop-down lists**: taller rows, bigger text and wider cells in every option list, easier to read and hit.
- **Larger SFZ browser** (the file box's ⌄): six bigger rows per page, bigger text and buttons.
- **Larger navigation arrows**: the file box's ‹ › ⌄ and the browser's page arrows are bigger and easier to hit.
- **Standalone zip** for `vstscanner.sh` setups, with a README for MockbaMod and other firmware.

Full history: [RELEASE_NOTES.md](https://github.com/gmorb/mpc-vst-sfizz/blob/v1.0.4/RELEASE_NOTES.md). Tests:
[TESTING.md](https://github.com/gmorb/mpc-vst-sfizz/blob/v1.0.4/TESTING.md).

## SHA-256
```
46cb44739ee07a0eff5fc51eaeea72cc368681ab0baa6c44cdb710f3cf5f6789  sfizz-1.0.4-mpc-armv7.zip
03aadba948791f18bd3e845e234cac3e2d2e93ff616159eb0211b4403a98cffc  sfizz-1.0.4-mpc-aarch64.zip
d4a9042bb9a8f7564149ca792d3f80bcb3a097313d4ddce435f71e7b16621efa  sfizz-for-MPC-OS-1.0.4.zip
```

sfizz is by the sfztools contributors; this is an unofficial port (BSD-2-Clause), not affiliated with inMusic or Akai.
