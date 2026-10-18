// SoundFont 2 support for sfizz for MPC OS: list an .sf2 file's presets, and turn one preset into an SFZ file plus
// mono WAV samples in a cache folder, which sfizz then loads (and streams) like any other SFZ instrument.
//
// The conversion follows the SF2 2.04 rules: instrument generators override the instrument's global zone and the
// defaults; preset generators add to them (key and velocity ranges intersect). Loops, root key, tuning, pan,
// attenuation (with the customary 0.4 factor, as FluidSynth and Polyphone), volume and modulation envelopes, filter,
// LFOs and exclusive classes become SFZ opcodes. SF2 modulators other than the defaults are not converted.
// Malformed files are rejected with a message, never read out of bounds.
#pragma once
#include <string>
#include <vector>

namespace sf2 {

struct Preset {
    std::string name;
    int bank = 0, program = 0;
};

// The presets of an SF2 file, sorted by bank, then program. False (and a message) if it can't be read.
bool list_presets(const std::string &path, std::vector<Preset> &out, std::string &error);

// Convert one preset (bank, program) into `<cache_dir>/<bank>-<program>.sfz`, writing the WAV files it needs into
// `<cache_dir>/Samples/` (ones already there are reused). Returns the SFZ path, or "" and a message. `name` gets the
// preset's name. Safe to call from several threads for the same cache folder (files appear by rename).
std::string preset_to_sfz(const std::string &path, int bank, int program, const std::string &cache_dir,
                          std::string &error, std::string *name = nullptr);

}  // namespace sf2
