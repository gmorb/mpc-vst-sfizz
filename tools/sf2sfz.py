#!/usr/bin/env python3
"""SoundFont 2 -> SFZ (one .sfz per preset, samples as WAV), for sfizz.

  sf2sfz.py <in.sf2> <out folder> [--bank-names "0=Instruments,10=Percussion"]

Follows the SF2 2.04 rules: instrument generators override the instrument's global zone and the defaults; preset
generators add to them (key and velocity ranges intersect). Loops, root key, tuning, pan, attenuation (with the
customary 0.4 factor, as FluidSynth and Polyphone), volume and modulation envelopes, filter, LFOs and exclusive classes
become SFZ opcodes. SF2 modulators other than the defaults are not converted.
"""
import math, os, re, struct, sys
import sf2parse

DEFAULTS = {8: 13500, 21: -12000, 22: 0, 23: -12000, 24: 0, 25: -12000, 26: -12000, 27: -12000, 28: -12000, 29: 0,
            30: -12000, 33: -12000, 34: -12000, 35: -12000, 36: -12000, 37: 0, 38: -12000, 56: 100, 58: -1}
ADDITIVE_SKIP = {41, 53, 43, 44, 46, 47, 54, 57, 58, 0, 1, 2, 3, 4, 12, 45, 50}   # not allowed / not additive at preset level


def tc2s(tc):
    return 2.0 ** (tc / 1200.0)


def abs_cents_hz(c):
    return 8.176 * 2.0 ** (c / 1200.0)


def fmt(x, nd=4):
    s = ('%.' + str(nd) + 'f') % x
    return s.rstrip('0').rstrip('.') if '.' in s else s


def safe(name):
    return re.sub(r'[^A-Za-z0-9 ._()+-]', '_', name).strip() or 'untitled'


def write_wav(path, frames16, low8, rate):
    """Mono WAV, 24-bit when the SF2 has sm24 data, else 16-bit."""
    n = len(frames16)
    if low8 is not None:
        b = bytearray(n * 3)
        hi = struct.pack('<%dh' % n, *frames16)
        b[1::3] = hi[0::2]
        b[2::3] = hi[1::2]
        b[0::3] = bytes(low8)
        bits, data = 24, bytes(b)
    else:
        bits, data = 16, struct.pack('<%dh' % n, *frames16)
    ba = bits // 8
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * ba, ba, bits))
        f.write(b'data' + struct.pack('<I', len(data)) + data)


def merged(inst_global, inst_zone, pre_global, pre_zone):
    g = dict(DEFAULTS)
    g.update(inst_global)
    g.update(inst_zone)
    p = dict(pre_global)
    p.update(pre_zone)
    for k, v in p.items():
        if k in (43, 44):
            lo, hi = g.get(k, (0, 127))
            g[k] = (max(lo, v[0]), min(hi, v[1]))
        elif k not in ADDITIVE_SKIP:
            g[k] = g.get(k, 0) + v
    return g


def region_opcodes(g, s, wav_rel, nframes):
    o = []
    lo, hi = g.get(43, (0, 127))
    vlo, vhi = g.get(44, (0, 127))
    if lo > hi or vlo > vhi:
        return None
    o.append('sample=%s' % wav_rel)
    o.append('lokey=%d hikey=%d' % (lo, hi))
    if (vlo, vhi) != (0, 127):
        o.append('lovel=%d hivel=%d' % (vlo, vhi))
    root = g[58] if g.get(58, -1) >= 0 else s['pitch']
    if root > 127:
        root = 60
    o.append('pitch_keycenter=%d' % root)
    tune = g.get(51, 0) * 100 + g.get(52, 0) + s['correction']
    fixed = 46 in g and 0 <= g[46] <= 127
    if fixed:   # the zone always plays as if this key were hit
        tune += (g[46] - root) * g.get(56, 100)
    if tune:
        o.append('tune=%d' % tune)
    if fixed:
        o.append('pitch_keytrack=0')
    elif g.get(56, 100) != 100:
        o.append('pitch_keytrack=%d' % g[56])
    # sample points (relative to this sample's WAV)
    start = g.get(0, 0) + 32768 * g.get(4, 0)
    end = (nframes - 1) + g.get(1, 0) + 32768 * g.get(12, 0)
    ls = s['loopstart'] - s['start'] + g.get(2, 0) + 32768 * g.get(45, 0)
    le = s['loopend'] - s['start'] - 1 + g.get(3, 0) + 32768 * g.get(50, 0)
    if start > 0:
        o.append('offset=%d' % start)
    if end < nframes - 1:
        o.append('end=%d' % max(start, end))
    mode = g.get(54, 0) & 3
    if mode in (1, 3) and 0 <= ls < le <= nframes - 1:
        o.append('loop_mode=%s loop_start=%d loop_end=%d' % ('loop_continuous' if mode == 1 else 'loop_sustain', ls, le))
    else:
        o.append('loop_mode=no_loop')
    att = g.get(48, 0)
    if att:
        o.append('volume=%s' % fmt(-att * 0.04, 2))   # cB x 0.4 (EMU convention) -> dB
    if g.get(17, 0):
        o.append('pan=%s' % fmt(max(-100, min(100, g[17] / 5.0)), 1))
    # volume envelope
    o.append('ampeg_delay=%s ampeg_attack=%s ampeg_hold=%s ampeg_decay=%s ampeg_sustain=%s ampeg_release=%s' % (
        fmt(0 if g[33] <= -12000 else tc2s(g[33])), fmt(tc2s(g[34])), fmt(0 if g[35] <= -12000 else tc2s(g[35])),
        fmt(tc2s(g[36])), fmt(100.0 * 10 ** (-max(0, min(1440, g[37])) / 200.0), 3), fmt(tc2s(g[38]))))
    # filter
    fc = g.get(8, 13500)
    fil = fc < 13500 or g.get(11, 0) or g.get(10, 0)
    if fil:
        o.append('fil_type=lpf_2p cutoff=%s' % fmt(min(abs_cents_hz(fc), 20000), 1))
        if g.get(9, 0) > 0:
            o.append('resonance=%s' % fmt(g[9] / 10.0, 1))
    # modulation envelope -> filter / pitch
    egs = (['fileg'] if fil and g.get(11, 0) else []) + (['pitcheg'] if g.get(7, 0) else [])
    stages = (('delay', fmt(0 if g[25] <= -12000 else tc2s(g[25]))), ('attack', fmt(tc2s(g[26]))),
              ('hold', fmt(0 if g[27] <= -12000 else tc2s(g[27]))), ('decay', fmt(tc2s(g[28]))),
              ('sustain', fmt(100 - max(0, min(1000, g[29])) / 10.0, 1)), ('release', fmt(tc2s(g[30]))))
    for eg in egs:
        o.append(' '.join('%s_%s=%s' % (eg, k, v) for k, v in stages))
    if 'fileg' in egs:
        o.append('fileg_depth=%d' % g[11])
    if 'pitcheg' in egs:
        o.append('pitcheg_depth=%d' % g[7])
    # LFOs (only where they do something)
    vib_depth = g.get(6, 0)
    if vib_depth:
        o.append('pitchlfo_delay=%s pitchlfo_freq=%s pitchlfo_depth=%d' % (fmt(tc2s(g[23])), fmt(abs_cents_hz(g[24]), 3), vib_depth))
    elif g.get(5, 0):
        o.append('pitchlfo_delay=%s pitchlfo_freq=%s pitchlfo_depth=%d' % (fmt(tc2s(g[21])), fmt(abs_cents_hz(g[22]), 3), g[5]))
    if g.get(13, 0):
        o.append('amplfo_delay=%s amplfo_freq=%s amplfo_depth=%s' % (fmt(tc2s(g[21])), fmt(abs_cents_hz(g[22]), 3), fmt(-g[13] / 10.0, 1)))
    if fil and g.get(10, 0):
        o.append('fillfo_delay=%s fillfo_freq=%s fillfo_depth=%d' % (fmt(tc2s(g[21])), fmt(abs_cents_hz(g[22]), 3), g[10]))
    if g.get(57, 0):
        o.append('group=%d off_by=%d' % (g[57], g[57]))
    return o


def convert(sf2_path, out_dir, bank_names=None, source_name=None):
    sf = sf2parse.read(sf2_path)
    smpl, sm24 = sf['smpl'], sf['sm24']
    os.makedirs(os.path.join(out_dir, 'Samples'), exist_ok=True)
    written = {}
    used_names = set()

    def wav_for(si):
        if si in written:
            return written[si]
        s = sf['samples'][si]
        frames = smpl[s['start']:s['end']]
        low = sm24[s['start']:s['end']] if sm24 is not None and len(sm24) >= s['end'] else None
        base = safe(s['name'])
        name, k = base, 2
        while name.lower() in used_names:
            name, k = '%s_%d' % (base, k), k + 1
        used_names.add(name.lower())
        fn = name + '.wav'
        write_wav(os.path.join(out_dir, 'Samples', fn), frames.tolist(), low, s['rate'])
        written[si] = (fn, len(frames))
        return written[si]

    count = 0
    for p in sorted(sf['presets'], key=lambda p: (p['bank'], p['program'])):
        sub = (bank_names or {}).get(p['bank'], 'Bank %d' % p['bank'])
        d = os.path.join(out_dir, sub)
        os.makedirs(d, exist_ok=True)
        lines = ['// %s (bank %d, program %d), converted from %s' % (p['name'], p['bank'], p['program'], source_name or os.path.basename(sf2_path)),
                 '<control>', 'default_path=../Samples/', '']
        regions = 0
        for pz in p['zones']:
            inst = sf['instruments'][pz[41]]
            for iz in inst['zones']:
                g = merged(inst['global'], iz, p['global'], pz)
                si = g[53]
                s = sf['samples'][si]
                if s['type'] & 0x8000:   # ROM sample: no data
                    continue
                fn, n = wav_for(si)
                ops = region_opcodes(g, s, fn, n)
                if ops is None:
                    continue
                lines.append('<region> ' + ' '.join(ops))
                regions += 1
        if not regions:
            continue
        fname = '%03d %s.sfz' % (p['program'], safe(p['name']).title())
        with open(os.path.join(d, fname), 'w') as f:
            f.write('\n'.join(lines) + '\n')
        count += 1
    return count, len(written)


if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]
    names = {}
    if len(sys.argv) > 4 and sys.argv[3] == '--bank-names':
        for kv in sys.argv[4].split(','):
            k, v = kv.split('=', 1)
            names[int(k)] = v
    n, w = convert(src, out, names, os.environ.get('SF2_NAME'))
    print('%d presets -> %d .sfz files, %d samples' % (n, n, w))
