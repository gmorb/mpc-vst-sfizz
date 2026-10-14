#!/usr/bin/env python3
"""sfizz for MPC OS (sizes are MPC font heights, ~1.52 x CSS px): TUI.json touch-ups the layout can't express (vst.json "skin_post", run on the built skin folder).

- Every live text is Roboto Regular, sfizz's own face (MPC resolves Roboto at any size).
- Knobs show their value inside the knob, as sfizz's do, not below it.
- Controls-panel knobs (the ones with a name) put the name in the grey pill above the knob: the component grows upward to
  cover the pill, so the name is still MPC's own Name label (the instrument's label_cc, via the dynamic name).
- The frequency "pill" knob is an invisible knob sized to its pill, its value centred in it.
- Popup fields (sfizz's dark value buttons) centre their value in the whole pill.
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

    json.dump(tui, open(path + ".new", "w"), indent=1)
    os.replace(path + ".new", path)
    print("skin_post: %d knob kinds, %d placed knobs, %d popup fields restyled" % (len(knobs), placed, len(fields)))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: skin_post.py <skin folder>")
    main(sys.argv[1])
