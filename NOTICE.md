# Components and licences

sfizz for MPC OS is BSD-2-Clause (`LICENSE`). The plugin (`sfizz_mpc.so`) is built from these components; their full
licence texts are in `src/sfizz/external/` and `src/sfizz/src/external/` in the source repository, and in `licenses/`
in the standalone release zip.

| Component | Use | Licence |
|---|---|---|
| [sfizz](https://github.com/sfztools/sfizz) 1.2.3, by the sfztools contributors | the SFZ engine (with this port's changes, `src/VENDORED.md`) | BSD-2-Clause |
| [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins), by sd88me and contributors | the VST2 wrapper, skin tools and installer | MIT |
| [Abseil](https://github.com/abseil/abseil-cpp) | containers and utilities | Apache-2.0 |
| [atomic_queue](https://github.com/max0x7ba/atomic_queue), Maxim Egorushkin | lock-free queues | MIT |
| [cephes](http://www.netlib.org/cephes/), Stephen Moshier | special functions | BSD-3-Clause (permission note in its licence file) |
| [ghc::filesystem](https://github.com/gulrak/filesystem), Steffen Schümann | file paths | MIT |
| [jsl](https://github.com/jpcima/jsl), Jean-Pierre Cimalando | allocators | BSL-1.0 |
| [SIMDe](https://github.com/simd-everywhere/simde), Evan Nemerson and contributors | SIMD portability | MIT |
| [st_audiofile](https://github.com/jpcima/st_audiofile), Jean-Pierre Cimalando | audio file reading | BSD-2-Clause |
| [dr_libs](https://github.com/mackron/dr_libs), David Reid | WAV and FLAC decoding | Public domain (Unlicense) or MIT-0 |
| [stb_vorbis](https://github.com/nothings/stb), Sean Barrett | Ogg Vorbis decoding | Public domain or MIT |
| [libaiff](https://github.com/mtrillo/libaiff), Marco Trillo | AIFF reading | MIT-style (its licence file) |
| [WavPack](https://github.com/dbry/WavPack), David Bryant | WavPack decoding | BSD-3-Clause |
| [cpuid](https://github.com/steinwurf/cpuid), Steinwurf | CPU feature detection | BSD-3-Clause |
| [HIIR](http://ldesoras.free.fr/prod.html), Laurent de Soras | half-band filters (oversampling) | WTFPL |
| [KISS FFT](https://github.com/mborgerding/kissfft), Mark Borgerding | FFT | BSD-3-Clause |
| [pugixml](https://pugixml.org), Arseny Kapoulkine | XML (Scala and settings files) | MIT |
| [spline](https://github.com/RolandRabien/spline), Roland Rabien | curve interpolation | BSD-3-Clause |
| [Surge tuning library](https://github.com/surge-synthesizer/tuning-library), Paul Walker | Scala tuning | MIT |
| [Roboto](https://fonts.google.com/specimen/Roboto), Google | the screen's font | Apache-2.0 |
| sfizz-ui artwork (logo, flask emblem), sfztools | the screen's logo and tile | BSD-2-Clause |

MPC and Force are trademarks of inMusic Brands; this project is not affiliated with inMusic or Akai.
