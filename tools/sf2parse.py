"""Minimal SoundFont 2 reader: presets, instruments, zones (generators), samples."""
import struct

GEN_NAMES = {0:'startAddrsOffset',1:'endAddrsOffset',2:'startloopAddrsOffset',3:'endloopAddrsOffset',4:'startAddrsCoarseOffset',
 5:'modLfoToPitch',6:'vibLfoToPitch',7:'modEnvToPitch',8:'initialFilterFc',9:'initialFilterQ',10:'modLfoToFilterFc',
 11:'modEnvToFilterFc',12:'endAddrsCoarseOffset',13:'modLfoToVolume',15:'chorusEffectsSend',16:'reverbEffectsSend',17:'pan',
 21:'delayModLFO',22:'freqModLFO',23:'delayVibLFO',24:'freqVibLFO',25:'delayModEnv',26:'attackModEnv',27:'holdModEnv',
 28:'decayModEnv',29:'sustainModEnv',30:'releaseModEnv',31:'keynumToModEnvHold',32:'keynumToModEnvDecay',33:'delayVolEnv',
 34:'attackVolEnv',35:'holdVolEnv',36:'decayVolEnv',37:'sustainVolEnv',38:'releaseVolEnv',39:'keynumToVolEnvHold',
 40:'keynumToVolEnvDecay',41:'instrument',43:'keyRange',44:'velRange',45:'startloopAddrsCoarseOffset',46:'keynum',
 47:'velocity',48:'initialAttenuation',50:'endloopAddrsCoarseOffset',51:'coarseTune',52:'fineTune',53:'sampleID',
 54:'sampleModes',56:'scaleTuning',57:'exclusiveClass',58:'overridingRootKey'}

def chunks(data, off, end):
    while off + 8 <= end:
        cid = data[off:off+4].decode('latin1'); size = struct.unpack_from('<I', data, off+4)[0]
        yield cid, off + 8, size
        off += 8 + size + (size & 1)

def cstr(b):
    return b.split(b'\0', 1)[0].decode('latin1', 'replace').strip()

def read(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'RIFF' and data[8:12] == b'sfbk', 'not a SoundFont 2 file'
    sub = {}
    for cid, off, size in chunks(data, 12, len(data)):
        if cid == 'LIST':
            kind = data[off:off+4].decode()
            for c2, o2, s2 in chunks(data, off + 4, off + size):
                sub[c2] = (o2, s2)
    info = {k: cstr(data[o:o+s]) for k, (o, s) in sub.items() if k in ('INAM', 'IENG', 'ICOP', 'ICMT', 'ISFT', 'IPRD')}
    so, ss = sub['smpl']
    smpl = memoryview(data)[so:so+ss].cast('h')
    sm24 = None
    if 'sm24' in sub:
        o, s = sub['sm24']; sm24 = memoryview(data)[o:o+s]
    def recs(name, fmt):
        o, s = sub[name]; n = struct.calcsize(fmt)
        return [struct.unpack_from(fmt, data, o + i*n) for i in range(s // n)]
    phdr = recs('phdr', '<20sHHHIII'); pbag = recs('pbag', '<HH'); pgen = recs('pgen', '<HH')
    inst = recs('inst', '<20sH'); ibag = recs('ibag', '<HH'); igen = recs('igen', '<HH')
    shdr = recs('shdr', '<20sIIIIIBbHH')
    def gens(genlist, a, b):
        g = {}
        for oper, amt in genlist[a:b]:
            if oper in (43, 44):
                g[oper] = (amt & 0xFF, amt >> 8)
            elif oper in (41, 53, 54, 56, 57, 58, 46, 47, 4, 12, 45, 50, 0, 1, 2, 3) and oper not in (0, 1, 2, 3, 4, 12, 45, 50):
                g[oper] = amt
            else:
                g[oper] = struct.unpack('<h', struct.pack('<H', amt))[0]
        return g
    def zones(hdrs, bags, genlist, idx, term):
        a, b = hdrs[idx][-1] if False else None, None
    instruments = []
    for i in range(len(inst) - 1):
        zl = []
        for z in range(inst[i][1], inst[i+1][1]):
            zl.append(gens(igen, ibag[z][0], ibag[z+1][0]))
        glob = zl[0] if zl and 53 not in zl[0] else {}
        zl = [z for z in zl if 53 in z]
        instruments.append({'name': cstr(inst[i][0]), 'global': glob, 'zones': zl})
    presets = []
    for i in range(len(phdr) - 1):
        zl = []
        for z in range(phdr[i][3], phdr[i+1][3]):
            zl.append(gens(pgen, pbag[z][0], pbag[z+1][0]))
        glob = zl[0] if zl and 41 not in zl[0] else {}
        zl = [z for z in zl if 41 in z]
        presets.append({'name': cstr(phdr[i][0]), 'program': phdr[i][1], 'bank': phdr[i][2], 'global': glob, 'zones': zl})
    samples = []
    for s in shdr[:-1]:
        samples.append({'name': cstr(s[0]), 'start': s[1], 'end': s[2], 'loopstart': s[3], 'loopend': s[4], 'rate': s[5],
                        'pitch': s[6], 'correction': s[7], 'link': s[8], 'type': s[9]})
    return {'info': info, 'smpl': smpl, 'sm24': sm24, 'instruments': instruments, 'presets': presets, 'samples': samples}
