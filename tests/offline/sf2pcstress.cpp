// program changes while notes play, in real time: SoundFont presets convert and swap under the audio thread
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
extern "C" {
#include "engine.h"
}
int main(int, char **argv) {
    const mpc_engine_t *e = mpc_engine(); void *o = e->create(argv[1]);
    std::string st = std::string("sfizz-mpc 1\nfile=") + argv[1] + "/Instruments/TimGM6mb.sf2#0:0\n";
    e->set_param(o, "state", st.c_str());
    int16_t buf[256]; unsigned seed = 7;
    for (int b = 0; b < 344 * 12; b++) {
        auto due = std::chrono::steady_clock::now() + std::chrono::microseconds(2902);
        seed = seed * 1103515245 + 12345;
        if (b % 8 == 0) { uint8_t m[3] = {0x90, uint8_t(36 + (seed >> 16) % 60), 100}; e->midi(o, m, 3); }
        if (b % 8 == 4) { uint8_t m[3] = {0x80, uint8_t(36 + (seed >> 20) % 60), 0}; e->midi(o, m, 3); }
        if (b % 100 == 50) { uint8_t pc[2] = {0xC0, uint8_t((seed >> 8) % 128)}; e->midi(o, pc, 2); }
        if (b % 500 == 250) { uint8_t cc[3] = {0xB0, 0, uint8_t(b % 1000 == 250 ? 0 : 128 & 127)}; e->midi(o, cc, 3); }
        e->render(o, buf, 128);
        std::this_thread::sleep_until(due);
    }
    char name[256] = {0}; e->get_param(o, "file_name", name, sizeof name);
    printf("pc stress done, last preset '%s'\n", name);
    e->destroy(o);
}
