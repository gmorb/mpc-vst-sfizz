// mutate an SF2's preset data / headers and truncate it; list and convert must never crash (run under ASan/UBSan)
#include "sf2_sfz.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
int main(int, char **argv) {
    FILE *f = fopen(argv[1], "rb"); std::vector<unsigned char> d; int c; while ((c = fgetc(f)) != EOF) d.push_back(c); fclose(f);
    srand(1); int lists = 0, convs = 0;
    for (int it = 0; it < 400; it++) {
        std::vector<unsigned char> m = d;
        size_t n = m.size();
        if (it % 5 == 4) m.resize(rand() % n);                     // truncated
        else for (int k = 0; k < 1 + rand() % 40; k++) {           // flips, mostly in the last 64 kB (pdta) or the header
            size_t pos = (rand() % 3 == 0) ? rand() % 200 : n - 1 - rand() % std::min<size_t>(n, 65536);
            if (pos < m.size()) m[pos] = rand();
        }
        FILE *o = fopen("/tmp/sf2fuzz.sf2", "wb"); fwrite(m.data(), 1, m.size(), o); fclose(o);
        std::vector<sf2::Preset> ps; std::string err;
        if (sf2::list_presets("/tmp/sf2fuzz.sf2", ps, err)) lists++;
        for (size_t i = 0; i < ps.size() && i < 4; i++) {
            std::string e;
            if (!sf2::preset_to_sfz("/tmp/sf2fuzz.sf2", ps[i].bank, ps[i].program, "/tmp/sf2fuzzout", e).empty()) convs++;
        }
    }
    printf("fuzz done: %d of 400 listed, %d presets converted\n", lists, convs);
}
