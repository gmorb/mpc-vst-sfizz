// sfizz for MPC OS: the sfizz 1.2.3 SFZ engine behind the mpc-vst-plugins engine interface (wrapper/engine.h).
//
// Threads. The wrapper never calls two of these at once for one instance, but render/midi come from the audio thread
// and set_param/get_param from the screen. Everything slow (loading an SFZ or Scala file, changing the voice count,
// oversampling or preload size, listing a folder) runs on this instance's own worker thread while it holds synth_m;
// render() only try-locks synth_m and outputs silence while a load holds it, so audio never waits on the disk.
// Values the screen sets that sfizz applies cheaply (quality, tuning, CCs) are stored as atomics and applied by
// render() under the lock, so every sfizz call is made with synth_m held.
//
// Layout on the device: <dir of the .so>/sfizz/ holds DefaultInstrument.sfz, DefaultScale.scl, Instruments/ (the
// plugin's own SFZ library) and Scales/ (.scl files). The browser also lists /media/* (SD cards, USB drives) and /sdcard.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include "sfizz.hpp"
extern "C" {
#include "engine.h"
void sfizz_mpc_set_gentle_loading(int gentle);   // vendored sfizz, FilePool.cpp
void sfizz_mpc_set_stream_budget(unsigned long long bytes);
void sfizz_mpc_stream_stats(unsigned long long* bytes, unsigned long long* budget, unsigned* streams, unsigned* starved,
                            unsigned* failed);
}

namespace {

constexpr int SR = 44100;
constexpr int BLOCK = 128;
constexpr int CC_SLOTS = 8;      // the Controls panel's knobs
constexpr int BR_ROWS = 8;       // browser rows on screen
constexpr int MAX_ENTRIES = 4000;
constexpr int MAX_MIDI = 256;

const int POLYPHONY[] = {1, 2, 4, 8, 16, 32, 64, 96, 128, 160, 192, 224, 256};
constexpr int N_POLY = int(sizeof POLYPHONY / sizeof POLYPHONY[0]);
const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const float TUNING_PRESETS[] = {380.0f, 409.0f, 415.0f, 422.5f, 423.2f, 435.0f, 439.0f, 440.0f, 442.0f, 445.0f, 451.0f};
constexpr int N_TUNING = int(sizeof TUNING_PRESETS / sizeof TUNING_PRESETS[0]);

// Parameters this engine reports as changed by itself (mpc_engine_changed): MIDI CCs moving the Volume, Pan and CC
// knobs, and the tuning preset setting the frequency.
enum Changed : uint32_t { CH_VOLUME = 1u << 0, CH_PAN = 1u << 1, CH_TUNING = 1u << 2, CH_BROWSE = 1u << 3, CH_CC0 = 1u << 8 };

struct Entry {
    std::string name, path;
    bool dir = false;
};

struct MidiEv {
    uint8_t b[3];
    int len;
};

std::string base_name(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
std::string dir_name(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string(".") : (s == 0 ? std::string("/") : p.substr(0, s));
}
std::string stem(const std::string &p) {
    std::string b = base_name(p);
    size_t d = b.find_last_of('.');
    return d == std::string::npos || d == 0 ? b : b.substr(0, d);
}
bool ends_with_ci(const std::string &s, const char *suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && !strcasecmp(s.c_str() + s.size() - n, suf);
}
bool is_dir(const std::string &p) {
    struct stat st;
    return !stat(p.c_str(), &st) && S_ISDIR(st.st_mode);
}
bool is_file(const std::string &p) {
    struct stat st;
    return !stat(p.c_str(), &st) && S_ISREG(st.st_mode);
}
bool less_ci(const std::string &a, const std::string &b) { return strcasecmp(a.c_str(), b.c_str()) < 0; }

// A folder's entries: sub-folders first, then files ending in `ext`, each sorted by name. Hidden entries and
// symlinked folders are skipped (no loops); at most MAX_ENTRIES.
std::vector<Entry> list_dir(const std::string &dir, const char *ext) {
    std::vector<Entry> dirs, files;
    DIR *d = opendir(dir.c_str());
    if (!d) return {};
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string p = dir == "/" ? "/" + std::string(e->d_name) : dir + "/" + e->d_name;
        struct stat lst;
        if (lstat(p.c_str(), &lst)) continue;
        if (S_ISDIR(lst.st_mode)) dirs.push_back({e->d_name, p, true});
        else if (ends_with_ci(e->d_name, ext) && is_file(p)) files.push_back({e->d_name, p, false});
        if (dirs.size() + files.size() >= size_t(MAX_ENTRIES)) break;
    }
    closedir(d);
    auto by_name = [](const Entry &a, const Entry &b) { return less_ci(a.name, b.name); };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    dirs.insert(dirs.end(), files.begin(), files.end());
    return dirs;
}

// Look for a file by name under a folder, a few levels deep (a library moved to another card keeps loading).
std::string find_by_name(const std::string &root, const std::string &name, int depth) {
    if (depth < 0) return {};
    std::vector<Entry> es = list_dir(root, ".sfz");
    for (const Entry &e : es)
        if (!e.dir && e.name == name) return e.path;
    for (const Entry &e : es)
        if (e.dir) {
            std::string r = find_by_name(e.path, name, depth - 1);
            if (!r.empty()) return r;
        }
    return {};
}

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The on-screen keyboard: two octaves and a C. Its keys are list tiles: white keys "kw_1".."kw_15", black keys in
// four groups (the gaps between them): "kba_1..2" (C# D#), "kbb_1..3" (F# G# A#), "kbc_1..2", "kbd_1..3".
constexpr int KB_KEYS = 25;
constexpr int TAP_MS = 600;   // a tap plays its note this long (MPC reports the touch, not the finger lifting)
int kb_offset(const char *key) {   // semitones above the keyboard's first C, or -1
    static const int white[15] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
    if (std::strncmp(key, "kw_", 3) == 0 || (key[0] == 'k' && key[1] == 'b' && key[2] >= 'a' && key[2] <= 'd' && key[3] == '_')) {
        const char *d = key + (key[1] == 'w' ? 3 : 4);
        if (!std::isdigit(static_cast<unsigned char>(*d))) return -1;
        int n = std::atoi(d) - 1;
        if (key[1] == 'w') return n >= 0 && n < 15 ? white[n] : -1;
        static const int black[4][3] = {{1, 3, -1}, {6, 8, 10}, {13, 15, -1}, {18, 20, 22}};
        return n >= 0 && n < 3 ? black[key[2] - 'a'][n] : -1;
    }
    return -1;
}

std::string note_name(int key) {
    key = std::max(0, std::min(127, key));
    return std::string(NOTE_NAMES[key % 12]) + " " + std::to_string(key / 12 - 1);
}

std::string memory_text(uint64_t bytes) {   // as sfizz's editor: kB, MB, then GB with one decimal
    double v = double(bytes) / 1e3;
    const char *unit = "kB";
    int prec = 0;
    if (v >= 1e3) { v /= 1e3; unit = "MB"; }
    if (v >= 1e3) { v /= 1e3; unit = "GB"; prec = 1; }
    char b[32];
    std::snprintf(b, sizeof b, "%.*f %s", prec, v, unit);
    return b;
}

struct Inst {
    std::string data_dir;
    sfz::Sfizz synth;
    sfz::ClientPtr client;
    std::mutex synth_m;   // held by every sfizz call (render, worker)

    // worker
    std::thread worker;
    std::mutex job_m;
    std::condition_variable job_cv;
    std::deque<std::function<void()>> jobs;
    bool quit = false;

    // MIDI from the audio thread, applied by render()
    MidiEv midi_q[MAX_MIDI];
    int midi_n = 0;

    // values set from the screen (applied by render() or by a worker job)
    std::atomic<int> volume{100}, pan{64};
    std::atomic<int> polyphony_idx{6}, oversampling_idx{0}, preload_idx{3}, sustain_cancels{0};
    std::atomic<int> sample_quality{3}, osc_quality{1}, root_note{0}, root_oct{5}, tuning_preset{7};
    std::atomic<float> tuning_freq{440.0f}, stretch{0.0f};
    std::atomic<int> cc_val[CC_SLOTS];
    std::atomic<int> browse{0};
    std::atomic<uint32_t> dirty{0};      // render(): which cheap settings to (re)apply
    enum : uint32_t { D_VOL = 1, D_PAN = 2, D_QUALITY = 4, D_TUNING = 8, D_STRETCH = 16, D_ROOT = 32, D_SUSTAIN = 64,
                      D_CC = 128 };
    std::atomic<uint32_t> cc_dirty{0};

    // what the screen shows (written by the worker or render() under ui_m)
    std::mutex ui_m;
    std::string file_path, file_label = "DefaultInstrument", status, scala_path, scala_label = "DefaultScale";
    int n_curves = 0, n_masters = 0, n_groups = 0, n_regions = 0, n_samples = 0;
    int slot_cc[CC_SLOTS];               // CC number per Controls knob, -1 = unused
    std::string slot_label[CC_SLOTS];
    std::map<int, std::string> sw_labels; // keyswitch key -> label
    bool has_sw = false;
    std::atomic<int> sw_current{-1};
    std::atomic<int> active_voices{0};
    std::atomic<uint64_t> mem_bytes{0};
    std::atomic<int> display_rev{0};
    std::atomic<uint32_t> changed{0};
    std::atomic<int> loading{0};

    // browser (worker + ui_m)
    std::string br_dir;                  // "" = the list of places
    std::vector<Entry> br_entries;
    int br_top = 0;

    // message replies during a query (callback runs inside sendMessage, synth_m held)
    std::vector<int> q_ccs;
    std::map<int, std::string> q_cc_label;
    std::map<int, float> q_cc_value;
    std::vector<int> q_sw;
    std::map<int, std::string> q_sw_label;
    int q_ints[5] = {0, 0, 0, 0, 0};

    int block_count = 0;
    int locked_blocks = 0;   // render() blocks in a row the synth was busy

    // browser taps: the listing they were made on, and when the last action happened (see set_param)
    std::atomic<unsigned> br_gen{0};
    std::atomic<long long> br_last_ms{0};

    // the on-screen keyboard: KB_KEYS keys from kb_base (a C); taps play a note for TAP_MS; keys light while sounding
    std::atomic<int> kb_base{48};
    std::atomic<uint8_t> held[128] = {};     // note on from MIDI (audio thread writes, everyone reads)
    std::atomic<uint8_t> tap_lit[128] = {};  // a tap's note is sounding
    std::atomic<uint32_t> tap_req[4] = {};   // taps waiting for the audio thread (bit per note)
    int tap_left[128] = {};                  // frames left of a tap's note (audio thread only)
    std::atomic<int> settings_pending{0};
    int applied_voices = 64, applied_factor = 1;   // what sfizz has (create() sets these)
    uint32_t applied_preload = 8192;

    Inst() {
        for (int i = 0; i < CC_SLOTS; i++) { cc_val[i] = 0; slot_cc[i] = -1; }
    }
};

// The decoded-sample memory budget (all instances): a third of the memory free when the first instance starts, within
// 48-384 MB. MPC itself and the user's other programs need the rest; a 32-bit process also has a limited address space.
void init_stream_budget() {
    static std::once_flag once;
    std::call_once(once, [] {
        unsigned long long availKb = 0;
        if (FILE *f = std::fopen("/proc/meminfo", "r")) {
            char line[128];
            while (std::fgets(line, sizeof line, f))
                if (std::sscanf(line, "MemAvailable: %llu kB", &availKb) == 1)
                    break;
            std::fclose(f);
        }
        unsigned long long budget = availKb ? availKb * 1024 / 3 : (192ull << 20);
        budget = std::max(48ull << 20, std::min(384ull << 20, budget));
        sfizz_mpc_set_stream_budget(budget);
    });
}

void push_job(Inst *o, std::function<void()> f) {
    {
        std::lock_guard<std::mutex> l(o->job_m);
        o->jobs.push_back(std::move(f));
    }
    o->job_cv.notify_one();
}

void worker_main(Inst *o) {
    for (;;) {
        std::function<void()> f;
        {
            std::unique_lock<std::mutex> l(o->job_m);
            o->job_cv.wait(l, [o] { return o->quit || !o->jobs.empty(); });
            if (o->quit) return;
            f = std::move(o->jobs.front());
            o->jobs.pop_front();
        }
        f();
    }
}

void on_message(void *data, int, const char *path, const char *sig, const sfizz_arg_t *args) {
    Inst *o = static_cast<Inst *>(data);
    auto bits = [&](std::vector<int> &out, int limit) {
        out.clear();
        if (std::strcmp(sig, "b")) return;
        const uint8_t *d = args[0].b->data;
        for (uint32_t i = 0; i < args[0].b->size * 8 && int(i) < limit; i++)
            if (d[i / 8] & (1u << (i % 8))) out.push_back(int(i));
    };
    unsigned idx = 0;
    if (!std::strcmp(path, "/cc/slots")) bits(o->q_ccs, 512);
    else if (!std::strcmp(path, "/sw/last/slots")) bits(o->q_sw, 128);
    else if (!std::strcmp(path, "/sw/last/current")) o->sw_current = !std::strcmp(sig, "i") ? args[0].i : -1;
    else if (!std::strcmp(path, "/mem/buffers") && !std::strcmp(sig, "h")) o->mem_bytes = uint64_t(args[0].h);
    else if (std::sscanf(path, "/cc%u/label", &idx) == 1 && std::strstr(path, "/label") && !std::strcmp(sig, "s"))
        o->q_cc_label[int(idx)] = args[0].s;
    else if (std::sscanf(path, "/cc%u/value", &idx) == 1 && std::strstr(path, "/value") && !std::strcmp(sig, "f"))
        o->q_cc_value[int(idx)] = args[0].f;
    else if (std::sscanf(path, "/sw/last/%u/label", &idx) == 1 && !std::strcmp(sig, "s")) o->q_sw_label[int(idx)] = args[0].s;
    else if (!std::strcmp(sig, "i")) {
        static const char *const counts[5] = {"/num_curves", "/num_masters", "/num_groups", "/num_regions", "/num_samples"};
        for (int i = 0; i < 5; i++)
            if (!std::strcmp(path, counts[i])) o->q_ints[i] = args[0].i;
    }
}

void send(Inst *o, const char *path) { o->synth.sendMessage(*o->client, 0, path, "", nullptr); }

// After a load (synth_m held): the instrument's counts, the CCs it uses (the Controls knobs) and its keyswitches.
void describe(Inst *o) {
    o->q_ccs.clear(); o->q_cc_label.clear(); o->q_cc_value.clear(); o->q_sw.clear(); o->q_sw_label.clear();
    for (const char *p : {"/num_curves", "/num_masters", "/num_groups", "/num_regions", "/num_samples", "/cc/slots",
                          "/sw/last/slots", "/sw/last/current", "/mem/buffers"})
        send(o, p);
    char path[64];
    for (int cc : std::vector<int>(o->q_ccs)) {
        std::snprintf(path, sizeof path, "/cc%d/label", cc); send(o, path);
        std::snprintf(path, sizeof path, "/cc%d/value", cc); send(o, path);
    }
    for (int cc : {7, 10}) { std::snprintf(path, sizeof path, "/cc%d/value", cc); send(o, path); }
    for (int k : std::vector<int>(o->q_sw)) { std::snprintf(path, sizeof path, "/sw/last/%d/label", k); send(o, path); }

    // Controls knobs: the instrument's CCs except Volume (7) and Pan (10), which have their own knobs.
    // Labelled CCs first (the instrument named them for a reason), then the rest, each by number.
    std::vector<int> labelled, plain;
    for (int cc : o->q_ccs) {
        if (cc == 7 || cc == 10 || cc >= 128) continue;
        (o->q_cc_label.count(cc) && !o->q_cc_label[cc].empty() ? labelled : plain).push_back(cc);
    }
    labelled.insert(labelled.end(), plain.begin(), plain.end());
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        o->n_curves = o->q_ints[0]; o->n_masters = o->q_ints[1]; o->n_groups = o->q_ints[2];
        o->n_regions = o->q_ints[3]; o->n_samples = o->q_ints[4];
        for (int i = 0; i < CC_SLOTS; i++) {
            int cc = i < int(labelled.size()) ? labelled[size_t(i)] : -1;
            o->slot_cc[i] = cc;
            o->slot_label[i] = cc < 0 ? "" : (o->q_cc_label.count(cc) && !o->q_cc_label[cc].empty() ? o->q_cc_label[cc]
                                                                                                     : "CC " + std::to_string(cc));
            float v = cc >= 0 && o->q_cc_value.count(cc) ? o->q_cc_value[cc] : 0.0f;
            o->cc_val[i] = int(std::lround(v * 127.0f));
        }
        o->sw_labels.clear();
        for (int k : o->q_sw) o->sw_labels[k] = o->q_sw_label.count(k) ? o->q_sw_label[k] : "";
        o->has_sw = !o->q_sw.empty();
    }
    // the instrument's own Volume/Pan defaults become the knobs' values
    if (o->q_cc_value.count(7)) o->volume = int(std::lround(o->q_cc_value[7] * 127.0f));
    if (o->q_cc_value.count(10)) o->pan = int(std::lround(o->q_cc_value[10] * 127.0f));
    o->dirty |= Inst::D_VOL | Inst::D_PAN;
    o->display_rev++;
}

void set_status(Inst *o, const std::string &s) {
    std::lock_guard<std::mutex> l(o->ui_m);
    o->status = s;
}

// worker: load an SFZ file (empty = the default instrument)
void load_file(Inst *o, std::string path) {
    if (path.empty()) path = o->data_dir + "/DefaultInstrument.sfz";
    if (!is_file(path)) {   // moved? look for it by name in the places the browser shows
        std::string found;
        std::vector<std::string> roots = {o->data_dir + "/Instruments"};
        for (const Entry &e : list_dir("/media", "\x01")) roots.push_back(e.path);
        roots.push_back("/sdcard");
        for (const std::string &r : roots)
            if (found.empty() && is_dir(r)) found = find_by_name(r, base_name(path), 4);
        if (found.empty()) {
            set_status(o, "Not found: " + base_name(path));
            std::lock_guard<std::mutex> l(o->ui_m);
            o->file_path = path;
            o->file_label = stem(path) + " (missing)";
            o->display_rev++;
            return;
        }
        path = found;
    }
    o->loading = 1;
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        o->status = "Loading " + base_name(path) + "...";
        o->file_label = "Loading...";
    }
    o->display_rev++;
    bool ok = false, oom = false;
    {
        std::lock_guard<std::mutex> l(o->synth_m);
        try {
            ok = o->synth.loadSfzFile(path);
            if (ok) describe(o);
        } catch (const std::bad_alloc&) {
            oom = true;   // not enough memory for this instrument's preloads
            try { o->synth.loadSfzFile(""); } catch (...) {}
        } catch (...) {
            try { o->synth.loadSfzFile(""); } catch (...) {}
        }
    }
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        o->file_path = path;
        o->file_label = stem(path);
        char b[96];
        if (ok) std::snprintf(b, sizeof b, "%d regions, %d samples", o->n_regions, o->n_samples);
        o->status = ok ? b : oom ? "Not enough memory for " + base_name(path) : "Could not load " + base_name(path);
    }
    o->loading = 0;
    o->changed |= CH_VOLUME | CH_PAN;
    for (int i = 0; i < CC_SLOTS; i++) o->changed |= CH_CC0 << i;
    o->display_rev++;
}

// worker: the SFZ files of the current file's folder, stepped by dir (-1 / +1), as sfizz's own arrows do
void step_file(Inst *o, int dir) {
    std::string cur, folder;
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        cur = o->file_path;
        folder = cur.empty() || cur == o->data_dir + "/DefaultInstrument.sfz" ? o->br_dir : dir_name(cur);
    }
    if (folder.empty()) folder = o->data_dir + "/Instruments";
    std::vector<Entry> es = list_dir(folder, ".sfz");
    std::vector<std::string> files;
    for (const Entry &e : es)
        if (!e.dir) files.push_back(e.path);
    if (files.empty()) { set_status(o, "No SFZ files in " + base_name(folder)); o->display_rev++; return; }
    auto it = std::find(files.begin(), files.end(), cur);
    int i = it == files.end() ? (dir > 0 ? 0 : int(files.size()) - 1)
                              : (int(it - files.begin()) + dir + int(files.size())) % int(files.size());
    load_file(o, files[size_t(i)]);
}

// worker: the browser's list of places (the plugin's library, cards and drives) or a folder
void browse_to(Inst *o, const std::string &dir) {
    std::vector<Entry> es;
    if (dir.empty()) {
        es.push_back({"Plugin Library", o->data_dir + "/Instruments", true});
        for (const Entry &e : list_dir("/media", "\x01"))
            if (e.dir) es.push_back({e.name, e.path, true});
        if (is_dir("/sdcard")) es.push_back({"sdcard", "/sdcard", true});
    } else {
        es = list_dir(dir, ".sfz");
    }
    std::lock_guard<std::mutex> l(o->ui_m);
    o->br_dir = dir;
    o->br_entries = std::move(es);
    o->br_top = 0;
    if (!dir.empty() && o->br_entries.empty()) o->status = "No SFZ files or folders here";
    o->br_gen++;
    o->br_last_ms = now_ms();
    o->display_rev++;
}

void browse_up(Inst *o) {
    std::string d;
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        d = o->br_dir;
    }
    if (d.empty()) return;
    std::string up = dir_name(d);
    // above a place (a card, the library, /sdcard) is the list of places
    if (d == o->data_dir + "/Instruments" || dir_name(d) == "/media" || d == "/sdcard" || up == "/" || up == ".") up.clear();
    browse_to(o, up);
}

void browse_tap(Inst *o, int row) {
    Entry e;
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        int i = o->br_top + row;
        if (i < 0 || i >= int(o->br_entries.size())) return;
        e = o->br_entries[size_t(i)];
    }
    if (e.dir) {
        if (!is_dir(e.path)) {   // the plugin library folder is created on first use
            mkdir(e.path.c_str(), 0755);
        }
        browse_to(o, e.path);
    } else {
        load_file(o, e.path);
    }
}

// worker: Scala files: DefaultScale.scl, then <data>/Scales/*.scl
std::vector<std::string> scala_files(Inst *o) {
    std::vector<std::string> out = {o->data_dir + "/DefaultScale.scl"};
    for (const Entry &e : list_dir(o->data_dir + "/Scales", ".scl"))
        if (!e.dir) out.push_back(e.path);
    return out;
}

void load_scala(Inst *o, std::string path) {
    if (path.empty() || !is_file(path)) path = o->data_dir + "/DefaultScale.scl";
    bool ok = false;
    {
        std::lock_guard<std::mutex> l(o->synth_m);
        try {
            ok = o->synth.loadScalaFile(path);
            o->synth.setScalaRootKey(o->root_oct * 12 + o->root_note);
        } catch (...) {}
    }
    std::lock_guard<std::mutex> l(o->ui_m);
    o->scala_path = path;
    o->scala_label = ok ? stem(path) : "Could not load " + base_name(path);
    o->display_rev++;
}

void step_scala(Inst *o, int dir) {
    std::vector<std::string> fs = scala_files(o);
    std::string cur;
    {
        std::lock_guard<std::mutex> l(o->ui_m);
        cur = o->scala_path;
    }
    auto it = std::find(fs.begin(), fs.end(), cur);
    int i = it == fs.end() ? 0 : (int(it - fs.begin()) + dir + int(fs.size())) % int(fs.size());
    load_scala(o, fs[size_t(i)]);
}

// worker: the settings that reallocate (voices, oversampling, preload). Each changes only when it differs from what
// sfizz has, since oversampling and preload size reload every sample; repeated requests (a Q-Link turn) coalesce into one.
void apply_engine_settings(Inst *o) {
    o->settings_pending = 0;
    int voices = POLYPHONY[std::max(0, std::min(N_POLY - 1, o->polyphony_idx.load()))];
    int factor = 1 << std::max(0, std::min(3, o->oversampling_idx.load()));
    uint32_t preload = uint32_t(1024) << std::max(0, std::min(6, o->preload_idx.load()));
    if (voices == o->applied_voices && factor == o->applied_factor && preload == o->applied_preload) return;
    std::lock_guard<std::mutex> l(o->synth_m);
    try {
        if (voices != o->applied_voices) o->synth.setNumVoices(voices);
        if (factor != o->applied_factor) o->synth.setOversamplingFactor(factor);
        if (preload != o->applied_preload) o->synth.setPreloadSize(preload);
    } catch (...) {   // out of memory: keep what sfizz has
        set_status(o, "Not enough memory for that setting");
    }
    o->applied_voices = voices;
    o->applied_factor = factor;
    o->applied_preload = preload;
    o->display_rev++;
}

void request_engine_settings(Inst *o) {
    if (!o->settings_pending.exchange(1)) push_job(o, [o] { apply_engine_settings(o); });
}

// ---- engine interface ----

void *create(const char *data_dir) {
    Inst *o = new Inst();
    o->data_dir = data_dir && *data_dir ? data_dir : ".";
    o->synth.setSampleRate(float(SR));
    o->synth.setSamplesPerBlock(BLOCK);
    o->synth.setNumVoices(64);
    o->synth.setPreloadSize(8192);
    o->synth.setSampleQuality(sfz::Sfizz::ProcessLive, 3);
    o->synth.setSampleQuality(sfz::Sfizz::ProcessFreewheeling, 3);
    o->synth.setOscillatorQuality(sfz::Sfizz::ProcessLive, 1);
    o->synth.setOscillatorQuality(sfz::Sfizz::ProcessFreewheeling, 1);
    o->client = o->synth.createClient(o);
    sfz::Sfizz::setReceiveCallback(*o->client, &on_message);
    // Gentle background loading, always (vendored FilePool.cpp): normal priority, paced, stops when a file is unused.
    // The sound is unchanged; it only stops each key hit from bursting a whole sample file through a real-time thread.
    sfizz_mpc_set_gentle_loading(1);
    init_stream_budget();
    // the built-in default (a sine) and the default scale load here, so a new instance plays at once
    load_scala(o, "");
    load_file(o, "");
    o->worker = std::thread(worker_main, o);
    push_job(o, [o] { browse_to(o, ""); });
    return o;
}

void destroy(void *inst) {
    Inst *o = static_cast<Inst *>(inst);
    {
        std::lock_guard<std::mutex> l(o->job_m);
        o->quit = true;
    }
    o->job_cv.notify_one();
    if (o->worker.joinable()) o->worker.join();
    delete o;
}

void midi(void *inst, const uint8_t *msg, int len) {
    Inst *o = static_cast<Inst *>(inst);
    if (len < 1 || len > 3 || o->midi_n >= MAX_MIDI) return;
    MidiEv &e = o->midi_q[o->midi_n++];
    std::memcpy(e.b, msg, size_t(len));
    e.len = len;
}

// a MIDI CC that a knob shows moves that knob
void cc_followed(Inst *o, int cc, int value) {
    if (cc == 7) { o->volume = value; o->changed |= CH_VOLUME; }
    else if (cc == 10) { o->pan = value; o->changed |= CH_PAN; }
    for (int i = 0; i < CC_SLOTS; i++)
        if (o->slot_cc[i] == cc) { o->cc_val[i] = value; o->changed |= CH_CC0 << i; }
}

void play_midi(Inst *o) {
    for (int k = 0; k < o->midi_n; k++) {
        const uint8_t *m = o->midi_q[k].b;
        int st = m[0] & 0xF0, len = o->midi_q[k].len;
        if (st == 0x90 && len == 3 && m[2] > 0) { o->synth.noteOn(0, m[1], m[2]); o->held[m[1] & 127] = 1; }
        else if ((st == 0x80 || st == 0x90) && len == 3) { o->synth.noteOff(0, m[1], m[2]); o->held[m[1] & 127] = 0; }
        else if (st == 0xB0 && len == 3) {
            if (m[1] == 120 || m[1] == 123) {   // all sound / notes off
                o->synth.allSoundOff();
                for (auto &h : o->held) h = 0;
                continue;
            }
            o->synth.hdcc(0, m[1], float(m[2]) / 127.0f);
            cc_followed(o, m[1], m[2]);
        } else if (st == 0xE0 && len == 3) o->synth.pitchWheel(0, ((m[2] << 7) | m[1]) - 8192);
        else if (st == 0xD0 && len >= 2) o->synth.channelAftertouch(0, m[1]);
        else if (st == 0xA0 && len == 3) o->synth.polyAftertouch(0, m[1], m[2]);
    }
    o->midi_n = 0;
}

// audio thread, synth held: the keyboard's taps start (or restart) their notes; notes whose time is up end
void play_taps(Inst *o, int frames) {
    for (int w = 0; w < 4; w++) {
        uint32_t bits = o->tap_req[w].exchange(0);
        while (bits) {
            int b = __builtin_ctz(bits);
            bits &= bits - 1;
            int note = w * 32 + b;
            if (o->tap_left[note] > 0) o->synth.noteOff(0, note, 0);
            o->synth.noteOn(0, note, 100);
            o->tap_left[note] = TAP_MS * SR / 1000;
            o->tap_lit[note] = 1;
        }
    }
    for (int note = 0; note < 128; note++)
        if (o->tap_left[note] > 0 && (o->tap_left[note] -= frames) <= 0) {
            o->tap_left[note] = 0;
            o->synth.noteOff(0, note, 0);
            o->tap_lit[note] = 0;
        }
}

void apply_dirty(Inst *o) {
    uint32_t d = o->dirty.exchange(0);
    if (d & Inst::D_VOL) o->synth.automateHdcc(0, 7, float(o->volume) / 127.0f);
    if (d & Inst::D_PAN) o->synth.automateHdcc(0, 10, float(o->pan) / 127.0f);
    if (d & Inst::D_QUALITY) {
        o->synth.setSampleQuality(sfz::Sfizz::ProcessLive, o->sample_quality);
        o->synth.setSampleQuality(sfz::Sfizz::ProcessFreewheeling, o->sample_quality);
        o->synth.setOscillatorQuality(sfz::Sfizz::ProcessLive, o->osc_quality);
        o->synth.setOscillatorQuality(sfz::Sfizz::ProcessFreewheeling, o->osc_quality);
    }
    if (d & Inst::D_TUNING) o->synth.setTuningFrequency(o->tuning_freq);
    if (d & Inst::D_STRETCH) o->synth.loadStretchTuningByRatio(o->stretch);
    if (d & Inst::D_ROOT) o->synth.setScalaRootKey(o->root_oct * 12 + o->root_note);
    if (d & Inst::D_SUSTAIN) o->synth.setSustainCancelsRelease(o->sustain_cancels != 0);
    uint32_t c = o->cc_dirty.exchange(0);
    for (int i = 0; i < CC_SLOTS; i++)
        if (c & (1u << i) && o->slot_cc[i] >= 0) o->synth.automateHdcc(0, o->slot_cc[i], float(o->cc_val[i]) / 127.0f);
}

void render(void *inst, int16_t *out, int frames) {
    Inst *o = static_cast<Inst *>(inst);
    float L[BLOCK], R[BLOCK];
    float *bufs[2] = {L, R};
    frames = std::max(0, std::min(BLOCK, frames));
    std::unique_lock<std::mutex> l(o->synth_m, std::try_to_lock);
    if (!l.owns_lock()) {
        // A load or a voice-count change holds the synth: silence meanwhile. MIDI waits through a short hold (~90 ms,
        // a settings change) and plays when it ends; after a long one (loading an instrument) late note-ons are
        // dropped, while note-offs and controllers are kept so nothing hangs and the knobs stay right.
        if (++o->locked_blocks > 32) {
            int k = 0;
            for (int i = 0; i < o->midi_n; i++) {
                const uint8_t *m = o->midi_q[i].b;
                if ((m[0] & 0xF0) == 0x90 && o->midi_q[i].len == 3 && m[2] > 0) continue;
                o->midi_q[k++] = o->midi_q[i];
            }
            o->midi_n = k;
        }
        std::memset(out, 0, size_t(frames) * 2 * sizeof(int16_t));
        return;
    }
    o->locked_blocks = 0;
    apply_dirty(o);
    play_midi(o);
    play_taps(o, frames);
    o->synth.renderBlock(bufs, size_t(frames), 1);
    if (++o->block_count % 32 == 0) {   // ~93 ms: the keyswitch, voices and memory readouts
        send(o, "/sw/last/current");
        if (o->block_count % 256 == 0) send(o, "/mem/buffers");
        o->active_voices = o->synth.getNumActiveVoices();
    }
    l.unlock();
    for (int i = 0; i < frames; i++) {
        float a = L[i] * 32767.0f, b = R[i] * 32767.0f;
        if (!(a == a)) a = 0.0f;   // NaN: silence, not a full-scale click
        if (!(b == b)) b = 0.0f;
        out[2 * i] = int16_t(std::lrint(std::max(-32768.0f, std::min(32767.0f, a))));
        out[2 * i + 1] = int16_t(std::lrint(std::max(-32768.0f, std::min(32767.0f, b))));
    }
}

bool trig(const char *val) { return std::atof(val) > 0.5f; }

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

int cc_slot_of(const char *key) {   // "cc_3" -> 2
    if (std::strncmp(key, "cc_", 3) || !std::isdigit(static_cast<unsigned char>(key[3]))) return -1;
    int n = std::atoi(key + 3) - 1;
    return n >= 0 && n < CC_SLOTS ? n : -1;
}

int br_row_of(const char *key) {   // "br_5" -> 4
    if (std::strncmp(key, "br_", 3) || !std::isdigit(static_cast<unsigned char>(key[3]))) return -1;
    int n = std::atoi(key + 3) - 1;
    return n >= 0 && n < BR_ROWS ? n : -1;
}

void restore_state(Inst *o, const char *text);

void set_param(void *inst, const char *key, const char *val) {
    Inst *o = static_cast<Inst *>(inst);
    int iv = int(std::lround(std::atof(val)));
    if (!std::strcmp(key, "state")) { restore_state(o, val); return; }
    if (!std::strcmp(key, "volume")) { o->volume = clampi(iv, 0, 127); o->dirty |= Inst::D_VOL; return; }
    if (!std::strcmp(key, "pan")) { o->pan = clampi(iv, 0, 127); o->dirty |= Inst::D_PAN; return; }
    if (int s = cc_slot_of(key); s >= 0) { o->cc_val[s] = clampi(iv, 0, 127); o->cc_dirty |= 1u << s; return; }
    if (!std::strcmp(key, "polyphony")) { o->polyphony_idx = clampi(iv, 0, N_POLY - 1); request_engine_settings(o); return; }
    if (!std::strcmp(key, "oversampling")) { o->oversampling_idx = clampi(iv, 0, 3); request_engine_settings(o); return; }
    if (!std::strcmp(key, "preload")) { o->preload_idx = clampi(iv, 0, 6); request_engine_settings(o); return; }
    if (!std::strcmp(key, "sustain_cancels")) { o->sustain_cancels = iv ? 1 : 0; o->dirty |= Inst::D_SUSTAIN; return; }
    if (!std::strcmp(key, "sample_quality")) { o->sample_quality = clampi(iv, 0, 10); o->dirty |= Inst::D_QUALITY; return; }
    if (!std::strcmp(key, "osc_quality")) { o->osc_quality = clampi(iv, 0, 3); o->dirty |= Inst::D_QUALITY; return; }
    if (!std::strcmp(key, "root_note")) { o->root_note = clampi(iv, 0, 11); o->dirty |= Inst::D_ROOT; return; }
    if (!std::strcmp(key, "root_oct")) { o->root_oct = clampi(iv, 0, 10); o->dirty |= Inst::D_ROOT; return; }
    if (!std::strcmp(key, "tuning_freq")) {
        o->tuning_freq = std::max(300.0f, std::min(500.0f, float(std::atof(val))));
        o->dirty |= Inst::D_TUNING;
        return;
    }
    if (!std::strcmp(key, "tuning_preset")) {
        int i = clampi(iv, 0, N_TUNING - 1);
        o->tuning_preset = i;
        o->tuning_freq = TUNING_PRESETS[i];
        o->dirty |= Inst::D_TUNING;
        o->changed |= CH_TUNING;
        return;
    }
    if (!std::strcmp(key, "stretch")) { o->stretch = std::max(0.0f, std::min(1.0f, float(std::atof(val)))); o->dirty |= Inst::D_STRETCH; return; }
    if (!std::strcmp(key, "browse")) { o->browse = iv ? 1 : 0; return; }
    if (int r = br_row_of(key); r >= 0) {
        // One tap, one action. A row's value 0 is the echo of it turning off (or a stale toggle), not a tap; and a tap
        // that arrives right after a new listing appeared, or acts on a listing that has changed since, is the same
        // finger landing on the next folder's row (it opened two folders, or a folder and a file, at once).
        if (!trig(val)) return;
        long long now = now_ms();
        if (now - o->br_last_ms.load() < 350) return;
        o->br_last_ms = now;
        unsigned gen = o->br_gen.load();
        push_job(o, [o, r, gen] { if (o->br_gen.load() == gen) browse_tap(o, r); });
        return;
    }
    if (int k = kb_offset(key); k >= 0) {
        int note = o->kb_base + k;
        if (note < 0 || note > 127) return;
        // value 1 is a tap; value 0 is a tap on a key that was lit (the toggle went off), else an echo
        if (!trig(val) && !o->held[note] && !o->tap_lit[note]) return;
        o->tap_req[note >> 5] |= 1u << (note & 31);
        return;
    }
    if (!std::strcmp(key, "kb_down") || !std::strcmp(key, "kb_up")) {
        if (!trig(val)) return;
        o->kb_base = clampi(o->kb_base + (key[3] == 'd' ? -12 : 12), 0, 96);
        o->display_rev++;
        return;
    }
    if (!trig(val)) return;   // the rest are triggers
    if (!std::strcmp(key, "file_prev")) push_job(o, [o] { step_file(o, -1); });
    else if (!std::strcmp(key, "file_next")) push_job(o, [o] { step_file(o, +1); });
    else if (!std::strcmp(key, "scala_prev")) push_job(o, [o] { step_scala(o, -1); });
    else if (!std::strcmp(key, "scala_next")) push_job(o, [o] { step_scala(o, +1); });
    else if (!std::strcmp(key, "scala_reset")) push_job(o, [o] { load_scala(o, ""); });
    else if (!std::strcmp(key, "br_up")) push_job(o, [o] { browse_up(o); });
    else if (!std::strcmp(key, "br_home")) push_job(o, [o] { browse_to(o, ""); });
    else if (!std::strcmp(key, "br_pgup") || !std::strcmp(key, "br_pgdn")) {
        std::lock_guard<std::mutex> l(o->ui_m);
        int n = int(o->br_entries.size());
        o->br_top += key[5] == 'u' ? -BR_ROWS : BR_ROWS;
        o->br_top = std::max(0, std::min(o->br_top, std::max(0, n - BR_ROWS)));
        o->br_gen++;
        o->display_rev++;
    }
}

int put(char *buf, int len, const std::string &s) { return std::snprintf(buf, size_t(len), "%s", s.c_str()); }
int puti(char *buf, int len, int v) { return std::snprintf(buf, size_t(len), "%d", v); }

std::string state_text(Inst *o) {
    std::lock_guard<std::mutex> l(o->ui_m);
    char b[512];
    std::snprintf(b, sizeof b,
                  "volume=%d\npan=%d\npolyphony=%d\noversampling=%d\npreload=%d\nsustain_cancels=%d\nsample_quality=%d\n"
                  "osc_quality=%d\nroot_note=%d\nroot_oct=%d\ntuning_freq=%.2f\ntuning_preset=%d\nstretch=%.4f\n",
                  o->volume.load(), o->pan.load(), o->polyphony_idx.load(), o->oversampling_idx.load(), o->preload_idx.load(),
                  o->sustain_cancels.load(), o->sample_quality.load(), o->osc_quality.load(), o->root_note.load(),
                  o->root_oct.load(), double(o->tuning_freq.load()), o->tuning_preset.load(), double(o->stretch.load()));
    std::string s = "sfizz-mpc 1\nfile=" + o->file_path + "\nscala=" + o->scala_path + "\n" + b
                    + "kb_base=" + std::to_string(o->kb_base.load()) + "\n";
    for (int i = 0; i < CC_SLOTS; i++)
        if (o->slot_cc[i] >= 0) s += "cc" + std::to_string(o->slot_cc[i]) + "=" + std::to_string(o->cc_val[i].load()) + "\n";
    return s;
}

void restore_state(Inst *o, const char *text) {
    if (std::strncmp(text, "sfizz-mpc 1\n", 12)) return;
    std::map<std::string, std::string> kv;
    std::string s = text + 12;
    size_t i = 0;
    while (i < s.size()) {
        size_t e = s.find('\n', i);
        if (e == std::string::npos) e = s.size();
        std::string line = s.substr(i, e - i);
        size_t eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
        i = e + 1;
    }
    auto num = [&](const char *k, double d) { return kv.count(k) ? std::atof(kv[k].c_str()) : d; };
    o->polyphony_idx = clampi(int(num("polyphony", 6)), 0, N_POLY - 1);
    o->oversampling_idx = clampi(int(num("oversampling", 0)), 0, 3);
    o->preload_idx = clampi(int(num("preload", 3)), 0, 6);
    o->sustain_cancels = int(num("sustain_cancels", 0)) ? 1 : 0;
    o->sample_quality = clampi(int(num("sample_quality", 3)), 0, 10);
    o->osc_quality = clampi(int(num("osc_quality", 1)), 0, 3);
    o->root_note = clampi(int(num("root_note", 0)), 0, 11);
    o->root_oct = clampi(int(num("root_oct", 5)), 0, 10);
    o->tuning_freq = float(std::max(300.0, std::min(500.0, num("tuning_freq", 440))));
    o->tuning_preset = clampi(int(num("tuning_preset", 7)), 0, N_TUNING - 1);
    o->stretch = float(std::max(0.0, std::min(1.0, num("stretch", 0))));
    o->kb_base = clampi(int(num("kb_base", 48)), 0, 96) / 12 * 12;
    int vol = clampi(int(num("volume", 100)), 0, 127), pan = clampi(int(num("pan", 64)), 0, 127);
    std::map<int, int> ccs;
    for (auto &p : kv)
        if (p.first.size() > 2 && p.first[0] == 'c' && p.first[1] == 'c' && std::isdigit(static_cast<unsigned char>(p.first[2])))
            ccs[std::atoi(p.first.c_str() + 2)] = clampi(std::atoi(p.second.c_str()), 0, 127);
    std::string file = kv["file"], scala = kv["scala"];
    o->volume = vol;   // shown at once; set again once the instrument (whose defaults would replace them) has loaded
    o->pan = pan;
    o->dirty |= Inst::D_QUALITY | Inst::D_TUNING | Inst::D_STRETCH | Inst::D_ROOT | Inst::D_SUSTAIN | Inst::D_VOL | Inst::D_PAN;
    request_engine_settings(o);
    push_job(o, [o, scala] { load_scala(o, scala); });
    push_job(o, [o, file, vol, pan, ccs] {
        load_file(o, file);
        // the project's own knob positions win over the instrument's defaults
        o->volume = vol;
        o->pan = pan;
        uint32_t c = 0;
        for (int i = 0; i < CC_SLOTS; i++) {
            auto it = o->slot_cc[i] >= 0 ? ccs.find(o->slot_cc[i]) : ccs.end();
            if (it != ccs.end()) { o->cc_val[i] = it->second; c |= 1u << i; o->changed |= CH_CC0 << i; }
        }
        o->cc_dirty |= c;
        o->dirty |= Inst::D_VOL | Inst::D_PAN;
        o->changed |= CH_VOLUME | CH_PAN;
        o->display_rev++;
    });
    if (!file.empty()) push_job(o, [o, file] { browse_to(o, dir_name(file)); });
}

int get_param(void *inst, const char *key, char *buf, int len) {
    Inst *o = static_cast<Inst *>(inst);
    // fast, lock-free values first (polled often, also from the audio thread)
    if (!std::strcmp(key, "volume")) return puti(buf, len, o->volume);
    if (!std::strcmp(key, "pan")) return puti(buf, len, o->pan);
    if (!std::strcmp(key, "volume_display")) return puti(buf, len, o->volume);
    if (!std::strcmp(key, "pan_display")) return puti(buf, len, o->pan);   // 0..127, as sfizz shows it
    if (!std::strcmp(key, "polyphony")) return puti(buf, len, o->polyphony_idx);
    if (!std::strcmp(key, "oversampling")) return puti(buf, len, o->oversampling_idx);
    if (!std::strcmp(key, "preload")) return puti(buf, len, o->preload_idx);
    if (!std::strcmp(key, "sustain_cancels")) return puti(buf, len, o->sustain_cancels);
    if (!std::strcmp(key, "sample_quality")) return puti(buf, len, o->sample_quality);
    if (!std::strcmp(key, "osc_quality")) return puti(buf, len, o->osc_quality);
    if (!std::strcmp(key, "root_note")) return puti(buf, len, o->root_note);
    if (!std::strcmp(key, "root_oct")) return puti(buf, len, o->root_oct);
    if (!std::strcmp(key, "tuning_freq")) return std::snprintf(buf, size_t(len), "%.1f", double(o->tuning_freq.load()));
    if (!std::strcmp(key, "tuning_freq_display")) return std::snprintf(buf, size_t(len), "%.1f Hz", double(o->tuning_freq.load()));
    if (!std::strcmp(key, "tuning_preset")) return puti(buf, len, o->tuning_preset);
    if (!std::strcmp(key, "stretch")) return std::snprintf(buf, size_t(len), "%.3f", double(o->stretch.load()));
    if (!std::strcmp(key, "stretch_display")) return std::snprintf(buf, size_t(len), "%d%%", int(std::lround(o->stretch * 100)));
    if (!std::strcmp(key, "browse")) return puti(buf, len, o->browse);
    if (!std::strcmp(key, "display_rev")) return puti(buf, len, o->display_rev);
    if (!std::strcmp(key, "voices")) return puti(buf, len, o->active_voices);
    if (int s = cc_slot_of(key); s >= 0) {
        const char *rest = key + 3;
        while (std::isdigit(static_cast<unsigned char>(*rest))) rest++;
        if (!*rest) return puti(buf, len, o->cc_val[s]);
        std::lock_guard<std::mutex> l(o->ui_m);
        if (!std::strcmp(rest, "_name")) return put(buf, len, o->slot_cc[s] < 0 ? std::string("-") : o->slot_label[s]);
        if (!std::strcmp(rest, "_display")) return o->slot_cc[s] < 0 ? put(buf, len, "") : puti(buf, len, o->cc_val[s]);
        return 0;
    }
    if (!std::strcmp(key, "ks_badge")) {
        int sw = o->sw_current;
        return put(buf, len, sw < 0 ? std::string("") : note_name(sw));
    }
    if (!std::strcmp(key, "memory")) return put(buf, len, memory_text(o->mem_bytes));
    if (int k = kb_offset(key); k >= 0) {
        int note = o->kb_base + k;
        bool ok = note >= 0 && note <= 127;
        if (std::strstr(key, "_on")) return puti(buf, len, ok && (o->held[note] || o->tap_lit[note]) ? 1 : 0);
        if (ok && note % 12 == 0) return std::snprintf(buf, size_t(len), "C%d", note / 12 - 1);   // the C keys' octave
        return put(buf, len, " ");
    }
    std::lock_guard<std::mutex> l(o->ui_m);
    if (int r = br_row_of(key); r >= 0) {
        int i = o->br_top + r;
        bool any = i < int(o->br_entries.size());
        if (std::strstr(key, "_on")) return puti(buf, len, any && !o->br_entries[size_t(i)].dir && o->br_entries[size_t(i)].path == o->file_path);
        if (!any) return put(buf, len, " ");
        const Entry &e = o->br_entries[size_t(i)];
        return put(buf, len, e.dir ? e.name + "/" : stem(e.name));
    }
    if (!std::strcmp(key, "file_name")) return put(buf, len, o->file_label);
    if (!std::strcmp(key, "ks_label")) {
        if (!o->has_sw) return put(buf, len, "No key switch");
        int sw = o->sw_current;
        if (sw < 0) return put(buf, len, " ");
        auto it = o->sw_labels.find(sw);
        return put(buf, len, it == o->sw_labels.end() || it->second.empty() ? std::string(" ") : it->second);
    }
    // display-only switches for the skin's when= panels, and the CC badges
    if (!std::strcmp(key, "has_sw")) return puti(buf, len, o->has_sw ? 1 : 0);
    if (!std::strncmp(key, "ccshow_", 7) || !std::strncmp(key, "ccl_", 4)) {
        int s = std::atoi(key + (key[2] == 's' ? 7 : 4)) - 1;
        if (s < 0 || s >= CC_SLOTS) return 0;
        if (key[2] == 's') return puti(buf, len, o->slot_cc[s] >= 0 ? 1 : 0);
        return o->slot_cc[s] < 0 ? put(buf, len, " ") : std::snprintf(buf, size_t(len), "CC %d", o->slot_cc[s]);
    }
    if (!std::strcmp(key, "n_curves")) return puti(buf, len, o->n_curves);
    if (!std::strcmp(key, "n_masters")) return puti(buf, len, o->n_masters);
    if (!std::strcmp(key, "n_groups")) return puti(buf, len, o->n_groups);
    if (!std::strcmp(key, "n_regions")) return puti(buf, len, o->n_regions);
    if (!std::strcmp(key, "n_samples")) return puti(buf, len, o->n_samples);
    if (!std::strcmp(key, "scala_name")) return put(buf, len, o->scala_label);
    if (!std::strcmp(key, "status")) return put(buf, len, o->status.empty() ? std::string(" ") : o->status);
    if (!std::strcmp(key, "br_loc")) {
        std::string d = o->br_dir.empty() ? std::string("Places") : o->br_dir;
        if (d.rfind(o->data_dir, 0) == 0) d = "Plugin Library" + d.substr(o->data_dir.size() + std::strlen("/Instruments"));
        if (d.size() > 44) d = "..." + d.substr(d.size() - 41);
        return put(buf, len, d);
    }
    if (!std::strcmp(key, "br_page")) {
        int n = int(o->br_entries.size());
        if (n <= BR_ROWS) return put(buf, len, n == 1 ? "1 item" : std::to_string(n) + " items");
        return std::snprintf(buf, size_t(len), "%d-%d of %d", o->br_top + 1, std::min(n, o->br_top + BR_ROWS), n);
    }
    return 0;
}

// "state" takes ui_m itself (state_text), so it is answered before get_param(), which holds ui_m for its text keys
int get_param_entry(void *inst, const char *key, char *buf, int len) {
    if (!std::strcmp(key, "state")) return put(buf, len, state_text(static_cast<Inst *>(inst)));
    return get_param(inst, key, buf, len);
}

}  // namespace

extern "C" const mpc_engine_t *mpc_engine(void) {
    static const mpc_engine_t api = {create, destroy, midi, set_param, get_param_entry, render, nullptr, nullptr};
    return &api;
}

extern "C" int mpc_engine_changed(void *inst, char *key, int len) {
    Inst *o = static_cast<Inst *>(inst);
    uint32_t c = o->changed.load();
    if (!c) return 0;
    for (int b = 0; b < 32; b++) {
        uint32_t m = 1u << b;
        if (!(c & m)) continue;
        o->changed &= ~m;
        if (m == CH_VOLUME) return std::snprintf(key, size_t(len), "volume");
        if (m == CH_PAN) return std::snprintf(key, size_t(len), "pan");
        if (m == CH_TUNING) return std::snprintf(key, size_t(len), "tuning_freq");
        if (m == CH_BROWSE) return std::snprintf(key, size_t(len), "browse");
        if (b >= 8 && b < 8 + CC_SLOTS) return std::snprintf(key, size_t(len), "cc_%d", b - 7);
    }
    return 0;
}
