// Mono/Legato stress: random overlapping notes, the pedal, all-notes-off, with Mode and Glide changed from another
// thread (the screen) while the audio thread plays; then a keyswitch check in Legato mode
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <cmath>
extern "C" {
#include "engine.h"
}
int main(int, char **argv) {
    const mpc_engine_t *e = mpc_engine(); void *o = e->create(argv[1]);
    std::atomic<bool> done{false};
    std::thread ui([&] { unsigned s = 3; while (!done) {
        s = s * 1103515245 + 12345; char v[16];
        snprintf(v, 16, "%u", (s >> 16) % 3); e->set_param(o, "mono_mode", v);
        snprintf(v, 16, "%.3f", ((s >> 8) % 1000) / 1000.0); e->set_param(o, "glide", v);
        char b[64]; e->get_param(o, "glide_display", b, 64);
        std::this_thread::sleep_for(std::chrono::milliseconds(37)); } });
    int16_t buf[256]; unsigned seed = 11;
    for (int b = 0; b < 344 * 8; b++) {
        seed = seed * 1103515245 + 12345;
        int k = 40 + (seed >> 16) % 40;
        if ((seed >> 4) % 3 == 0) { uint8_t m[3] = {0x90, uint8_t(k), uint8_t(30 + (seed >> 8) % 90)}; e->midi(o, m, 3); }
        if ((seed >> 6) % 3 == 0) { uint8_t m[3] = {0x80, uint8_t(40 + (seed >> 12) % 40), 0}; e->midi(o, m, 3); }
        if (b % 200 == 100) { uint8_t m[3] = {0xB0, 64, uint8_t(b % 400 == 100 ? 127 : 0)}; e->midi(o, m, 3); }
        if (b % 700 == 350) { uint8_t m[3] = {0xB0, 123, 0}; e->midi(o, m, 3); }
        e->render(o, buf, 128);
    }
    done = true; ui.join();
    // keyswitch in Legato: hold a note, press the keyswitch key: the switch changes and the note keeps sounding
    std::string st = std::string("sfizz-mpc 1\nfile=") + argv[2] + "\nmono_mode=2\n";
    e->set_param(o, "state", st.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    uint8_t all[3] = {0xB0, 123, 0}; e->midi(o, all, 3); e->render(o, buf, 128);
    char lab[64]; e->get_param(o, "ks_label", lab, 64); printf("keyswitch before: %s\n", lab);
    uint8_t on[3] = {0x90, 60, 100}; e->midi(o, on, 3);
    for (int i = 0; i < 20; i++) e->render(o, buf, 128);
    uint8_t ks[3] = {0x90, 25, 100}, ksoff[3] = {0x80, 25, 0}; e->midi(o, ks, 3); e->midi(o, ksoff, 3);
    double r = 0; for (int i = 0; i < 40; i++) { e->render(o, buf, 128); for (int j = 0; j < 256; j++) r += double(buf[j]) * buf[j]; }
    for (int i = 0; i < 40; i++) e->render(o, buf, 128);
    e->get_param(o, "ks_label", lab, 64); printf("keyswitch after C#1: %s; note 60 still sounding: rms %.4f\n", lab, std::sqrt(r / (40 * 256)) / 32768);
    e->destroy(o);
    printf("mono stress done\n");
}
