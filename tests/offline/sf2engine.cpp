// SF2 support through the engine API: browse, load presets, ‹ ›, projects, program change, missing and broken files
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
extern "C" {
#include "engine.h"
}
static const mpc_engine_t *e; static void *o;
static std::string g(const char *k) { char b[512] = {0}; e->get_param(o, k, b, sizeof b); return b; }
static void ms(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }
static void wait_loaded() { for (int i = 0; i < 300; i++) { ms(100); if (g("file_name") != "Loading...") return; } }
static void rows(const char *what) {
    printf("%-26s [%s] page '%s':", what, g("br_loc").c_str(), g("br_page").c_str());
    for (int r = 1; r <= 6; r++) { char k[8]; snprintf(k, 8, "br_%d", r); printf(" [%s]", g(k).c_str()); }
    printf("\n");
}
static double play(int note) {
    int16_t buf[256]; double s = 0; int n = 0;
    uint8_t on[3] = {0x90, uint8_t(note), 100}, off[3] = {0x80, uint8_t(note), 0};
    e->midi(o, on, 3);
    for (int b = 0; b < 200; b++) { e->render(o, buf, 128); for (int i = 0; i < 256; i++) { s += double(buf[i]) * buf[i]; n++; } }
    e->midi(o, off, 3);
    for (int b = 0; b < 50; b++) e->render(o, buf, 128);
    return std::sqrt(s / n) / 32768.0;
}
static void status(const char *what) { printf("%-26s file '%s' | status '%s' | regions %s\n", what, g("file_name").c_str(), g("status").c_str(), g("n_regions").c_str()); }
int main(int, char **argv) {
    e = mpc_engine(); o = e->create(argv[1]);
    e->set_param(o, "browse", "1"); ms(800);
    e->set_param(o, "br_home", "1"); ms(800);
    e->set_param(o, "br_1", "1"); ms(800);                       // Plugin Library
    rows("library");
    e->set_param(o, "br_3", "1"); ms(800); rows("tapped TimGM6mb.sf2");
    e->set_param(o, "br_1", "1"); wait_loaded(); status("tapped preset row 1");
    printf("  row 1 lit: %s; note C4 rms %.4f\n", g("br_1_on").c_str(), play(60));
    auto t0 = std::chrono::steady_clock::now();
    ms(800); e->set_param(o, "file_next", "1"); ms(200); wait_loaded(); status("file_next");
    ms(800); e->set_param(o, "file_next", "1"); ms(200); wait_loaded(); status("file_next again");
    ms(800); e->set_param(o, "file_prev", "1"); ms(200); wait_loaded(); status("file_prev (cached)");
    printf("  ‹ › took %.1f s for 3 steps\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    uint8_t cc0[3] = {0xB0, 0, 0}, pc[2] = {0xC0, 40};
    int16_t buf[256];
    e->midi(o, cc0, 3); e->midi(o, pc, 2); e->render(o, buf, 128); ms(300); wait_loaded(); status("program change 40");
    printf("  violin-ish C5 rms %.4f\n", play(72));
    uint8_t pc2[2] = {0xC0, 127}; uint8_t cc128[3] = {0xB0, 0, 127};
    e->midi(o, cc128, 3); e->midi(o, pc2, 2); e->render(o, buf, 128); ms(300); wait_loaded(); status("bank 127 program 127");
    char st[2048] = {0}; e->get_param(o, "state", st, sizeof st);
    std::string state = st; printf("  state file line: %s\n", state.substr(state.find("file="), state.find('\n', state.find("file=")) - state.find("file=")).c_str());
    e->destroy(o);
    // a project reopened: the preset loads from its saved path
    o = e->create(argv[1]); ms(500);
    e->set_param(o, "state", state.c_str()); ms(1500); wait_loaded(); status("project restored");
    // the same preset with the SoundFont moved elsewhere: found by name
    std::string moved = state; size_t p = moved.find("Instruments/"); moved.replace(p, 12, "Gone/");
    printf("  moved state: %s\n", moved.substr(moved.find("file="), moved.find('\n', moved.find("file=")) - moved.find("file=")).c_str());
    e->set_param(o, "state", moved.c_str()); ms(1500); wait_loaded(); status("project, SoundFont moved");
    // a damaged SoundFont
    e->set_param(o, "browse", "1"); ms(800); e->set_param(o, "br_home", "1"); ms(800); e->set_param(o, "br_1", "1"); ms(800);
    rows("library again");
    e->set_param(o, "br_1", "1"); ms(800); status("tapped broken.sf2"); rows("after broken");
    e->set_param(o, "br_2", "1"); ms(800); rows("Majora.sf2 presets");
    e->set_param(o, "br_pgdn", "1"); ms(800); rows("page down");
    e->set_param(o, "br_up", "1"); ms(800); rows("Up");
    e->destroy(o);
    return 0;
}
