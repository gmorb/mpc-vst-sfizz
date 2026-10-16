#!/usr/bin/env python3
"""sfizz for MPC OS: the skin design. Draws every piece of artwork into art/ and writes layout.conf, from one set of
coordinates, so the two never disagree.

The page is sfizz 1.2.3's editor (plugins/editor/layout/main.fl in sfztools/sfizz-ui) with its four tabs laid side by
side on MPC's 1280 x 628 plugin area: the header (logo, file box, Volume / Pan), then Information | Controls |
Settings, then the piano. Colours are sfizz's Default theme (resources/Themes/Default/theme.xml), text is Roboto
(sfizz's own font, which MPC also draws live). The SFZ browser (the file box's chevron) covers Information and
Controls while it is open.

    python3 design.py        -> art/*.png, layout.conf      (needs Pillow)
Coordinates here are plugin-area pixels (0..1280 x 0..628); layout.conf adds MPC's y offset (86).
"""
import math
import os
from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "art")
FONT = os.path.join(HERE, "fonts", "Roboto-Regular.ttf")
W, H, YOFF, SS = 1280, 628, 86, 2   # SS: supersampling for smooth edges

# sfizz Default theme
FRAME = (0xd3, 0xd7, 0xcf)        # frameBackground
BOX = (0xba, 0xbd, 0xb6)          # boxBackground (normal)
DARK = (0x2e, 0x34, 0x36)         # titleBoxBackground / valueBackground / inverted boxBackground
INFO = (0x54, 0x56, 0x53)         # the Information panel (a dark wash over the frame)
INK = (0, 0, 0)
WHITE = (0xff, 0xff, 0xff)
INACTIVE = (0xb2, 0xb2, 0xb2)     # inactiveText
ORANGE = (0xfd, 0x98, 0x00)       # highlightedText / iconHighlight
KNOB_ON = (0x00, 0xaa, 0x11)      # knobActiveTrack
KNOB_OFF = (0x30, 0x30, 0x30)     # knobInactiveTrack (normal)
KNOB_OFF_INV = (0x60, 0x60, 0x60) # knobInactiveTrack (inverted)
BADGE = (0x00, 0x6b, 0x0b)        # knobLabelBackground ("CC 7")
PILL = (0x69, 0x6b, 0x67)         # CC name pill and knob disc in the Controls panel
KS_PILL = (0x9a, 0x9a, 0x9a)      # keyswitch badge (inverted valueBackground)
KEY_BLACK = (0x59, 0x59, 0x59)
# a pressed key, as sfizz's piano draws a key the instrument doesn't tint: 0.2 darker (white) or lighter (black) in luma
KEY_WHITE_DOWN = (0xcc, 0xcc, 0xcc)
KEY_BLACK_DOWN = (0x8c, 0x8c, 0x8c)

# ---- geometry (plugin pixels) ----
LOGO = (8, 6, 278, 134)
FILEBOX = (294, 6, 620, 134)
VOLBOX = (922, 6, 350, 134)
INFOP = (8, 148, 278, 382)
CTRLP = (294, 148, 430, 382)
SETP = (732, 148, 540, 382)
BROWSER = (8, 148, 716, 382)      # covers Information + Controls while browsing
KEYS = (40, 540, 1200, 62)        # piano: 75 white keys, notes 0..127

FX = FILEBOX[0] + 18              # file box text left edge
FX2 = FILEBOX[0] + FILEBOX[2] - 14
ROW1, RULE1, ROW2, RULE2, ROW3 = 32, 58, 83, 106, 122   # file name, rule, keyswitch, rule, Voices/Max/Memory

CC_COLS, CC_CELL_W, CC_R = 4, 107, 26
CC_ROWS_Y = (176, 344)            # top of each CC cell row
SET_A, SET_B = SETP[0] + 16, SETP[0] + 286   # settings columns
SET_W = 238
VOL_CX = (VOLBOX[0] + 95, VOLBOX[0] + 255)
VOL_R = 26

BR_ROWS, BR_TH, BR_GAP = 8, 32, 4
BR_LIST_Y = BROWSER[1] + 58


def font(px):
    return ImageFont.truetype(FONT, int(round(px * SS)))


class Canvas:
    """A supersampled RGBA drawing, saved at 1x."""

    def __init__(self, w, h, bg=(0, 0, 0, 0)):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w * SS, h * SS), bg)
        self.d = ImageDraw.Draw(self.im)

    def s(self, *v):
        return [int(round(x * SS)) for x in v]

    def rrect(self, x, y, w, h, r, fill, outline=None, width=1):
        self.d.rounded_rectangle(self.s(x, y, x + w, y + h), radius=r * SS, fill=fill,
                                 outline=outline, width=int(width * SS) if outline else 0)

    def rect(self, x, y, w, h, fill):
        self.d.rectangle(self.s(x, y, x + w, y + h), fill=fill)

    def line(self, pts, fill, width):
        self.d.line(self.s(*[c for p in pts for c in p]), fill=fill, width=int(round(width * SS)), joint="curve")

    def text(self, x, y, s, px, fill, anchor="lm"):
        self.d.text((x * SS, y * SS), s, font=font(px), fill=fill, anchor=anchor)

    def paste(self, img, x, y):
        img = img.resize((img.width * SS // 2, img.height * SS // 2), Image.LANCZOS) if SS != 2 else img
        self.im.alpha_composite(img.convert("RGBA"), (int(x * SS), int(y * SS)))

    def save(self, name):
        self.im.resize((self.w, self.h), Image.LANCZOS).save(os.path.join(ART, name))
        return "art/" + name


def chevron(c, cx, cy, size, direction, fill, width=2.2):
    """sfizz's thin chevrons: < > v (the file box arrows and the drop-down marks)."""
    s = size / 2
    if direction == "left":
        pts = [(cx + s / 2, cy - s), (cx - s / 2, cy), (cx + s / 2, cy + s)]
    elif direction == "right":
        pts = [(cx - s / 2, cy - s), (cx + s / 2, cy), (cx - s / 2, cy + s)]
    elif direction == "up":
        pts = [(cx - s, cy + s / 2), (cx, cy - s / 2), (cx + s, cy + s / 2)]
    else:
        pts = [(cx - s, cy - s / 2), (cx, cy + s / 2), (cx + s, cy - s / 2)]
    c.line(pts, fill, width)


# ---- the page ----

def piano(c, white=WHITE, black=KEY_BLACK):
    x0, y0, kw_total, kh = KEYS
    n_white = sum(1 for k in range(128) if k % 12 in (0, 2, 4, 5, 7, 9, 11))
    ww = kw_total / n_white
    c.rect(x0 - 4, y0 - 4, kw_total + 8, kh + 8, (0xc4, 0xc8, 0xc0))
    xs = {}
    i = 0
    for k in range(128):
        if k % 12 in (0, 2, 4, 5, 7, 9, 11):
            x = x0 + i * ww
            xs[k] = x
            c.rect(x, y0, ww, kh, white)
            c.line([(x, y0), (x, y0 + kh)], INK, 1)
            i += 1
    c.line([(x0 + kw_total, y0), (x0 + kw_total, y0 + kh)], INK, 1)
    c.line([(x0, y0), (x0 + kw_total, y0)], INK, 1)
    c.line([(x0, y0 + kh), (x0 + kw_total, y0 + kh)], INK, 1)
    bw, bh = ww * 0.6, kh * 0.62
    for k in range(128):
        if k % 12 in (1, 3, 6, 8, 10):
            x = xs[k - 1] + ww - bw / 2
            c.rect(x, y0, bw, bh, INK)
            c.rect(x + 1.2, y0, bw - 2.4, bh - 1.4, black)
    for octave in range(11):   # C-1 .. C9, as sfizz numbers octaves (middle C = C4)
        k = octave * 12
        if k in xs:
            c.text(xs[k] + 2, y0 + kh + 13, str(octave - 1), 15, INK, anchor="lm")


def key_rects():
    """Each key's box on the strip (as piano() lays it out): (key, x, y, w, h)."""
    x0, y0, kw_total, kh = KEYS
    ww = kw_total / sum(1 for k in range(128) if k % 12 in (0, 2, 4, 5, 7, 9, 11))
    out, xs, i = [], {}, 0
    for k in range(128):
        if k % 12 in (0, 2, 4, 5, 7, 9, 11):
            xs[k] = x0 + i * ww
            out.append((k, xs[k], y0, ww, kh))
            i += 1
        else:
            out.append((k, xs[k - 1] + ww - ww * 0.6 / 2, y0, ww * 0.6, kh * 0.62))
    return out


def pressed_keys():
    """One picture per key, shown over the strip while the key's note is on: the pixels that change when that key is
    drawn pressed (all white keys pressed in one drawing, all black in another; each key keeps only its own box)."""
    def strip(white, black):
        c = Canvas(W, H)
        piano(c, white, black)
        return c.im.resize((W, H), Image.LANCZOS)
    base, whites, blacks = strip(WHITE, KEY_BLACK), strip(KEY_WHITE_DOWN, KEY_BLACK), strip(WHITE, KEY_BLACK_DOWN)
    out = {}
    for k, x, y, w, h in key_rects():
        src = blacks if k % 12 in (1, 3, 6, 8, 10) else whites
        box = (int(x) - 1, int(y) - 1, int(x + w) + 2, int(y + h) + 2)
        a, b = base.crop(box), src.crop(box)
        mask = Image.eval(ImageChops.difference(a.convert("RGB"), b.convert("RGB")).convert("L"), lambda v: 255 if v > 2 else 0)
        pic = b.copy()
        pic.putalpha(mask)
        name = "key_%d.png" % k
        pic.save(os.path.join(ART, name))
        out[k] = ("art/" + name, box)
    return out


def browser_tile():
    """The 270x110 tile MPC's Sounds > INSTRUMENTS page shows for the plugin: sfizz's logo on its dark title colour,
    with the flask emblem from its editor background."""
    c = Canvas(270, 110, DARK + (255,))
    emblem = Image.open(os.path.join(ART, "background@2x.png")).convert("RGBA")
    flask = emblem.crop((755, 0, 1012, emblem.height))
    flask = flask.crop(flask.getbbox())
    r, g, b, a = flask.split()
    flask = Image.merge("RGBA", (r, g, b, a.point(lambda v: min(255, v * 3))))   # stronger than the faint backdrop
    fh = 88
    fw = round(flask.width * fh / flask.height)
    c.im.alpha_composite(flask.resize((fw * SS, fh * SS), Image.LANCZOS), (int((262 - fw) * SS), int((110 - fh) / 2 * SS)))
    logo = Image.open(os.path.join(ART, "logo_text_shaded@2x.png")).convert("RGBA")
    lw, lh = 166, 82
    c.im.alpha_composite(logo.resize((lw * SS, lh * SS), Image.LANCZOS), (int(14 * SS), int((110 - lh) / 2 * SS)))
    return c.save("tile.png")


def header(c):
    x, y, w, h = LOGO
    c.rrect(x, y, w, h, 8, DARK)
    logo = Image.open(os.path.join(ART, "logo_text_shaded@2x.png")).convert("RGBA")   # 240 x 118 (2x art)
    lw, lh = 228, 112
    big = logo.resize((lw * SS, lh * SS), Image.LANCZOS)
    c.im.alpha_composite(big, (int((x + (w - lw) / 2) * SS), int((y + (h - lh) / 2) * SS)))

    x, y, w, h = FILEBOX
    c.rrect(x, y, w, h, 8, DARK)
    c.line([(FX, y + RULE1), (FX2, y + RULE1)], WHITE, 1.4)
    c.line([(FX, y + RULE2), (FX2, y + RULE2)], WHITE, 1.4)
    c.text(FX + 84, y + ROW3, "Voices:", 18, WHITE, anchor="rm")
    c.text(FX + 300, y + ROW3, "Max:", 18, WHITE, anchor="rm")
    c.text(FX + 486, y + ROW3, "Memory:", 18, WHITE, anchor="rm")
    chevron(c, FX + 380, y + ROW3, 12, "down", WHITE, 1.8)   # the Max drop-down mark

    x, y, w, h = VOLBOX
    c.rrect(x, y, w, h, 8, DARK)
    for cx, name, cc in zip(VOL_CX, ("Volume", "Pan"), ("CC 7", "CC 10")):
        c.text(cx, y + 20, name, 18, WHITE, anchor="mm")
        c.rrect(cx - 66, y + h - 36, 132, 28, 4, BADGE)
        c.text(cx, y + h - 22, cc, 17, WHITE, anchor="mm")


def info_panel(c):
    x, y, w, h = INFOP
    c.rrect(x, y, w, h, 8, INFO)
    for i, name in enumerate(("Curves:", "Masters:", "Groups:", "Regions:", "Samples:")):
        c.text(x + 18, y + 28 + i * 36, name, 18, WHITE)


def controls_panel(c):
    x, y, w, h = CTRLP
    emblem = Image.open(os.path.join(ART, "background@2x.png")).convert("RGBA")
    tw = int(w * 0.92)
    em = emblem.resize((tw * SS, int(emblem.height * tw / emblem.width) * SS), Image.LANCZOS)
    c.im.alpha_composite(em, (int((x + (w - tw) / 2) * SS), int((y + h - 6) * SS - em.height)))


def settings_panel(c):
    x, y, w, h = SETP
    c.rrect(x, y, w, h, 8, BOX)

    def title(cx, ty, s):
        c.text(cx, ty, s, 18, INK)
        c.line([(cx, ty + 15), (cx + SET_W, ty + 15)], INK, 1.2)

    def label(cx, ty, s):
        c.text(cx, ty, s, 17, INK)

    title(SET_A, y + 20, "Engine options")
    for i, s in enumerate(("Preload size", "Oversampling", "Sustain cancels release")):
        label(SET_A, y + 58 + i * 36, s)
    title(SET_A, y + 186, "Rendering quality")
    for i, s in enumerate(("Sample", "Oscillator")):
        label(SET_A, y + 224 + i * 36, s)
    title(SET_B, y + 20, "Tuning")
    for i, s in enumerate(("Scala file", "Root key", "Frequency")):
        label(SET_B, y + 58 + i * 36, s)
    label(SET_B, y + 212, "Stretch")
    # value pills behind the live values (sfizz's dark value buttons)
    for px, py, pw in settings_pills():
        c.rrect(px, py, pw, 28, 5, DARK)
    chevron(c, SET_B + SET_W - 14, y + 130, 12, "down", INK, 2.0)   # the frequency presets drop-down


def settings_pills():
    """(x, y, w) of each dark value pill in the Settings panel."""
    y = SETP[1]
    a2 = SET_A + SET_W
    b2 = SET_B + SET_W
    return [(a2 - 96, y + 44, 96), (a2 - 96, y + 80, 96),            # preload, oversampling
            (a2 - 112, y + 210, 112), (a2 - 112, y + 246, 112),      # sample, oscillator
            (b2 - 154, y + 44, 120),                                 # scala file (the X sits after it)
            (b2 - 96, y + 80, 52), (b2 - 40, y + 80, 40),            # root key note, octave
            (b2 - 122, y + 116, 88)]                                 # frequency


def page():
    c = Canvas(W, H, FRAME + (255,))
    header(c)
    info_panel(c)
    controls_panel(c)
    settings_panel(c)
    piano(c)
    return c.save("bg.png")


# ---- pieces shown by when= lines ----

def ks_badge():
    c = Canvas(84, 34)
    c.rrect(0, 0, 84, 34, 5, KS_PILL)
    return c.save("ks_badge.png")


def ks_none():
    c = Canvas(300, 40)
    c.text(0, 20, "No key switch", 26, INACTIVE)
    return c.save("ks_none.png")


def cc_cell():
    """One Controls knob's frame: the name pill above and the green CC badge below (the live texts sit on them)."""
    c = Canvas(CC_CELL_W - 6, 140)
    c.rrect(0, 0, CC_CELL_W - 6, 26, 4, PILL)
    c.rrect(0, 114, CC_CELL_W - 6, 26, 4, BADGE)
    return c.save("cc_cell.png")


def browser_panel():
    x, y, w, h = BROWSER
    c = Canvas(w, h)
    c.rrect(0, 0, w, h, 8, DARK)
    c.line([(16, 50), (w - 16, 50)], WHITE, 1.4)
    return c.save("browser.png")


# ---- control images ----

def knob_strip(name, r, dark):
    """A sfizz StyledKnob filmstrip: grey track, green value arc over 270 degrees, a pointer line, and (on light panels)
    the grey disc the value sits in. 128 frames, minimum first."""
    s = 2 * r + 10
    frames = 128
    strip = Image.new("RGBA", (s * SS, s * frames * SS), (0, 0, 0, 0))
    for f in range(frames):
        v = f / (frames - 1)
        c = Canvas(s, s)
        cx = cy = s / 2
        rr = r - 1
        box = c.s(cx - rr, cy - rr, cx + rr, cy + rr)
        trackw = int(round(5 * SS))
        start, end = 135, 405   # PIL angles: 0 = 3 o'clock, clockwise
        c.d.arc(box, start, end, fill=KNOB_OFF_INV if dark else KNOB_OFF, width=trackw)
        if v > 0.001:
            c.d.arc(box, start, start + 270 * v, fill=KNOB_ON, width=trackw)
        if not dark:
            dr = r - 9
            c.d.ellipse(c.s(cx - dr, cy - dr, cx + dr, cy + dr), fill=PILL)
        a = math.radians(start + 270 * v)
        r1, r2 = (r - 13, r - 2) if dark else (r - 10, r - 2)
        c.line([(cx + r1 * math.cos(a), cy + r1 * math.sin(a)), (cx + r2 * math.cos(a), cy + r2 * math.sin(a))],
               (0xfa, 0xfa, 0xfa), 2.0)
        strip.alpha_composite(c.im, (0, f * s * SS))
    strip = strip.resize((s, s * frames), Image.LANCZOS)
    strip.save(os.path.join(ART, name))
    return "art/" + name


def blank_strip(name, w, h):
    """An invisible knob (the frequency pill drags like a knob; the pill itself is in the background)."""
    Image.new("RGBA", (w, h * 128), (0, 0, 0, 0)).save(os.path.join(ART, name))
    return "art/" + name


def icon_button(name, w, h, draw, bg=None):
    """Off and on (pressed) images of an icon button: white icon, orange while pressed, as sfizz's icons."""
    out = []
    for state, colour in (("off", WHITE), ("on", ORANGE)):
        c = Canvas(w, h)
        if bg:
            c.rrect(0, 0, w, h, 5, bg)
        draw(c, w / 2, h / 2, colour)
        out.append(c.save("%s_%s.png" % (name, state)))
    return out


def checkbox():
    out = []
    for state in ("off", "on"):
        c = Canvas(24, 24)
        c.rect(3, 3, 18, 18, WHITE)
        c.d.rectangle(c.s(3, 3, 21, 21), outline=INK, width=int(1.4 * SS))
        if state == "on":
            c.line([(7, 12), (10.5, 16), (17, 7.5)], INK, 2.4)
        out.append(c.save("check_%s.png" % state))
    return out


def pill_button(name, w, h, fill):
    """A plain pill (the Scala file button); the live name is drawn on top."""
    out = []
    for state, f in (("off", fill), ("on", (0x45, 0x4d, 0x50))):
        c = Canvas(w, h)
        c.rrect(0, 0, w, h, 5, f)
        out.append(c.save("%s_%s.png" % (name, state)))
    return out


def house(c, cx, cy, col):
    c.line([(cx - 9, cy - 1), (cx, cy - 9), (cx + 9, cy - 1)], col, 2.2)
    c.line([(cx - 6, cy - 3), (cx - 6, cy + 8), (cx + 6, cy + 8), (cx + 6, cy - 3)], col, 2.2)


def folder_up(c, cx, cy, col):
    c.line([(cx - 10, cy - 5), (cx - 10, cy + 8), (cx + 10, cy + 8), (cx + 10, cy - 3), (cx, cy - 3), (cx - 3, cy - 7),
            (cx - 10, cy - 7), (cx - 10, cy - 5)], col, 1.8)
    c.line([(cx, cy + 5), (cx, cy - 1)], col, 1.8)
    c.line([(cx - 3, cy + 2), (cx, cy - 1), (cx + 3, cy + 2)], col, 1.8)


def cross(c, cx, cy, col):
    c.line([(cx - 6, cy - 6), (cx + 6, cy + 6)], col, 2.0)
    c.line([(cx - 6, cy + 6), (cx + 6, cy - 6)], col, 2.0)


# ---- layout ----

def L(*parts):
    return " ".join(str(p) for p in parts)


def ly(y):
    return y + YOFF


def write_layout(a):
    out = []
    o = out.append
    o("# sfizz for MPC OS skin layout: generated by design.py (art in art/). y = plugin y + 86.")
    o("art_css=skin.css")
    for k, v in (("bg", "d3d7cf"), ("ink", "ffffff"), ("ink_dim", "ffffff"), ("accent", "fd9800"), ("accent_hi", "fd9800"),
                 ("line", "babdb6"), ("lcd", "2e3436"), ("box", "2e3436"), ("seg_active", "fd9800"),
                 ("seg_inactive", "2e3436"), ("seg_active_tx", "000000"), ("tile_on", "006b0b"), ("display_ink", "ffffff"),
                 ("btn_text", "ffffff")):
        o("theme_%s=%s" % (k, v))
    o("qlinks_track = volume,pan,cc_1,cc_2,cc_3,cc_4,cc_5,cc_6,cc_7,cc_8,tuning_freq,stretch,sample_quality,osc_quality")
    o("")
    o("[tab SFIZZ]")
    o(L("art", 'file="%s"' % a["bg"], "x=0 y=%d w=1280 h=628 fit=stretch" % YOFF))
    fy = FILEBOX[1]
    # file box
    o(L("readout", "cx=%d cy=%d w=%d h=40" % (FX + 245, ly(fy + ROW1), 490), 'label=""', "key=file_name",
        "tsize=42 tfont=Roboto tcolor=ffffff talign=left"))
    o(L("button", "cx=%d cy=%d" % (FX2 - 82, ly(fy + ROW1)), 'label=""', "key=file_prev",
        "img=%s img_on=%s w=32 h=36" % a["prev"]))
    o(L("button", "cx=%d cy=%d" % (FX2 - 48, ly(fy + ROW1)), 'label=""', "key=file_next",
        "img=%s img_on=%s w=32 h=36" % a["next"]))
    o(L("toggle", "cx=%d cy=%d" % (FX2 - 14, ly(fy + ROW1)), 'label=""', "key=browse",
        "img=%s img_on=%s w=32 h=36 ns=0" % a["chev"]))
    o(L("art", 'file="%s"' % a["ks_badge"], "x=%d y=%d w=84 h=34" % (FX, ly(fy + ROW2 - 17)), "when=has_sw:On"))
    o(L("readout", "cx=%d cy=%d w=84 h=34" % (FX + 42, ly(fy + ROW2)), 'label=""', "key=ks_badge",
        "tsize=33 tfont=Roboto tcolor=000000", "when=has_sw:On"))
    o(L("readout", "cx=%d cy=%d w=%d h=40" % (FX + 92 + 240, ly(fy + ROW2), 480), 'label=""', "key=ks_label",
        "tsize=40 tfont=Roboto tcolor=ffffff talign=left", "when=has_sw:On"))
    o(L("art", 'file="%s"' % a["ks_none"], "x=%d y=%d w=300 h=40" % (FX, ly(fy + ROW2 - 20)), "when=has_sw:Off"))
    o(L("readout", "cx=%d cy=%d w=70 h=28" % (FX + 92 + 35, ly(fy + ROW3)), 'label=""', "key=voices",
        "tsize=27 tfont=Roboto tcolor=ffffff talign=left"))
    o(L("popup", "cx=%d cy=%d w=76 h=30" % (FX + 340, ly(fy + ROW3)), 'label=""', "key=polyphony field=none accent=ffffff"))
    o(L("readout", "cx=%d cy=%d w=90 h=28" % (FX + 494 + 45, ly(fy + ROW3)), 'label=""', "key=memory",
        "tsize=27 tfont=Roboto tcolor=ffffff talign=left"))
    # Volume / Pan
    for cx, key in zip(VOL_CX, ("volume", "pan")):
        o(L("knob", "cx=%d cy=%d r=%d" % (cx, ly(VOLBOX[1] + 62), VOL_R), 'label=""', "key=%s" % key,
            "strip=%s ns=0 vs=20 bw=120" % a["knob_dark"]))
    # Information
    ix, iy = INFOP[0], INFOP[1]
    for i, key in enumerate(("n_curves", "n_masters", "n_groups", "n_regions", "n_samples")):
        o(L("readout", "cx=%d cy=%d w=90 h=32" % (ix + 170 + 45, ly(iy + 28 + i * 36)), 'label=""', "key=%s" % key,
            "tsize=27 tfont=Roboto tcolor=ffffff talign=left", "when=browse:Off"))
    # Controls: the instrument's CC knobs (shown only for the CCs it uses)
    cx0 = CTRLP[0] + (CTRLP[2] - CC_COLS * CC_CELL_W) // 2
    for n in range(8):
        col, row = n % CC_COLS, n // CC_COLS
        cx = cx0 + col * CC_CELL_W + CC_CELL_W // 2
        top = CC_ROWS_Y[row]
        o(L("art", 'file="%s"' % a["cc_cell"], "x=%d y=%d w=%d h=140" % (cx - (CC_CELL_W - 6) // 2, ly(top), CC_CELL_W - 6),
            "when=ccshow_%d:On" % (n + 1)))
        o(L("knob", "cx=%d cy=%d r=%d" % (cx, ly(top + 70), CC_R), 'label=""', "key=cc_%d" % (n + 1),
            "strip=%s ns=17 vs=18 bw=%d" % (a["knob_light"], CC_CELL_W - 8), "when=ccshow_%d:On" % (n + 1)))
        o(L("readout", "cx=%d cy=%d w=%d h=26" % (cx, ly(top + 127), CC_CELL_W - 8), 'label=""', "key=ccl_%d" % (n + 1),
            "tsize=23 tfont=Roboto tcolor=ffffff", "when=ccshow_%d:On" % (n + 1)))
    # Settings
    pills = settings_pills()
    sy = SETP[1]

    def pill_popup(i, key):
        px, py, pw = pills[i]
        o(L("popup", "cx=%d cy=%d w=%d h=28" % (px + pw // 2, ly(py + 14), pw), 'label=""', "key=%s" % key,
            "field=none accent=ffffff"))

    pill_popup(0, "preload")
    pill_popup(1, "oversampling")
    o(L("toggle", "cx=%d cy=%d" % (SET_A + SET_W - 48, ly(sy + 130)), 'label=""', "key=sustain_cancels",
        "img=%s img_on=%s w=24 h=24 ns=0" % a["check"]))
    pill_popup(2, "sample_quality")
    pill_popup(3, "osc_quality")
    px, py, pw = pills[4]
    o(L("button", "cx=%d cy=%d" % (px + pw // 2, ly(py + 14)), 'label=""', "key=scala_next",
        "img=%s img_on=%s w=%d h=28" % (a["scala"][0], a["scala"][1], pw), "tsize=22 get=scala_name"))
    o(L("button", "cx=%d cy=%d" % (SET_B + SET_W - 15, ly(py + 14)), 'label=""', "key=scala_reset",
        "img=%s img_on=%s w=30 h=28" % a["reset"]))
    pill_popup(5, "root_note")
    pill_popup(6, "root_oct")
    px, py, pw = pills[7]
    o(L("knob", "cx=%d cy=%d r=13" % (px + pw // 2, ly(py + 14)), 'label=""', "key=tuning_freq",
        "strip=%s ns=0 vs=16 bw=%d" % (a["blank"], pw)))
    o(L("popup", "cx=%d cy=%d w=46 h=28" % (SET_B + SET_W - 8, ly(py + 14)), 'label=""', "key=tuning_preset",
        "field=none accent=none cols=1 cw=300"))
    o(L("knob", "cx=%d cy=%d r=22" % (SET_B + SET_W - 34, ly(sy + 212)), 'label=""', "key=stretch",
        "strip=%s ns=0 vs=15 bw=90" % a["knob_light_s"]))
    # SFZ browser (over Information + Controls while open)
    bx, by, bw, bh = BROWSER
    o(L("art", 'file="%s"' % a["browser"], "x=%d y=%d w=%d h=%d" % (bx, ly(by), bw, bh), "when=browse:On"))
    o(L("button", "cx=%d cy=%d" % (bx + 30, ly(by + 26)), 'label=""', "key=br_home",
        "img=%s img_on=%s w=36 h=36" % a["home"], "when=browse:On"))
    o(L("button", "cx=%d cy=%d" % (bx + 72, ly(by + 26)), 'label=""', "key=br_up",
        "img=%s img_on=%s w=36 h=36" % a["up"], "when=browse:On"))
    o(L("readout", "cx=%d cy=%d w=%d h=34" % (bx + 96 + 210, ly(by + 26), 420), 'label=""', "key=br_loc",
        "tsize=28 tfont=Roboto tcolor=ffffff talign=left", "when=browse:On"))
    o(L("readout", "cx=%d cy=%d w=120 h=30" % (bx + bw - 160, ly(by + 26)), 'label=""', "key=br_page",
        "tsize=22 tfont=Roboto tcolor=b2b2b2 talign=right", "when=browse:On"))
    o(L("button", "cx=%d cy=%d" % (bx + bw - 76, ly(by + 26)), 'label=""', "key=br_pgup",
        "img=%s img_on=%s w=34 h=34" % a["pgup"], "when=browse:On"))
    o(L("button", "cx=%d cy=%d" % (bx + bw - 38, ly(by + 26)), 'label=""', "key=br_pgdn",
        "img=%s img_on=%s w=34 h=34" % a["pgdn"], "when=browse:On"))
    o(L("list", "x=%d y=%d w=%d h=%d" % (bx + 14, ly(BR_LIST_Y), bw - 28, BR_ROWS * (BR_TH + BR_GAP) - BR_GAP),
        "cols=1 rows=%d th=%d gap=%d key=br" % (BR_ROWS, BR_TH, BR_GAP), "tsize=28 tfont=Roboto tcolor=ffffff talign=left",
        "color=2e3436", "when=browse:On"))
    o(L("readout", "cx=%d cy=%d w=%d h=28" % (bx + bw // 2, ly(by + bh - 18), bw - 40), 'label=""', "key=status",
        "tsize=22 tfont=Roboto tcolor=b2b2b2", "when=browse:On"))
    # the piano strip: a key shows pressed while its note sounds (parameters key_0..key_127, set by the engine)
    for k, (img, (x0, y0, x1, y1)) in sorted(a["keys"].items()):
        o(L("art", 'file="%s"' % img, "x=%d y=%d w=%d h=%d" % (x0, ly(y0), x1 - x0, y1 - y0), "when=key_%d:On" % k))
    o('qlinks "SFIZZ" = volume,pan,cc_1,cc_2,cc_3,cc_4,cc_5,cc_6,cc_7,cc_8,tuning_freq,stretch,sample_quality,osc_quality,-,-')
    open(os.path.join(HERE, "layout.conf"), "w").write("\n".join(out) + "\n")


def main():
    os.makedirs(ART, exist_ok=True)
    a = {}
    a["bg"] = page()
    a["ks_badge"] = ks_badge()
    a["ks_none"] = ks_none()
    a["cc_cell"] = cc_cell()
    a["browser"] = browser_panel()
    a["knob_dark"] = knob_strip("knob_dark.png", VOL_R, True)
    a["knob_light"] = knob_strip("knob_light.png", CC_R, False)
    a["knob_light_s"] = knob_strip("knob_light_s.png", 22, False)
    a["blank"] = blank_strip("knob_blank.png", 36, 36)
    a["prev"] = tuple(icon_button("prev", 32, 36, lambda c, x, y, col: chevron(c, x, y, 18, "left", col)))
    a["next"] = tuple(icon_button("next", 32, 36, lambda c, x, y, col: chevron(c, x, y, 18, "right", col)))
    a["chev"] = tuple(icon_button("chevron", 32, 36, lambda c, x, y, col: chevron(c, x, y, 18, "down", col)))
    a["home"] = tuple(icon_button("home", 36, 36, house))
    a["up"] = tuple(icon_button("up", 36, 36, folder_up))
    a["pgup"] = tuple(icon_button("pgup", 34, 34, lambda c, x, y, col: chevron(c, x, y, 16, "up", col)))
    a["pgdn"] = tuple(icon_button("pgdn", 34, 34, lambda c, x, y, col: chevron(c, x, y, 16, "down", col)))
    a["reset"] = tuple(icon_button("reset", 30, 28, cross, bg=DARK))
    a["check"] = tuple(checkbox())
    browser_tile()
    a["keys"] = pressed_keys()
    a["scala"] = tuple(pill_button("scala", settings_pills()[4][2], 28, DARK))
    write_layout(a)
    print("art/ and layout.conf written")


if __name__ == "__main__":
    main()
