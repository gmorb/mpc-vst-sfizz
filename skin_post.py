#!/usr/bin/env python3
"""sfizz for MPC OS (sizes are MPC font heights, ~1.52 x CSS px): TUI.json touch-ups the layout can't express (vst.json "skin_post", run on the built skin folder).

- Every live text is Roboto Regular, sfizz's own face (MPC resolves Roboto at any size).
- Knobs show their value inside the knob, as sfizz's do, not below it.
- Controls-panel knobs (the ones with a name) put the name in the grey pill above the knob: the component grows upward to
  cover the pill, so the name is still MPC's own Name label (the instrument's label_cc, via the dynamic name).
- The frequency "pill" knob is an invisible knob sized to its pill, its value centred in it.
- Popup fields (sfizz's dark value buttons) centre their value in the whole pill.
- Popup option lists are re-laid and re-drawn larger (taller rows, bigger text) than the kit draws them.
Fails loudly if what it changes is missing, so a generator change can't silently undo the look.
"""
import json
import os
import sys

CC_NAME_PILL = 26        # design.py: the name pill's height
CC_PILL_TO_KNOB = 39     # design.py: cell top -> knob top (top + 70 - knob size / 2)


def sub(defn, name):
    for c in defn["componentsData"]:
        if c["componentData"]["name"] == name:
            return c
    return None


def rect(c):
    return [int(float(v)) for v in c["bounds"]["bounds"].split()]


def set_rect(c, x, y, w, h):
    c["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, h)


def text_style(c, size=None, colour=None, just=None):
    ts = c["componentData"]["data"]["textStyle"]
    ts["font"]["name"] = "Roboto"
    ts["font"]["style"] = "Regular"
    if size:
        ts["font"]["height"] = float(size)
    if colour:
        ts["colour"] = "ff" + colour
    if just:
        ts["justification"] = just
    ts["case"] = "Original"


def main(skin_dir):
    path = os.path.join(skin_dir, "Plugin Skins", "TUI.json")
    tui = json.load(open(path))
    pd = tui["pageData"]
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
    pages = [defs[t["componentName"]] for t in pd["tabs"]]

    # 1. every live text in Roboto Regular
    def fonts(o):
        if isinstance(o, dict):
            ts = o.get("textStyle")
            if isinstance(ts, dict) and "font" in ts:
                ts["font"]["name"] = "Roboto"
                ts["font"]["style"] = "Regular"
            for v in o.values():
                fonts(v)
        elif isinstance(o, list):
            for v in o:
                fonts(v)
    fonts(pd)

    # 2. knobs
    grown = {}   # definition key -> how far it grew upward
    knobs = [k for k in defs if k.startswith("shKnob") and not k.startswith("shKnobSide")]
    if len(knobs) < 4:
        sys.exit("skin_post: expected the sfizz knobs, found %s" % knobs)
    for key in knobs:
        d = defs[key]
        knob, value, name, focus = sub(d, "Knob"), sub(d, "Value"), sub(d, "Name"), sub(d, "Focus")
        kx, ky, ks, _ = rect(knob)
        cw = rect(focus)[2]
        strip = knob["componentData"]["data"]["filmStrip"]
        blank = ks <= 40   # the frequency pill: an invisible r=13 knob
        if blank:
            pill_h = 28
            set_rect(value, 0, (ks - pill_h) // 2, cw, pill_h)
            text_style(value, 24, "ffffff", "horizontallyCentred verticallyCentred")
            set_rect(focus, 0, 0, cw, ks)
            grown[key] = ("blank", ks)
            continue
        size = 25   # MPC font height (~1.52 x CSS px): ~16 px, as sfizz's knob values
        if ks < 60:   # the Stretch knob: sfizz draws no value on it
            set_rect(value, kx, ky, ks, ks)
            value["componentData"]["data"]["textStyle"]["colour"] = "00000000"
            set_rect(focus, 0, 0, cw, ks)
            grown[key] = ("same", ks)
            continue
        if name is not None:   # a Controls-panel knob: name in the pill above
            dy = CC_PILL_TO_KNOB
            set_rect(knob, kx, dy, ks, ks)
            set_rect(value, kx, dy, ks, ks)
            set_rect(name, 0, 0, cw, CC_NAME_PILL)
            text_style(name, 23, "ffffff", "horizontallyCentred verticallyCentred")
            set_rect(focus, 0, 0, cw, dy + ks)
            grown[key] = ("up", dy, dy + ks)
        else:
            set_rect(value, kx, ky, ks, ks)
            set_rect(focus, 0, 0, cw, ks)
            grown[key] = ("same", ks)
        text_style(value, size, "ffffff", "horizontallyCentred verticallyCentred")

    # placed knobs: their boxes follow the definitions
    placed = 0
    for page in pages:
        for c in page["componentsData"]:
            g = grown.get(c["componentData"]["type"])
            if not g:
                continue
            x, y, w, h = rect(c)
            if g[0] == "up":
                set_rect(c, x, y - g[1], w, g[2])
            else:
                set_rect(c, x, y, w, g[1])
            placed += 1
    if placed < 12:
        sys.exit("skin_post: expected at least 12 placed knobs, found %d" % placed)

    # 3. popup fields: the value centred in the whole pill
    fields = [k for k in defs if k.startswith("shPopField")]
    if not fields:
        sys.exit("skin_post: no popup fields found")
    for key in fields:
        d = defs[key]
        v = sub(d, "Value")
        w, h = rect(sub(d, "Focus"))[2:]
        set_rect(v, 0, 0, w, h)
        text_style(v, 25, None, "horizontallyCentred verticallyCentred")

    # 4. popup option lists, larger than the kit draws them: taller rows, wider cells, bigger text
    lists = enlarge_popups(skin_dir, defs, pages)

    json.dump(tui, open(path + ".new", "w"), indent=1)
    os.replace(path + ".new", path)
    prune_images(skin_dir, path)
    print("skin_post: %d knob kinds, %d placed knobs, %d popup fields restyled, %d popup lists enlarged"
          % (len(knobs), placed, len(fields), lists))


# ---- popup lists ----
# The kit draws each option as a 40 px row with ~13 px text, as wide as its field. On the MPC's screen at arm's length
# that is small to read and to hit, so the lists are re-laid and re-drawn here: POP_ROW px rows, POP_TEXT px Roboto, cells
# at least POP_MIN_W wide (or the longest option plus padding). Placement follows the kit's rule: below the field if the
# list fits there, else above, else from the top of the plugin area; columns only when one column doesn't fit.
POP_ROW, POP_GAP, POP_PAD, POP_TEXT, POP_MIN_W, POP_TEXT_PAD = 46, 3, 8, 20, 120, 36
SCREEN_W, SCREEN_H, LAYOUT_Y_OFF = 1280, 628, 86   # the plugin area, and layout.conf's y offset (kit shadow coordinates)
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "Roboto-Regular.ttf")
OPTIONS = {p["key"]: p["options"] for p in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "params.json")))["params"]
           if "options" in p}


def layout_popups():
    """{key: (field x, y, w, h) in TUI coordinates} from layout.conf's popup lines."""
    here = os.path.dirname(os.path.abspath(__file__))
    out = {}
    for line in open(os.path.join(here, "layout.conf")):
        if not line.startswith("popup "):
            continue
        kv = dict(t.split("=", 1) for t in line.split() if "=" in t)
        cx, cy, w, h = (int(kv[k]) for k in ("cx", "cy", "w", "h"))
        out[kv["key"]] = (cx - w // 2, cy - h // 2 - LAYOUT_Y_OFF, w, h)
    return out


def popup_geometry(n, cell_w, field):
    """(panel x, y, w, h), [option rects], rows: the kit's placement rule with this file's sizes."""
    fx, fy, fw, fh = field
    below, above = SCREEN_H - (fy + fh + 4), fy - 4
    for cols in range(1, n + 1):
        rows = -(-n // cols)
        ph = rows * (POP_ROW + POP_GAP) - POP_GAP + 2 * POP_PAD
        if ph <= max(below, above):
            break
    pw = cols * cell_w + (cols - 1) * POP_GAP + 2 * POP_PAD
    py = fy + fh + 4 if ph <= below else (fy - 4 - ph if ph <= above else max(0, (SCREEN_H - ph) // 2))
    px = max(0, min(fx, SCREEN_W - pw))
    opts = [(px + POP_PAD + (o // rows) * (cell_w + POP_GAP), py + POP_PAD + (o % rows) * (POP_ROW + POP_GAP), cell_w, POP_ROW)
            for o in range(n)]
    return (px, py, pw, ph), opts


def enlarge_popups(skin_dir, defs, pages):
    from PIL import Image, ImageDraw, ImageFont
    pdir = os.path.join(skin_dir, "Plugin Skins")
    font = ImageFont.truetype(FONT, POP_TEXT)
    fields = layout_popups()
    placed = {}
    for page in pages:
        for c in page["componentsData"]:
            placed.setdefault(c["componentData"]["type"], []).append(c)
    done = 0
    for key, field in sorted(fields.items()):
        pkey = next((k for k in defs if k.startswith("shPopPanel_") and k.endswith("_" + key)), None)
        if not pkey:
            sys.exit("skin_post: no option list for popup %s" % key)
        prefix = "shPopOpt_" + pkey[len("shPopPanel_"):] + "_"
        okeys = sorted((k for k in defs if k.startswith(prefix) and k[len(prefix):].isdigit()), key=lambda k: int(k[len(prefix):]))
        labels = OPTIONS[key]
        if len(labels) != len(okeys):
            sys.exit("skin_post: popup %s has %d options in params.json, %d in the skin" % (key, len(labels), len(okeys)))
        # the kit's own pictures give the colours: panel fill and border, option fill and ink off and on
        old_panel = sub(defs[pkey], "Image")["componentData"]["data"]["image"]
        pim = Image.open(os.path.join(pdir, old_panel)).convert("RGB")
        panel_fill, panel_edge = pim.getpixel((pim.width // 2, 6)), pim.getpixel((0, pim.height // 2))
        b0 = sub(defs[okeys[0]], "Button")["componentData"]["data"]
        off0 = Image.open(os.path.join(pdir, b0["offImage"])).convert("RGB")
        on0 = Image.open(os.path.join(pdir, b0["onImage"])).convert("RGB")
        off_fill, on_fill = off0.getpixel((3, off0.height // 2)), on0.getpixel((3, on0.height // 2))
        off_fill = tuple(round(v + (255 - v) * 0.08) for v in off_fill)   # a shade lighter than the panel: rows read apart
        off_ink, on_ink = (240, 242, 240), (24, 26, 28)
        cell_w = max(field[2], POP_MIN_W, max(int(font.getlength(t)) for t in labels) + POP_TEXT_PAD)
        (px, py, pw, ph), orects = popup_geometry(len(okeys), cell_w, field)
        # the panel
        img = Image.new("RGB", (pw, ph), panel_edge)
        ImageDraw.Draw(img).rectangle([2, 2, pw - 3, ph - 3], fill=panel_fill)
        pname = "sh_poplist_%s.png" % key
        img.save(os.path.join(pdir, pname))
        im = sub(defs[pkey], "Image")
        im["componentData"]["data"]["image"] = pname
        set_rect(im, 0, 0, pw, ph)
        for c in placed[pkey]:
            set_rect(c, px, py, pw, ph)
        # the options
        for o, (okey, label, (ox, oy, ow, oh)) in enumerate(zip(okeys, labels, orects)):
            files = {}
            for state, fill, ink in (("on", on_fill, on_ink), ("off", off_fill, off_ink)):
                img = Image.new("RGB", (ow, oh), panel_fill)
                d = ImageDraw.Draw(img)
                d.rounded_rectangle([0, 0, ow - 1, oh - 1], radius=5, fill=fill)
                tw = font.getlength(label)
                l, t, r, b = font.getbbox("Hg")
                d.text(((ow - tw) / 2, (oh - (b - t)) / 2 - t), label, font=font, fill=ink)
                files[state] = "sh_popopt_%s_%d_%s.png" % (key, o, state)
                img.save(os.path.join(pdir, files[state]))
            btn = sub(defs[okey], "Button")
            btn["componentData"]["data"]["onImage"], btn["componentData"]["data"]["offImage"] = files["on"], files["off"]
            set_rect(btn, 0, 0, ow, oh)
            for c in placed[okey]:
                set_rect(c, ox, oy, ow, oh)
        done += 1
    if done != len(fields):
        sys.exit("skin_post: enlarged %d of %d popup lists" % (done, len(fields)))
    return done


def prune_images(skin_dir, tui_path):
    """Remove the kit's popup pictures that nothing references any more."""
    text = open(tui_path).read()
    pdir = os.path.join(skin_dir, "Plugin Skins")
    for f in os.listdir(pdir):
        if (f.startswith("sh_po_") or f.startswith("sh_pop_")) and f.endswith(".png") and f not in text:
            os.remove(os.path.join(pdir, f))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: skin_post.py <skin folder>")
    main(sys.argv[1])
