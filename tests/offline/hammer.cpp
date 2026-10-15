// Chords hammered with the pedal down, real time (rt) or with the loader given all the time it wants (slow).
// Writes the output; comparing rt with slow shows any note that ran out of streamed data (starvation).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unistd.h>
#include <vector>
#include <utility>
#include "sfizz.hpp"
extern "C" void sfizz_mpc_set_gentle_loading(int);
extern "C" void sfizz_mpc_set_stream_budget(unsigned long long);
extern "C" unsigned sfizz_mpc_underruns();
extern "C" unsigned sfizz_mpc_streams_taken();
extern "C" void sfizz_mpc_stream_stats(unsigned long long*, unsigned long long*, unsigned*, unsigned*, unsigned*);
static long rss_mb() { FILE *f = fopen("/proc/self/statm", "r"); long a, b; fscanf(f, "%ld %ld", &a, &b); fclose(f); return b * sysconf(_SC_PAGESIZE) / 1048576; }
static int run(int argc, char **argv) {
    bool slow = argv[2][0] == 's';
    sfizz_mpc_set_gentle_loading(atoi(argv[5]));
    sfizz_mpc_set_stream_budget(atoll(argv[4]) << 20);
    sfz::Sfizz s; s.setSampleRate(44100); s.setSamplesPerBlock(128); s.setNumVoices(64); s.loadSfzFile(argv[1]);
    float L[128], R[128]; float *b[2] = {L, R};
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    FILE *out = fopen(argv[3], "wb");
    long base = rss_mb(), peak = base; int maxv = 0; double worst = 0;
    unsigned long long by, bud, peakBytes = 0; unsigned st, sv, fl, peakStreams = 0;
    srand(5); std::vector<std::pair<double,int>> times;
    s.hdcc(0, 64, 1.0f);
    for (int i = 0; i < 344 * 14; i++) {
        auto t0 = std::chrono::steady_clock::now();
        if (i < 344 * 8 && i % 52 == 0) {           // a chord of 4-9 keys every 150 ms for 8 s
            int n = 4 + rand() % 6, root = 28 + rand() % 50;
            for (int k = 0; k < n; k++) s.noteOn(0, std::min(108, root + rand() % 24), 30 + rand() % 97);
        }
        if (i % 344 == 200) s.hdcc(0, 64, (i / 344) % 3 == 2 ? 0.0f : 1.0f);   // pedal lifted now and then
        s.renderBlock(b, 128);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (i > 10) worst = std::max(worst, ms);
        times.push_back({ms, i});
        for (int k = 0; k < 128; k++) { fwrite(&L[k], 4, 1, out); fwrite(&R[k], 4, 1, out); }
        maxv = std::max(maxv, s.getNumActiveVoices());
        if (i % 34 == 0) {
            peak = std::max(peak, rss_mb());
            sfizz_mpc_stream_stats(&by, &bud, &st, &sv, &fl);
            peakBytes = std::max(peakBytes, by); peakStreams = std::max(peakStreams, st);
        }
        if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(25));
        else std::this_thread::sleep_until(t0 + std::chrono::microseconds(2902));
    }
    fclose(out);
    std::sort(times.rbegin(), times.rend());
    if (getenv("WORST")) for (int k = 0; k < 12; k++) fprintf(stderr, "block %d (%s): %.2f ms\n", times[k].second, times[k].second % 52 == 0 ? "chord" : (times[k].second % 344 == 200 ? "pedal" : "-"), times[k].first);
    sfizz_mpc_stream_stats(&by, &bud, &st, &sv, &fl);
    printf("%s: RSS %ld -> peak %ld MB; stream peak %llu MB of %llu, peak streams %u, refusals %u, failed %u, underruns %u, retired %u; max voices %d; worst block %.2f ms\n",
           slow ? "slow" : "rt", base, peak, peakBytes >> 20, bud >> 20, peakStreams, sv, fl, sfizz_mpc_underruns(), sfizz_mpc_streams_taken(), maxv, worst);
    return 0;
}

// x86: an executable. ARM: build with -DAB_MAIN -shared and run through abhost under qemu-arm (TESTING.md).
#ifdef AB_MAIN
extern "C" int ab_main(int argc, char **argv) { return run(argc, argv); }
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
