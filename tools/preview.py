#!/usr/bin/env python3
"""Pictures of VADER's pages as MPC would draw them, with the engine really playing (test/skin_dump.c): the screen's
tiles, the envelope line, the corner bars and every value come from the engine, not from guesses.

    python3 tools/preview.py "<skin>/Plugin Skins" <out dir> [preset] [shape] [file]

MPC draws its live text in Titillium Web; this preview uses Chakra Petch (bundled) for it, so the values' letter shapes
differ a little. Popup lists are drawn closed."""
import json
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "art", "fonts", "ChakraPetch-SemiBold.ttf")


def dump(args):
    out = subprocess.run([os.path.join(ROOT, "build", "host", "skin_dump")] + args, cwd=ROOT, capture_output=True,
                         text=True, check=True).stdout
    vals = {}
    for line in out.splitlines():
        i, n, d = line.split("\t", 2)
        vals[int(i)] = (float(n), d)
    return vals


def main(skin, out_dir, args):
    vals = dump(args)
    tui = json.load(open(os.path.join(skin, "TUI.json")))
    pd = tui["pageData"]
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"]["localComponentDefinitions"]}
    cache = {}

    def img(name):
        if name not in cache:
            cache[name] = Image.open(os.path.join(skin, name)).convert("RGBA")
        return cache[name]

    def visible(c):
        for h in c["bounds"].get("additionalInvalidatingHandles", []):
            if h.startswith("IndexedEnabling/"):
                _, i, n, p = h.split("/")
                v = vals.get(int(p.split()[1]), (0, ""))[0]
                if round(v * (int(n) - 1)) != int(i):
                    return False
        return True

    os.makedirs(out_dir, exist_ok=True)
    seen = set()
    written = []
    for t in pd["tabs"]:
        name = t["componentName"].split("|")[0]
        if name in seen:
            continue
        seen.add(name)
        page = Image.new("RGBA", (1280, 628), (6, 10, 18, 255))
        for c in defs[t["componentName"]]["componentsData"]:
            if not visible(c):
                continue
            x, y, w, h = (int(float(v)) for v in c["bounds"]["bounds"].split())
            cd = c["componentData"]
            hmap = {m["key"]: int(m["value"].split()[1]) for m in c.get("handle remapping", {}).get("map", [])
                    if str(m.get("value", "")).startswith("Parameter")}
            if cd["type"] == "Image":
                page.alpha_composite(img(cd["data"]["image"]).resize((w, h)), (x, y))
                continue
            d = defs.get(cd["type"])
            if not d:
                continue
            for part in d["componentsData"]:
                pdx = part["componentData"]
                px, py, pw, ph = (int(float(v)) for v in part["bounds"]["bounds"].split())
                data = pdx["data"]
                handle = data.get("handleName", "Data")
                p = hmap.get(handle, hmap.get("Data"))
                v, text = vals.get(p, (0.0, "")) if p is not None else (0.0, "")
                if pdx["type"] == "Image":
                    page.alpha_composite(img(data["image"]).resize((pw, ph)), (x + px, y + py))
                elif pdx["type"] == "Knob":
                    strip = img(data["filmStrip"])
                    n = data["numFrames"]
                    fh = strip.height // n
                    k = max(0, min(n - 1, round(v * (n - 1))))
                    fr = strip.crop((0, k * fh, strip.width, (k + 1) * fh)).resize((pw, ph))
                    page.alpha_composite(fr, (x + px, y + py))
                elif pdx["type"] == "Button":
                    n = data["numButtonsInGroup"]
                    on = round(v * (n - 1)) == data["buttonId"] if n > 1 else v > 0.5
                    page.alpha_composite(img(data["onImage" if on else "offImage"]).resize((pw, ph)), (x + px, y + py))
                elif pdx["type"] == "Label":
                    st = data["textStyle"]
                    f = ImageFont.truetype(FONT, int(st["font"]["height"] * 0.9))
                    col = "#" + st["colour"][2:]
                    just = st["justification"]
                    dd = ImageDraw.Draw(page)
                    ty = y + py + ph / 2
                    if just.startswith("left"):
                        dd.text((x + px, ty), text, font=f, fill=col, anchor="lm")
                    elif just.startswith("right"):
                        dd.text((x + px + pw, ty), text, font=f, fill=col, anchor="rm")
                    else:
                        dd.text((x + px + pw / 2, ty), text, font=f, fill=col, anchor="mm")
        path = os.path.join(out_dir, "%s.png" % name.lower())
        page.convert("RGB").save(path)
        written.append(path)
    print("preview: " + ", ".join(written))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
