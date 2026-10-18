// SoundFont 2 -> SFZ for sfizz for MPC OS (see sf2_sfz.h). A C++ port of tools/sf2sfz.py (tests/offline/sf2_reference.py compares them): the SFZ text it writes is the
// same, region for region, except that samples are named by index (Samples/s<N>.wav) so a cache can grow one preset at
// a time. Only the preset data (pdta) is read into memory; sample data is read per sample when its WAV is written.
#include "sf2_sfz.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace sf2 {
namespace {

using Gens = std::map<int, int>;   // generator -> amount; key/velocity ranges (43, 44) as lo + 256 * hi

struct SampleHdr {
    std::string name;
    uint32_t start, end, loopstart, loopend, rate;
    int pitch, correction;
    uint16_t type;
};
struct Zones {
    std::string name;
    Gens global;
    std::vector<Gens> zones;
};
struct PresetData : Zones {
    int bank = 0, program = 0;
};

struct File {
    FILE *f = nullptr;
    long smpl_off = -1, sm24_off = -1;
    uint32_t smpl_size = 0, sm24_size = 0;
    std::vector<PresetData> presets;
    std::vector<Zones> instruments;
    std::vector<SampleHdr> samples;
    ~File() { if (f) std::fclose(f); }
};

uint16_t u16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

std::string cstr(const uint8_t *p, size_t n) {
    size_t len = 0;
    while (len < n && p[len]) len++;
    std::string s(reinterpret_cast<const char *>(p), len);
    for (char &c : s)
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '_';   // latin-1 to ASCII
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool read_at(FILE *f, long off, void *buf, size_t n) {
    return !std::fseek(f, off, SEEK_SET) && std::fread(buf, 1, n, f) == n;
}

// the generators of bag range [a, b) in a gen list (4-byte records), as the SF2 spec types them
Gens gens(const std::vector<uint8_t> &gen, size_t a, size_t b) {
    Gens g;
    size_t n = gen.size() / 4;
    for (size_t i = a; i < b && i < n; i++) {
        int oper = u16(&gen[i * 4]);
        uint16_t amt = u16(&gen[i * 4 + 2]);
        switch (oper) {
        case 43: case 44: g[oper] = (amt & 0xFF) + 256 * (amt >> 8); break;
        case 41: case 53: case 54: case 56: case 57: case 58: case 46: case 47: g[oper] = amt; break;
        default: g[oper] = int16_t(amt); break;
        }
    }
    return g;
}

// zones of each header (presets or instruments): bag[k] .. bag[k+1] of the next header; `link` marks real zones
template <class Hdr>
bool zones(std::vector<Hdr> &out, const std::vector<int> &first_bag, const std::vector<uint8_t> &bag,
           const std::vector<uint8_t> &gen, int link, std::string &error) {
    size_t nbag = bag.size() / 4;
    for (size_t i = 0; i + 1 < first_bag.size(); i++) {
        int a = first_bag[i], b = first_bag[i + 1];
        if (a < 0 || b < a || size_t(b) >= nbag + (b == a ? 1 : 0)) { error = "damaged SoundFont (zone index)"; return false; }
        std::vector<Gens> zl;
        for (int z = a; z < b; z++) {
            size_t g0 = u16(&bag[size_t(z) * 4]), g1 = u16(&bag[size_t(z + 1) * 4]);
            if (g1 < g0) { error = "damaged SoundFont (generator index)"; return false; }
            zl.push_back(gens(gen, g0, g1));
        }
        if (!zl.empty() && !zl[0].count(link)) out[i].global = zl[0];
        for (Gens &z : zl)
            if (z.count(link)) out[i].zones.push_back(std::move(z));
    }
    return true;
}

std::unique_ptr<File> open_sf2(const std::string &path, std::string &error) {
    std::unique_ptr<File> sf(new File);
    sf->f = std::fopen(path.c_str(), "rb");
    if (!sf->f) { error = "can't open the file"; return nullptr; }
    std::fseek(sf->f, 0, SEEK_END);
    long fsize = std::ftell(sf->f);
    uint8_t h[12];
    if (fsize < 12 || !read_at(sf->f, 0, h, 12) || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "sfbk", 4)) {
        error = "not a SoundFont 2 file";
        return nullptr;
    }
    std::map<std::string, std::vector<uint8_t>> pdta;
    int major = 2;
    long off = 12;
    while (off + 8 <= fsize) {
        uint8_t ch[12];
        if (!read_at(sf->f, off, ch, 8)) break;
        uint32_t size = u32(ch + 4);
        long end = std::min(fsize, off + 8 + long(size));
        if (!std::memcmp(ch, "LIST", 4) && read_at(sf->f, off + 8, ch + 8, 4)) {
            std::string kind(reinterpret_cast<char *>(ch + 8), 4);
            long o2 = off + 12;
            while (o2 + 8 <= end) {
                uint8_t sc[8];
                if (!read_at(sf->f, o2, sc, 8)) break;
                std::string id(reinterpret_cast<char *>(sc), 4);
                uint32_t s2 = u32(sc + 4);
                if (o2 + 8 + long(s2) > end) s2 = uint32_t(end - o2 - 8);
                if (kind == "INFO" && id == "ifil" && s2 >= 4) {
                    uint8_t v[4];
                    if (read_at(sf->f, o2 + 8, v, 4)) major = u16(v);
                } else if (kind == "sdta" && id == "smpl") {
                    sf->smpl_off = o2 + 8; sf->smpl_size = s2;
                } else if (kind == "sdta" && id == "sm24") {
                    sf->sm24_off = o2 + 8; sf->sm24_size = s2;
                } else if (kind == "pdta" && s2 <= (64u << 20)) {
                    std::vector<uint8_t> &buf = pdta[id];
                    buf.resize(s2);
                    if (s2 && !read_at(sf->f, o2 + 8, buf.data(), s2)) { error = "can't read the file"; return nullptr; }
                }
                o2 += 8 + long(s2) + (s2 & 1);
            }
        }
        off += 8 + long(size) + (size & 1);
    }
    if (major >= 3) { error = "SF3 (compressed) SoundFonts aren't supported: use the SF2 version"; return nullptr; }
    for (const char *id : {"phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr"})
        if (!pdta.count(id)) { error = "damaged SoundFont (no " + std::string(id) + ")"; return nullptr; }
    const std::vector<uint8_t> &phdr = pdta["phdr"], &inst = pdta["inst"], &shdr = pdta["shdr"];
    size_t np = phdr.size() / 38, ni = inst.size() / 22, ns = shdr.size() / 46;
    if (np < 1 || ni < 1 || ns < 1) { error = "damaged SoundFont (empty lists)"; return nullptr; }

    std::vector<int> pbag_first, ibag_first;
    sf->presets.resize(np - 1);
    for (size_t i = 0; i < np; i++) {
        const uint8_t *r = &phdr[i * 38];
        pbag_first.push_back(u16(r + 24));
        if (i + 1 < np) {
            sf->presets[i].name = cstr(r, 20);
            sf->presets[i].program = u16(r + 20);
            sf->presets[i].bank = u16(r + 22);
        }
    }
    sf->instruments.resize(ni - 1);
    for (size_t i = 0; i < ni; i++) {
        const uint8_t *r = &inst[i * 22];
        ibag_first.push_back(u16(r + 20));
        if (i + 1 < ni) sf->instruments[i].name = cstr(r, 20);
    }
    if (!zones(sf->presets, pbag_first, pdta["pbag"], pdta["pgen"], 41, error)) return nullptr;
    if (!zones(sf->instruments, ibag_first, pdta["ibag"], pdta["igen"], 53, error)) return nullptr;
    for (size_t i = 0; i + 1 < ns; i++) {
        const uint8_t *r = &shdr[i * 46];
        SampleHdr s;
        s.name = cstr(r, 20);
        s.start = u32(r + 20); s.end = u32(r + 24); s.loopstart = u32(r + 28); s.loopend = u32(r + 32);
        s.rate = u32(r + 36); s.pitch = r[40]; s.correction = int8_t(r[41]); s.type = u16(r + 44);
        sf->samples.push_back(s);
    }
    if (sf->smpl_off < 0) { error = "damaged SoundFont (no sample data)"; return nullptr; }
    return sf;
}

// ---- conversion (tools/sf2sfz.py, function for function) ----

const std::map<int, int> DEFAULTS = {{8, 13500}, {21, -12000}, {22, 0}, {23, -12000}, {24, 0}, {25, -12000},
    {26, -12000}, {27, -12000}, {28, -12000}, {29, 0}, {30, -12000}, {33, -12000}, {34, -12000}, {35, -12000},
    {36, -12000}, {37, 0}, {38, -12000}, {56, 100}, {58, -1}};
bool additive_skip(int k) {
    for (int s : {41, 53, 43, 44, 46, 47, 54, 57, 58, 0, 1, 2, 3, 4, 12, 45, 50})
        if (k == s) return true;
    return false;
}
int get(const Gens &g, int k, int def) {
    auto it = g.find(k);
    return it == g.end() ? def : it->second;
}
double tc2s(double tc) { return std::pow(2.0, tc / 1200.0); }
double abs_cents_hz(double c) { return 8.176 * std::pow(2.0, c / 1200.0); }

std::string fmt(double x, int nd = 4) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", nd, x);
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}
std::string strf(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string strf(const char *f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

Gens merged(const Gens &ig, const Gens &iz, const Gens &pg, const Gens &pz) {
    Gens g(DEFAULTS.begin(), DEFAULTS.end());
    for (auto &kv : ig) g[kv.first] = kv.second;
    for (auto &kv : iz) g[kv.first] = kv.second;
    Gens p = pg;
    for (auto &kv : pz) p[kv.first] = kv.second;
    for (auto &kv : p) {
        int k = kv.first, v = kv.second;
        if (k == 43 || k == 44) {
            int cur = get(g, k, 0 + 256 * 127);
            int lo = std::max(cur & 0xFF, v & 0xFF), hi = std::min(cur >> 8, v >> 8);
            g[k] = lo + 256 * hi;   // lo > hi is an empty range: the region is dropped
        } else if (!additive_skip(k)) {
            g[k] = get(g, k, 0) + v;
        }
    }
    return g;
}

// the region's opcodes, or false for an empty key/velocity range
bool region_opcodes(const Gens &g, const SampleHdr &s, const std::string &wav, long nframes, std::vector<std::string> &o) {
    int kr = get(g, 43, 256 * 127), vr = get(g, 44, 256 * 127);
    int lo = kr & 0xFF, hi = kr >> 8, vlo = vr & 0xFF, vhi = vr >> 8;
    if (lo > hi || vlo > vhi) return false;
    o.push_back("sample=" + wav);
    o.push_back(strf("lokey=%d hikey=%d", lo, hi));
    if (!(vlo == 0 && vhi == 127)) o.push_back(strf("lovel=%d hivel=%d", vlo, vhi));
    int root = get(g, 58, -1) >= 0 ? g.at(58) : s.pitch;
    if (root > 127) root = 60;
    o.push_back(strf("pitch_keycenter=%d", root));
    int tune = get(g, 51, 0) * 100 + get(g, 52, 0) + s.correction;
    bool fixed = g.count(46) && g.at(46) >= 0 && g.at(46) <= 127;
    if (fixed) tune += (g.at(46) - root) * get(g, 56, 100);
    if (tune) o.push_back(strf("tune=%d", tune));
    if (fixed) o.push_back("pitch_keytrack=0");
    else if (get(g, 56, 100) != 100) o.push_back(strf("pitch_keytrack=%d", g.at(56)));
    long start = get(g, 0, 0) + 32768L * get(g, 4, 0);
    long end = (nframes - 1) + get(g, 1, 0) + 32768L * get(g, 12, 0);
    long ls = long(s.loopstart) - long(s.start) + get(g, 2, 0) + 32768L * get(g, 45, 0);
    long le = long(s.loopend) - long(s.start) - 1 + get(g, 3, 0) + 32768L * get(g, 50, 0);
    if (start > 0) o.push_back(strf("offset=%ld", start));
    if (end < nframes - 1) o.push_back(strf("end=%ld", std::max(start, end)));
    int mode = get(g, 54, 0) & 3;
    if ((mode == 1 || mode == 3) && 0 <= ls && ls < le && le <= nframes - 1)
        o.push_back(strf("loop_mode=%s loop_start=%ld loop_end=%ld", mode == 1 ? "loop_continuous" : "loop_sustain", ls, le));
    else
        o.push_back("loop_mode=no_loop");
    int att = get(g, 48, 0);
    if (att) o.push_back("volume=" + fmt(-att * 0.04, 2));
    if (get(g, 17, 0)) o.push_back("pan=" + fmt(std::max(-100.0, std::min(100.0, g.at(17) / 5.0)), 1));
    o.push_back("ampeg_delay=" + fmt(g.at(33) <= -12000 ? 0 : tc2s(g.at(33))) + " ampeg_attack=" + fmt(tc2s(g.at(34))) +
                " ampeg_hold=" + fmt(g.at(35) <= -12000 ? 0 : tc2s(g.at(35))) + " ampeg_decay=" + fmt(tc2s(g.at(36))) +
                " ampeg_sustain=" + fmt(100.0 * std::pow(10.0, -std::max(0, std::min(1440, g.at(37))) / 200.0), 3) +
                " ampeg_release=" + fmt(tc2s(g.at(38))));
    int fc = get(g, 8, 13500);
    bool fil = fc < 13500 || get(g, 11, 0) || get(g, 10, 0);
    if (fil) {
        o.push_back("fil_type=lpf_2p cutoff=" + fmt(std::min(abs_cents_hz(fc), 20000.0), 1));
        if (get(g, 9, 0) > 0) o.push_back("resonance=" + fmt(g.at(9) / 10.0, 1));
    }
    std::vector<const char *> egs;
    if (fil && get(g, 11, 0)) egs.push_back("fileg");
    if (get(g, 7, 0)) egs.push_back("pitcheg");
    const std::pair<const char *, std::string> stages[6] = {
        {"delay", fmt(g.at(25) <= -12000 ? 0 : tc2s(g.at(25)))}, {"attack", fmt(tc2s(g.at(26)))},
        {"hold", fmt(g.at(27) <= -12000 ? 0 : tc2s(g.at(27)))}, {"decay", fmt(tc2s(g.at(28)))},
        {"sustain", fmt(100 - std::max(0, std::min(1000, g.at(29))) / 10.0, 1)}, {"release", fmt(tc2s(g.at(30)))}};
    for (const char *eg : egs) {
        std::string line;
        for (const auto &st : stages) line += (line.empty() ? "" : " ") + std::string(eg) + "_" + st.first + "=" + st.second;
        o.push_back(line);
    }
    for (const char *eg : egs) {
        if (!std::strcmp(eg, "fileg")) o.push_back(strf("fileg_depth=%d", g.at(11)));
        if (!std::strcmp(eg, "pitcheg")) o.push_back(strf("pitcheg_depth=%d", g.at(7)));
    }
    int vib = get(g, 6, 0);
    if (vib)
        o.push_back("pitchlfo_delay=" + fmt(tc2s(g.at(23))) + " pitchlfo_freq=" + fmt(abs_cents_hz(g.at(24)), 3) +
                    strf(" pitchlfo_depth=%d", vib));
    else if (get(g, 5, 0))
        o.push_back("pitchlfo_delay=" + fmt(tc2s(g.at(21))) + " pitchlfo_freq=" + fmt(abs_cents_hz(g.at(22)), 3) +
                    strf(" pitchlfo_depth=%d", g.at(5)));
    if (get(g, 13, 0))
        o.push_back("amplfo_delay=" + fmt(tc2s(g.at(21))) + " amplfo_freq=" + fmt(abs_cents_hz(g.at(22)), 3) +
                    " amplfo_depth=" + fmt(-g.at(13) / 10.0, 1));
    if (fil && get(g, 10, 0))
        o.push_back("fillfo_delay=" + fmt(tc2s(g.at(21))) + " fillfo_freq=" + fmt(abs_cents_hz(g.at(22)), 3) +
                    strf(" fillfo_depth=%d", g.at(10)));
    if (get(g, 57, 0)) o.push_back(strf("group=%d off_by=%d", g.at(57), g.at(57)));
    return true;
}

std::atomic<unsigned> g_tmp{0};
std::string tmp_name(const std::string &path) {
    return path + ".tmp" + std::to_string(getpid()) + "_" + std::to_string(g_tmp++);
}

bool write_all(FILE *f, const void *p, size_t n) { return std::fwrite(p, 1, n, f) == n; }

// Samples/s<N>.wav: mono, 24-bit when the file has sm24 data for it, else 16-bit. Frames in the WAV, or -1 on failure.
long write_wav(File &sf, const SampleHdr &s, const std::string &path, std::string &error) {
    long total = long(sf.smpl_size / 2);
    long a = long(s.start), b = std::min(long(s.end), total);
    long n = b - a;
    if (a < 0 || n <= 0) return 0;
    struct stat st;
    bool low = sf.sm24_off >= 0 && long(sf.sm24_size) >= long(s.end);
    int ba = low ? 3 : 2;
    if (!stat(path.c_str(), &st) && st.st_size == long(44 + n * ba)) return n;   // already in the cache
    std::vector<uint8_t> hi(size_t(n) * 2), lo(low ? size_t(n) : 0), out;
    if (!read_at(sf.f, sf.smpl_off + a * 2, hi.data(), hi.size()) ||
        (low && !read_at(sf.f, sf.sm24_off + a, lo.data(), lo.size()))) {
        error = "can't read the sample data";
        return -1;
    }
    if (low) {
        out.resize(size_t(n) * 3);
        for (long i = 0; i < n; i++) {
            out[size_t(i) * 3] = lo[size_t(i)];
            out[size_t(i) * 3 + 1] = hi[size_t(i) * 2];
            out[size_t(i) * 3 + 2] = hi[size_t(i) * 2 + 1];
        }
    }
    const std::vector<uint8_t> &data = low ? out : hi;
    uint32_t rate = s.rate, dsize = uint32_t(data.size());
    uint8_t h[44];
    auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; i++) h[o + i] = uint8_t(v >> (8 * i)); };
    auto put16 = [&](int o, uint16_t v) { h[o] = uint8_t(v); h[o + 1] = uint8_t(v >> 8); };
    std::memcpy(h, "RIFF", 4); put32(4, 36 + dsize); std::memcpy(h + 8, "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, 1); put32(24, rate); put32(28, rate * uint32_t(ba)); put16(32, uint16_t(ba));
    put16(34, uint16_t(ba * 8)); std::memcpy(h + 36, "data", 4); put32(40, dsize);
    std::string tmp = tmp_name(path);
    FILE *f = std::fopen(tmp.c_str(), "wb");
    bool ok = f && write_all(f, h, 44) && write_all(f, data.data(), data.size());
    if (f && std::fclose(f)) ok = false;
    if (!ok || std::rename(tmp.c_str(), path.c_str())) {
        std::remove(tmp.c_str());
        error = "can't write the converted samples (is the card full?)";
        return -1;
    }
    return n;
}

}  // namespace

bool list_presets(const std::string &path, std::vector<Preset> &out, std::string &error) {
    out.clear();
    std::unique_ptr<File> sf = open_sf2(path, error);
    if (!sf) return false;
    for (const PresetData &p : sf->presets) out.push_back({p.name, p.bank, p.program});
    std::stable_sort(out.begin(), out.end(), [](const Preset &a, const Preset &b) {
        return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
    });
    return true;
}

std::string preset_to_sfz(const std::string &path, int bank, int program, const std::string &cache_dir,
                          std::string &error, std::string *name) {
    std::unique_ptr<File> sf = open_sf2(path, error);
    if (!sf) return {};
    const PresetData *p = nullptr;
    for (const PresetData &x : sf->presets)
        if (x.bank == bank && x.program == program) { p = &x; break; }
    if (!p) { error = strf("no preset %d:%03d in this SoundFont", bank, program); return {}; }
    if (name) *name = p->name;
    std::string sdir = cache_dir + "/Samples";
    mkdir(cache_dir.c_str(), 0755);
    mkdir(sdir.c_str(), 0755);
    std::string base = path.substr(path.find_last_of('/') + 1);
    std::string text = strf("// %s (bank %d, program %d), converted from %s\n<control>\ndefault_path=Samples/\n\n",
                            p->name.c_str(), p->bank, p->program, base.c_str());
    std::map<int, long> written;
    int regions = 0;
    for (const Gens &pz : p->zones) {
        int ii = pz.at(41);
        if (ii < 0 || size_t(ii) >= sf->instruments.size()) continue;
        const Zones &in = sf->instruments[size_t(ii)];
        for (const Gens &iz : in.zones) {
            Gens g = merged(in.global, iz, p->global, pz);
            int si = g.at(53);
            if (si < 0 || size_t(si) >= sf->samples.size()) continue;
            const SampleHdr &s = sf->samples[size_t(si)];
            if (s.type & 0x8000) continue;   // ROM sample: no data
            if (s.rate < 400 || s.rate > 400000) continue;   // a damaged header: no WAV reader would take it
            std::string wav = strf("s%d.wav", si);
            if (!written.count(si)) {
                long n = write_wav(*sf, s, sdir + "/" + wav, error);
                if (n < 0) return {};
                written[si] = n;
            }
            if (written[si] <= 0) continue;
            std::vector<std::string> ops;
            if (!region_opcodes(g, s, wav, written[si], ops)) continue;
            text += "<region>";
            for (const std::string &op : ops) text += " " + op;
            text += "\n";
            regions++;
        }
    }
    if (!regions) { error = "preset " + p->name + " has no playable sounds"; return {}; }
    std::string sfz = strf("%s/%d-%d.sfz", cache_dir.c_str(), p->bank, p->program);
    std::string tmp = tmp_name(sfz);
    FILE *f = std::fopen(tmp.c_str(), "wb");
    bool ok = f && write_all(f, text.data(), text.size());
    if (f && std::fclose(f)) ok = false;
    if (!ok || std::rename(tmp.c_str(), sfz.c_str())) {
        std::remove(tmp.c_str());
        error = "can't write the converted instrument (is the card full?)";
        return {};
    }
    return sfz;
}

}  // namespace sf2
