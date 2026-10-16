#!/usr/bin/env python3
"""VADER's five pages: where every control sits on MPC's 1280 x 628 plugin screen, and the builder that draws them.

    python3 tools/vader_skin.py --layout              writes layout.conf (the tabs and their Q-Links, for gen_vst.py)
    python3 tools/vader_skin.py "<skin>/Plugin Skins"  builds every page into the skin gen_vst.py made

The look (from the mockup): no panels. Every knob is a LINE: a thin track with a thicker glowing blue line on top that
grows with the value. A stripe across the top of every page shows the control last touched (its name and value, and the
same line). Section titles and control names are baked in Chakra Petch (SIL OFL); values are MPC's own text.
Background: art/background.png (any size: it is cropped to fill the page) when it exists, else a plain dark ground.

The MAIN page carries the screen: TILE_COLS x TILE_ROWS line tiles (tools/gen_params.py) that the engine switches
(src/screen.c), the envelope drawn as ENV_COLS columns, and four corner bars.
Every control's TUI.json definition is written here; gen_vst.py's generated ones are removed."""
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_params as G  # noqa: E402

W, H = 1280, 628
FONT_B = os.path.join(ROOT, "art", "fonts", "ChakraPetch-Bold.ttf")
FONT_S = os.path.join(ROOT, "art", "fonts", "ChakraPetch-SemiBold.ttf")
BG_IMAGE = os.path.join(ROOT, "art", "background.png")

ACCENT = (58, 160, 255)
TRACK = (34, 68, 108)
INK = (207, 227, 255)
INK_DIM = (111, 143, 184)
WHITE = (255, 255, 255)
AMBER = (255, 176, 32)
CYAN = (125, 252, 255)
GROUND = (0, 0, 0)
BTN_ON = (15, 42, 74)
BTN_OFF = (11, 20, 36)

TABS = ["MAIN", "CORNERS", "ORBIT", "ARP", "FX"]
HL = 50          # a line control's height: name row, then the line
LINE_H = 16      # its line's strip frame height
LINE_Y = 30      # where the line sits inside the control
TILE_W, TILE_H = 20, 18
SCREEN = (312, 114, 656, 296)               # the screen window on MAIN
TILES_XY = (320, 118)                        # its top-left tile
ENV_BOX = (16, 94, 280, 64)
ENV_XY = (28, 98)                            # the envelope columns' top-left
ENV_CW, ENV_RH = 8, 4                        # column width, row height


# ------------------------------------------------------------------------------------------------ the pages
def hl(key, x, y, w, label=None, when=None):
    return {"kind": "hline", "key": key, "x": x, "y": y, "w": w, "label": label, "when": when}


def tog(key, x, y, w, label=None, h=40, when=None):
    return {"kind": "toggle", "key": key, "x": x, "y": y, "w": w, "h": h, "label": label, "when": when}


def btn(key, x, y, w, label, h=40):
    return {"kind": "button", "key": key, "x": x, "y": y, "w": w, "h": h, "label": label}


def radio(key, x, y, w, labels=None, h=40, cols=None, vertical=False, order=None):
    return {"kind": "radio", "key": key, "x": x, "y": y, "w": w, "h": h, "labels": labels, "cols": cols,
            "vertical": vertical, "order": order}


def popup(key, x, y, w, caption, h=40, list_at=None, cols=2, ow=150, oh=40):
    return {"kind": "popup", "key": key, "x": x, "y": y, "w": w, "h": h, "caption": caption, "list_at": list_at,
            "cols": cols, "ow": ow, "oh": oh}


def field(key, x, y, w, caption, h=44):
    return {"kind": "field", "key": key, "x": x, "y": y, "w": w, "h": h, "caption": caption}


def fader(key, x, y, label):
    return {"kind": "fader", "key": key, "x": x, "y": y, "w": 72, "h": 186, "label": label}


def title(text, x, y, w):
    return {"kind": "title", "text": text, "x": x, "y": y, "w": w}


def text(t, x, y, size=17, colour=INK_DIM, w=None):
    return {"kind": "text", "text": t, "x": x, "y": y, "size": size, "colour": colour, "w": w}


def mixer(x, y):
    return [title("MIXER", x, y, 288)] + [fader(k, x + i * 72, y + 22, l) for i, (k, l) in
                                          enumerate([("mix_rev", "REVERB"), ("mix_ch", "CHORUS"), ("mix_dly", "DELAY"),
                                                     ("volume", "VOLUME")])]


def page_main():
    L, C, R = 16, 312, 980
    c = []
    c += [title("ENVELOPE", L, 70, 280), {"kind": "envwin"}]
    c += [hl("attack", L, 166, 132), hl("decay", L + 148, 166, 132), hl("sustain", L, 220, 132), hl("release", L + 148, 220, 132)]
    c += [title("MOD", L, 278, 280)]
    c += [hl("orb_speed", L, 300, 132, "ORBIT SPEED", when="alt:OFF"), hl("orb_size", L + 148, 300, 132, "ORBIT SIZE", when="alt:OFF"),
          hl("sub_size", L, 354, 132, "SUBORBIT SIZE", when="alt:OFF"), hl("sub_speed", L + 148, 354, 132, "SUBORB SPEED", when="alt:OFF"),
          hl("rotate", L, 300, 132, "ROTATE", when="alt:ON"), hl("orb_env", L + 148, 300, 132, "ORBIT ENV", when="alt:ON"),
          hl("detune", L, 354, 132, "DETUNE", when="alt:ON"), hl("glide", L + 148, 354, 132, "GLIDE", when="alt:ON")]
    c += [title("VIBRATO / TREMOLO", L, 412, 280)]
    c += [hl("vib_speed", L, 434, 132, "VIB SPEED"), hl("vib_depth", L + 148, 434, 132, "VIB DEPTH"),
          hl("trem_speed", L, 488, 132, "TREM SPEED"), hl("trem_depth", L + 148, 488, 132, "TREM DEPTH")]
    c += [title("CORNER SELECT", L, 546, 280), radio("corner", L, 568, 280, h=42)]
    # centre: the screen
    c += [popup("shape", C, 68, 300, "SHAPE", list_at=(C + 8, 118), cols=4, ow=158, oh=44),
          {"kind": "screen"}, {"kind": "tiles"}, {"kind": "bars", "x": C, "y": 416}]
    keys1 = [("f_og1", "OVERTONE GEN 1", "drive", "DRIVE AMOUNT"), ("f_og2", "OVERTONE GEN 2", "drive_col", "DRIVE COLOUR"),
             ("f_blend", "OVERTONE BLEND", "rev_size", "REVERB SIZE"), ("f_cutoff", "FILTER CUTOFF", "rev_col", "REVERB COLOUR"),
             ("f_reso", "FILTER RESO", "mres", "MASTER RESO")]
    for i, (k, l, k2, l2) in enumerate(keys1):
        c += [hl(k, C + i * 133, 446, 124, l, when="alt:OFF"), hl(k2, C + i * 133, 446, 124, l2, when="alt:ON")]
    keys2 = [("dly_time", "DELAY TIME"), ("dly_fb", "DELAY FEEDBK"), ("ch_speed", "CHORUS SPEED"), ("ch_depth", "CHORUS DEPTH"),
             ("ch_col", "CHORUS COLOUR")]
    for i, (k, l) in enumerate(keys2):
        c += [hl(k, C + i * 133, 504, 124, l)]
    c += [tog("alt", C, 566, 190, "ALT"), tog("motion", C + 206, 566, 190, "MOTION"),
          text("ALT: the second function of each knob", C + 412, 576, 16, w=244)]
    # right: mixer, arpeggiator, sync
    c += mixer(R, 70)
    c += [title("ARPEGGIATOR", R, 286, 288), tog("arp_on", R, 308, 140, "ACTIVE"), tog("latch", R + 148, 308, 140, "LATCH"),
          popup("arp_mode", R, 356, 140, "DIR", list_at=(R - 330, 300)), popup("arp_rate", R + 148, 356, 140, "RATE", list_at=(R - 330, 300)),
          hl("note_len", R, 404, 288, "NOTE LENGTH")]
    c += [title("SYNC", R, 462, 288),
          tog("retrig", R, 484, 92, "RETRIG"), tog("orb_sync", R + 98, 484, 92, "ORBITER"), tog("vt_sync", R + 196, 484, 92, "VIB/TREM"),
          btn("tap", R, 532, 92, "TAP"), tog("dly_sync", R + 98, 532, 92, "DELAY"), tog("ch_sync", R + 196, 532, 92, "CHORUS")]
    q = [("MAIN 1", ["f_og1", "f_og2", "f_blend", "f_cutoff", "f_reso", "orb_speed", "orb_size", "sub_size",
                     "attack", "decay", "sustain", "release", "sub_speed", "mix_rev", "mix_dly", "volume"]),
         ("MAIN 2", ["dly_time", "dly_fb", "ch_speed", "ch_depth", "ch_col", "vib_speed", "vib_depth", "trem_speed",
                     "trem_depth", "note_len", "mix_ch", "drive", "drive_col", "rev_size", "rev_col", "mres"])]
    return c, q


def page_corners():
    c, q = [], []
    for ci, cl in enumerate("abcd"):
        x = 16 + ci * 316
        c += [title("CORNER " + cl.upper(), x, 70, 300)]
        c += [hl(cl + "_og1", x, 96, 142, "GEN 1"), hl(cl + "_og2", x + 158, 96, 142, "GEN 2"),
              hl(cl + "_oct", x, 152, 142, "OCTAVE"), hl(cl + "_tune", x + 158, 152, 142, "GEN 2 TUNE"),
              hl(cl + "_fine", x, 208, 142, "GEN 2 FINE"), hl(cl + "_level", x + 158, 208, 142, "LEVEL")]
        c += [text("BLEND", x, 266, 17), popup(cl + "_mode", x, 288, 142, "", list_at=(x if ci < 3 else x - 10, 334)),
              hl(cl + "_blend", x + 158, 280, 142, "BLEND")]
        c += [text("FILTER", x, 340, 17), popup(cl + "_ftype", x, 362, 142, "", list_at=(x if ci < 3 else x - 10, 408)),
              hl(cl + "_reso", x + 158, 354, 142, "RESONANCE")]
        c += [hl(cl + "_cutoff", x, 414, 300, "CUTOFF")]
        c += [text("FILTER ON", x, 474, 17), radio(cl + "_fpos", x, 496, 300, h=42)]
        q.append(("CORNER " + cl.upper(), [cl + f for f in ("_og1", "_og2", "_blend", "_cutoff", "_reso", "_level", "_tune", "_fine",
                                                             "_oct", "_mode", "_ftype", "_fpos")]))
    return c, q


def page_orbit():
    A, B, Cc = 16, 440, 864
    c = [title("ORBITER", A, 70, 400), popup("shape", A, 92, 400, "SHAPE", h=44, list_at=(A, 140), cols=4, ow=158, oh=44)]
    c += [hl("orb_speed", A, 146, 192, "ORBIT SPEED"), hl("orb_size", A + 208, 146, 192, "ORBIT SIZE"),
          hl("sub_size", A, 200, 192, "SUBORBIT SIZE"), hl("sub_speed", A + 208, 200, 192, "SUBORBIT SPEED"),
          hl("rotate", A, 254, 192, "ROTATE"), hl("orb_env", A + 208, 254, 192, "ORBIT ENV"),
          hl("spin", A, 308, 192, "3D SPIN"), hl("tilt", A + 208, 308, 192, "3D TILT")]
    c += [tog("retrig", A, 368, 192, "RETRIG"), tog("orb_sync", A + 208, 368, 192, "ORBITER SYNC"),
          text("SYNC TO", A, 420, 17), popup("orb_div", A + 100, 414, 300, "", list_at=(A + 100, 460), cols=2, ow=150),
          hl("shape_osc", A, 470, 400, "SHAPE SOUND"),
          text("SHAPE SOUND: the shape itself played as audio (x left, y right)", A, 530, 16, w=400)]
    c += [title("SHAPE FILES", B, 70, 400), field("file", B, 92, 400, "FILE", h=56),
          btn("load", B, 158, 192, "LOAD", h=48), btn("rescan", B + 208, 158, 192, "RESCAN", h=48),
          text("SVG, OBJ, PNG or JPG files in the plugin's Shapes folder. Pick one, press LOAD: it becomes the USER shape.",
               B, 216, 16, w=400)]
    c += [title("VOICE", B, 290, 400), hl("octave", B, 312, 192, "OCTAVE"), hl("t_volume", B + 208, 312, 192, "VOLUME"),
          hl("detune", B, 366, 192, "DETUNE"), hl("glide", B + 208, 366, 192, "GLIDE"),
          hl("velocity", B, 420, 400, "VELOCITY"), text("UNISON", B, 480, 17), radio("unison", B, 502, 400, h=42)]
    c += [title("TIMBRES", Cc, 70, 400), text("KEYS PLAY", Cc, 92, 17), radio("keys", Cc, 114, 400, h=42),
          text("WITH KEYS ON AUTO", Cc, 166, 17), radio("tmode", Cc, 188, 400, h=42),
          hl("split", Cc, 240, 400, "SPLIT NOTE"), text("VOICES", Cc, 300, 17), radio("voices", Cc, 322, 400, h=42),
          text("LOWER plays timbre A, UPPER timbre B, AUTO both: layered (DUAL) or split at the SPLIT NOTE "
               "(A below, B above). TIMBRE A/B in the top strip picks the one you edit.", Cc, 380, 16, w=400)]
    c += [title("MY PRESETS", Cc, 470, 400), field("user_slot", Cc, 494, 400, "SLOT", h=52),
          btn("save_user", Cc, 556, 192, "SAVE", h=48), btn("load_user", Cc + 208, 556, 192, "LOAD", h=48)]
    q = [("ORBIT", ["orb_speed", "orb_size", "sub_size", "sub_speed", "rotate", "orb_env", "spin", "tilt",
                    "shape", "shape_osc", "detune", "glide", "octave", "t_volume", "velocity", "file"])]
    return c, q


def page_arp():
    c = [title("ARPEGGIATOR", 16, 70, 1248), tog("arp_on", 16, 96, 170, "ACTIVE"), tog("latch", 202, 96, 170, "LATCH"),
         hl("arp_oct", 396, 92, 240, "OCTAVES"), hl("note_len", 660, 92, 300, "NOTE LENGTH"), hl("arp_steps", 984, 92, 280, "STEPS"),
         text("DIRECTION", 16, 156, 17), radio("arp_mode", 16, 178, 600, h=42),
         text("RATE", 648, 156, 17), radio("arp_rate", 648, 178, 616, h=42)]
    c += [title("STEP PATTERN", 16, 236, 1248)]
    for i in range(16):
        x = 16 + i * 78
        c += [text(str(i + 1), x + 26, 258, 18, INK), radio("step%d" % (i + 1), x, 282, 70, labels=["OFF", "ON", "ACC", "TIE"],
                                                         h=46, vertical=True, order=[2, 1, 3, 0])]
    c += [text("Each arp step plays ON, plays with an ACCENT, holds the last note (TIE) or rests (OFF). "
               "STEPS sets how many steps loop.", 16, 500, 17, w=1248)]
    q = [("ARP", ["arp_on", "latch", "arp_mode", "arp_rate", "arp_oct", "note_len", "arp_steps"]),
         ("STEPS 1-8", ["step%d" % (i + 1) for i in range(8)]), ("STEPS 9-16", ["step%d" % (i + 9) for i in range(8)])]
    return c, q


def page_fx():
    c = [title("DRIVE", 16, 70, 300), hl("drive", 16, 92, 300, "AMOUNT"), hl("drive_col", 16, 146, 300, "COLOUR"),
         title("REVERB", 16, 214, 300), hl("rev_size", 16, 236, 300, "SIZE"), hl("rev_col", 16, 290, 300, "COLOUR"),
         title("FILTERS", 16, 358, 300), hl("mres", 16, 380, 300, "MASTER RESONANCE")]
    c += [title("CHORUS", 332, 70, 300), hl("ch_speed", 332, 92, 300, "SPEED"), hl("ch_depth", 332, 146, 300, "DEPTH"),
          hl("ch_col", 332, 200, 300, "COLOUR"), tog("ch_sync", 332, 262, 300, "SYNC TO TEMPO")]
    c += [title("DELAY", 648, 70, 300), hl("dly_time", 648, 92, 300, "TIME"), hl("dly_fb", 648, 146, 300, "FEEDBACK"),
          tog("dly_sync", 648, 206, 142, "SYNC"), btn("tap", 806, 206, 142, "TAP"),
          text("DIVISION", 648, 262, 17), popup("dly_div", 648, 284, 300, "", list_at=(648, 330), cols=2, ow=150)]
    c += mixer(980, 70)
    q = [("FX", ["drive", "drive_col", "ch_speed", "ch_depth", "ch_col", "dly_time", "dly_fb", "rev_size",
                 "rev_col", "mres", "mix_rev", "mix_ch", "mix_dly", "volume", "dly_div", "ch_sync"])]
    return c, q


PAGES = [page_main, page_corners, page_orbit, page_arp, page_fx]


def stripe():
    """the strip across every page: the name, the readout and its line, PRESET, TIMBRE A/B"""
    return [{"kind": "readout"}, field("preset", 672, 8, 260, "PRESET", h=44),
            radio("timbre", 944, 8, 320, labels=["TIMBRE A", "TIMBRE B"], h=44)]


# ------------------------------------------------------------------------------------------------ layout.conf
def write_layout():
    L = ["# VADER -- the tabs and their Q-Links for mpc-vst-plugins (GENERATED by tools/vader_skin.py --layout: edit that).",
         "# tools/vader_skin.py then builds every control of the five pages itself; the knobs below only carry the Q-Links.",
         "theme_bg=060a12", "theme_panel=060a12", "theme_ink=cfe3ff", "theme_accent=3aa0ff", ""]
    for name, fn in zip(TABS, PAGES):
        comps, qsets = fn()
        L.append("[tab %s]" % name)
        keys = []
        for _, ks in qsets:
            for k in ks:
                if k not in keys:
                    keys.append(k)
        for i, k in enumerate(keys):
            L.append('knob cx=%d cy=%d r=12 label="" key=%s ns=0 bw=30' % (24 + (i % 30) * 40, 110 + (i // 30) * 40, k))
        for qn, ks in qsets:
            L.append('qlinks "%s" = %s' % (qn, ",".join(ks)))
        L.append("")
    open(os.path.join(ROOT, "layout.conf"), "w").write("\n".join(L))
    print("layout.conf written")


# ------------------------------------------------------------------------------------------------ drawing
def _pil():
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    return Image, ImageDraw, ImageFilter, ImageFont


_fonts = {}


def font(px, bold=True):
    _, _, _, ImageFont = _pil()
    k = (px, bold)
    if k not in _fonts:
        _fonts[k] = ImageFont.truetype(FONT_B if bold else FONT_S, px)
    return _fonts[k]


def fit(text_, w, px, bold=True, minpx=14):
    """the biggest size up to px at which text fits w"""
    while px > minpx and font(px, bold).getlength(text_) > w:
        px -= 1
    return font(px, bold)


def glow_line(img, xy0, xy1, colour, width, glow=6, strength=0.9):
    """a line with a soft glow of its own colour"""
    Image, ImageDraw, ImageFilter, _ = _pil()
    g = Image.new("RGBA", img.size, colour + (0,))
    m = Image.new("L", img.size, 0)
    ImageDraw.Draw(m).line([xy0, xy1], fill=255, width=width + glow)
    m = m.filter(ImageFilter.GaussianBlur(glow / 2))
    g.putalpha(m.point(lambda v: int(v * strength * 0.6)))
    img.alpha_composite(g)
    ImageDraw.Draw(img).line([xy0, xy1], fill=colour + (255,), width=width)


def line_strip(w, frames=101):
    """a line control's filmstrip: the thin track and the thick glowing line over it, from nothing to full"""
    Image, ImageDraw, _, _ = _pil()
    strip = Image.new("RGBA", (w, LINE_H * frames), (0, 0, 0, 0))
    for k in range(frames):
        fr = Image.new("RGBA", (w, LINE_H), (0, 0, 0, 0))
        d = ImageDraw.Draw(fr)
        d.line([(2, LINE_H // 2), (w - 3, LINE_H // 2)], fill=TRACK + (255,), width=2)
        x = 2 + (w - 6) * k / (frames - 1)
        if x > 3:
            glow_line(fr, (3, LINE_H // 2), (x, LINE_H // 2), ACCENT, 6, glow=6)
        strip.alpha_composite(fr, (0, k * LINE_H))
    return strip


def fader_strip(w, h, frames=101):
    Image, ImageDraw, _, _ = _pil()
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(fr)
        cx = w // 2
        d.line([(cx, 2), (cx, h - 3)], fill=TRACK + (255,), width=2)
        y = (h - 3) - (h - 6) * k / (frames - 1)
        if y < h - 4:
            glow_line(fr, (cx, h - 4), (cx, y), ACCENT, 7, glow=7)
        strip.alpha_composite(fr, (0, k * h))
    return strip


def meter_strip(w, h, frames=101, colour=ACCENT):
    Image, ImageDraw, _, _ = _pil()
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(fr)
        d.line([(1, h // 2), (w - 2, h // 2)], fill=TRACK + (255,), width=2)
        x = 1 + (w - 4) * k / (frames - 1)
        if x > 2:
            glow_line(fr, (2, h // 2), (x, h // 2), colour, max(3, h // 2 - 1), glow=4)
        strip.alpha_composite(fr, (0, k * h))
    return strip


SS = 4   # drawn 4 times larger, then shrunk: smooth edges


def _piece(fr, ang, k, A, step, colour, width, glow):
    """a straight piece: angle ang of A steps, k steps of step px from the tile's middle (src/screen.c piece_frame)"""
    Image, ImageDraw, ImageFilter, _ = _pil()
    w, h = TILE_W, TILE_H
    th = ang * math.pi / A
    ux, uy, nx, ny = math.cos(th), math.sin(th), -math.sin(th), math.cos(th)
    px, py = w / 2 + nx * k * step, h / 2 + ny * k * step
    t0, t1 = -100.0, 100.0
    for p_, q_ in ((-ux, px), (ux, w - px), (-uy, py), (uy, h - py)):
        if abs(p_) < 1e-9:
            if q_ < 0:
                return
            continue
        r = q_ / p_
        if p_ < 0:
            t0 = max(t0, r)
        else:
            t1 = min(t1, r)
    if t1 - t0 < 0.5:
        return
    a, b = (px + ux * t0, py + uy * t0), (px + ux * t1, py + uy * t1)
    big = Image.new("RGBA", (w * SS, h * SS), colour + (0,))
    m = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(m).line([(a[0] * SS, a[1] * SS), (b[0] * SS, b[1] * SS)], fill=255, width=int(width * SS))
    halo = m.filter(ImageFilter.GaussianBlur(glow * SS)).point(lambda v: int(min(255, v * 1.6)))
    big.putalpha(Image.fromarray(__import__("numpy").maximum(__import__("numpy").asarray(m), __import__("numpy").asarray(halo) * 0.55).astype("uint8")))
    fr.alpha_composite(big.resize((w, h), Image.LANCZOS))


def _dot(fr, sx, sy, colour):
    Image, ImageDraw, ImageFilter, _ = _pil()
    w, h = TILE_W, TILE_H
    cx, cy = (sx + 0.5) * w / G.DOT_NX, (sy + 0.5) * h / G.DOT_NY
    big = Image.new("RGBA", (w * SS, h * SS), colour + (0,))
    m = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(m).ellipse([(cx - 6.5) * SS, (cy - 6.5) * SS, (cx + 6.5) * SS, (cy + 6.5) * SS], fill=130)
    m = m.filter(ImageFilter.GaussianBlur(1.6 * SS))
    ImageDraw.Draw(m).ellipse([(cx - 4.3) * SS, (cy - 4.3) * SS, (cx + 4.3) * SS, (cy + 4.3) * SS], fill=255)
    big.putalpha(m)
    fr.alpha_composite(big.resize((w, h), Image.LANCZOS))


def tile_strip():
    """the screen's pieces, in src/screen.c's order: empty; every straight piece of the shape (dim blue); every piece
    of the trail (bright cyan); the path dot (amber) and the orbiter dot (cyan) at each of their 5 x 5 places"""
    Image, _, _, _ = _pil()
    w, h = TILE_W, TILE_H
    strip = Image.new("RGBA", (w, h * G.TILE_FRAMES), (0, 0, 0, 0))
    frames = [None]
    hs, ht = (G.SHAPE_OFF - 1) // 2, (G.TRAIL_OFF - 1) // 2
    frames += [("s", a, k - hs) for a in range(G.SHAPE_ANG) for k in range(G.SHAPE_OFF)]
    frames += [("t", a, k - ht) for a in range(G.TRAIL_ANG) for k in range(G.TRAIL_OFF)]
    frames += [("p", j % G.DOT_NX, j // G.DOT_NX) for j in range(G.DOT_NX * G.DOT_NY)]
    frames += [("o", j % G.DOT_NX, j // G.DOT_NX) for j in range(G.DOT_NX * G.DOT_NY)]
    assert len(frames) == G.TILE_FRAMES
    for i, f in enumerate(frames):
        if f is None:
            continue
        fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        if f[0] == "s":
            _piece(fr, f[1], f[2], G.SHAPE_ANG, G.SHAPE_DSTEP, (70, 160, 255), 1.7, 1.0)
        elif f[0] == "t":
            _piece(fr, f[1], f[2], G.TRAIL_ANG, G.TRAIL_DSTEP, CYAN, 2.2, 1.2)
        else:
            _dot(fr, f[1], f[2], AMBER if f[0] == "p" else CYAN)
        strip.alpha_composite(fr, (0, i * h))
    return strip


_paths = None


def builtin_paths():
    """the built-in 2D shapes' paths, from the engine itself (tools/shape_dump.c, built by build.sh)"""
    global _paths
    if _paths is None:
        import subprocess
        exe = os.path.join(ROOT, "build", "tools", "shape_dump")
        out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.strip().splitlines()
        _paths = [[tuple(float(v) for v in pt.split(",")) for pt in line.split()] for line in out]
    return _paths


def shape_picture(idx, k, q):
    """the ready-drawn outline of built-in shape idx at size step k (1..8) and quarter turn q: (image, x, y) on the
    screen, the same place the engine puts its path (src/engine.c screen_update)"""
    Image, ImageDraw, ImageFilter, _ = _pil()
    import numpy as np
    c, s_ = [(1, 0), (0, 1), (-1, 0), (0, -1)][q]
    sc = k / G.PIC_SIZES
    pts = []
    for x, y in builtin_paths()[idx]:
        xr, yr = (x * c - y * s_) * sc, (x * s_ + y * c) * sc
        pts.append((G.SQUARE_X + (xr + 1) * 0.5 * G.SQUARE, (yr + 1) * 0.5 * G.SQUARE))
    pad = 12
    x0, y0 = max(0, int(min(p[0] for p in pts)) - pad), max(0, int(min(p[1] for p in pts)) - pad)
    x1 = min(G.TILE_COLS * TILE_W, int(max(p[0] for p in pts)) + pad + 1)
    y1 = min(G.TILE_ROWS * TILE_H, int(max(p[1] for p in pts)) + pad + 1)
    w, h = x1 - x0, y1 - y0
    m = Image.new("L", (w * SS, h * SS), 0)
    seq = [((px - x0) * SS, (py - y0) * SS) for px, py in pts]
    ImageDraw.Draw(m).line(seq + [seq[0]], fill=255, width=int(2.4 * SS), joint="curve")
    halo = m.filter(ImageFilter.GaussianBlur(4 * SS))
    a = np.maximum(np.asarray(m, np.float32), np.minimum(255, np.asarray(halo, np.float32) * 2.2) * 0.5)
    big = Image.new("RGBA", (w * SS, h * SS), (58, 160, 255, 0))
    big.putalpha(Image.fromarray(a.astype("uint8")))
    return big.resize((w, h), Image.LANCZOS), x0, y0


def glass():
    """the fixed glass over the screen: thin scanlines and darker edges"""
    Image, ImageDraw, _, _ = _pil()
    import numpy as np
    w, h = G.TILE_COLS * TILE_W, G.TILE_ROWS * TILE_H
    a = np.zeros((h, w), np.float32)
    a[::3, :] = 70
    yy, xx = np.mgrid[0:h, 0:w]
    v = np.clip(((xx - w / 2) / (w * 0.62)) ** 2 + ((yy - h / 2) / (h * 0.75)) ** 2 - 0.35, 0, 1) * 200
    a = np.clip(a + v, 0, 200)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    im.putalpha(Image.fromarray(a.astype("uint8")))
    return im


def env_strip():
    """an envelope column: a stroke from row r0 to row r1 (src/screen.c env_pair_index), every pair"""
    Image, ImageDraw, _, _ = _pil()
    w, h = ENV_CW, G.ENV_ROWS * ENV_RH
    strip = Image.new("RGBA", (w, h * G.ENV_FRAMES), (0, 0, 0, 0))
    k = 0
    for r0 in range(G.ENV_ROWS):
        for r1 in range(r0, G.ENV_ROWS):
            fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            y0, y1 = r0 * ENV_RH + ENV_RH / 2, r1 * ENV_RH + ENV_RH / 2
            glow_line(fr, (w / 2, y0 - 1.5), (w / 2, y1 + 1.5), (216, 236, 255), 3, glow=4)
            strip.alpha_composite(fr, (0, k * h))
            k += 1
    assert k == G.ENV_FRAMES
    return strip


def button_img(w, h, label, on, px=19):
    Image, ImageDraw, ImageFilter, _ = _pil()
    pad = 6
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if on:
        g = Image.new("RGBA", (w, h), ACCENT + (0,))
        m = Image.new("L", (w, h), 0)
        ImageDraw.Draw(m).rounded_rectangle([2, 2, w - 3, h - 3], radius=10, outline=255, width=4)
        g.putalpha(m.filter(ImageFilter.GaussianBlur(3)).point(lambda v: int(v * 0.7)))
        im.alpha_composite(g)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([3, 3, w - 4, h - 4], radius=9, fill=(BTN_ON if on else BTN_OFF) + (255,),
                        outline=(ACCENT if on else TRACK) + (255,), width=2)
    if label:
        f = fit(label, w - 2 * pad - 4, px)
        d.text((w / 2, h / 2 + 1), label, font=f, fill=(WHITE if on else INK) + (255,), anchor="mm")
    return im


def label_img(text_, w, px=18, colour=INK, bold=True, h=24):
    Image, ImageDraw, _, _ = _pil()
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    f = fit(text_, w, px, bold)
    ImageDraw.Draw(im).text((0, h / 2), text_, font=f, fill=colour + (255,), anchor="lm")
    return im


# ------------------------------------------------------------------------------------------------ TUI.json pieces
def B(x, y, w, h):
    return "%d %d %d %d" % (round(x), round(y), round(w), round(h))


def sub(name, typ, data, b, visible="Always"):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": []},
            "bounds": {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": visible,
                       "boundsType": "Absolute", "bounds": b, "additionalInvalidatingHandles": []}}


def focus(w, h):
    return sub("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": "00000000",
                                  "backgroundInset": 2.0, "outlineThickness": 0.0}, B(0, 0, w, h), "WhenFocussed")


def action(on, handler, extra=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data",
            "additionalData": extra, "handle remapping": {"version": 1, "map": []}}


KNOB_ACTIONS = [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"),
                action("Enter Pressed", "Show Overlay", "knob overlay")]
TOGGLE_ACTIONS = [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
RADIO_ACTIONS = [action("Mouse Down", "Q-Link")]
OPEN_ACTIONS = [action("Mouse Down", "Toggle Switch"), action("Enter Pressed", "Toggle Switch")]


def definition(key, actions, parts, ignore=False):
    return {"key": key, "value": {
        "version": 4, "actions": actions,
        "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "0", "image": ""},
                           "unfocussed": {"version": 1, "colour": "0", "image": ""}},
        "ignoreMousePresses": ignore, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True,
        "componentsData": parts}}


def filmstrip(img, frames, w, h, drag="Horizontal", x=0, y=0):
    return sub("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": frames,
                                "invert": False, "dragOrientation": drag, "handleName": "Data"}, B(x, y, w, h))


def button(on_img, off_img, bid, n, w, h, x=0, y=0):
    return sub("Button", "Button", {"version": 2, "onImage": on_img, "offImage": off_img, "buttonId": bid,
                                    "numButtonsInGroup": n, "handleName": "Data", "gestureBehaviour": "Instant"},
               B(x, y, w, h))


def text_label(kind, x, y, w, h, px, colour="ffffff", just="horizontallyCentred verticallyCentred", handle="Data",
               style="SemiBold"):
    """MPC's own text (Titillium Web): the bound parameter's value ("Value") or name ("Name")"""
    return sub(kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web",
                                                                               "style": style, "height": px},
                                                         "colour": "ff" + colour, "justification": just, "case": "Original"},
                               "type": kind, "handleName": handle}, B(x, y, w, h))


def image(img, x, y, w, h):
    return sub("Image", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": img}, B(x, y, w, h))


def placed(dkey, name, index, rect, focus_on=True, extra=None, cond=None):
    x, y, w, h = rect
    m = [{"key": "Data", "value": "Parameter %d" % index}]
    for hname, hidx in (extra or {}).items():
        m.append({"key": hname, "value": "Parameter %d" % hidx})
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": dkey, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": m},
            "bounds": {"version": 2, "acceptsHWFocus": "Yes" if focus_on else "No", "showWhenDataModelInvalid": "Hide",
                       "whenVisible": "Always", "boundsType": "Absolute", "bounds": B(x, y, w, h),
                       "additionalInvalidatingHandles": [cond] if cond else []}}


# ------------------------------------------------------------------------------------------------ the builder
class Skin:
    def __init__(self, skin, params):
        self.skin = skin
        self.index = {p["key"]: i for i, p in enumerate(params)}
        self.params = {p["key"]: p for p in params}
        self.defs = {}
        self.files = set()

    def save(self, img, name):
        if name not in self.files:
            img.save(os.path.join(self.skin, name))
            self.files.add(name)
        return name

    def opts(self, key):
        return self.params[key].get("options") or []

    def cond(self, when):
        """when="alt:ON" -> MPC's IndexedEnabling handle"""
        if not when:
            return None
        k, o = when.split(":")
        opts = self.opts(k)
        return "IndexedEnabling/%d/%d/Parameter %d" % (opts.index(o), len(opts), self.index[k])


def background(page_no):
    Image, ImageDraw, ImageFilter, _ = _pil()
    if os.path.exists(BG_IMAGE):
        src = Image.open(BG_IMAGE).convert("RGB")
        s = max(W / src.width, H / src.height)
        src = src.resize((int(src.width * s + 1), int(src.height * s + 1)), Image.LANCZOS)
        x0, y0 = (src.width - W) // 2, (src.height - H) // 2
        bg = src.crop((x0, y0, x0 + W, y0 + H)).convert("RGBA")
    else:
        import numpy as np
        rng = np.random.default_rng(11 + page_no)
        a = np.zeros((H, W, 3), np.float32) + np.array(GROUND, np.float32)
        bg = Image.fromarray(np.clip(a, 0, 255).astype("uint8"), "RGB").convert("RGBA")
    # the stripe
    band = Image.new("RGBA", (W, 60), (0, 0, 0, 230))
    bg.alpha_composite(band, (0, 0))
    d = ImageDraw.Draw(bg)
    d.line([(0, 60), (W, 60)], fill=TRACK + (255,), width=2)
    vf = font(32)
    x = 18
    for ch in "VADER":   # letter-spaced, with a blue glow
        g = Image.new("RGBA", (60, 60), ACCENT + (0,))
        m = Image.new("L", (60, 60), 0)
        ImageDraw.Draw(m).text((8, 30), ch, font=vf, fill=255, anchor="lm")
        g.putalpha(m.filter(ImageFilter.GaussianBlur(4)).point(lambda v: int(v * 0.8)))
        bg.alpha_composite(g, (int(x) - 8, 0))
        d.text((x, 30), ch, font=vf, fill=WHITE + (255,), anchor="lm")
        x += vf.getlength(ch) + 9
    return bg


def window(d, rect, radius=10, fill=(0, 0, 0, 255)):
    x, y, w, h = rect
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=radius, fill=fill, outline=(29, 58, 92, 255), width=2)


def build_page(sk, page_no, comps):
    Image, ImageDraw, ImageFilter, _ = _pil()
    bg = background(page_no)
    d = ImageDraw.Draw(bg)
    kids, top = [], []
    t = TABS[page_no][:2].lower()

    def define(key, actions, parts, ignore=False):
        if key not in sk.defs:
            sk.defs[key] = definition(key, actions, parts, ignore)
        return key

    for c in stripe() + comps:
        k = c["kind"]
        key = c.get("key")
        cond = sk.cond(c.get("when"))
        if k == "title":
            d.text((c["x"], c["y"] + 9), c["text"], font=font(18), fill=ACCENT + (255,), anchor="lm")
            tw = font(18).getlength(c["text"])
            if c["w"] > tw + 16:
                d.line([(c["x"] + tw + 10, c["y"] + 10), (c["x"] + c["w"], c["y"] + 10)], fill=TRACK + (255,), width=1)
        elif k == "text":
            if c.get("w"):   # wrapped
                f = font(c["size"], False)
                words, line, yy = c["text"].split(), "", c["y"]
                for wd in words:
                    test = (line + " " + wd).strip()
                    if f.getlength(test) > c["w"] and line:
                        d.text((c["x"], yy), line, font=f, fill=c["colour"] + (255,))
                        yy += c["size"] + 5
                        line = wd
                    else:
                        line = test
                if line:
                    d.text((c["x"], yy), line, font=f, fill=c["colour"] + (255,))
            else:
                d.text((c["x"], c["y"]), c["text"], font=font(c["size"]), fill=c["colour"] + (255,))
        elif k == "readout":
            dk = define("vdReadout", [], [text_label("Value", 0, 0, 470, 34, 26.0, "ffffff",
                                                    "left verticallyCentred", style="Bold")])
            kids.append(placed(dk, "readout", sk.index["readout"], (196, 4, 470, 34), False))
            strip = sk.save(meter_strip(470, 12), "vd_rdline.png")
            dk = define("vdRdLine", [], [filmstrip(strip, 101, 470, 12)], ignore=True)
            kids.append(placed(dk, "readout line", sk.index["rd_line"], (196, 40, 470, 12), False))
        elif k == "hline":
            x, y, w = c["x"], c["y"], c["w"]
            lab = c["label"] or sk.params[key]["name"]
            vw = 56   # the value, right of the line
            limg = sk.save(label_img(lab, w, 18), "vd_lab_%s_%s_%d.png" % (t, key, w))
            strip = sk.save(line_strip(w - vw - 4), "vd_line_%d.png" % (w - vw - 4))
            dk = define("vdLine_%s_%s_%d" % (t, key, w), KNOB_ACTIONS,
                        [focus(w, HL), image(limg, 0, 0, w, 24),
                         text_label("Value", w - vw, 22, vw, 28, 22.0, "ffffff", "right verticallyCentred"),
                         filmstrip(strip, 101, w - vw - 4, LINE_H, "Horizontal", 0, LINE_Y)])
            kids.append(placed(dk, key, sk.index[key], (x, y, w, HL), True, cond=cond))
        elif k == "fader":
            x, y, w, h = c["x"], c["y"], c["w"], c["h"]
            limg = sk.save(label_img(c["label"], w, 16, INK, h=22), "vd_flab_%s.png" % key)
            fh = 128
            strip = sk.save(fader_strip(28, fh), "vd_fader_%d.png" % fh)
            dk = define("vdFader_%s" % key, KNOB_ACTIONS,
                        [focus(w, h), image(limg, 4, 0, w - 4, 22),
                         filmstrip(strip, 101, 28, fh, "Vertical", (w - 28) // 2, 26),
                         text_label("Value", 0, 26 + fh + 2, w, 28, 22.0, "ffffff")])
            kids.append(placed(dk, key, sk.index[key], (x, y, w, h), True))
        elif k in ("toggle", "button"):
            x, y, w, h = c["x"], c["y"], c["w"], c["h"]
            lab = c["label"] or sk.params[key]["name"]
            on = sk.save(button_img(w, h, lab, True), "vd_btn_%s_%d_%d_on.png" % (key, w, h))
            off = sk.save(button_img(w, h, lab, False), "vd_btn_%s_%d_%d_off.png" % (key, w, h))
            dk = define("vdTog_%s_%d_%d" % (key, w, h), TOGGLE_ACTIONS, [focus(w, h), button(on, off, 1, 1, w, h)])
            kids.append(placed(dk, key, sk.index[key], (x, y, w, h), True, cond=cond))
        elif k == "radio":
            opts = sk.opts(key)
            labels = c["labels"] or opts
            n = len(opts)
            order = c["order"] or list(range(n))
            x, y, w, h = c["x"], c["y"], c["w"], c["h"]
            for slot, o in enumerate(order):
                if c["vertical"]:
                    rx, ry, rw = x, y + slot * (h + 6), w
                else:
                    rw = (w - 6 * (n - 1)) / n
                    rx, ry = x + slot * (rw + 6), y
                rw_i = int(rw)
                on = sk.save(button_img(rw_i, h, labels[o], True), "vd_opt_%s_%d_%d_on.png" % (key, o, rw_i))
                off = sk.save(button_img(rw_i, h, labels[o], False), "vd_opt_%s_%d_%d_off.png" % (key, o, rw_i))
                dk = define("vdOpt_%s_%d_%d" % (key, o, rw_i), RADIO_ACTIONS, [focus(rw_i, h), button(on, off, o, n, rw_i, h)])
                kids.append(placed(dk, "%s %s" % (key, opts[o]), sk.index[key], (rx, ry, rw_i, h), slot == 0))
        elif k == "popup":
            x, y, w, h = c["x"], c["y"], c["w"], c["h"]
            opts = sk.opts(key)
            window(d, (x, y, w, h), 9, BTN_OFF + (255,))
            cap = c["caption"]
            cw_ = 0
            if cap:
                d.text((x + 12, y + h / 2), cap, font=font(18), fill=INK_DIM + (255,), anchor="lm")
                cw_ = font(18).getlength(cap) + 22
            d.polygon([(x + w - 26, y + h / 2 - 4), (x + w - 14, y + h / 2 - 4), (x + w - 20, y + h / 2 + 4)], fill=ACCENT + (255,))
            oi = sk.index[key + "__open"]
            dk = define("vdPopField_%d_%d_%d" % (w, h, cw_), OPEN_ACTIONS,
                        [focus(w, h), text_label("Value", 12 + cw_, 0, w - 44 - cw_, h, 22.0, "ffffff",
                                                 "left verticallyCentred", handle="Text")])
            kids.append(placed(dk, key + " field", oi, (x, y, w, h), True, extra={"Text": sk.index[key]}))
            # the list, shown while open
            lx, ly = c["list_at"] or (x, y + h + 4)
            cols, ow, oh = c["cols"], c["ow"], c["oh"]
            rows = math.ceil(len(opts) / cols)
            pw, ph = cols * (ow + 6) + 14, rows * (oh + 6) + 14
            lx = min(max(4, lx), W - pw - 4)
            ly = min(max(64, ly), H - ph - 4)
            shown = "IndexedEnabling/1/2/Parameter %d" % oi
            panel = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
            window(ImageDraw.Draw(panel), (0, 0, pw, ph), 12, (3, 8, 16, 250))
            pimg = sk.save(panel, "vd_pop_%dx%d.png" % (pw, ph))
            pk = define("vdPopPanel_%dx%d" % (pw, ph), [], [image(pimg, 0, 0, pw, ph)])
            parts = [placed(pk, key + " list", oi, (lx, ly, pw, ph), False, cond=shown)]
            for o, name in enumerate(opts):
                ox, oy = lx + 7 + (o % cols) * (ow + 6), ly + 7 + (o // cols) * (oh + 6)
                on = sk.save(button_img(ow, oh, name, True), "vd_po_%s_%d_%d_on.png" % (key.split("_")[-1], o, ow))
                off = sk.save(button_img(ow, oh, name, False), "vd_po_%s_%d_%d_off.png" % (key.split("_")[-1], o, ow))
                okey = define("vdPopOpt_%s_%d_%d" % (key, o, ow), RADIO_ACTIONS, [button(on, off, o, len(opts), ow, oh)])
                pp = placed(okey, "%s %s" % (key, name), sk.index[key], (ox, oy, ow, oh), False, cond=shown)
                parts.append(pp)
            for p_ in parts:
                p_["bounds"]["showWhenDataModelInvalid"] = "Show"
            top += parts
        elif k == "field":
            x, y, w, h = c["x"], c["y"], c["w"], c["h"]
            window(d, (x, y, w, h), 9, BTN_OFF + (255,))
            cap = c["caption"]
            d.text((x + 12, y + h / 2), cap, font=font(18), fill=INK_DIM + (255,), anchor="lm")
            cw_ = font(18).getlength(cap) + 22
            d.text((x + w - 18, y + h / 2), "<>", font=font(16), fill=ACCENT + (255,), anchor="mm")
            empty = sk.save(Image.new("RGBA", (8, 16), (0, 0, 0, 0)), "vd_empty.png")
            dk = define("vdField_%d_%d_%d" % (w, h, cw_), KNOB_ACTIONS,
                        [focus(w, h), filmstrip(empty, 2, 8, 8, "Horizontal", 0, 0),
                         text_label("Value", 12 + cw_, 0, w - 50 - cw_, h, 24.0, "ffffff", "left verticallyCentred")])
            kids.append(placed(dk, key, sk.index[key], (x, y, w, h), True))
        elif k == "envwin":
            window(d, ENV_BOX, 8)
            strip = sk.save(env_strip(), "vd_env.png")
            eh = G.ENV_ROWS * ENV_RH
            dk = define("vdEnvCol", [], [filmstrip(strip, G.ENV_FRAMES, ENV_CW, eh, "Vertical")], ignore=True)
            for i in range(G.ENV_COLS):
                kids.append(placed(dk, "env %d" % i, sk.index["e%02d" % i], (ENV_XY[0] + i * ENV_CW, ENV_XY[1], ENV_CW, eh), False))
        elif k == "screen":
            window(d, SCREEN, 12)
            # the corners: at the corners of the square (true proportions)
            sq = (TILES_XY[0] + G.SQUARE_X, TILES_XY[1], G.SQUARE, G.SQUARE)
            for (lab, ax, ay) in (("A", 0, 0), ("B", 1, 0), ("C", 0, 1), ("D", 1, 1)):
                sx = sq[0] - 26 if not ax else sq[0] + sq[2] + 10
                sy = sq[1] + 2 if not ay else sq[1] + sq[3] - 26
                d.text((sx, sy), lab, font=font(20), fill=(70, 120, 180, 255))
            # faint corner marks of the square
            for (cx_, cy_, dx, dy) in ((sq[0], sq[1], 1, 1), (sq[0] + sq[2], sq[1], -1, 1), (sq[0], sq[1] + sq[3], 1, -1),
                                       (sq[0] + sq[2], sq[1] + sq[3], -1, -1)):
                d.line([(cx_, cy_), (cx_ + 14 * dx, cy_)], fill=(30, 60, 100, 255), width=2)
                d.line([(cx_, cy_), (cx_, cy_ + 14 * dy)], fill=(30, 60, 100, 255), width=2)
            # the built-in shapes' pictures, one shown at a time (the engine's "shape_pic")
            n = G.PIC_COUNT + 1
            for idx in range(G.PIC_SHAPES):
                for k_ in range(1, G.PIC_SIZES + 1):
                    for q in range(G.PIC_ROTS):
                        pic = 1 + idx * G.PIC_SIZES * G.PIC_ROTS + (k_ - 1) * G.PIC_ROTS + q
                        im, px, py = shape_picture(idx, k_, q)
                        name = sk.save(im, "vd_shape_%d.png" % pic)
                        dk = define("vdShapePic_%d" % pic, [], [image(name, 0, 0, im.width, im.height)], ignore=True)
                        cond = "IndexedEnabling/%d/%d/Parameter %d" % (pic, n, sk.index["shape_pic"])
                        kids.append(placed(dk, "shape %d" % pic, sk.index["shape_pic"],
                                           (TILES_XY[0] + px, TILES_XY[1] + py, im.width, im.height), False, cond=cond))
        elif k == "tiles":
            strip = sk.save(tile_strip(), "vd_tiles.png")
            dk = define("vdTile", [], [filmstrip(strip, G.TILE_FRAMES, TILE_W, TILE_H, "Vertical")], ignore=True)
            for r in range(G.TILE_ROWS):
                for q in range(G.TILE_COLS):
                    i = r * G.TILE_COLS + q
                    kids.append(placed(dk, "t%d" % i, sk.index["t%03d" % i],
                                       (TILES_XY[0] + q * TILE_W, TILES_XY[1] + r * TILE_H, TILE_W, TILE_H), False))
            gname = sk.save(glass(), "vd_glass.png")
            gk = define("vdGlass", [], [image(gname, 0, 0, G.TILE_COLS * TILE_W, G.TILE_ROWS * TILE_H)], ignore=True)
            kids.append(placed(gk, "glass", sk.index["shape_pic"], (TILES_XY[0], TILES_XY[1], G.TILE_COLS * TILE_W,
                                                                  G.TILE_ROWS * TILE_H), False))
        elif k == "bars":
            strip = sk.save(meter_strip(124, 14, colour=CYAN), "vd_bar.png")
            dk = define("vdBar", [], [filmstrip(strip, 101, 124, 14, "Horizontal")], ignore=True)
            for i, cl in enumerate("ABCD"):
                bx = c["x"] + i * 166
                d.text((bx, c["y"] + 11), cl, font=font(20), fill=WHITE + (255,), anchor="lm")
                kids.append(placed(dk, "corner %s" % cl, sk.index["w" + cl.lower()], (bx + 24, c["y"] + 4, 124, 14), False))
    return bg, kids + top


def main(skin):
    tui_path = os.path.join(skin, "TUI.json")
    tui = json.load(open(tui_path))
    cdefs = tui["pageData"]["componentDefinitions"]
    defs = cdefs["localComponentDefinitions"]
    params = json.load(open(os.path.join(ROOT, "params.json")))["params"]
    sk = Skin(skin, params)
    pages = {}
    for n, (name, fn) in enumerate(zip(TABS, PAGES)):
        comps, _ = fn()
        bg, kids = build_page(sk, n, comps)
        bgname = "vd_bg_%d.png" % n
        bg.convert("RGB").save(os.path.join(skin, bgname))
        sk.files.add(bgname)
        pages[name] = [sub("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname},
                           B(0, 0, W, H))] + kids
    keep = []
    for e in defs:
        tab = e["key"].split("|")[0]
        if "|" in e["key"] and tab in pages:
            e["value"]["componentsData"] = pages[tab]
            e["value"]["backgroundData"] = {"version": 1, "focussed": {"version": 1, "colour": "ff060a12", "image": ""},
                                            "unfocussed": {"version": 1, "colour": "ff060a12", "image": ""}}
            keep.append(e)
    cdefs["localComponentDefinitions"] = keep + list(sk.defs.values())
    json.dump(tui, open(tui_path, "w"), indent=1)
    for f in os.listdir(skin):
        if f.endswith(".png") and f not in sk.files:
            os.remove(os.path.join(skin, f))
    print("vader_skin: %d definitions, %d pictures, %d components on MAIN" % (len(sk.defs), len(sk.files), len(pages["MAIN"])))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--layout":
        write_layout()
    elif len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.exit(__doc__)
