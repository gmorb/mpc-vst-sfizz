#!/usr/bin/env python3
"""The README's pictures: docs/images/cpu.png (matplotlib) and docs/images/banner.png (HTML through headless Chromium,
Playwright; 1280x640, also the size GitHub wants for a repository's social preview).   tools/make_images.py

The CPU figures are the offline measurements in TESTING.md ("CPU per voice"): instructions per voice per 128-frame
block, Salamander Grand Piano V3 (WAV), 16 held voices, under QEMU with the hotblocks plugin. Update both together.
"""
import base64
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
OUT = os.path.join(ROOT, "docs", "images")

QUALITIES = ["Linear", "Polynomial", "Sinc 8", "Sinc 24"]
CPU = {   # thousands of instructions per voice per block: (sfizz 1.2.3, this port)
    "Gen1 (armv7, RK3288)":   [(34.2, 10.8), (36.5, 13.2), (55.9, 14.5), (91.2, 68.4)],
    "Gen2 (aarch64, RK3588)": [(31.6, 9.3), (33.6, 11.2), (39.3, 12.0), (83.3, 61.0)],
}
DEFAULT = {"Gen1 (armv7, RK3288)": "Linear", "Gen2 (aarch64, RK3588)": "Sinc 8"}

SURFACE, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
UPSTREAM, PORT = "#eb6834", "#2a78d6"   # categorical slots 2 and 1 (validated: dataviz validate_palette.js)


def cpu_chart(path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
    from matplotlib.patches import FancyBboxPatch
    for f in ("/usr/share/fonts/opentype/inter/Inter-Regular.otf", "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"):
        if os.path.exists(f):
            font_manager.fontManager.addfont(f)
    plt.rcParams.update({"font.family": "Inter", "font.size": 11, "text.color": INK,
                         "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2})
    fig, axes = plt.subplots(1, 2, figsize=(12.8, 5.6), dpi=100, sharey=True, facecolor=SURFACE)
    fig.subplots_adjust(left=0.07, right=0.985, top=0.72, bottom=0.15, wspace=0.08)
    w, gap = 0.36, 0.03
    for ax, (gen, rows) in zip(axes, CPU.items()):
        ax.set_facecolor(SURFACE)
        for i, (up, port) in enumerate(rows):
            for x, v, c in ((i - w / 2 - gap / 2, up, UPSTREAM), (i + w / 2 + gap / 2, port, PORT)):
                # bar with a rounded data end, square at the baseline
                ax.add_patch(FancyBboxPatch((x - w / 2, 0), w, v, boxstyle="round,pad=0,rounding_size=0.06",
                                            mutation_aspect=40, linewidth=0, facecolor=c))
                ax.add_patch(plt.Rectangle((x - w / 2, 0), w, min(v, 3), linewidth=0, facecolor=c, antialiased=False))
                ax.text(x, v + 1.5, ("%.1f" % v).rstrip("0").rstrip("."), ha="center", va="bottom", fontsize=10.5,
                        color=INK if c == PORT else INK2, fontweight="semibold" if c == PORT else "normal")
        ax.set_xticks(range(len(QUALITIES)))
        ax.set_xticklabels(["%s%s\n%.1f× less" % (q, " (default)" if q == DEFAULT[gen] else "", u / n)
                            for q, (u, n) in zip(QUALITIES, rows)], fontsize=10.5, linespacing=1.6)
        ax.tick_params(axis="x", length=0, pad=6)
        ax.tick_params(axis="y", length=0)
        ax.set_xlim(-0.6, len(QUALITIES) - 0.4)
        ax.set_ylim(0, 100)
        ax.set_yticks([0, 25, 50, 75, 100])
        ax.grid(axis="y", color=GRID, linewidth=1)
        ax.set_axisbelow(True)
        for s in ("top", "right", "left"):
            ax.spines[s].set_visible(False)
        ax.spines["bottom"].set_color("#b9b8b2")
        ax.set_title(gen, loc="left", fontsize=12.5, fontweight="semibold", pad=10)
    axes[0].set_ylabel("thousand instructions per voice per block")
    fig.text(0.07, 0.94, "CPU per voice: sfizz 1.2.3 vs sfizz for MPC OS", fontsize=17, fontweight="semibold")
    fig.text(0.07, 0.885, "ARM instructions per voice per 128-frame block, lower is better. Salamander Grand Piano V3, "
             "16 held voices, measured under emulation (QEMU).", fontsize=10.5, color=INK2)
    # legend (two series: always present)
    lx = 0.07
    for j, (name, c) in enumerate((("sfizz 1.2.3", UPSTREAM), ("this port", PORT))):
        fig.patches.append(FancyBboxPatch((lx + j * 0.11, 0.818), 0.012, 0.024, boxstyle="round,pad=0,rounding_size=0.003",
                                          transform=fig.transFigure, facecolor=c, linewidth=0))
        fig.text(lx + j * 0.11 + 0.018, 0.83, name, va="center", fontsize=10.5, color=INK)
    fig.savefig(path, facecolor=SURFACE)
    plt.close(fig)


def banner(path):
    from playwright.sync_api import sync_playwright
    def data(p):
        return "data:image/png;base64," + base64.b64encode(open(os.path.join(ROOT, p), "rb").read()).decode()
    font = base64.b64encode(open(os.path.join(ROOT, "fonts", "Roboto-Regular.ttf"), "rb").read()).decode()
    html = """<!doctype html><html><head><meta charset="utf-8"><style>
@font-face { font-family: R; src: url(data:font/ttf;base64,%(font)s); }
html, body { margin: 0; }
body { width: 1280px; height: 640px; overflow: hidden; font-family: R, sans-serif; color: #e9ecea;
       background: #2e3436 radial-gradient(760px 560px at 84%% 60%%, #434c50 0%%, rgba(46,52,54,0) 70%%); position: relative; }
.logo { position: absolute; left: 64px; top: 58px; width: 324px; }
.title { position: absolute; left: 66px; top: 196px; font-size: 44px; letter-spacing: .3px; }
.title b { font-weight: normal; color: #ffffff; }
.sub { position: absolute; left: 68px; top: 262px; width: 420px; font-size: 19px; line-height: 1.5; color: #b9c0bd; }
.chips { position: absolute; left: 66px; top: 392px; display: flex; flex-wrap: wrap; gap: 10px; width: 430px; }
.chip { font-size: 15px; padding: 7px 13px; border-radius: 6px; background: #3a4144; color: #dfe4e2; border: 1px solid #50585b; }
.chip.g { background: #006b00; border-color: #0a8a0a; color: #fff; }
.foot { position: absolute; left: 68px; bottom: 40px; font-size: 13px; color: #7f8885; }
.shot { position: absolute; left: 520px; top: 109px; width: 860px; border-radius: 10px;
        box-shadow: 0 30px 70px rgba(0,0,0,.55), 0 0 0 1px rgba(255,255,255,.06); }
</style></head><body>
<img class="logo" src="%(tile)s">
<div class="title"><b>for MPC OS</b></div>
<div class="sub">The sfizz 1.2.3 sampler for MPC and Force standalone hardware: SFZ and SoundFont 2, streaming
from SD cards and USB drives.</div>
<div class="chips"><span class="chip g">Gen1 · armv7</span><span class="chip g">Gen2 · aarch64</span>
<span class="chip">MPC OS 3.x</span><span class="chip">SFZ + SoundFont 2</span><span class="chip">key switches · Scala</span><span class="chip">BSD-2-Clause</span></div>
<div class="foot">Unofficial port. sfizz by the sfztools contributors. Not affiliated with inMusic or Akai.</div>
<img class="shot" src="%(shot)s">
</body></html>""" % {"font": font, "tile": data("art/tile.png"), "shot": data("docs/screen.png")}
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 1280, "height": 640})
        pg.set_content(html)
        pg.wait_for_timeout(300)
        pg.screenshot(path=path)
        b.close()


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    what = sys.argv[1:] or ["cpu", "banner"]
    if "cpu" in what:
        cpu_chart(os.path.join(OUT, "cpu.png"))
    if "banner" in what:
        banner(os.path.join(OUT, "banner.png"))
    print("wrote", ", ".join(what), "in", os.path.relpath(OUT))
