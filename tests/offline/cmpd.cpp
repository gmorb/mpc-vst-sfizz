// Deterministic render for comparing builds: cmpr <sfz> <quality> <out.raw> [bend|pan]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "sfizz.hpp"
extern "C" void sfizz_mpc_set_gentle_loading(int) __attribute__((weak));
static int run(int argc, char **argv) {
    if (sfizz_mpc_set_gentle_loading) sfizz_mpc_set_gentle_loading(0);   // whole files, as upstream: timing-independent
    sfz::Sfizz s; s.setSampleRate(44100); s.setSamplesPerBlock(128); s.setNumVoices(64);
    s.setSampleQuality(sfz::Sfizz::ProcessLive, atoi(argv[2]));
    s.setSampleQuality(sfz::Sfizz::ProcessFreewheeling, atoi(argv[2]));
    s.loadSfzFile(argv[1]);
    const char *mode = argc > 4 ? argv[4] : "";
    float L[128], R[128]; float *b[2] = {L, R};
    const int notes[6] = {36, 48, 55, 60, 64, 79};
    for (int n : notes) s.noteOn(0, n, 100);
    s.renderBlock(b, 128);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));   // all files decoded
    FILE *f = fopen(argv[3], "wb");
    for (int i = 0; i < 344 * 3; i++) {
        if (!strcmp(mode, "bend")) s.pitchWheel(5, (i * 37) % 16000 - 8000);
        if (!strcmp(mode, "pan")) s.hdcc(7, 10, float((i * 13) % 128) / 127.0f);
        if (i == 344) { s.noteOn(0, 67, 80); s.renderBlock(b, 128); ; }
        if (i == 600) for (int n : notes) s.noteOff(0, n, 0);
        s.renderBlock(b, 128);
        fwrite(L, 4, 128, f); fwrite(R, 4, 128, f);
    }
    fclose(f);
    printf("ok voices=%d\n", s.getNumActiveVoices());
    return 0;
}

// x86: an executable. ARM: build with -DAB_MAIN -shared and run through abhost under qemu-arm (TESTING.md).
#ifdef AB_MAIN
extern "C" int ab_main(int argc, char **argv) { return run(argc, argv); }
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
