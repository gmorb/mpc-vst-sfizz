# usage: python3 tests/offline/sf2_reference.py <in.sf2> <out folder>; then diff -r against tests/offline/sf2conv.cpp output
# reference: sf2sfz.py's logic, samples named s<index>.wav and one <bank>-<prog>.sfz per preset (as the C++ cache)
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import sf2parse, sf2sfz
src, out = sys.argv[1], sys.argv[2]
sf = sf2parse.read(src)
os.makedirs(out + '/Samples', exist_ok=True)
done = {}
seen = set()
for p in sorted(sf['presets'], key=lambda p: (p['bank'], p['program'])):
    if (p['bank'], p['program']) in seen: continue
    seen.add((p['bank'], p['program']))
    lines = ['// %s (bank %d, program %d), converted from %s' % (p['name'], p['bank'], p['program'], os.path.basename(src)),
             '<control>', 'default_path=Samples/', '']
    n = 0
    for pz in p['zones']:
        inst = sf['instruments'][pz[41]]
        for iz in inst['zones']:
            g = sf2sfz.merged(inst['global'], iz, p['global'], pz)
            si = g[53]; s = sf['samples'][si]
            if s['type'] & 0x8000: continue
            fn = 's%d.wav' % si
            if si not in done:
                frames = sf['smpl'][s['start']:s['end']]
                low = sf['sm24'][s['start']:s['end']] if sf['sm24'] is not None and len(sf['sm24']) >= s['end'] else None
                sf2sfz.write_wav(out + '/Samples/' + fn, frames.tolist(), low, s['rate'])
                done[si] = len(frames)
            ops = sf2sfz.region_opcodes(g, s, fn, done[si])
            if ops is None: continue
            lines.append('<region> ' + ' '.join(ops)); n += 1
    if n:
        open('%s/%d-%d.sfz' % (out, p['bank'], p['program']), 'w').write('\n'.join(lines) + '\n')
