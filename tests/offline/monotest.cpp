// Mono / Legato / glide through the engine API: renders scenarios with the default sine to raw int16 files
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
extern "C" {
#include "engine.h"
}
static const mpc_engine_t *e; static void *o; static FILE *out;
static void blocks(int n) { int16_t b[256]; for (int i = 0; i < n; i++) { e->render(o, b, 128); fwrite(b, 2, 256, out); } }
static void on(int k) { uint8_t m[3] = {0x90, uint8_t(k), 100}; e->midi(o, m, 3); }
static void off(int k) { uint8_t m[3] = {0x80, uint8_t(k), 0}; e->midi(o, m, 3); }
int main(int, char **argv) {
    e = mpc_engine(); o = e->create(argv[1]);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const char *modes[] = {"0", "1", "2"};
    for (int mi = 0; mi < 3; mi++) for (const char *gl : {"0", "0.3162"}) {   // glide 0 / 0.2 s
        e->set_param(o, "mono_mode", modes[mi]); e->set_param(o, "glide", gl);
        char d[64] = {0}; e->get_param(o, "glide_display", d, 64);
        std::string fn = std::string("/tmp/claude-0/pg/mono_") + modes[mi] + "_" + gl + ".raw";
        out = fopen(fn.c_str(), "wb");
        blocks(10);
        on(60); blocks(100);          // 0.29 s of C4
        on(67); blocks(150);          // G4 while C4 held
        off(67); blocks(150);         // back to C4 (still held)
        off(60); blocks(200);         // all released
        fclose(out);
        printf("mode %s glide %s (%s) -> %s\n", modes[mi], gl, d, fn.c_str());
    }
    char st[1024] = {0}; e->get_param(o, "state", st, sizeof st);
    printf("state has: %s %s\n", strstr(st, "mono_mode=") ? "mono_mode" : "-", strstr(st, "glide=") ? "glide" : "-");
    e->destroy(o);
}
