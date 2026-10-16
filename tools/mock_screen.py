#!/usr/bin/env python3
"""Offline mock of the plugin screen with live text filled in: the built skin's background and control images, plus
sample values drawn where MPC draws its live labels (Roboto, the sizes and colours in TUI.json). For checking the design
against sfizz's own editor before anything goes to a device; MPC itself draws the real thing.

    tools/mock_screen.py "<plugin folder>" params.json out.png [key=value ...] [+<param>=<option index> ...]
    key=value is a live text; key_on=1 lights a list tile; key@=0..1 is a knob's position.
"""
import json
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "..", "fonts", "Roboto-Regular.ttf")


def rect(b):
    return [float(v) for v in b.split()]


def main():
    skin, params_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    values, show = {}, {}
    for a in sys.argv[4:]:
        k, _, v = a.partition("=")
        if k.startswith("+"):
            show[k[1:]] = int(v)
        else:
            values[k] = v
    P = json.load(open(params_path))["params"]
    keys = [p["key"] for p in P]
    pdir = os.path.join(skin, "Plugin Skins")
    tui = json.load(open(os.path.join(pdir, "TUI.json")))
    pd = tui["pageData"]
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
    page = defs[pd["tabs"][0]["componentName"]]
    im = Image.open(os.path.join(pdir, "sh_bg_0.png")).convert("RGBA")
    dr = ImageDraw.Draw(im)
    state = {keys.index(k): v for k, v in show.items()}

    def visible(c):
        for h in c["bounds"].get("additionalInvalidatingHandles", []):
            if h.startswith("IndexedEnabling/"):
                _, idx, n, par = h.split("/")
                p = int(par.split()[1])
                if p >= len(keys):
                    return False   # an open popup
                if state.get(p, 0) != int(idx):
                    return False
        return True

    for c in page["componentsData"]:
        if not visible(c):
            continue
        x, y, w, h = rect(c["bounds"]["bounds"])
        d = defs.get(c["componentData"]["type"])
        if c["componentData"]["type"] == "Image" or d is None:
            img = c["componentData"].get("data", {}).get("image")
            if img and os.path.exists(os.path.join(pdir, img)):
                part = Image.open(os.path.join(pdir, img)).convert("RGBA")
                im.alpha_composite(part, (int(x), int(y)))
            continue
        hm = {m["key"]: m["value"] for m in c.get("handle remapping", {}).get("map", [])}
        for sub in d["componentsData"]:
            sd = sub["componentData"]
            sx, sy, sw, sh = rect(sub["bounds"]["bounds"])
            handle = sd.get("data", {}).get("handleName", "Data")
            pv = hm.get(handle, hm.get("Data", ""))
            p = int(pv.split()[1]) if str(pv).startswith("Parameter ") else -1
            key = keys[p] if 0 <= p < len(keys) else None
            if sd["type"] == "Knob":
                strip = Image.open(os.path.join(pdir, sd["data"]["filmStrip"])).convert("RGBA")
                fw = strip.width
                n = strip.height // fw
                v = float(values.get(key + "@", 0.5)) if key else 0.5
                f = strip.crop((0, round(v * (n - 1)) * fw, fw, (round(v * (n - 1)) + 1) * fw))
                im.alpha_composite(f, (int(x + sx), int(y + sy)))
            elif sd["type"] == "Button" and sd["data"].get("offImage"):
                b = Image.open(os.path.join(pdir, sd["data"]["offImage"])).convert("RGBA")
                bid = sd["data"].get("buttonId", 0)
                if sd["data"].get("numButtonsInGroup", 1) > 1 and key and str(values.get(key, "")) == str(bid):
                    b = Image.open(os.path.join(pdir, sd["data"]["onImage"])).convert("RGBA")
                elif key and values.get(key + "_on") == "1":   # a list tile the engine lights (key_on=1)
                    b = Image.open(os.path.join(pdir, sd["data"]["onImage"])).convert("RGBA")
                im.alpha_composite(b, (int(x + sx), int(y + sy)))
            elif sd["type"] == "Label":
                kind = sd["data"].get("type")
                if not key:
                    continue
                text = values.get(key + ":name" if kind == "Name" else key)
                if text is None:
                    continue
                ts = sd["data"]["textStyle"]
                col = ts["colour"]
                if col.startswith("00"):
                    continue
                rgb = tuple(int(col[i:i + 2], 16) for i in (2, 4, 6))
                font = ImageFont.truetype(FONT, int(ts["font"]["height"] / 1.52))
                j = ts.get("justification", "")
                tw = dr.textlength(text, font=font)
                tx = x + sx + (sw - tw) / 2 if "horizontallyCentred" in j else x + sx + 4
                if "right" in j.lower():
                    tx = x + sx + sw - tw - 4
                dr.text((tx, y + sy + sh / 2), text, font=font, fill=rgb, anchor="lm")
    im.convert("RGB").save(out)


if __name__ == "__main__":
    main()
