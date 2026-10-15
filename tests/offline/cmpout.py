import sys, numpy as np
a = np.fromfile(sys.argv[1], np.float32); b = np.fromfile(sys.argv[2], np.float32)
n = min(len(a), len(b)); d = np.abs(a[:n] - b[:n]).reshape(-1, 256).max(1)
peak = np.abs(b).max()
bad = np.nonzero(d > 1e-6)[0]
print(f"blocks differing: {len(bad)} of {len(d)}; max diff {d.max():.3g} ({20*np.log10(max(d.max(),1e-12)/peak):.1f} dB re peak)", "first at %.2f s" % (bad[0]*128/44100) if len(bad) else "")
