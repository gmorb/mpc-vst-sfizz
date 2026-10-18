// usage: sf2conv <in.sf2> <out folder> (build: g++ -std=gnu++17 -Isrc tests/offline/sf2conv.cpp src/sf2_sfz.cpp)
// convert every preset of an SF2 with sf2_sfz.cpp into <out>/<bank>-<prog>.sfz (test harness)
#include "sf2_sfz.h"
#include <cstdio>
int main(int argc, char **argv) {
    std::vector<sf2::Preset> ps; std::string err;
    if (!sf2::list_presets(argv[1], ps, err)) { printf("list error: %s\n", err.c_str()); return 1; }
    int ok = 0;
    for (auto &p : ps) {
        std::string name, e;
        std::string sfz = sf2::preset_to_sfz(argv[1], p.bank, p.program, argv[2], e, &name);
        if (sfz.empty()) printf("%d:%d %s: %s\n", p.bank, p.program, p.name.c_str(), e.c_str()); else ok++;
    }
    printf("%zu presets, %d converted\n", ps.size(), ok);
}
