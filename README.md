# sfizz for MPC OS

The [sfizz](https://github.com/sfztools/sfizz) 1.2.3 SFZ sampler as a native instrument plugin for Akai MPC OS standalone
hardware (Gen1: Force, MPC Live / Live II, One, X, Key 61; RK3288, armv7), with a one-page screen modelled on sfizz's own
editor. Built on [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (VST2 wrapper, skin tools, installer).
Requires a modded unit with root access, as every plugin from that project does.

![The screen](docs/screen.png)

**Download** the `sfizz-<version>-mpc-armv7.zip` from [Releases](../../releases). **Manual:** [docs/MANUAL.pdf](docs/MANUAL.pdf).

## Install
1. Unzip, copy the `sfizz-<version>` folder to the device: `scp -r sfizz-1.0.2 root@<device-ip>:/tmp/`
2. Run the installer: `ssh root@<device-ip> sh /tmp/sfizz-1.0.2/install.sh` (it stops and restarts MPC: save first).
3. Add **sfizz** to a track from the plugin browser. Tap ⌄ to open an SFZ from an SD card or USB drive.

Requirements: MPC OS 3.x, a Gen1 (32-bit) device, root SSH. The zip's `INSTALL.md` also has the manual steps.
`uninstall.sh` removes it and keeps the SFZ files you put in its library folder.

## What's on the page
sfizz's editor has four tabs; on the MPC they sit side by side on one page, under the same header and over the same piano:

| sfizz | Here |
|---|---|
| File box: name, ‹ › (previous / next SFZ in the folder), ⌄ menu | Same. ⌄ opens the SFZ browser over the left half of the page |
| Key switch badge and label, "No key switch" | Same |
| Voices / Max / Memory | Same; Max is a drop-down |
| Volume (CC 7) and Pan (CC 10) knobs | Same |
| Information tab | Left panel: Curves, Masters, Groups, Regions, Samples |
| Controls tab | Middle panel: the instrument's CC knobs (its `label_cc` names, CC badges), up to 8 |
| Settings tab | Right panel: Engine options, Rendering quality, Tuning (Scala file, root key, frequency with the historical presets, stretch) |
| Piano | Same strip, as a picture (it doesn't light up keys) |

Left out, because they mean nothing on the MPC: the volume meters, Theme, Zoom, the "when freewheeling" quality rows
(MPC never freewheels), and the file menu's Edit / Create / Open folder entries.

**Q-Links**: Volume, Pan, the 8 CC knobs, Frequency, Stretch, Sample and Oscillator quality.

## Instruments
The browser lists the plugin's own library (`sfizz/Instruments` inside the plugin folder), every SD card and USB drive
(`/media/*`) and `/sdcard`. Tap a folder to open it, an SFZ to load it; the loaded one is lit green. ‹ › in the header step
through the SFZ files of the loaded one's folder. A project saves the file by path and finds it by name if the card or
folder moved. Scala files (`.scl`) go in `sfizz/Scales`; the Scala file button steps through them, × resets to equal temperament.

Samples stream from disk (sfizz's preload + streaming), so libraries larger than the MPC's memory play in full; raise
Preload size if a slow card gives dropouts at note starts. A good free library to start with:
[Salamander Grand Piano V3](https://github.com/sfzinstruments/SalamanderGrandPiano) (16 layers, 24-bit FLAC, CC BY 3.0).

## CPU and memory
sfizz normally reads the whole rest of a sample file the moment a key is hit, on a real-time thread, and keeps it with no
memory limit: with big piano libraries that spiked the MPC's CPU to 100% on every note, and enough chords could exhaust
memory. This port streams files a little ahead of each note on a normal-priority thread, within a memory budget (a third
of the free memory, 48 to 384 MB); when it runs out, the notes that have played longest fade out first. Rendering is
about 3.5× cheaper per voice on ARM than upstream at Sinc 8 (NEON paths, stages merged into single passes, unused work
skipped), with the same sound. Details and measurements in `src/VENDORED.md`.

## Sound quality
Sample quality defaults to **Sinc 8**. Measured offline it removes about 24 dB of interpolation distortion over sfizz's
own default (Polynomial) on samples pitched down, for a little more CPU per voice. Polynomial is the cheapest; Sinc 24 and up are cleaner still. Samples are kept in 32-bit float, so 24-bit libraries keep their depth.

## Build
```
pip install ziglang==0.13.0        # the armhf cross compiler (glibc 2.28 target, below MPC OS 2.x's 2.31)
git clone https://github.com/sd88me/mpc-vst-plugins && git -C mpc-vst-plugins checkout e18a0a1da84515096d354f399dd4cd7f9c079fb5
MPC_VST=$PWD/mpc-vst-plugins HTML_ART_PYTHON=/path/to/python-with-playwright-and-pillow ./build.sh
```
`build.sh skin` draws the art (`design.py`) and builds the skin; `build.sh so` builds `build/sfizz_mpc.so`. Tests are in
[TESTING.md](TESTING.md); releasing and the catalog are in [CLAUDE.md](CLAUDE.md) (instructions for people and agents alike).

## Files
- `src/sfizz_mpc.cpp`: the engine adapter (sfizz → `mpc_engine()`); `src/sfizz/`: vendored sfizz 1.2.3 (`src/VENDORED.md`)
- `params.json`: the parameters, in VST index order (never reorder: projects store values by index)
- `design.py` → `art/`, `layout.conf`; `skin_post.py`: TUI.json touch-ups (Roboto text, values inside knobs)
- `data/`: the plugin's `sfizz/` folder (default instrument and scale, empty library folders)
- `tests/`, `tools/`: offline tests (x86 host test libraries, ARM VST2 host under QEMU, screen mock)
- `packaging/catalog/sfizz.json`: the MPC OS plugin catalog entry; `docs/`: manual, screenshots

## Credits and licence
sfizz is by the [sfztools](https://github.com/sfztools) contributors; this port uses its 1.2.3 engine with documented changes to sample streaming, memory and speed (`src/VENDORED.md`) and draws
its screen after sfizz's editor (logo and emblem from sfizz-ui's resources, Roboto). It is not an official sfztools
release. BSD-2-Clause (`LICENSE`), as sfizz; sfizz's dependencies keep their own licences (`src/sfizz/external`);
Roboto is Apache-2.0. MPC and Force are trademarks of inMusic; this project is not affiliated with inMusic or Akai.
