#!/usr/bin/env python3
"""The single source of VADER's plugin parameters: writes params.json (for mpc-vst-plugins' gen_vst.py) and
src/vader_params.h (what each parameter is inside the engine).

    python3 tools/gen_params.py

MPC stores a project's values by parameter index: once released, parameters are only ever appended to this list.

Kinds (vader_params.h):
  SND    a sound parameter of the timbre being edited (TIMBRE A/B): the engine keeps one value per timbre
  FRONT  the main page's corner knobs (OVERTONE GEN 1/2, BLEND, CUTOFF, RESONANCE): they edit the corner picked with
         CORNER SELECT (or all four), of the timbre being edited
  GLOB   a parameter of the whole instance (arpeggiator, effects, mixer, play mode, the screen)
  TRIG   a button that does something (tap tempo, load a shape file, rescan the folder)
  STR    text the engine writes (the readout strip)
  DISP   a read-only screen parameter the engine moves (line tiles, the envelope line, corner bars)
  POPUP  a list's "open" flag, kept by the wrapper only (never sent to the engine)"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SHAPES = ["CIRCLE", "FIGURE 8", "STAR", "SQUARE", "TRIANGLE", "LISSA 3:2", "LISSA 5:4", "ROSE 3", "ROSE 5",
          "SPIRAL", "HEART", "ZIGZAG", "SPIRO", "CUBE", "ICOSA", "USER"]
MODES = ["MIX", "AM", "PM", "AM+PM", "PD", "RING", "FOLD"]
FTYPES = ["LP12", "LP24", "BP", "HP", "LADDER"]
FPOS = ["POST", "GEN 1", "GEN 2"]
DIVS = ["4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16"]
ARP_MODES = ["UP", "DOWN", "UP-DOWN", "RANDOM", "ORDER", "CHORD"]
ARP_RATES = ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"]
STEP = ["OFF", "ON", "ACCENT", "TIE"]
CORNERS = "ABCD"

# Screen geometry (tools/vader_art.py draws the same grid)
TILE_COLS, TILE_ROWS = 32, 16
TILE_W, TILE_H = 20, 18
# 0.4.3: a tile shows one straight piece of line from one point on its edge to another (src/screen.c). The points sit
# at fixed places along each edge (EDGE_NX on the top and bottom, EDGE_NY on the left and right), and a tile's
# neighbour has the same places on the edge they share, so a line leaves one tile exactly where it enters the next: no
# breaks, no steps. Frames: 0 empty, then every pair of points (i < j), numbered pair_index(i, j).
#   main tiles: the shape (when it has no picture), dim blue; then the same pieces faint and thin, for the orbit line
#   orbit tiles: the orbit line where it crosses itself or the shape (a tile holds one piece per layer), faint
#   trail tiles: the light trail, in TRAIL_LV brightnesses (newest: bright and thick, then softer), its own layer
EDGE_NX, EDGE_NY = 7, 6                      # 2.9 px apart on the top and bottom, 3 px on the sides
EDGE_N = 2 * (EDGE_NX + EDGE_NY)
EDGE_PAIRS = EDGE_N * (EDGE_N - 1) // 2
TRAIL_NX, TRAIL_NY, TRAIL_LV = 7, 6, 2       # 2.9 and 3 px apart (two brightnesses must fit in one strip)
TRAIL_N = 2 * (TRAIL_NX + TRAIL_NY)
TILE_FRAMES = 1 + 2 * EDGE_PAIRS
ORBIT_FRAMES = 1 + EDGE_PAIRS
TRAIL_FRAMES = 1 + TRAIL_LV * (TRAIL_N * (TRAIL_N - 1) // 2)
# the built-in 2D shapes as ready-drawn pictures: one per shape, size step and quarter turn
# 0.4.0: one filmstrip per shape and size step; its frames are the turns, over the shape's own symmetry (a STAR
# repeats every 1/5 turn, so 6 frames cover it in 12-degree steps). PIC_SYM: how many times a shape repeats in one
# turn (0: round, one frame); PIC_ROT: frames per repeat. Checked against the real paths by tools/vader_skin.py.
PIC_SHAPES, PIC_SIZES = 13, 12
PIC_SYM = [0, 2, 5, 4, 1, 1, 1, 3, 5, 1, 1, 2, 5]
PIC_ROT = [1, 6, 6, 6, 8, 8, 8, 6, 6, 8, 8, 6, 6]
# the 3D shapes (CUBE, ICOSA): frames = the spin (P3_YAW steps over its repeat), one strip per size and tilt step
P3_SHAPES, P3_SIZES, P3_TILT, P3_YAW = 2, 3, 3, 12
P3_SYM = [4, 2]
# your own shape: VADER draws these strips itself when a file is loaded (the skin ships them empty)
U_SIZES, U_ROT = 8, 8
SEL_2D = 1                                   # the screen-picture selector: 0 none | 2D | 3D | your own
SEL_3D = SEL_2D + PIC_SHAPES * PIC_SIZES
SEL_USER = SEL_3D + P3_SHAPES * P3_SIZES * P3_TILT
PIC_COUNT = SEL_USER + U_SIZES - 1
SQUARE_X = 176            # the corners' square on the screen: x 176..464, y 0..288 (true proportions)
SQUARE = 288
# 0.4.3: the two balls, each one sharp round dot in its own colour, which steps with its size (the orbiter with
# SUBORBIT SIZE, the path point with ORBIT SIZE). Each ball has its own layer of BALL_N x BALL_N cells over the corners'
# square, BALL_PITCH px apart; a cell's frames are the ball at BALL_POS x BALL_POS places BALL_STEP px apart inside it,
# drawn whole (the cell's picture reaches the ball's radius past its edges, so it is never cut). One cell at a time shows it.
# A layer per colour step (BALL_COLS), switched by BALL_O / BALL_P; the orbiter's has a copy under the path point
# (BALL_O = colour + BALL_COLS) for when it passes behind it. The balls stay whole inside the screen.
BALL_N, BALL_PITCH, BALL_STEP, BALL_POS, BALL_COLS = 8, 36, 3, 12, 6
BALL_FRAMES = 1 + BALL_POS * BALL_POS
BALL_OD, BALL_PD = 32, 22                  # the orbiter's and the path point's diameter (px)
ENV_COLS, ENV_ROWS = 32, 14
# 0.6.0: the QR code of the companion screen's address: version 4 (33 modules), level H (a logo may cover the middle)
QR_VER, QR_N = 4, 33
QR_BYTES = (QR_N + 7) // 8
QR_MOD = 7                 # a module's size on the skin (px)
QR_HOLE = 11               # modules under the logo (centred)
ENV_FRAMES = ENV_ROWS * (ENV_ROWS + 1) // 2

# ---- per corner (x4, keys a_..d_): (field, name, default, min, max, options, int)
CORNER = [
    ("og1", "GEN 1", 0.0, 0, 1, None, False),
    ("og2", "GEN 2", 0.25, 0, 1, None, False),
    ("oct", "OCTAVE", 0, -2, 2, None, True),
    ("tune", "GEN 2 TUNE", 0, -24, 24, None, True),
    ("fine", "GEN 2 FINE", 0, -50, 50, None, True),
    ("mode", "BLEND MODE", 0, 0, 0, MODES, False),
    ("blend", "BLEND", 0.3, 0, 1, None, False),
    ("cutoff", "CUTOFF", 0.75, 0, 1, None, False),
    ("reso", "RESONANCE", 0.15, 0, 1, None, False),
    ("ftype", "FILTER", 0, 0, 0, FTYPES, False),
    ("fpos", "FILTER ON", 0, 0, 0, FPOS, False),
    ("level", "LEVEL", 0.8, 0, 1, None, False),
]
# the four corners start as four different sounds
CORNER_DEFAULTS = {
    "a": {"og1": 0.0, "og2": 0.10},
    "b": {"og1": 0.30, "og2": 0.55, "mode": 2, "blend": 0.25},
    "c": {"og1": 0.62, "og2": 0.20, "tune": 12, "cutoff": 0.6},
    "d": {"og1": 0.85, "og2": 0.40, "mode": 4, "blend": 0.45, "tune": 7},
}
FRONT = [("og1", "OVERTONE GEN 1"), ("og2", "OVERTONE GEN 2"), ("blend", "OVERTONE BLEND"), ("cutoff", "FILTER CUTOFF"),
         ("reso", "FILTER RESONANCE")]

# ---- the rest of a timbre's sound: (key, name, default, min, max, options, int)
SOUND = [
    ("shape", "SHAPE", 2, 0, 0, SHAPES, False),
    ("orb_speed", "ORBIT SPEED", 0.35, 0, 1, None, False),
    ("orb_size", "ORBIT SIZE", 0.7, 0, 1, None, False),
    ("sub_size", "SUBORBIT SIZE", 0.3, 0, 1, None, False),
    ("sub_speed", "SUBORBIT SPEED", 0.45, 0, 1, None, False),
    ("detune", "DETUNE", 0.2, 0, 1, None, False),
    ("glide", "GLIDE", 0.0, 0, 1, None, False),
    ("rotate", "ROTATE", 0.0, 0, 1, None, False),
    ("spin", "3D SPIN", 0.2, 0, 1, None, False),
    ("tilt", "3D TILT", 0.3, 0, 1, None, False),
    ("orb_env", "ORBIT ENV", 0.0, -1, 1, None, False),
    ("retrig", "RETRIG", 0, 0, 0, ["OFF", "ON"], False),
    ("orb_sync", "ORBITER SYNC", 0, 0, 0, ["OFF", "ON"], False),
    ("orb_div", "ORBIT SYNC", 2, 0, 0, DIVS, False),
    ("attack", "ATTACK", 0.05, 0, 1, None, False),
    ("decay", "DECAY", 0.4, 0, 1, None, False),
    ("sustain", "SUSTAIN", 0.7, 0, 1, None, False),
    ("release", "RELEASE", 0.35, 0, 1, None, False),
    ("vib_speed", "VIBRATO SPEED", 0.4, 0, 1, None, False),
    ("vib_depth", "VIBRATO DEPTH", 0.0, 0, 1, None, False),
    ("trem_speed", "TREMOLO SPEED", 0.4, 0, 1, None, False),
    ("trem_depth", "TREMOLO DEPTH", 0.0, 0, 1, None, False),
    ("vt_sync", "VIB/TREM SYNC", 0, 0, 0, ["OFF", "ON"], False),
    ("octave", "TIMBRE OCTAVE", 0, -2, 2, None, True),
    ("t_volume", "TIMBRE VOLUME", 0.8, 0, 1, None, False),
    ("unison", "UNISON", 0, 0, 0, ["1", "2", "4", "8"], False),
    ("shape_osc", "SHAPE SOUND", 0.0, 0, 1, None, False),
    ("velocity", "VELOCITY", 0.5, 0, 1, None, False),
]

# ---- the whole instance: (key, name, default, min, max, options, int)
GLOBAL = [
    ("timbre", "EDIT TIMBRE", 0, 0, 0, ["A", "B"], False),
    ("keys", "KEYS", 0, 0, 0, ["LOWER", "AUTO", "UPPER"], False),
    ("tmode", "TIMBRE MODE", 0, 0, 0, ["DUAL", "SPLIT"], False),
    ("split", "SPLIT NOTE", 60, 24, 96, None, True),
    ("voices", "VOICES", 0, 0, 0, ["8", "16"], False),
    ("corner", "CORNER SELECT", 4, 0, 0, ["A", "B", "C", "D", "ALL"], False),
    ("alt", "ALT", 0, 0, 0, ["OFF", "ON"], False),
    ("motion", "MOTION", 1, 0, 0, ["OFF", "ON"], False),
    ("arp_on", "ARP ACTIVE", 0, 0, 0, ["OFF", "ON"], False),
    ("arp_mode", "ARP DIRECTION", 0, 0, 0, ARP_MODES, False),
    ("arp_rate", "ARP RATE", 3, 0, 0, ARP_RATES, False),
    ("arp_oct", "ARP OCTAVES", 1, 1, 4, None, True),
    ("note_len", "NOTE LENGTH", 0.5, 0, 1, None, False),
    ("latch", "LATCH", 0, 0, 0, ["OFF", "ON"], False),
    ("arp_steps", "STEPS", 16, 1, 16, None, True),
] + [("step%d" % (i + 1), "STEP %d" % (i + 1), 1, 0, 0, STEP, False) for i in range(16)] + [
    ("drive", "DRIVE AMOUNT", 0.0, 0, 1, None, False),
    ("drive_col", "DRIVE COLOUR", 0.5, 0, 1, None, False),
    ("ch_speed", "CHORUS SPEED", 0.3, 0, 1, None, False),
    ("ch_depth", "CHORUS DEPTH", 0.4, 0, 1, None, False),
    ("ch_col", "CHORUS COLOUR", 0.5, 0, 1, None, False),
    ("ch_sync", "CHORUS SYNC", 0, 0, 0, ["OFF", "ON"], False),
    ("dly_time", "DELAY TIME", 0.4, 0, 1, None, False),
    ("dly_fb", "DELAY FEEDBACK", 0.35, 0, 1, None, False),
    ("dly_sync", "DELAY SYNC", 1, 0, 0, ["OFF", "ON"], False),
    ("dly_div", "DELAY DIVISION", 5, 0, 0, DIVS, False),
    ("rev_size", "REVERB SIZE", 0.6, 0, 1, None, False),
    ("rev_col", "REVERB COLOUR", 0.5, 0, 1, None, False),
    ("mres", "MASTER RESONANCE", 0.0, 0, 1, None, False),
    ("mix_rev", "REVERB", 0.25, 0, 1, None, False),
    ("mix_ch", "CHORUS", 0.2, 0, 1, None, False),
    ("mix_dly", "DELAY", 0.15, 0, 1, None, False),
    ("volume", "VOLUME", 0.75, 0, 1, None, False),
    ("file", "SHAPE FILE", 0, 0, 499, None, True),
    ("preset", "PRESET", 0, 0, 15, None, True),
]
TRIGGERS = [("tap", "TAP"), ("load", "LOAD"), ("rescan", "RESCAN")]
# 0.2.0, appended at the end (saved projects keep every earlier index)
GLOBAL_LATE = [("user_slot", "MY PRESET", 1, 1, 999, None, True)]
TRIGGERS_LATE = [("save_user", "SAVE"), ("load_user", "LOAD PRESET")]

# 0.4.0, appended: the IMAGE page. A picture file loaded as the shape is also the IMAGE; four MODs each read a SOURCE
# under the orbiter (BRIGHT: the picture's brightness on a spectrogram colour scale; HUE: its colour round the colour
# wheel; COLOUR: how colourful; DEPTH: the slope the orbiter travels on, bright = high; TURN: how sharply the outline
# turns there; HEADING: which way the outline runs) and move a sound control (TO) by AMOUNT, glided by SMOOTH.
IMG_SRC = ["BRIGHT", "HUE", "COLOUR", "DEPTH", "TURN", "HEADING"]
IMG_TO = ["CUTOFF", "RESO", "OCTAVE", "PITCH", "GEN 1", "GEN 2", "BLEND", "LEVEL"]
IMG_DEF = [(0, 0), (4, 0), (1, 6), (3, 3)]   # (source, to) of MOD 1..4
SOUND_LATE = []
for i, (src, to) in enumerate(IMG_DEF):
    n = i + 1
    SOUND_LATE += [("im%d_src" % n, "MOD %d SOURCE" % n, src, 0, 0, IMG_SRC, False),
                   ("im%d_to" % n, "MOD %d TO" % n, to, 0, 0, IMG_TO, False),
                   ("im%d_amt" % n, "MOD %d AMOUNT" % n, 0.0, -1, 1, None, False),
                   ("im%d_smooth" % n, "MOD %d SMOOTH" % n, 0.3, 0, 1, None, False)]
POPUPS_LATE = ["im%d_%s" % (i + 1, f) for i in range(4) for f in ("src", "to")]

# lists shown as a field that opens (the wrapper keeps their "open" flags)
POPUPS = ["shape", "orb_div", "arp_mode", "arp_rate", "dly_div"] + ["%s_%s" % (c, f) for c in "abcd" for f in ("mode", "ftype")]


def entry(key, name, d, lo, hi, opts, isint):
    e = {"key": key, "name": name}
    if opts:
        e["options"] = list(opts)
        e["default"] = opts[int(d)]
    else:
        e.update({"default": d, "min": lo, "max": hi})
        if isint:
            e["display"] = "int"
    return e


def build():
    params, rows = [], []   # rows: (key, kind, slot, min, max, def, nopts, isint, opts)

    def add(key, name, d, lo, hi, opts, isint, kind, slot, extra=None):
        e = entry(key, name, d, lo, hi, opts, isint)
        if extra:
            e.update(extra)
        if kind in ("FRONT", "SND", "GLOB") and not opts and not isint:
            e["dynamic_display"] = True   # the engine writes the value as 0..100 (src/engine.c value_text)
        params.append(e)
        rows.append((key, kind, slot, lo, hi, d, len(opts or []), int(bool(isint)), opts))

    snd_slots = []   # sound slots: corner fields first (corner-major), then SOUND
    for c in "abcd":
        for f, *_ in CORNER:
            snd_slots.append("%s_%s" % (c, f))
    for k, *_ in SOUND + SOUND_LATE:
        snd_slots.append(k)
    glob_slots = [k for k, *_ in GLOBAL + GLOBAL_LATE]

    # 1. the main page's corner knobs
    for i, (f, name) in enumerate(FRONT):
        fi = [c[0] for c in CORNER].index(f)
        lo, hi, d = 0, 1, CORNER[fi][2]
        add("f_" + f, name, d, lo, hi, None, False, "FRONT", fi, {"dynamic_name": True})
    # 2. a timbre's sound
    for k, name, d, lo, hi, opts, isint in SOUND:
        extra = {"dynamic_display": True} if k == "shape" else None
        add(k, name, d, lo, hi, opts, isint, "SND", snd_slots.index(k), extra)
    # 3. the four corners
    for ci, c in enumerate("abcd"):
        for f, name, d, lo, hi, opts, isint in CORNER:
            d = CORNER_DEFAULTS[c].get(f, d)
            add("%s_%s" % (c, f), "%s %s" % (CORNERS[ci], name), d, lo, hi, opts, isint, "SND",
                snd_slots.index("%s_%s" % (c, f)))
    # 4. the instance
    for k, name, d, lo, hi, opts, isint in GLOBAL:
        extra = None
        if k == "file":
            extra = {"dynamic_display": True, "nudge_pct": 10}
        if k == "preset":
            extra = {"dynamic_display": True}
        if k in ("corner", "timbre"):   # the corner knobs' names ("GEN 1 [A]") follow them: ask MPC to redraw (wrapper #486)
            extra = {"refresh_display": True}
        add(k, name, d, lo, hi, opts, isint, "GLOB", glob_slots.index(k), extra)
    for k, name in TRIGGERS:
        add(k, name, 0, 0, 1, None, False, "TRIG", 0, {"momentary": True})
    add("readout", "READOUT", 0, 0, 1, None, False, "STR", 0, {"display": "string"})
    # 5. the screen (read-only)
    add("rd_line", "READOUT LINE", 0, 0, 100, None, True, "DISP", 0)
    for i, c in enumerate("abcd"):
        add("w" + c, "CORNER %s MIX" % CORNERS[i], 25, 0, 100, None, True, "DISP", 1 + i)
    for i in range(ENV_COLS):
        add("e%02d" % i, "ENV LINE %d" % (i + 1), 0, 0, ENV_FRAMES - 1, None, True, "DISP", 100 + i)
    for i in range(TILE_COLS * TILE_ROWS):
        add("t%03d" % i, "SCREEN %d" % (i + 1), 0, 0, TILE_FRAMES - 1, None, True, "DISP", 1000 + i)
    # 6. the lists' open flags
    for k in POPUPS:
        params.append({"key": k + "__open", "name": k + " list", "default": 0, "min": 0, "max": 1, "popup_of": k})
        rows.append((k + "__open", "POPUP", 0, 0, 1, 0, 0, 0, None))
    # 0.2.0 (appended): which ready-drawn shape picture the screen shows (0: none, the tiles draw the shape)
    add("shape_pic", "SHAPE PICTURE", 0, 0, PIC_COUNT, None, True, "DISP", 2000)
    for k, name, d, lo, hi, opts, isint in GLOBAL_LATE:
        add(k, name, d, lo, hi, opts, isint, "GLOB", glob_slots.index(k), {"dynamic_display": True, "nudge_pct": 10})
    for k, name in TRIGGERS_LATE:
        add(k, name, 0, 0, 1, None, False, "TRIG", 0, {"momentary": True})
    # 0.4.0 (appended): the screen pictures' turn frames; the IMAGE page
    for n in (6, 8, 12):
        add("pic_r%d" % n, "PICTURE TURN %d" % n, 0, 0, n - 1, None, True, "DISP", 2100 + n)
    for k, name, d, lo, hi, opts, isint in SOUND_LATE:
        extra = {"dynamic_display": True} if not opts else None
        add(k, name, d, lo, hi, opts, isint, "SND", snd_slots.index(k), extra)
    for i in range(4):
        add("im_m%d" % (i + 1), "MOD %d NOW" % (i + 1), 0, 0, 100, None, True, "DISP", 2200 + i)
    for i in range(TILE_COLS * TILE_ROWS):   # 0.4.2 (appended): the light trail's layer
        add("r%03d" % i, "TRAIL %d" % (i + 1), 0, 0, TRAIL_FRAMES - 1, None, True, "DISP", 4000 + i)
    for k in POPUPS_LATE:
        params.append({"key": k + "__open", "name": k + " list", "default": 0, "min": 0, "max": 1, "popup_of": k})
        rows.append((k + "__open", "POPUP", 0, 0, 1, 0, 0, 0, None))
    # 0.4.3 (appended): the balls' cells and colours
    for i in range(BALL_N * BALL_N):
        add("bo%02d" % i, "ORBITER %d" % (i + 1), 0, 0, BALL_FRAMES - 1, None, True, "DISP", 5000 + i)
    for i in range(BALL_N * BALL_N):
        add("bp%02d" % i, "PATH POINT %d" % (i + 1), 0, 0, BALL_FRAMES - 1, None, True, "DISP", 5100 + i)
    add("ball_o", "ORBITER COLOUR", 0, 0, 2 * BALL_COLS - 1, None, True, "DISP", 5200)
    add("ball_p", "PATH POINT COLOUR", 0, 0, BALL_COLS - 1, None, True, "DISP", 5201)
    for i in range(TILE_COLS * TILE_ROWS):   # the orbit line's second layer
        add("l%03d" % i, "ORBIT LINE %d" % (i + 1), 0, 0, ORBIT_FRAMES - 1, None, True, "DISP", 6000 + i)
    # 0.6.0: the companion screen's address (text) and its QR code on the MAIN page: QR_N rows of QR_BYTES bytes, each a
    # byte of 8 modules (a filmstrip of 256 frames), set once when the address is known
    add("web_url", "COMPANION SCREEN", 0, 0, 1, None, False, "STR", 1, {"display": "string"})
    for i in range(QR_N * QR_BYTES):
        add("q%03d" % i, "QR %d" % (i + 1), 0, 0, 255, None, True, "DISP", 7000 + i)
    return params, rows, snd_slots, glob_slots


def cf(v):
    """a C float literal"""
    t = "%.6g" % float(v)
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


def c_name(k):
    return k.upper().replace("__", "_")


def write():
    params, rows, snd, glob = build()
    json.dump({"params": params}, open(os.path.join(ROOT, "params.json"), "w"), indent=1)
    L = ["/* GENERATED by tools/gen_params.py: do not edit. VADER's parameters, in VST index order. */", "#pragma once", ""]
    L.append("enum { K_FRONT, K_SND, K_GLOB, K_TRIG, K_STR, K_DISP, K_POPUP };")
    L.append("#define NP %d" % len(rows))
    L.append("#define NSND %d" % len(snd))
    L.append("#define NGLOB %d" % len(glob))
    L.append("#define NCF %d   /* fields per corner */" % len(CORNER))
    L.append("#define TILE_COLS %d\n#define TILE_ROWS %d\n#define TILE_FRAMES %d" % (TILE_COLS, TILE_ROWS, TILE_FRAMES))
    for k in ("QR_VER", "QR_N", "QR_BYTES", "QR_MOD", "QR_HOLE", "TILE_W", "TILE_H", "EDGE_NX", "EDGE_NY", "EDGE_N", "EDGE_PAIRS", "ORBIT_FRAMES", "TRAIL_NX", "TRAIL_NY", "TRAIL_N", "TRAIL_LV", "TRAIL_FRAMES", "PIC_SHAPES",
              "PIC_SIZES", "PIC_COUNT", "SQUARE_X", "SQUARE", "P3_SHAPES", "P3_SIZES", "P3_TILT", "P3_YAW",
              "U_SIZES", "U_ROT", "SEL_2D", "SEL_3D", "SEL_USER", "BALL_N", "BALL_PITCH", "BALL_STEP", "BALL_POS",
              "BALL_COLS", "BALL_FRAMES", "BALL_OD", "BALL_PD"):
        L.append("#define %s %d" % (k, globals()[k]))
    L.append("static const unsigned char PIC_SYM[%d] = {%s};" % (PIC_SHAPES, ", ".join(map(str, PIC_SYM))))
    L.append("static const unsigned char PIC_ROT[%d] = {%s};" % (PIC_SHAPES, ", ".join(map(str, PIC_ROT))))
    L.append("static const unsigned char P3_SYM[%d] = {%s};" % (P3_SHAPES, ", ".join(map(str, P3_SYM))))
    L.append("#define ENV_COLS %d\n#define ENV_ROWS %d\n#define ENV_FRAMES %d" % (ENV_COLS, ENV_ROWS, ENV_FRAMES))
    L.append("enum { " + ", ".join("P_%s" % c_name(r[0]) for r in rows) + " };")
    L.append("enum { " + ", ".join("S_%s" % c_name(k) for k in snd) + " };")
    L.append("enum { " + ", ".join("G_%s" % c_name(k) for k in glob) + " };")
    L.append("enum { " + ", ".join("CF_%s" % c_name(f) for f, *_ in CORNER) + " };")
    # option lists
    seen = {}
    for r in rows:
        if r[8]:
            t = tuple(r[8])
            if t not in seen:
                seen[t] = "OPT%d" % len(seen)
                L.append("static const char *const %s[] = {%s};" % (seen[t], ", ".join('"%s"' % o for o in t)))
    L.append("typedef struct { const char *key; const char *name; unsigned char kind; short slot; float min, max, def; "
             "unsigned char nopts, isint; const char *const *opts; } vp_t;")
    L.append("static const vp_t VP[NP] = {")
    names = {p["key"]: p["name"] for p in params}
    for r in rows:
        key, kind, slot, lo, hi, d, nopts, isint, opts = r
        if nopts:
            lo, hi = 0, nopts - 1
        L.append('    {"%s", "%s", K_%s, %d, %s, %s, %s, %d, %d, %s},' % (
            key, names[key], kind, slot, cf(lo), cf(hi), cf(d), nopts, isint, seen[tuple(opts)] if opts else "0"))
    L.append("};")
    # defaults per sound / global slot
    sdef = [0.0] * len(snd)
    gdef = [0.0] * len(glob)
    for r in rows:
        if r[1] == "SND":
            sdef[r[2]] = float(r[5])
        elif r[1] == "GLOB":
            gdef[r[2]] = float(r[5])
    L.append("static const float SND_DEF[NSND] = {%s};" % ", ".join(cf(v) for v in sdef))
    L.append("static const float GLOB_DEF[NGLOB] = {%s};" % ", ".join(cf(v) for v in gdef))
    L.append("/* the parameter that shows each sound / global slot */")
    sp = [0] * len(snd)
    gp = [0] * len(glob)
    for i, r in enumerate(rows):
        if r[1] == "SND":
            sp[r[2]] = i
        elif r[1] == "GLOB":
            gp[r[2]] = i
    L.append("static const short SND_P[NSND] = {%s};" % ", ".join(map(str, sp)))
    L.append("static const short GLOB_P[NGLOB] = {%s};" % ", ".join(map(str, gp)))
    open(os.path.join(ROOT, "src", "vader_params.h"), "w").write("\n".join(L) + "\n")
    print("gen_params: %d parameters (%d sound slots, %d global)" % (len(rows), len(snd), len(glob)))


if __name__ == "__main__":
    write()
