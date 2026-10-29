#!/usr/bin/env python3
"""VADER's six pages: where every control sits on MPC's 1280 x 628 plugin screen, and the builder that draws them.

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
INK = (255, 255, 255)
INK_DIM = (232, 236, 242)
WHITE = (255, 255, 255)
AMBER = (255, 176, 32)
CYAN = (125, 252, 255)
GROUND = (0, 0, 0)
BTN_ON = (0, 0, 0)
BTN_OFF = (0, 0, 0)

TABS = ["MAIN", "CORNERS", "ORBIT", "IMAGE", "ARP", "FX", "MODE", "VOICES", "PLAY", "TRAVEL", "SAMPLES", "SAMPLE EDIT"]
HL = 50          # a line control's height: name row, then the line
LINE_H = 16      # its line's strip frame height
LINE_Y = 30      # where the line sits inside the control
TILE_W, TILE_H = 20, 18
SCREEN = (312, 114, 656, 296)               # the screen window on MAIN
TILES_XY = (320, 118)                        # its top-left tile
ENV_BOX = (16, 92, 400, 64)
ENV_XY = (24, 96)                            # the envelope columns' top-left
ENV_CW, ENV_RH = 12, 4                       # column width, row height (32 columns: 384 px)
# 0.6.0: the companion screen's QR code on MAIN: QR_N modules of QR_MOD px, a quiet zone of 4 modules, on a light tile
QR_QUIET = 3                                  # modules of quiet zone (the standard says 4; 3 reads fine, and fits)
QR_TILE = G.QR_N * G.QR_MOD + 2 * QR_QUIET * G.QR_MOD   # 273
QR_XY = (436 + (408 - QR_TILE) // 2, 120)    # the tile's top-left
QR_MOD_XY = (QR_XY[0] + QR_QUIET * G.QR_MOD, QR_XY[1] + QR_QUIET * G.QR_MOD)   # the first module


# ------------------------------------------------------------------------------------------------ the pages
def hl(key, x, y, w, label=None, when=None, big=False):
    return {"kind": "hline", "key": key, "x": x, "y": y, "w": w, "label": label, "when": when, "big": big}


HLB = 52         # a big line control's height (the MAIN page, 0.6.0): a 22 px name, a thicker line, a 26 px value
LINE_HB = 18


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


def fader(key, x, y, label, w=72, big=False):
    return {"kind": "fader", "key": key, "x": x, "y": y, "w": w, "h": 186, "label": label, "big": big}


def title(text, x, y, w):
    return {"kind": "title", "text": text, "x": x, "y": y, "w": w}


def text(t, x, y, size=18, colour=INK_DIM, w=None):
    return {"kind": "text", "text": t, "x": x, "y": y, "size": size, "colour": colour, "w": w}


def mixer(x, y, w=288, big=False):
    fw = w // 4
    return [title("MIXER", x, y, w)] + [fader(k, x + i * fw, y + 22, l, w=fw, big=big) for i, (k, l) in
                                        enumerate([("mix_rev", "REVERB"), ("mix_ch", "CHORUS"), ("mix_dly", "DELAY"),
                                                   ("volume", "VOLUME")])]


def page_main():
    """0.6.0: three columns of big controls; in the middle, instead of the screen, the companion screen's QR code
    around the VADER mark (the phone draws the screen). ENV_BOX / QR_XY say where the envelope and the code sit."""
    L, C, R = 16, 436, 864
    W = 400
    c = []
    # left: the envelope, the orbit and shape (ALT: the voice, vibrato, tremolo), the delay (ALT: the chorus)
    c += [title("ENVELOPE", L, 70, W), {"kind": "envwin"}]
    c += [hl("attack", L, 166, 192, big=True), hl("decay", L + 208, 166, 192, big=True),
          hl("sustain", L, 222, 192, big=True), hl("release", L + 208, 222, 192, big=True)]
    c += [title("ORBIT AND SHAPE  /  ALT: VOICE, VIB, TREM", L, 282, W)]
    c += [hl("orb_speed", L, 304, 192, "ORBIT SPEED", when="alt:OFF", big=True), hl("orb_size", L + 208, 304, 192, "ORBIT SIZE", when="alt:OFF", big=True),
          hl("sub_size", L, 360, 192, "SUBORBIT SIZE", when="alt:OFF", big=True), hl("sub_speed", L + 208, 360, 192, "SUBORB SPEED", when="alt:OFF", big=True),
          hl("rotate", L, 416, 192, "ROTATE", when="alt:OFF", big=True), hl("spin", L + 208, 416, 192, "3D SPIN", when="alt:OFF", big=True),
          hl("tilt", L, 472, 192, "3D TILT", when="alt:OFF", big=True), hl("orb_env", L + 208, 472, 192, "ORBIT ENV", when="alt:OFF", big=True),
          hl("detune", L, 304, 192, "DETUNE", when="alt:ON", big=True), hl("glide", L + 208, 304, 192, "GLIDE", when="alt:ON", big=True),
          hl("velocity", L, 360, 192, "VELOCITY", when="alt:ON", big=True), hl("t_volume", L + 208, 360, 192, "VOLUME", when="alt:ON", big=True),
          hl("vib_speed", L, 416, 192, "VIB SPEED", when="alt:ON", big=True), hl("vib_depth", L + 208, 416, 192, "VIB DEPTH", when="alt:ON", big=True),
          hl("trem_speed", L, 472, 192, "TREM SPEED", when="alt:ON", big=True), hl("trem_depth", L + 208, 472, 192, "TREM DEPTH", when="alt:ON", big=True)]
    c += [title("DELAY  /  ALT: CHORUS", L, 532, W)]
    c += [hl("dly_time", L, 554, 192, "DLY TIME", when="alt:OFF", big=True), hl("dly_fb", L + 208, 554, 192, "FEEDBACK", when="alt:OFF", big=True),
          hl("ch_speed", L, 554, 192, "CH SPEED", when="alt:ON", big=True), hl("ch_depth", L + 208, 554, 192, "CH DEPTH", when="alt:ON", big=True)]
    # middle: the shape, the QR code with the mark, the corner, the corner's sound (ALT: drive, reverb, master reso)
    c += [popup("shape", C, 68, 408, "SHAPE", h=44, list_at=(min(C - 100, 1280 - 948 - 16), 118), cols=6, ow=158, oh=44), {"kind": "qr"}]
    c += [title("CORNER SELECT", C, 398, 408), radio("corner", C, 416, 408, h=38)]
    c += [hl("f_og1", C, 460, 192, "GEN 1", when="alt:OFF", big=True), hl("f_og2", C + 208, 460, 192, "GEN 2", when="alt:OFF", big=True),
          hl("f_blend", C, 512, 192, "BLEND", when="alt:OFF", big=True), hl("f_cutoff", C + 208, 512, 192, "CUTOFF", when="alt:OFF", big=True),
          hl("f_reso", C, 564, 192, "RESO", when="alt:OFF", big=True),
          hl("drive", C, 460, 192, "DRIVE", when="alt:ON", big=True), hl("drive_col", C + 208, 460, 192, "DRV COLOUR", when="alt:ON", big=True),
          hl("rev_size", C, 512, 192, "REV SIZE", when="alt:ON", big=True), hl("rev_col", C + 208, 512, 192, "REV COLOUR", when="alt:ON", big=True),
          hl("mres", C, 564, 192, "MSTR RESO", when="alt:ON", big=True)]
    c += [tog("alt", C + 208, 568, 92, "ALT", h=42)]   # 0.6.1: no MOTION button (the picture is drawn on the phone; the setting stays on)
    # right: the mixer, the arpeggiator, the sync
    c += mixer(R, 70, W, big=True)
    c += [title("ARPEGGIATOR", R, 292, W), tog("arp_on", R, 314, 192, "ACTIVE", h=44), tog("latch", R + 208, 314, 192, "LATCH", h=44),
          popup("arp_mode", R, 366, 192, "DIR", h=44, list_at=(R - 330, 300)), popup("arp_rate", R + 208, 366, 192, "RATE", h=44, list_at=(R - 330, 300)),
          hl("note_len", R, 418, W, "NOTE LENGTH", big=True)]
    c += [title("SYNC", R, 480, W),
          tog("retrig", R, 502, 124, "RETRIG", h=44), tog("orb_sync", R + 138, 502, 124, "ORBITER", h=44), tog("vt_sync", R + 276, 502, 124, "V/T", h=44),
          btn("tap", R, 554, 124, "TAP", h=44), tog("dly_sync", R + 138, 554, 124, "DELAY", h=44), tog("ch_sync", R + 276, 554, 124, "CHORUS", h=44)]
    q = [("MAIN 1", ["f_og1", "f_og2", "f_blend", "f_cutoff", "f_reso", "orb_speed", "orb_size", "sub_size",
                     "attack", "decay", "sustain", "release", "sub_speed", "mix_rev", "mix_dly", "volume"]),
         ("MAIN 2", ["dly_time", "dly_fb", "ch_speed", "ch_depth", "f_reso", "vib_speed", "vib_depth", "trem_speed",
                     "trem_depth", "note_len", "mix_ch", "drive", "drive_col", "rev_size", "rev_col", "mres"]),
         ("MAIN 3", ["rotate", "spin", "tilt", "orb_env", "detune", "glide", "velocity", "t_volume"])]
    return c, q


def page_corners():
    c, q = [], []
    for ci, cl in enumerate("abcd"):
        x = 16 + ci * 316
        c += [title("CORNER " + cl.upper(), x, 70, 300)]
        c += [hl(cl + "_og1", x, 96, 142, "GEN 1"), hl(cl + "_og2", x + 158, 96, 142, "GEN 2"),
              hl(cl + "_oct", x, 152, 142, "OCTAVE"), hl(cl + "_tune", x + 158, 152, 142, "GEN 2 TUNE"),
              hl(cl + "_fine", x, 208, 142, "GEN 2 FINE"), hl(cl + "_level", x + 158, 208, 142, "LEVEL")]
        c += [text("BLEND", x, 266, 18), popup(cl + "_mode", x, 288, 142, "", list_at=(x if ci < 3 else x - 10, 334)),
              hl(cl + "_blend", x + 158, 280, 142, "BLEND")]
        c += [text("FILTER", x, 340, 18), popup(cl + "_ftype", x, 362, 142, "", list_at=(x if ci < 3 else x - 10, 408)),
              hl(cl + "_reso", x + 158, 354, 142, "RESONANCE")]
        c += [hl(cl + "_cutoff", x, 414, 300, "CUTOFF")]
        c += [text("FILTER ON", x, 474, 18), radio(cl + "_fpos", x, 496, 300, h=42)]
        q.append(("CORNER " + cl.upper(), [cl + f for f in ("_og1", "_og2", "_blend", "_cutoff", "_reso", "_level", "_tune", "_fine",
                                                             "_oct", "_mode", "_ftype", "_fpos")]))
    return c, q


def page_orbit():
    A, B, Cc = 16, 440, 864
    c = [title("ORBITER", A, 70, 400), popup("shape", A, 92, 400, "SHAPE", h=44, list_at=(A, 140), cols=6, ow=158, oh=44)]
    c += [hl("orb_speed", A, 146, 192, "ORBIT SPEED"), hl("orb_size", A + 208, 146, 192, "ORBIT SIZE"),
          hl("sub_size", A, 200, 192, "SUBORBIT SIZE"), hl("sub_speed", A + 208, 200, 192, "SUBORBIT SPEED"),
          hl("rotate", A, 254, 192, "ROTATE"), hl("orb_env", A + 208, 254, 192, "ORBIT ENV"),
          hl("spin", A, 308, 192, "3D SPIN"), hl("tilt", A + 208, 308, 192, "3D TILT")]
    c += [tog("retrig", A, 368, 192, "RETRIG"), tog("orb_sync", A + 208, 368, 192, "ORBITER SYNC"),
          text("SYNC TO", A, 420, 18), popup("orb_div", A + 100, 414, 300, "", list_at=(A + 100, 460), cols=2, ow=150),
          hl("shape_osc", A, 470, 400, "SHAPE SOUND")]
    c += [title("SHAPE FILES", B, 70, 400), field("file", B, 92, 400, "FILE", h=56),
          btn("load", B, 158, 192, "LOAD", h=48), btn("rescan", B + 208, 158, 192, "RESCAN", h=48)]
    c += [title("COMPANION SCREEN", B, 216, 400), field("web_url", B, 238, 400, "AT", h=44)]   # 0.6.0: the address (the QR code is on MAIN)
    c += [title("VOICE", B, 290, 400), hl("octave", B, 312, 192, "OCTAVE"), hl("t_volume", B + 208, 312, 192, "VOLUME"),
          hl("detune", B, 366, 192, "DETUNE"), hl("glide", B + 208, 366, 192, "GLIDE"),
          hl("velocity", B, 420, 400, "VELOCITY"), text("UNISON", B, 480, 18), radio("unison", B, 502, 400, h=42)]
    c += [title("TIMBRES", Cc, 70, 400), text("KEYS PLAY", Cc, 92, 18), radio("keys", Cc, 114, 400, h=42),
          text("WITH KEYS ON AUTO", Cc, 166, 18), radio("tmode", Cc, 188, 400, h=42),
          hl("split", Cc, 240, 400, "SPLIT NOTE"), text("VOICES", Cc, 300, 18), radio("voices", Cc, 322, 400, h=42)]
    c += [title("MY PRESETS", Cc, 470, 400), field("user_slot", Cc, 494, 400, "SLOT", h=52),
          btn("save_user", Cc, 556, 192, "SAVE", h=48), btn("load_user", Cc + 208, 556, 192, "LOAD", h=48)]
    q = [("ORBIT", ["orb_speed", "orb_size", "sub_size", "sub_speed", "rotate", "orb_env", "spin", "tilt",
                    "shape", "shape_osc", "detune", "glide", "octave", "t_volume", "velocity", "file"])]
    return c, q


def page_arp():
    c = [title("ARPEGGIATOR", 16, 70, 1248), tog("arp_on", 16, 96, 170, "ACTIVE"), tog("latch", 202, 96, 170, "LATCH"),
         hl("arp_oct", 396, 92, 240, "OCTAVES"), hl("note_len", 660, 92, 300, "NOTE LENGTH"), hl("arp_steps", 984, 92, 280, "STEPS"),
         text("DIRECTION", 16, 156, 18), radio("arp_mode", 16, 178, 600, h=42),
         text("RATE", 648, 156, 18), radio("arp_rate", 648, 178, 616, h=42)]
    c += [title("STEP PATTERN", 16, 236, 1248)]
    for i in range(16):
        x = 16 + i * 78
        c += [text(str(i + 1), x + 26, 258, 18, INK), radio("step%d" % (i + 1), x, 282, 70, labels=["OFF", "ON", "ACC", "TIE"],
                                                         h=46, vertical=True, order=[2, 1, 3, 0])]
    c += [title("STEP OCTAVE  (-2 .. +2)", 16, 470, 1248)]
    for i in range(16):
        c += [hl("stepo%d" % (i + 1), 16 + i * 78, 494, 72, str(i + 1))]
    c += [text("OCTAVE WALK", 16, 560, 18), radio("arp_walk", 150, 552, 420, h=42), hl("arp_human", 600, 548, 330, "HUMANIZE"), hl("arp_chain", 940, 548, 324, "CHAIN")]
    q = [("ARP", ["arp_on", "latch", "arp_mode", "arp_rate", "arp_oct", "note_len", "arp_steps", "arp_walk"]),
         ("STEPS 1-8", ["step%d" % (i + 1) for i in range(8)]), ("STEPS 9-16", ["step%d" % (i + 9) for i in range(8)]),
         ("STEP OCT 1-8", ["stepo%d" % (i + 1) for i in range(8)]), ("STEP OCT 9-16", ["stepo%d" % (i + 9) for i in range(8)]),
         ("HUMAN", ["arp_human", "arp_chain"])]
    return c, q


def page_fx():
    c = [title("DRIVE", 16, 70, 300), hl("drive", 16, 92, 300, "AMOUNT"), hl("drive_col", 16, 146, 300, "COLOUR"),
         title("REVERB", 16, 214, 300), hl("rev_size", 16, 236, 300, "SIZE"), hl("rev_col", 16, 290, 300, "COLOUR"),
         title("FILTERS", 16, 358, 300), hl("mres", 16, 380, 300, "MASTER RESONANCE")]
    c += [title("CHORUS", 332, 70, 300), hl("ch_speed", 332, 92, 300, "SPEED"), hl("ch_depth", 332, 146, 300, "DEPTH"),
          hl("ch_col", 332, 200, 300, "COLOUR"), tog("ch_sync", 332, 262, 300, "SYNC TO TEMPO")]
    c += [title("DELAY", 648, 70, 300), hl("dly_time", 648, 92, 300, "TIME"), hl("dly_fb", 648, 146, 300, "FEEDBACK"),
          tog("dly_sync", 648, 206, 142, "SYNC"), btn("tap", 806, 206, 142, "TAP"),
          text("DIVISION", 648, 262, 18), popup("dly_div", 648, 284, 300, "", list_at=(648, 330), cols=2, ow=150),
          text("MODE", 648, 354, 22), radio("dly_mode", 648, 378, 300, h=42),
          tog("dly_hold", 648, 430, 142, "HOLD"), tog("dly_add", 806, 430, 142, "ADD"), tog("dly_rev", 648, 480, 300, "REVERSE")]
    c += mixer(980, 70)
    q = [("FX", ["drive", "drive_col", "ch_speed", "ch_depth", "ch_col", "dly_time", "dly_fb", "rev_size",
                 "rev_col", "mres", "mix_rev", "mix_ch", "mix_dly", "volume", "dly_div", "ch_sync"]),
         ("DELAY", ["dly_mode", "dly_hold", "dly_add", "dly_rev"])]
    return c, q


def page_image():
    c = [title("PICTURE", 16, 70, 1248), field("file", 16, 92, 560, "FILE", h=56),
         btn("load", 592, 92, 200, "LOAD", h=56), btn("rescan", 808, 92, 200, "RESCAN", h=56)]
    for i in range(4):
        n, x = i + 1, 16 + i * 316
        c += [title("MOD %d" % n, x, 170, 300),
              popup("im%d_src" % n, x, 194, 300, "FROM", h=48, list_at=(x, 246), cols=2, ow=140, oh=44),
              popup("im%d_to" % n, x, 254, 300, "TO", h=48, list_at=(x, 306), cols=2, ow=140, oh=44),
              hl("im%d_amt" % n, x, 316, 300, "AMOUNT"), hl("im%d_smooth" % n, x, 374, 300, "SMOOTH"),
              {"kind": "meter", "key": "im_m%d" % n, "x": x, "y": 436, "w": 300, "label": "NOW"}]
    q = [("IMAGE", ["im%d_amt" % (i + 1) for i in range(4)] + ["im%d_smooth" % (i + 1) for i in range(4)] +
          ["im%d_src" % (i + 1) for i in range(4)] + ["im%d_to" % (i + 1) for i in range(4)])]
    return c, q


def page_mode():
    """0.8: the MODE page. Its eight knobs are named by the current mode (live text); LAYER picks the next eight."""
    c = [title("MODE", 16, 70, 608), popup("sim_mode", 16, 92, 400, "MODE", h=48, list_at=(16, 144), cols=4, ow=158, oh=44),
         text("LAYER", 440, 104, 22), radio("sim_layer", 520, 92, 168, h=48)]
    for i in range(8):
        x, y = 16 + (i % 4) * 316, 168 + (i // 4) * 84
        c += [{"kind": "livetext", "key": "mn%d" % (i + 1), "x": x, "y": y, "w": 300}, hl("mk%d" % (i + 1), x, y + 6, 300, " ", big=True)]
    c += [title("TRAVEL", 16, 350, 608), popup("orb_dir", 16, 372, 300, "DIRECTION", h=48, list_at=(16, 424), cols=2, ow=150, oh=44)]
    c += [title("SHAPE NUMBERS  (MATHS SHAPES)", 16, 440, 1248)]
    c += [hl("sh_a", 16, 466, 300, "SHAPE A", big=True), hl("sh_b", 332, 466, 300, "SHAPE B", big=True),
          hl("sh_c", 648, 466, 300, "SHAPE C", big=True), hl("sh_d", 964, 466, 300, "SHAPE D", big=True)]
    q = [("MODE", ["mk%d" % (i + 1) for i in range(8)]), ("SHAPE", ["sh_a", "sh_b", "sh_c", "sh_d", "orb_dir", "sim_mode",
                                                                  "sim_layer"])]
    return c, q


def page_voices():
    """0.8: each corner's voice: VADER's own overtone generators (SYNTH) or one of the 14 voices from LXRXXL ZYNTH, with its
    four character controls (named live by the voice)."""
    c, q = [], []
    for ci, cl in enumerate("abcd"):
        x = 16 + ci * 316
        c += [title("CORNER " + cl.upper(), x, 70, 300), popup(cl + "_eng", x, 92, 300, "VOICE", h=48,
                                                              list_at=(x if ci < 2 else x - 100, 144), cols=3, ow=100, oh=44)]
        for k in range(4):
            y = 160 + k * 84
            c += [{"kind": "livetext", "key": "cn%d" % (ci * 4 + k + 1), "x": x, "y": y, "w": 300},
                  hl("%s_v%d" % (cl, k + 1), x, y + 6, 300, " ", big=True)]
        q.append(("VOICE " + cl.upper(), [cl + "_eng"] + ["%s_v%d" % (cl, k + 1) for k in range(4)]))
    return c, q


def page_play():
    """0.8: playing without keys: SELF PLAY (the orbiter picks notes from a scale) and the DRONE (a chord held on)."""
    A, B = 16, 440
    c = [title("SELF PLAY", A, 70, 400), tog("self_play", A, 92, 400, "SELF PLAY", h=48),
         text("SCALE", A, 156, 22), popup("self_scale", A + 110, 146, 290, "", h=48, list_at=(A, 198), cols=2, ow=200, oh=44),
         hl("self_root", A, 260, 400, "ROOT NOTE", big=True), hl("self_range", A, 330, 400, "OCTAVES", big=True),
         text("RATE", A, 408, 22), text("(the ARP RATE sets the speed)", A + 90, 410, 18)]
    c += [title("DRONE", B, 70, 400), tog("drone", B, 92, 400, "DRONE", h=48),
          text("CHORD", B, 156, 22), popup("drone_chord", B + 110, 146, 290, "", h=48, list_at=(B, 198), cols=2, ow=200, oh=44)]
    c += [title("VOICE MODE", A, 470, 400), radio("voice_mode", A, 492, 400, h=48),
          hl("voice_cap", A, 556, 400, "VOICE LIMIT", big=True),
          {"kind": "livetext", "key": "cpu", "x": B, "y": 420, "w": 400}]
    c += [title("RANDOM SOUND", B, 260, 400), btn("randomize", B, 282, 192, "RANDOMIZE", h=48), btn("undo_random", B + 208, 282, 192, "UNDO", h=48),
          hl("rand_amt", B, 346, 400, "HOW FAR", big=True)]
    q = [("PLAY", ["self_play", "self_scale", "self_root", "self_range", "drone", "drone_chord", "voice_mode", "voice_cap", "rand_amt"]),
         ("RANDOM", ["randomize", "undo_random"])]
    return c, q


def page_samples():
    """0.8: one sample per corner (and each corner's GEN 2 ratio and limiter) (a WAV from the Samples folder, "VADER Samples" on a card or drive): played one-shot at the
    root note, or as a loop over 1-4 bars of the tempo."""
    c, q = [], []
    for ci, cl in enumerate("abcd"):
        x = 16 + ci * 316
        c += [title("CORNER " + cl.upper(), x, 70, 300),
              hl("smp_" + cl, x, 92, 300, "SAMPLE", big=True),
              text("MODE", x, 180, 22),
              popup("smp_len_" + cl, x, 202, 300, "", h=48, list_at=(x if ci < 2 else x - 100, 254), cols=1, ow=300, oh=44),
              hl("smp_root_" + cl, x, 272, 300, "ROOT NOTE", big=True),
              title("GEN 2 RATIO", x, 350, 300),
              popup(cl + "_rmode", x, 372, 300, "", h=48, list_at=(x if ci < 2 else x - 100, 424), cols=1, ow=300, oh=44),
              hl(cl + "_ratio", x, 440, 300, "RATIO", big=True),
              hl(cl + "_lim", x, 516, 300, "LIMITER", big=True)]
        q.append(("SAMPLE " + cl.upper(), ["smp_" + cl, "smp_len_" + cl, "smp_root_" + cl, cl + "_rmode", cl + "_ratio", cl + "_lim"]))
    return c, q


def page_sample_edit():
    """0.8: three controls for the sample of whichever corner is selected: where it starts, where it ends, and how much of
    that part plays."""
    c = [title("CORNER SELECT", 16, 70, 600), radio("corner", 16, 92, 600, h=48),
         title("SAMPLE WINDOW  (THE SELECTED CORNER)", 16, 170, 1248),
         hl("smp_st", 16, 196, 400, "START", big=True), hl("smp_en", 432, 196, 400, "END", big=True),
         hl("smp_ln", 848, 196, 400, "LENGTH", big=True),
         text("START and END cut the sample; LENGTH plays only that part of the cut (loops fit it to the bars).", 16, 290, 22)]
    q = [("SAMPLE", ["smp_st", "smp_en", "smp_ln", "corner"])]
    return c, q


def page_travel():
    """0.8: how the orbiter travels and how its shape is bent: speed shaping, the part of the path used, stretch, skew, the
    centre point, the Warp pad, Focus (steer by hand), aftertouch."""
    A, B, C = 16, 440, 864
    c = [title("SPEED", A, 70, 400),
         hl("spd_lfo_rate", A, 92, 192, "LFO RATE", big=True), hl("spd_lfo_depth", A + 208, 92, 192, "LFO DEPTH", big=True),
         text("CURVE", A, 166, 22), popup("spd_curve", A + 110, 156, 290, "", h=48, list_at=(A, 208), cols=2, ow=200, oh=44),
         hl("corner_stop", A, 268, 400, "STOP AT CORNERS", big=True),
         title("PART OF THE PATH", A, 340, 400),
         hl("path_start", A, 362, 192, "START", big=True), hl("path_end", A + 208, 362, 192, "END", big=True),
         hl("soft_turn", A, 424, 400, "SOFT TURN", big=True),
         title("SUBORBIT BALL", A, 490, 400), tog("sub_link", A, 512, 400, "SAME DIRECTION AS ORBITER", h=48),
         popup("sub_dir", A, 568, 192, "", h=48, list_at=(A, 380), cols=1, ow=192, oh=44),
         popup("sub_shape", A + 208, 568, 192, "", h=48, list_at=(A + 208, 330), cols=1, ow=192, oh=44)]
    c += [title("BEND THE SHAPE", B, 70, 400),
          hl("stretch_x", B, 92, 192, "STRETCH X", big=True), hl("stretch_y", B + 208, 92, 192, "STRETCH Y", big=True),
          hl("skew", B, 154, 192, "SKEW", big=True), hl("morph", B + 208, 154, 192, "MORPH ROUND", big=True),
          title("CENTRE POINT  (MIDI CC 20, 21)", B, 232, 400),
          hl("centre_x", B, 254, 192, "CENTRE X", big=True), hl("centre_y", B + 208, 254, 192, "CENTRE Y", big=True)]
    c += [title("WARP PAD", C, 70, 400),
          hl("warp_x", C, 92, 192, "WARP X", big=True), hl("warp_y", C + 208, 92, 192, "WARP Y", big=True),
          btn("warp_reset", C, 154, 400, "WARP RESET", h=48),
          title("FOCUS  (STEER BY HAND)", C, 224, 400), tog("focus", C, 246, 400, "FOCUS", h=48),
          hl("focus_x", C, 308, 192, "FOCUS X", big=True), hl("focus_y", C + 208, 308, 192, "FOCUS Y", big=True),
          title("PLAYING", C, 386, 400), hl("at_sub", C, 408, 400, "AFTERTOUCH > SUBORBIT", big=True)]
    c += [title("MORE BALLS, ONE SOUND", B, 340, 400),
          hl("sound_balls", B, 362, 192, "SOUND BALLS", big=True), hl("kal_n", B + 208, 362, 192, "KALEIDOSCOPE", big=True),
          tog("kal_sound", B, 424, 400, "KALEIDOSCOPE SOUND", h=48)]
    q = [("SPEED", ["spd_lfo_rate", "spd_lfo_depth", "spd_curve", "corner_stop", "path_start", "path_end", "soft_turn", "at_sub"]),
         ("BEND", ["stretch_x", "stretch_y", "skew", "morph", "centre_x", "centre_y", "warp_x", "warp_y"]),
         ("FOCUS", ["focus", "focus_x", "focus_y", "warp_reset"]),
         ("BALLS", ["sound_balls", "kal_n", "kal_sound"]), ("SUBORBIT", ["sub_link", "sub_dir", "sub_shape"])]
    return c, q


PAGES = [page_main, page_corners, page_orbit, page_image, page_arp, page_fx, page_mode, page_voices, page_play, page_travel, page_samples, page_sample_edit]


def stripe():
    """the strip across every page: the name, the readout and its line, PRESET, TIMBRE A/B"""
    return [{"kind": "readout"}, field("preset", 672, 8, 260, "PRESET", h=44),
            radio("timbre", 944, 8, 320, labels=["TIMBRE A", "TIMBRE B"], h=44)]


# ------------------------------------------------------------------------------------------------ layout.conf
def write_layout():
    L = ["# VADER -- the tabs and their Q-Links for mpc-vst-plugins (GENERATED by tools/vader_skin.py --layout: edit that).",
         "# tools/vader_skin.py then builds every control of the six pages itself; the knobs below only carry the Q-Links.",
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


def fit(text_, w, px, bold=True, minpx=17):
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


def line_strip(w, frames=101, lh=None):
    """a line control's filmstrip: the thin track and the thick glowing line over it, from nothing to full"""
    Image, ImageDraw, _, _ = _pil()
    lh = lh or LINE_H
    strip = Image.new("RGBA", (w, lh * frames), (0, 0, 0, 0))
    for k in range(frames):
        fr = Image.new("RGBA", (w, lh), (0, 0, 0, 0))
        d = ImageDraw.Draw(fr)
        d.line([(2, lh // 2), (w - 3, lh // 2)], fill=TRACK + (255,), width=2)
        x = 2 + (w - 6) * k / (frames - 1)
        if x > 3:
            glow_line(fr, (3, lh // 2), (x, lh // 2), ACCENT, 6 if lh <= 16 else 8, glow=6)
        strip.alpha_composite(fr, (0, k * lh))
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


def edge_xy(k, nx, ny):
    """place k round a tile's edge (src/screen.c edge_point): top left to right, right top to bottom, bottom right to
    left, left bottom to top, each in the middle of its stretch"""
    w, h = TILE_W, TILE_H
    if k < nx:
        return ((k + 0.5) * w / nx, 0.0)
    k -= nx
    if k < ny:
        return (float(w), (k + 0.5) * h / ny)
    k -= ny
    if k < nx:
        return ((nx - 1 - k + 0.5) * w / nx, float(h))
    k -= nx
    return (0.0, (ny - 1 - k + 0.5) * h / ny)


def _dots(fr, p0, p1, colour, r, gap, alpha=1.0):
    """a piece drawn as a row of small round dots (the orbit line), gap px apart, evenly spaced from its ends"""
    Image, ImageDraw, _, _ = _pil()
    w, h = TILE_W, TILE_H
    L = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    n = max(1, int(round(L / gap)))
    m = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(m)
    for k in range(n):
        t = (k + 0.5) / n
        x, y = (p0[0] + (p1[0] - p0[0]) * t) * SS, (p0[1] + (p1[1] - p0[1]) * t) * SS
        d.ellipse([x - r * SS, y - r * SS, x + r * SS, y + r * SS], fill=int(255 * alpha))
    big = Image.new("RGBA", (w * SS, h * SS), colour + (0,))
    big.putalpha(m)
    fr.alpha_composite(big.resize((w, h), Image.LANCZOS))


def _seg(fr, p0, p1, colour, width, glow, alpha=1.0):
    """a straight piece from p0 to p1 (tile pixels, on its edges), drawn past both ends so the pieces of two tiles
    meet without a gap; the tile's edge cuts it"""
    Image, ImageDraw, ImageFilter, _ = _pil()
    w, h = TILE_W, TILE_H
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L = math.hypot(dx, dy) or 1.0
    ex, ey = dx / L * 2.0, dy / L * 2.0
    a = ((p0[0] - ex) * SS, (p0[1] - ey) * SS)
    b = ((p1[0] + ex) * SS, (p1[1] + ey) * SS)
    big = Image.new("RGBA", (w * SS, h * SS), colour + (0,))
    m = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(m).line([a, b], fill=255, width=max(1, int(round(width * SS))))
    halo = m.filter(ImageFilter.GaussianBlur(glow * SS)).point(lambda v: int(min(255, v * 1.6)))
    np_ = __import__("numpy")
    big.putalpha(Image.fromarray((np_.maximum(np_.asarray(m), np_.asarray(halo) * 0.55) * alpha).astype("uint8")))
    fr.alpha_composite(big.resize((w, h), Image.LANCZOS))


def _pairs(n):
    return [(i, j) for i in range(n) for j in range(i + 1, n)]   # src/screen.c pair_index order


SHAPE_LOOK = ((70, 160, 255), 1.7, 1.0, 1.0)    # the shape's pieces: colour, width, glow, opacity
ORBIT_LOOK = ((90, 160, 245), "dots", 1.15, 4.0, 0.85)   # the orbit line: many small dots, 4 px apart


def edge_strip(looks):
    """tiles of edge pieces (src/screen.c piece_frame): empty, then for each look a piece between every pair of edge
    places. The main tiles: the shape's look, then the orbit line's; the orbit line's own layer: its look alone."""
    Image, _, _, _ = _pil()
    w, h = TILE_W, TILE_H
    pairs = _pairs(G.EDGE_N)
    assert len(pairs) == G.EDGE_PAIRS
    strip = Image.new("RGBA", (w, h * (1 + len(looks) * len(pairs))), (0, 0, 0, 0))
    f = 1
    for look in looks:
        for i, j in pairs:
            fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            p0, p1 = edge_xy(i, G.EDGE_NX, G.EDGE_NY), edge_xy(j, G.EDGE_NX, G.EDGE_NY)
            if look[1] == "dots":
                _dots(fr, p0, p1, look[0], look[2], look[3], alpha=look[4])
            else:
                _seg(fr, p0, p1, look[0], look[1], look[2], alpha=look[3])
            strip.alpha_composite(fr, (0, f * h))
            f += 1
    return strip


# the light trail's brightnesses, newest to oldest: (colour, line width, glow, opacity)
TRAIL_LOOK = [((170, 255, 255), 2.6, 1.5, 1.0), ((80, 210, 255), 1.9, 1.2, 0.6)]


def trail_strip():
    """the light trail's layer (src/screen.c piece_frame): empty, then for each brightness every piece"""
    Image, _, _, _ = _pil()
    w, h = TILE_W, TILE_H
    strip = Image.new("RGBA", (w, h * G.TRAIL_FRAMES), (0, 0, 0, 0))
    pairs = _pairs(G.TRAIL_N)
    f = 1
    for lv in range(G.TRAIL_LV):
        col, width, glow, al = TRAIL_LOOK[lv]
        for i, j in pairs:
            fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            _seg(fr, edge_xy(i, G.TRAIL_NX, G.TRAIL_NY), edge_xy(j, G.TRAIL_NX, G.TRAIL_NY), col, width, glow, alpha=al)
            strip.alpha_composite(fr, (0, f * h))
            f += 1
    assert f == G.TRAIL_FRAMES
    return strip


# 0.4.3: the balls' colours, from small to large (the path point with ORBIT SIZE, the orbiter with SUBORBIT SIZE)
PATH_COLS = [(255, 225, 74), (255, 176, 32), (255, 128, 26), (255, 84, 40), (255, 52, 90), (255, 60, 170)]
ORB_COLS = [(70, 120, 255), (60, 175, 255), (125, 252, 255), (90, 255, 200), (110, 255, 140), (190, 255, 90)]


def ball_strip(d, colour):
    """a ball's cell (src/screen.c ball): empty, then one sharp round dot of diameter d at each of BALL_POS x BALL_POS
    places BALL_STEP px apart, its whole disc inside the frame (the frame reaches d/2 past the cell on every side)"""
    Image, ImageDraw, _, _ = _pil()
    r = d / 2
    fw = G.BALL_PITCH + d
    strip = Image.new("RGBA", (fw, fw * G.BALL_FRAMES), (0, 0, 0, 0))
    big = 8   # drawn 8 times larger, then shrunk: a clean round edge, no glow
    for iy in range(G.BALL_POS):
        for ix in range(G.BALL_POS):
            cx, cy = r + ix * G.BALL_STEP + G.BALL_STEP / 2, r + iy * G.BALL_STEP + G.BALL_STEP / 2
            m = Image.new("L", (fw * big, fw * big), 0)
            ImageDraw.Draw(m).ellipse([(cx - r) * big, (cy - r) * big, (cx + r) * big, (cy + r) * big], fill=255)
            fr = Image.new("RGBA", (fw, fw), colour + (0,))
            fr.putalpha(m.resize((fw, fw), Image.LANCZOS))
            strip.alpha_composite(fr, (0, (1 + iy * G.BALL_POS + ix) * fw))
    return strip


_turns = None


def turned_paths():
    """{("2d", shape, frame) or ("3d", j, tilt, frame): points}, turned by the engine's own code (tools/shape_dump.c)"""
    global _turns
    if _turns is None:
        import subprocess
        exe = os.path.join(ROOT, "build", "tools", "shape_dump")
        out = subprocess.run([exe, "turns"], capture_output=True, text=True, check=True).stdout.strip().splitlines()
        _turns = {}
        for line in out:
            f = line.split()
            n = 3 if f[0] == "2d" else 4
            _turns[(f[0],) + tuple(int(v) for v in f[1:n])] = [tuple(float(v) for v in q.split(",")) for q in f[n:]]
    return _turns


def picture_strip(frames_pts, scale):
    """one shape at one size: every frame (a list of path points, -1..1) drawn as the engine places it on the screen,
    in one rectangle that holds them all (cut to the screen). Returns (strip image, x, y, w, h)."""
    Image, ImageDraw, ImageFilter, _ = _pil()
    import numpy as np
    SW, SH = G.TILE_COLS * TILE_W, G.TILE_ROWS * TILE_H
    scr = [[(G.SQUARE_X + (x * scale + 1) * 0.5 * G.SQUARE, (y * scale + 1) * 0.5 * G.SQUARE) for x, y in pts] for pts in frames_pts]
    pad = 12
    x0 = max(0, int(min(p[0] for f in scr for p in f)) - pad)
    y0 = max(0, int(min(p[1] for f in scr for p in f)) - pad)
    x1 = min(SW, int(max(p[0] for f in scr for p in f)) + pad + 1)
    y1 = min(SH, int(max(p[1] for f in scr for p in f)) + pad + 1)
    w, h = x1 - x0, y1 - y0
    strip = Image.new("RGBA", (w, h * len(scr)), (0, 0, 0, 0))
    for i, f in enumerate(scr):
        m = Image.new("L", (w * SS, h * SS), 0)
        seq = [((px - x0) * SS, (py - y0) * SS) for px, py in f]
        ImageDraw.Draw(m).line(seq + [seq[0]], fill=255, width=int(2.4 * SS), joint="curve")
        halo = m.filter(ImageFilter.GaussianBlur(4 * SS))
        a = np.maximum(np.asarray(m, np.float32), np.minimum(255, np.asarray(halo, np.float32) * 2.2) * 0.5)
        big = Image.new("RGBA", (w * SS, h * SS), (58, 160, 255, 0))
        big.putalpha(Image.fromarray(a.astype("uint8")))
        strip.alpha_composite(big.resize((w, h), Image.LANCZOS), (0, i * h))
    return strip, x0, y0, w, h


def upic_frame(k):
    """your own shape's strip frame at size step k: the same as src/vader.h upic_frame"""
    import numpy as np
    r = float(np.float32(1.4142) * np.float32(k) / np.float32(G.U_SIZES) * np.float32(G.SQUARE) * np.float32(0.5) + np.float32(12))
    return 2 * math.ceil(min(r, G.SQUARE_X + G.SQUARE * 0.5)), 2 * math.ceil(min(r, G.SQUARE * 0.5))


# ------------------------------------------------------------------------------------------------ the QR code (0.6.0)
# light modules on the dark panel (an inverted code: phone cameras read it): neon blue dots on black, no white
QR_BG = (0, 0, 0)
QR_EYE_RING = (40, 150, 255)
QR_EYE_CORE = (30, 110, 255)
QR_MARK_INK = (70, 170, 255)


def qr_blue(t):
    """deep blue (t = 0) to bright cyan-blue (t = 1); never white"""
    t = min(1.0, max(0.0, t))
    return (int(20 + 50 * t), int(120 + 90 * t), 255)


def _qr_rng(seed):
    """a small deterministic generator, so a rebuild draws the same art"""
    x = (seed * 2654435761 + 97) & 0xffffffff
    while True:
        x = (x * 1103515245 + 12345) & 0x7fffffff
        yield x >> 8


def qr_shape(d, x, y, m, kind, col):
    """one dark module as a small flat shape in a m x m cell at (x, y)"""
    pad = 0.5
    x0, y0, x1, y1 = x + pad, y + pad, x + m - pad, y + m - pad
    if kind == 0:
        d.ellipse([x0, y0, x1, y1], fill=col)
    elif kind == 1:
        d.rounded_rectangle([x0, y0, x1, y1], radius=1.5, fill=col)
    elif kind == 2:
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        d.polygon([(cx, y0), (x1, cy), (cx, y1), (x0, cy)], fill=col)
    elif kind == 3:
        d.pieslice([x0 - (x1 - x0), y0 - (y1 - y0), x1, y1], 0, 90, fill=col)       # a quarter circle, bottom-right
    elif kind == 4:
        d.pieslice([x0, y0, x1 + (x1 - x0), y1 + (y1 - y0)], 180, 270, fill=col)   # a quarter circle, top-left
    else:
        d.rectangle([x0, y0 + 1.5, x1, y1 - 1.5], fill=col)                        # a bar


def qr_row_strip(row):
    """row's filmstrip: 256 frames of 8 modules (frame k: the byte k, the leftmost module its top bit), each dark module
    a shape of the row's own set; a light module shows the tile"""
    Image, ImageDraw, _, _ = _pil()
    m = G.QR_MOD
    SS = 4
    kinds = [0] * 8                                  # round dots only
    t = row / float(G.QR_N - 1)                      # the blue brightens down the code
    cols = [qr_blue(t + (i - 3.5) * 0.03) for i in range(8)]
    strip = Image.new("RGBA", (8 * m, m * 256), (0, 0, 0, 0))
    for k in range(256):
        fr = Image.new("RGBA", (8 * m * SS, m * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(fr)
        for i in range(8):
            if k & (0x80 >> i):
                qr_shape(d, i * m * SS, 0, m * SS, kinds[i], cols[i] + (255,))
        strip.alpha_composite(fr.resize((8 * m, m), Image.LANCZOS), (0, k * m))
    return strip


def qr_eye(d, x, y, m):
    """a finder pattern (7 x 7 modules at x, y) as a target: a blue rounded ring, a dark gap, a blue rounded centre"""
    d.rectangle([x, y, x + 7 * m - 1, y + 7 * m - 1], fill=QR_BG + (255,))     # opaque: it covers the dots drawn under it
    d.rounded_rectangle([x, y, x + 7 * m - 1, y + 7 * m - 1], radius=2 * m, fill=QR_EYE_RING + (255,))
    d.rounded_rectangle([x + m, y + m, x + 6 * m - 1, y + 6 * m - 1], radius=1.5 * m, fill=QR_BG + (255,))
    d.rounded_rectangle([x + 2 * m, y + 2 * m, x + 5 * m - 1, y + 5 * m - 1], radius=m, fill=QR_EYE_CORE + (255,))


def qr_eye_image():
    """the finder pattern as its own opaque picture, placed over the QR strips"""
    Image, ImageDraw, _, _ = _pil()
    m = G.QR_MOD
    SS = 4
    im = Image.new("RGBA", (7 * m * SS, 7 * m * SS), (0, 0, 0, 255))
    qr_eye(ImageDraw.Draw(im), 0, 0, m * SS)
    return im.resize((7 * m, 7 * m), Image.LANCZOS)


def logo_tile(size):
    """the logo sketch (art/vader_logo_sketch.png, transparent) cropped square round its triangle, on black"""
    Image, _, _, _ = _pil()
    im = Image.open(os.path.join(ROOT, "art", "vader_logo_sketch.png")).convert("RGBA")
    w, h = im.size
    cx, cy = int(w * 0.51), int(h * 0.42)
    side = min(w, int(h * 0.78))
    x0 = max(0, min(w - side, cx - side // 2))
    y0 = max(0, min(h - side, cy - side // 2))
    sq = im.crop((x0, y0, x0 + side, y0 + side)).resize((size, size), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 255))
    out.alpha_composite(sq)
    return out


def vader_mark(size):
    """VADER's mark: the corners' square with its four corner points, the orbit ring through it and the orbiter;
    art/logo.png is used instead when it exists"""
    Image, ImageDraw, _, _ = _pil()
    logo = os.path.join(ROOT, "art", "logo.png")
    if os.path.exists(logo):
        return Image.open(logo).convert("RGBA").resize((size, size), Image.LANCZOS)
    SS = 4
    s = size * SS
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s * 0.18, fill=QR_BG + (255,))   # the dark ground, so no modules show through
    q0, q1 = s * 0.2, s * 0.8
    d.rectangle([q0, q0, q1, q1], outline=QR_MARK_INK + (255,), width=max(1, s // 22))
    r = s * 0.085
    for (cx, cy) in ((q0, q0), (q1, q0), (q0, q1), (q1, q1)):
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(125, 215, 255, 255))
    d.ellipse([s * 0.26, s * 0.37, s * 0.74, s * 0.63], outline=QR_MARK_INK + (255,), width=max(1, s // 26))
    rr = s * 0.09
    d.ellipse([s * 0.63 - rr, s * 0.41 - rr, s * 0.63 + rr, s * 0.41 + rr], fill=(125, 215, 255, 255))
    return im.resize((size, size), Image.LANCZOS)


def glass():
    """the fixed glass over the screen: thin scanlines and darker edges"""
    Image, ImageDraw, _, _ = _pil()
    import numpy as np
    w, h = G.TILE_COLS * TILE_W, G.TILE_ROWS * TILE_H
    a = np.zeros((h, w), np.float32)
    a[::3, :] = 28
    yy, xx = np.mgrid[0:h, 0:w]
    v = np.clip(((xx - w / 2) / (w * 0.62)) ** 2 + ((yy - h / 2) / (h * 0.75)) ** 2 - 0.45, 0, 1) * 110
    a = np.clip(a + v, 0, 120)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    im.putalpha(Image.fromarray(a.astype("uint8")))
    # 0.5.0: two 2 x 2 markers, opaque, at the tiles' top-left and bottom-right (src/overlay.c MARK_TL / MARK_BR): the
    # live screen finds the box on the display by them, and hides itself while they are not both visible
    for (x0, y0, cols) in ((0, 0, [(23, 7, 91), (91, 7, 23), (7, 91, 23), (23, 91, 7)]),
                           (w - 2, h - 2, [(91, 23, 7), (7, 23, 91), (61, 7, 61), (7, 61, 61)])):
        for k, c in enumerate(cols):
            im.putpixel((x0 + k % 2, y0 + k // 2), c + (255,))
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
        elif k == "livetext":
            dk = define("vdLive_%s_%d" % (c["key"], c["w"]), [], [text_label("Value", 0, 0, c["w"], 28, 22.0, "ffffff",
                                                                          "left verticallyCentred", style="Bold")])
            kids.append(placed(dk, c["key"], sk.index[c["key"]], (c["x"], c["y"], c["w"], 28), False))
        elif k == "hline":
            x, y, w = c["x"], c["y"], c["w"]
            lab = c["label"] or sk.params[key]["name"]
            big = c.get("big")
            vw = 64 if big else 56   # the value, right of the line
            hh, lh, ly, lpx, vpx = (HLB, LINE_HB, 32, 22, 26.0) if big else (HL, LINE_H, LINE_Y, 18, 22.0)
            limg = sk.save(label_img(lab, w, lpx, h=28 if big else 24), "vd_lab_%s_%s_%d%s.png" % (t, key, w, "b" if big else ""))
            strip = sk.save(line_strip(w - vw - 4, lh=lh), "vd_line_%d_%d.png" % (w - vw - 4, lh))
            dk = define("vdLine_%s_%s_%d%s" % (t, key, w, "b" if big else ""), KNOB_ACTIONS,
                        [focus(w, hh), image(limg, 0, 0, w, 28 if big else 24),
                         text_label("Value", w - vw, 22 if big else 22, vw, 30 if big else 28, vpx, "ffffff", "right verticallyCentred"),
                         filmstrip(strip, 101, w - vw - 4, lh, "Horizontal", 0, ly)])
            kids.append(placed(dk, key, sk.index[key], (x, y, w, hh), True, cond=cond))
        elif k == "fader":
            x, y, w, h = c["x"], c["y"], c["w"], c["h"]
            big = c.get("big")
            limg = sk.save(label_img(c["label"], w, 22 if big else 18, INK, h=26 if big else 24), "vd_flab_%s_%d.png" % (key, w))
            fh = 120   # 101 frames: the strip stays under MPC's 12288 px
            fw = 36 if big else 28
            strip = sk.save(fader_strip(fw, fh), "vd_fader_%d_%d.png" % (fw, fh))
            dk = define("vdFader_%s_%d" % (key, w), KNOB_ACTIONS,
                        [focus(w, h), image(limg, 4, 0, w - 4, 26 if big else 22),
                         filmstrip(strip, 101, fw, fh, "Vertical", (w - fw) // 2, 28 if big else 26),
                         text_label("Value", 0, 28 + fh + 2, w, 30, 26.0 if big else 22.0, "ffffff")])
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
        elif k == "qr":
            m = G.QR_MOD
            tx, ty = QR_XY
            mx, my = QR_MOD_XY
            d.rounded_rectangle([tx, ty, tx + QR_TILE - 1, ty + QR_TILE - 1], radius=16, fill=QR_BG + (255,))   # a quiet dark ground
            for r in range(G.QR_N):
                strip = sk.save(qr_row_strip(r), "vd_qr_%02d.png" % r)
                dk = define("vdQr_%02d" % r, [], [filmstrip(strip, 256, 8 * m, m, "Vertical")], ignore=True)
                for b in range(G.QR_BYTES):
                    i = r * G.QR_BYTES + b
                    kids.append(placed(dk, "qr %d" % i, sk.index["q%03d" % i], (mx + b * 8 * m, my + r * m, 8 * m, m), False))
            eye = sk.save(qr_eye_image(), "vd_qr_eye.png")     # the three finder patterns, opaque, over the strips
            ek = define("vdQrEye", [], [image(eye, 0, 0, 7 * m, 7 * m)], ignore=True)
            for n_, (ex, ey) in enumerate(((0, 0), (G.QR_N - 7, 0), (0, G.QR_N - 7))):
                kids.append(placed(ek, "qr eye %d" % n_, sk.index["q000"], (mx + ex * m, my + ey * m, 7 * m, 7 * m), False))
            hole = G.QR_HOLE * m
            mark = sk.save(vader_mark(hole), "vd_mark.png")
            mk = define("vdMark", [], [image(mark, 0, 0, hole, hole)], ignore=True)
            kids.append(placed(mk, "mark", sk.index["q000"], (mx + (G.QR_N - G.QR_HOLE) // 2 * m, my + (G.QR_N - G.QR_HOLE) // 2 * m, hole, hole), False))
            # 0.6.1: over all of it, the logo: SHOW QR = LOGO (0) draws the opaque logo, QR (1) nothing, so the code shows;
            # a tap on it toggles (like any toggle button), and the setting is kept with the plugin
            lg = sk.save(logo_tile(QR_TILE), "vd_logo_on.png")
            lg_off = sk.save(Image.new("RGBA", (QR_TILE, QR_TILE), (0, 0, 0, 0)), "vd_logo_off.png")
            lk = define("vdQrSwitch", TOGGLE_ACTIONS, [focus(QR_TILE, QR_TILE), button(lg_off, lg, 1, 1, QR_TILE, QR_TILE)])
            kids.append(placed(lk, "qr show", sk.index["qr_show"], (QR_XY[0], QR_XY[1], QR_TILE, QR_TILE), True))
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
            # the shape's pictures: a strip per shape and size step (frames: its turns or spin), one shown at a time
            # (the engine's "shape_pic"; the frame from "pic_r6" / "pic_r8" / "pic_r12")
            n = G.PIC_COUNT + 1
            T_ = turned_paths()

            def put(sel, strip, x0, y0, w, h, nfr, name):
                fname = sk.save(strip, name)
                cond = "IndexedEnabling/%d/%d/Parameter %d" % (sel, n, sk.index["shape_pic"])
                if nfr == 1:
                    dk = define("vdPic_%d" % sel, [], [image(fname, 0, 0, w, h)], ignore=True)
                    pidx = sk.index["shape_pic"]
                else:
                    dk = define("vdPic_%d" % sel, [], [filmstrip(fname, nfr, w, h, "Vertical")], ignore=True)
                    pidx = sk.index["pic_r%d" % nfr]
                kids.append(placed(dk, "picture %d" % sel, pidx, (TILES_XY[0] + x0, TILES_XY[1] + y0, w, h), False, cond=cond))
            for idx in range(G.PIC_SHAPES):
                nfr = G.PIC_ROT[idx] if G.PIC_SYM[idx] else 1
                frames = [T_[("2d", idx, f)] for f in range(nfr)]
                for k_ in range(1, G.PIC_SIZES + 1):
                    sel = G.SEL_2D + idx * G.PIC_SIZES + (k_ - 1)
                    strip, x0, y0, w, h = picture_strip(frames, k_ / G.PIC_SIZES)
                    put(sel, strip, x0, y0, w, h, nfr, "vd_pic_%d.png" % sel)
            for j in range(G.P3_SHAPES):
                for k_ in range(1, G.P3_SIZES + 1):
                    for ti in range(G.P3_TILT):
                        sel = G.SEL_3D + (j * G.P3_SIZES + (k_ - 1)) * G.P3_TILT + ti
                        frames = [T_[("3d", j, ti, f)] for f in range(G.P3_YAW)]
                        strip, x0, y0, w, h = picture_strip(frames, k_ / G.P3_SIZES)
                        put(sel, strip, x0, y0, w, h, G.P3_YAW, "vd_pic_%d.png" % sel)
            for k_ in range(1, G.U_SIZES + 1):   # your own shape: empty here, VADER writes them when a file is loaded
                w, h = upic_frame(k_)
                x0, y0 = int(G.SQUARE_X + G.SQUARE / 2 - w / 2), int(G.SQUARE / 2 - h / 2)
                put(G.SEL_USER + k_ - 1, Image.new("RGBA", (w, h * G.U_ROT), (0, 0, 0, 0)), x0, y0, w, h, G.U_ROT,
                    "vd_user_%d.png" % k_)
        elif k == "tiles":
            # the light trail's layer, under the main tiles (the dots stay on top of it)
            tstrip = sk.save(trail_strip(), "vd_trail.png")
            tk = define("vdTrail", [], [filmstrip(tstrip, G.TRAIL_FRAMES, TILE_W, TILE_H, "Vertical")], ignore=True)
            for r in range(G.TILE_ROWS):
                for q in range(G.TILE_COLS):
                    i = r * G.TILE_COLS + q
                    kids.append(placed(tk, "r%d" % i, sk.index["r%03d" % i],
                                       (TILES_XY[0] + q * TILE_W, TILES_XY[1] + r * TILE_H, TILE_W, TILE_H), False))
            strip = sk.save(edge_strip([SHAPE_LOOK, ORBIT_LOOK]), "vd_tiles.png")
            dk = define("vdTile", [], [filmstrip(strip, G.TILE_FRAMES, TILE_W, TILE_H, "Vertical")], ignore=True)
            for r in range(G.TILE_ROWS):
                for q in range(G.TILE_COLS):
                    i = r * G.TILE_COLS + q
                    kids.append(placed(dk, "t%d" % i, sk.index["t%03d" % i],
                                       (TILES_XY[0] + q * TILE_W, TILES_XY[1] + r * TILE_H, TILE_W, TILE_H), False))
            # 0.4.3: the orbit line's second layer, where it crosses itself or the shape
            ostrip = sk.save(edge_strip([ORBIT_LOOK]), "vd_orbit.png")
            ok_ = define("vdOrbit", [], [filmstrip(ostrip, G.ORBIT_FRAMES, TILE_W, TILE_H, "Vertical")], ignore=True)
            for r in range(G.TILE_ROWS):
                for q in range(G.TILE_COLS):
                    i = r * G.TILE_COLS + q
                    kids.append(placed(ok_, "l%d" % i, sk.index["l%03d" % i],
                                       (TILES_XY[0] + q * TILE_W, TILES_XY[1] + r * TILE_H, TILE_W, TILE_H), False))
            gname = sk.save(glass(), "vd_glass.png")
            gk = define("vdGlass", [], [image(gname, 0, 0, G.TILE_COLS * TILE_W, G.TILE_ROWS * TILE_H)], ignore=True)
            kids.append(placed(gk, "glass", sk.index["shape_pic"], (TILES_XY[0], TILES_XY[1], G.TILE_COLS * TILE_W,
                                                                  G.TILE_ROWS * TILE_H), False))
            # 0.4.3: the balls, over the glass so they stay sharp. A layer of cells per colour, shown by the colour's
            # parameter (ball_o / ball_p); the orbiter's layers come twice, under the path point (ball_o 6..11: it
            # passes behind) and over it (0..5)
            def balls(prefix, d, colour, step_key, step_value, steps):
                fw = G.BALL_PITCH + d
                name = "vd_ball_%s_%d.png" % (prefix, step_value % G.BALL_COLS)
                strip = name if name in sk.files else sk.save(ball_strip(d, colour), name)
                bk = define("vdBall_%s_%d" % (prefix, step_value % G.BALL_COLS), [],
                            [filmstrip(strip, G.BALL_FRAMES, fw, fw, "Vertical")], ignore=True)
                cond = "IndexedEnabling/%d/%d/Parameter %d" % (step_value, steps, sk.index[step_key])
                for r in range(G.BALL_N):
                    for q in range(G.BALL_N):
                        i = r * G.BALL_N + q
                        x = TILES_XY[0] + G.SQUARE_X + q * G.BALL_PITCH - d // 2
                        y = TILES_XY[1] + r * G.BALL_PITCH - d // 2
                        kids.append(placed(bk, "%s%d %d" % (prefix, step_value, i), sk.index["b%s%02d" % (prefix, i)],
                                           (x, y, fw, fw), False, cond=cond))
            for k_ in range(G.BALL_COLS):
                balls("o", G.BALL_OD, ORB_COLS[k_], "ball_o", G.BALL_COLS + k_, 2 * G.BALL_COLS)
            for k_ in range(G.BALL_COLS):
                balls("p", G.BALL_PD, PATH_COLS[k_], "ball_p", k_, G.BALL_COLS)
            for k_ in range(G.BALL_COLS):
                balls("o", G.BALL_OD, ORB_COLS[k_], "ball_o", k_, 2 * G.BALL_COLS)
        elif k == "meter":   # an IMAGE MOD's source, live
            x, y, w = c["x"], c["y"], c["w"]
            d.text((x, y + 12), c["label"], font=font(20), fill=WHITE + (255,), anchor="lm")
            strip = sk.save(meter_strip(w - 64, 14, colour=CYAN), "vd_meter_%d.png" % (w - 64))
            dk = define("vdMeter_%d" % (w - 64), [], [filmstrip(strip, 101, w - 64, 14, "Horizontal")], ignore=True)
            kids.append(placed(dk, c["key"], sk.index[c["key"]], (x + 64, y + 5, w - 64, 14), False))
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
    from PIL import Image
    tall = [(f, Image.open(os.path.join(skin, f)).size) for f in sk.files if f.endswith(".png")]
    over = [f for f, (w_, h_) in tall if h_ > 12288 or w_ > 12288]
    if over:
        sys.exit("vader_skin: pictures over MPC's 12288 px: %s" % ", ".join(over))
    print("vader_skin: pictures take %.0f MB once loaded" % (sum(w_ * h_ * 4 for _, (w_, h_) in tall) / 1e6))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--layout":
        write_layout()
    elif len(sys.argv) > 1:
        main(sys.argv[1])
    else:
        sys.exit(__doc__)
