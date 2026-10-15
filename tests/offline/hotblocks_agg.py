# Per-function ARM instruction counts from QEMU's hotblocks plugin: render run minus a 0-block run (TESTING.md).
#   python3 hotblocks_agg.py <lib.so> <hot.txt> <hot0.txt> <blocks> <voices>
import re, subprocess, collections, bisect, sys
LIB, HOT, HOT0 = sys.argv[1], sys.argv[2], sys.argv[3]
def load(path):
    txt = open(path).read()
    base_rt = int(re.search(r"ABMAIN (0x[0-9a-f]+)", txt).group(1), 16) if "ABMAIN" in txt else None
    return txt, base_rt
nm = subprocess.run(["arm-linux-gnueabihf-nm", "-C", "--defined-only", "-n", LIB], capture_output=True, text=True).stdout
syms = sorted((int(p[0], 16), p[2]) for p in (l.split(" ", 2) for l in nm.splitlines()) if len(p) == 3 and p[1] in "tTwW")
addrs = [a for a, _ in syms]
abmain = next(a for a, n in syms if n.startswith("ab_main"))
def agg(path, base):
    txt, _ = load(path)
    out = collections.Counter()
    loadaddr = base - (abmain & ~1)
    for line in txt.splitlines():
        m = re.match(r"(0x[0-9a-f]+), (\d+), (\d+), (\d+)", line)
        if not m: continue
        pc, icount, ecount = int(m.group(1), 16), int(m.group(3)), int(m.group(4))
        off = pc - loadaddr
        i = bisect.bisect_right(addrs, off) - 1
        name = re.sub(r"\(.*", "", syms[i][1])[:80] if 0 <= i and 0 <= off < 0x2000000 else "(libc/libm/ld)"
        out[name or "(unnamed)"] += icount * ecount
    return out
_, base = load(HOT)
a = agg(HOT, base)
_, base0 = load(HOT0)
b = agg(HOT0, base0 if base0 else base)
d = collections.Counter({k: a[k] - b.get(k, 0) for k in a})
tot = sum(v for v in d.values() if v > 0)
blocks, voices = int(sys.argv[4]), int(sys.argv[5])
print(f"render total: {tot/1e6:.0f}M instr = {tot/blocks/voices/1000:.1f}K per voice-block")
for k, v in d.most_common(20):
    if v > 0: print(f"{100*v/tot:5.1f}%  {v/blocks/voices:7.0f} per voice-block  {k}")
