// Stress the engine's streaming under sanitizers: fast notes and chords with the pedal in real time, a tiny memory budget
// (eviction and starvation), instrument switches, preload-size and voice-count changes while files stream, two instances
// sharing the budget, instances closed mid-stream.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
extern "C" {
#include "engine.h"
void sfizz_mpc_set_stream_budget(unsigned long long bytes);
void sfizz_mpc_stream_stats(unsigned long long*, unsigned long long*, unsigned*, unsigned*, unsigned*);
unsigned sfizz_mpc_underruns();
unsigned sfizz_mpc_streams_taken();
}
static void play(const mpc_engine_t *e, void *o, int blocks, int seed, const char *alt, const char *mpcdata) {
    int16_t out[256];
    srand(seed);
    char chunk[1024];
    for (int i = 0; i < blocks; i++) {
        auto due = std::chrono::steady_clock::now() + std::chrono::microseconds(2902);
        if (i % 6 == 0) { uint8_t m[3] = {0x90, uint8_t(30 + rand() % 70), uint8_t(20 + rand() % 107)}; e->midi(o, m, 3); }
        if (i % 6 == 3) { uint8_t m[3] = {0x80, uint8_t(30 + rand() % 70), 0}; e->midi(o, m, 3); }
        if (i % 97 == 0) { uint8_t m[3] = {0xB0, 64, uint8_t((i / 97) % 2 ? 127 : 0)}; e->midi(o, m, 3); }
        if (i % 52 == 0) for (int k = 0; k < 4 + rand() % 6; k++) { uint8_t m[3] = {0x90, uint8_t(40 + k * 5 + rand() % 3), 100}; e->midi(o, m, 3); }
        if (i == blocks / 3) e->set_param(o, "preload", "5");
        if (i == blocks / 2) e->set_param(o, "polyphony", "9");
        if (alt && i == 2 * blocks / 3) {
            snprintf(chunk, sizeof chunk, "sfizz-mpc 1\nfile=%s\n", alt);
            e->set_param(o, "state", chunk);
        }
        e->render(o, out, 128);
        std::this_thread::sleep_until(due);
    }
    (void)mpcdata;
}
static int run(int argc, char **argv) {
    const mpc_engine_t *e = mpc_engine();
    char chunk[1024];
    for (int round = 0; round < 2; round++) {
        void *a = e->create(argv[1]);
        void *b = e->create(argv[1]);
        sfizz_mpc_set_stream_budget((getenv("STRESS_BUDGET_MB") ? strtoull(getenv("STRESS_BUDGET_MB"), 0, 10) : 24ull) << 20);   // tiny by default: forces eviction and starvation
        snprintf(chunk, sizeof chunk, "sfizz-mpc 1\nfile=%s\n", argv[2]);
        e->set_param(a, "state", chunk);
        e->set_param(b, "state", chunk);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        std::thread tb([&] { play(e, b, 344 * 4, 11 + round, nullptr, argv[1]); });
        play(e, a, 344 * 6, 7 + round, argc > 3 ? argv[3] : nullptr, argv[1]);
        tb.join();
        unsigned long long bytes, budget; unsigned streams, starved, failed;
        sfizz_mpc_stream_stats(&bytes, &budget, &streams, &starved, &failed);
        printf("round %d: stream memory %llu MB of %llu MB, %u streams, %u budget refusals, %u failed allocations, %u underruns, %u retired\n", round,
               bytes >> 20, budget >> 20, streams, starved, failed, sfizz_mpc_underruns(), sfizz_mpc_streams_taken());
        e->destroy(b);   // mid-stream
        e->destroy(a);
    }
    printf("stress OK\n");
    return 0;
}

// x86: an executable. ARM: build with -DAB_MAIN -shared and run through abhost under qemu-arm (TESTING.md).
#ifdef AB_MAIN
extern "C" int ab_main(int argc, char **argv) { return run(argc, argv); }
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
