// ARM instruction-count benchmark (run under qemu-arm + libinsn). Modes:
//   ab render <sfz> <quality> <voices> <blocks>  -- holds <voices> notes, waits for streaming, renders <blocks>
//   ab decode <sfz> <notes>                       -- upstream loading: each note's whole file decodes on loader threads
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "sfizz.hpp"
extern "C" void sfizz_mpc_set_gentle_loading(int);
extern "C" int ab_main(int argc, char **argv);
extern "C" int ab_main(int argc, char **argv) {
    sfizz_mpc_set_gentle_loading(0);
    sfz::Sfizz s; s.setSampleRate(44100); s.setSamplesPerBlock(128); s.setNumVoices(256);
    float L[128], R[128]; float *b[2] = {L, R};
    if (!strcmp(argv[1], "render")) {
        int q = atoi(argv[3]), nv = atoi(argv[4]), blocks = atoi(argv[5]);
        s.setSampleQuality(sfz::Sfizz::ProcessLive, q);
        s.loadSfzFile(argv[2]);
        s.hdcc(0, 64, 1.0f);
        for (int i = 0; i < nv; i++) s.noteOn(0, 36 + (i * 7) % 60, 90);
        s.renderBlock(b, 128);
        std::this_thread::sleep_for(std::chrono::seconds(40));   // let the files stream completely
        fprintf(stderr, "ABMAIN %p\n", (void*)&ab_main);
        for (int i = 0; i < blocks; i++) s.renderBlock(b, 128);
        printf("voices active %d\n", s.getNumActiveVoices());
    } else {
        int n = atoi(argv[3]);
        s.loadSfzFile(argv[2]);
        for (int i = 0; i < n; i++) s.noteOn(0, 40 + i * 5, 90);
        s.renderBlock(b, 128);
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}
