The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as a native instrument plugin for MPC OS standalone
hardware: SFZ libraries streamed from SD cards and USB drives, key switches, the instrument's own CC controls and Scala
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

## New in 1.1.0
- **Gen2 build** (aarch64): a native 64-bit build of the same engine. A new sfizz on Gen2 starts with Sample quality
  Sinc 8, Oscillator quality High and 96 voices (Gen1: Linear, Linear, 64), may keep up to 1 GB of decoded samples
  (Gen1: 384 MB) and streams 2 s ahead of each note (Gen1: 1.25 s). Measured offline, Sinc 8 costs about 1.3× Linear's
  CPU per voice there, about a third of what upstream sfizz needs on the same CPU. The Gen2 build has been tested
  on an MPC Live III (Gen2).
- **Larger drop-down lists**: taller rows, bigger text and wider cells in every option list, easier to read and hit.
- **Larger SFZ browser** (the file box's ⌄): six bigger rows per page, bigger text and buttons.
- **Larger navigation arrows**: the file box's ‹ › ⌄ and the browser's page arrows are bigger and easier to hit.
- **Standalone zip** for `vstscanner.sh` setups, with a README for MockbaMod and other firmware.

Full history: [RELEASE_NOTES.md](https://github.com/gmorb/mpc-vst-sfizz/blob/v1.1.0/RELEASE_NOTES.md). Tests:
[TESTING.md](https://github.com/§±secret(GITHUB_USERNAME)/mpc-vst-sfizz/blob/v1.1.0/TESTING.md).

## SHA-256
```
64d7b78a1d5b036ccd2d2776f226d76dc96dd01a46fa6cc0edd9d23b53443a67  sfizz-1.1.0-mpc-aarch64.zip
6e1a0531226436cc0ad4b17f3a2e20e2e7c369bf828a2a6294ea193bab11f2ea  sfizz-for-MPC-OS-1.1.0.zip
eafbcad326993d5c113e9b05e0e60ed478587179255960657b4d492939dbb4f1  sfizz-for-MPC-OS-1.1.0-manual.pdf
```

sfizz is by the sfztools contributors; this is an unofficial port (BSD-2-Clause), not affiliated with inMusic or Akai.