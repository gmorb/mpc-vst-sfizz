The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as a native instrument plugin for MPC OS standalone
hardware: SFZ libraries streamed from SD cards and USB drives, key switches, the instrument's own CC controls and Scala
tuning, on a one-page screen modelled on sfizz's own editor.

## Download the zip for your device
| Device | Zip |
|---|---|
| **Gen1**: MPC Live / Live II, One, X, Key 61, Force (RK3288, 32-bit) | `sfizz-1.0.4-mpc-armv7.zip` |
| **Gen2**: MPC Live III and other RK3588 units (64-bit) | `sfizz-1.0.4-mpc-aarch64.zip` |

Same plugin, screen, parameters and ID on both: projects move between generations. Needs MPC OS 3.x and root SSH access.
The installer checks that the zip matches the device. The manual is attached (`sfizz-for-MPC-OS-1.0.4-manual.pdf`).

## Install
```
scp -r sfizz-1.0.4 root@<device-ip>:/tmp/
ssh root@<device-ip> sh /tmp/sfizz-1.0.4/install.sh      # stops and restarts MPC: save first
```
Then add **sfizz** from the plugin browser, or from Sounds › INSTRUMENTS (the sfizz tile, then Default).

## New in 1.0.4
- **Gen2 build** (aarch64): a native 64-bit build of the same engine. A new sfizz on Gen2 starts with Sample quality
  Sinc 8, Oscillator quality High and 96 voices (Gen1: Linear, Linear, 64), may keep up to 1 GB of decoded samples
  (Gen1: 384 MB) and streams 2 s ahead of each note (Gen1: 1.25 s). Measured offline, Sinc 8 costs about 1.3× Linear's
  CPU per voice there, about a third of what upstream sfizz needs on the same CPU. The Gen2 build has been tested
  offline (emulated) only so far.
- **Browser**: one touch now acts once. Every browsing action is ignored for 0.7 s after the previous one or after a new
  folder listing appears.
- **Lighter defaults on Gen1**: Sample and Oscillator quality start at Linear. Raise Sample quality when there is CPU
  to spare; projects keep the quality they were saved with.
- **Instruments tile**: sfizz shows as an artwork tile in Sounds › INSTRUMENTS (tap it, then Default).

Full history: [RELEASE_NOTES.md](https://github.com/gmorb/mpc-vst-sfizz/blob/v1.0.4/RELEASE_NOTES.md). Tests:
[TESTING.md](https://github.com/gmorb/mpc-vst-sfizz/blob/v1.0.4/TESTING.md).

## SHA-256
```
28aa71aeba2e2ffab017301b2b7adac8a1cbc30f2df9ec617094e9b88f4ae85f  sfizz-1.0.4-mpc-armv7.zip
9ab2e1764485ee140492c35385345162f40f13fee187002f4af593f6bd5fb15d  sfizz-1.0.4-mpc-aarch64.zip
```

sfizz is by the sfztools contributors; this is an unofficial port (BSD-2-Clause), not affiliated with inMusic or Akai.
