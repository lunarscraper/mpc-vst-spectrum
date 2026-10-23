#!/usr/bin/env python3
"""skin_post.py <skin folder>: adds Spectrum's 31 bars to the SPECTRUM page (gen_vst.py runs it after the skin is built).

The layout's own `meter` widget needs the browser renderer ("art": "html", Playwright), which this port's build does
not use; so this script does what tools/shadow_skin.py does for a filmstrip `meter`, by hand: one display-only Knob
component with knobType FilmStrip (frames of the bar's own w x h, numFrames = the frame count, as stock display strips
and mpc-vst-plugins' meters), placed once per bar and bound to parameter b01..b31 ("Parameter <VST index>").
MPC moves such a filmstrip when the plugin reports the value with audioMasterAutomate (verified for meters on an
MPC One, mpc-vst-plugins docs/SKIN_STUDIO.md). All 31 bars share one strip image.
It also draws the scale into the page background with Pillow: faint level lines and the frequency labels.
Coordinates here are page pixels (0..1280 x 0..628); layout.conf's are 86 px lower (Force Shadow's y)."""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
NBARS, LEVELS = 31, 20
X0, PITCH, BW = 81, 29, 21            # first bar's centre, distance between bars, bar width
TOP, BH = 52, 440                     # bars' top edge and height (page pixels)
SEG_GAP = 4
LABELS = {2: "31", 5: "63", 8: "125", 11: "250", 14: "500", 17: "1K", 20: "2K", 23: "4K", 26: "8K", 29: "16K"}
LIT = [(46, 196, 182), (46, 196, 182), (127, 231, 220), (240, 200, 80), (240, 110, 60)]   # bottom .. top colour bands
OFF = (22, 40, 40)
STRIP = "spec_bar_%dx%d_f%d.png" % (BW, BH, LEVELS + 1)


def seg_colour(i):
    """segment i (0 = bottom) of LEVELS: teal for most, light teal, then amber and red near the top"""
    f = i / (LEVELS - 1)
    if f < 0.55:
        return LIT[0]
    if f < 0.75:
        return LIT[2]
    if f < 0.9:
        return LIT[3]
    return LIT[4]


def make_strip(path):
    seg_h = (BH - (LEVELS - 1) * SEG_GAP) / LEVELS
    img = Image.new("RGBA", (BW, BH * (LEVELS + 1)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for frame in range(LEVELS + 1):          # frame k: k segments lit from the bottom
        y0 = frame * BH
        for i in range(LEVELS):
            yb = y0 + BH - i * (seg_h + SEG_GAP)
            d.rectangle([0, round(yb - seg_h), BW - 1, round(yb) - 1], fill=seg_colour(i) if i < frame else OFF)
    img.save(path)


def draw_scale(bg_path):
    img = Image.open(bg_path).convert("RGB")
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(os.path.join(HERE, "fonts", "TitilliumWeb-SemiBold.ttf"), 18)
    small = ImageFont.truetype(os.path.join(HERE, "fonts", "TitilliumWeb-SemiBold.ttf"), 15)
    x_left, x_right = X0 - BW // 2 - 6, X0 + (NBARS - 1) * PITCH + BW // 2 + 6
    for k in range(6):                       # level lines: top, every fifth of the range, bottom
        y = TOP + round(k * BH / 5)
        d.line([(x_left, y), (x_right, y)], fill=(30, 44, 46), width=1)
    for b, lab in LABELS.items():
        cx = X0 + b * PITCH
        w = d.textlength(lab, font=font)
        d.text((cx - w / 2, TOP + BH + 12), lab, font=font, fill=(154, 168, 171))
    d.text((x_left, TOP + BH + 44), "Hz", font=small, fill=(90, 104, 106))
    img.save(bg_path)


def param_indices():
    plist = json.load(open(os.path.join(HERE, "params.json")))["params"]
    return {p["key"]: i for i, p in enumerate(plist)}


def bounds(x, y, w, h, focus="No", show="Show"):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": "Always",
            "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h), "additionalInvalidatingHandles": []}


def main():
    skin = sys.argv[1]
    pdir = os.path.join(skin, "Plugin Skins")
    tui_path = os.path.join(pdir, "TUI.json")
    tui = json.load(open(tui_path))
    page = tui["pageData"]
    tab = next(t for t in page["tabs"] if t["tabName"] == "SPECTRUM")
    defs = page["componentDefinitions"]["localComponentDefinitions"]
    tabdef = next(d for d in defs if d["key"] == tab["componentName"])
    kids = tabdef["value"]["componentsData"]
    bg = next(k for k in kids if k["componentData"]["type"] == "Image")["componentData"]["data"]["image"]

    make_strip(os.path.join(pdir, STRIP))
    draw_scale(os.path.join(pdir, bg))

    key = "specBar_%dx%d_f%d" % (BW, BH, LEVELS + 1)
    clear = {"version": 1, "colour": "0", "image": ""}
    knob = {"version": 2, "componentData": {"version": 1, "name": "Meter", "type": "Knob", "data": {
        "version": 5, "knobType": "FilmStrip", "filmStrip": STRIP, "numFrames": LEVELS + 1, "invert": False,
        "dragOrientation": "Vertical", "handleName": "Data"}},
        "handle remapping": {"version": 1, "map": []}, "bounds": bounds(0, 0, BW, BH, show="Show")}
    defs.append({"key": key, "value": {"version": 4, "actions": [],
                                       "backgroundData": {"version": 1, "focussed": clear, "unfocussed": clear},
                                       "ignoreMousePresses": True, "disableCoarseDataWheel": False, "repeats": 1,
                                       "hideQLinkBounds": True, "componentsData": [knob]}})
    idx = param_indices()
    for b in range(NBARS):
        name = "b%02d" % (b + 1)
        x = X0 + b * PITCH - BW // 2
        kids.append({"version": 2,
                     "componentData": {"version": 1, "name": "Band %02d" % (b + 1), "type": key,
                                       "data": {"version": 1, "handleName": "Data"}},
                     "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % idx[name]}]},
                     "bounds": bounds(x, TOP, BW, BH, focus="No", show="Hide")})
    json.dump(tui, open(tui_path, "w"), indent=1)
    print("skin_post: %d bars (%s, %d frames) on page SPECTRUM" % (NBARS, STRIP, LEVELS + 1))


if __name__ == "__main__":
    main()
