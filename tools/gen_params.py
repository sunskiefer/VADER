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
SHAPES += ["N-GON", "BUTTERFLY", "SUPERFORMULA", "LISSAJOUS", "ROSE", "LIMACON", "CARDIOID", "SPIROGRAPH", "ARCHIMEDES",
           "FERMAT", "HYPERBOLIC", "LITUUS", "LOG SPIRAL", "GOLDEN SPIRAL", "EULER SPIRAL", "TORUS KNOT", "MOBIUS",
           "HARMONOGRAPH", "PI LISSAJOUS", "EPICYCLES", "PULSE", "ADSR PATH"]
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
    ("preset", "PRESET", 0, 0, 21, None, True),
]
TRIGGERS = [("tap", "TAP"), ("load", "LOAD"), ("rescan", "RESCAN")]
# 0.2.0, appended at the end (saved projects keep every earlier index)
GLOBAL_LATE = [("user_slot", "MY PRESET", 1, 1, 999, None, True)]
GLOBAL_LATE2 = [("qr_show", "SHOW QR", 0, 0, 0, ["LOGO", "QR"], False)]   # 0.6.1, appended last: the MAIN picture, tapped: the logo or the QR code
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
# 0.8 (appended): the shape numbers of the maths shapes and the orbiter's way of travelling
SIM_MODES = ["PATH", "CHASE", "RECAMAN", "METEOR RAIN", "KEPLER", "ROSETTE", "FIGURE 8", "LORENZ", "ROSSLER", "THOMAS",
             "AIZAWA", "PENDULUM", "BOUNCE", "RIPPLE"]
# what the eight knobs of a mode's first layer do (the knobs' live names); src/modes.c reads the same
MODE_KNOBS = [['', '', '', '', '', '', '', ''], ['SPEED', 'BALLS', 'DIRECTION', 'PULL', 'SPREAD', 'TURN', 'SIZE', 'RESET'], ['STEPS/S', 'SCALE', 'AXIS', 'ARC', 'RANGE', 'SEED', 'SIZE', 'RESET'], ['FALL', 'RATE', 'SPREAD', 'SPEED', 'LIFE', 'SEED', 'SIZE', 'TURN'], ['SPEED', 'ECCENTRIC', 'PRECESS', 'SIZE', 'TILT', 'WELL', 'TURN', 'PHASE'], ['SPEED', 'ECCENTRIC', 'PRECESS', 'SIZE', 'TILT', 'WELL', 'TURN', 'PHASE'], ['SPEED', 'SIZE', 'TURN', 'OFFSET', 'BALLS', 'SEED', 'SPREAD', 'RESET'], ['SPEED', 'SIGMA', 'RHO', 'BETA', 'SIZE', 'TURN', 'BALLS', 'SEED'], ['SPEED', 'A', 'B', 'C', 'SIZE', 'TURN', 'BALLS', 'SEED'], ['SPEED', 'DAMP', 'SIZE', 'TURN', 'BALLS', 'SEED', 'SPREAD', 'RESET'], ['SPEED', 'A', 'B', 'C', 'SIZE', 'TURN', 'BALLS', 'SEED'], ['SPEED', 'LENGTH 1', 'LENGTH 2', 'MASS', 'DAMP', 'SEED', 'SIZE', 'KICK'], ['SPEED', 'GRAVITY', 'ELASTIC', 'BALLS', 'ANGLE', 'SEED', 'SIZE', 'KICK'], ['SPEED', 'WAVES', 'DECAY', 'SPREAD', 'RATE', 'SEED', 'SIZE', 'TURN']]
# 0.8: the corners' voice engines from LXRXXL ZYNTH. (name, ZYNTH engine id, the four ZYNTH controls (0-based V index) shown
# per corner, their names). "SYNTH" (0) is VADER's own two overtone generators.
ENGINE_TAB = [
    ("ANALOG", 0, [0, 1, 4, 2], ["WAVE", "PULSE WIDTH", "SYNC", "DETUNE"]),
    ("TRIO", 1, [0, 3, 4, 1], ["WAVE", "DETUNE", "LEVEL 2", "OSC 2 INTERVAL"]),
    ("WAVE", 5, [0, 1, 4, 3], ["BANK", "POS X", "WARP AMOUNT", "WARP TYPE"]),
    ("CHIP", 6, [0, 1, 5, 3], ["WAVE", "BITS", "SWEEP", "ARP"]),
    ("MACRO", 7, [0, 1, 2, 7], ["SHAPE", "TIMBRE", "COLOR", "CRUSH"]),
    ("WHEEL", 8, [0, 2, 3, 5], ["BASS", "BRIGHT", "PERC", "LESLIE"]),
    ("VOICE", 9, [0, 1, 2, 5], ["VOWEL", "FORMANT", "BREATH", "TONE"]),
    ("NOISE", 12, [0, 1, 2, 5], ["TYPE", "COLOR", "RESO", "GRIT"]),
    ("TONE", 13, [0, 1, 11, 5], ["WAVE", "FM AMOUNT", "FOLD", "DECIMATE"]),
    ("METAL", 14, [0, 1, 2, 4], ["CHARACTER", "SPREAD", "FM AMOUNT", "BAND"]),
    ("BAZZ", 15, [0, 1, 5, 6], ["OSC MODE", "WAVE POS", "TONE", "OSC 2 LEVEL"]),
    ("DRONE", 16, [0, 3, 2, 5], ["WAVE", "STACK", "DETUNE", "MOTION"]),
    ("TEXTURE", 17, [0, 1, 3, 4], ["SOURCE", "DENSITY", "COLOR", "RESO"]),
    ("FX", 18, [0, 2, 4, 5], ["TYPE", "SWEEP DEPTH", "CHAOS", "WOBBLE"]),
]
ENGINES = ["SYNTH"] + [e[0] for e in ENGINE_TAB]
SOUND_LATE3 = []
for _c in "abcd":
    SOUND_LATE3 += [("%s_eng" % _c, "%s ENGINE" % _c.upper(), 0, 0, 0, ENGINES, False)]
    SOUND_LATE3 += [("%s_v%d" % (_c, _k + 1), "%s CHARACTER %d" % (_c.upper(), _k + 1), 0.5, 0, 1, None, False) for _k in range(4)]
# 0.8, appended: the self-playing switch and the drone
SELF_SCALES = ["MAJOR", "MINOR", "DORIAN", "PENTATONIC", "MINOR PENT", "WHOLE TONE", "BLUES", "CHROMATIC"]
DRONE_CHORDS = ["ROOT", "ROOT + FIFTH", "ROOT + OCTAVE", "MINOR", "MAJOR", "SUS 4"]
GLOBAL_LATE3 = [("stepo%d" % (i + 1), "STEP %d OCTAVE" % (i + 1), 0, -2, 2, None, True) for i in range(16)] + [("self_play", "SELF PLAY", 0, 0, 0, ["OFF", "ON"], False),
                ("self_scale", "SELF SCALE", 0, 0, 0, SELF_SCALES, False),
                ("self_root", "SELF ROOT NOTE", 0, 0, 11, None, True),
                ("self_range", "SELF OCTAVES", 2, 1, 4, None, True),
                ("drone", "DRONE", 0, 0, 0, ["OFF", "ON"], False),
                ("drone_chord", "DRONE CHORD", 1, 0, 0, DRONE_CHORDS, False),
                ("voice_mode", "VOICE MODE", 0, 0, 0, ["POLY", "DUAL", "MONO"], False),
                ("rand_amt", "RANDOM AMOUNT", 0.6, 0, 1, None, False),
                ("arp_walk", "ARP OCTAVE WALK", 0, 0, 0, ["SPAN", "RISE", "RANDOM"], False),
                ("arp_human", "ARP HUMANIZE", 0.0, 0, 1, None, False),
                ("dly_mode", "DELAY MODE", 0, 0, 0, ["TAPE", "DIGITAL"], False),
                ("dly_hold", "DELAY HOLD", 0, 0, 0, ["OFF", "ON"], False),
                ("dly_add", "DELAY ADD", 0, 0, 0, ["OFF", "ON"], False),
                ("dly_rev", "DELAY REVERSE", 0, 0, 0, ["OFF", "ON"], False)]
GLOBAL_LATE5 = [("voice_cap", "VOICE LIMIT", 16, 1, 16, None, True)]   # 0.8: the most notes sounding (less CPU)
# 0.8, appended: samples, one per corner (a WAV from the Samples folders; OFF = the corner's own voice)
SMP_LENS = ["ONE SHOT", "LOOP 1 BAR", "LOOP 2 BARS", "LOOP 3 BARS", "LOOP 4 BARS"]
GLOBAL_LATE4 = ([("smp_%s" % c, "SAMPLE %s" % c.upper(), 0, 0, 499, None, True) for c in "abcd"]
                + [("smp_len_%s" % c, "SAMPLE %s MODE" % c.upper(), 0, 0, 0, SMP_LENS, False) for c in "abcd"]
                + [("smp_root_%s" % c, "SAMPLE %s ROOT NOTE" % c.upper(), 60, 0, 127, None, True) for c in "abcd"])
# 0.8, appended: the orbiter's travel (speed shaping, the part of the path used), its transform, the Warp pad, Focus
CURVES = ["LINEAR", "SLOW MIDDLE", "FAST MIDDLE", "PULSE"]
SOUND_LATE4 = [
    ("spd_lfo_rate", "SPEED LFO RATE", 0.3, 0, 1, None, False), ("spd_lfo_depth", "SPEED LFO DEPTH", 0.0, 0, 1, None, False),
    ("spd_curve", "SPEED CURVE", 0, 0, 0, CURVES, False), ("corner_stop", "STOP AT CORNERS", 0.0, 0, 1, None, False),
    ("path_start", "PATH START", 0.0, 0, 1, None, False), ("path_end", "PATH END", 1.0, 0, 1, None, False),
    ("soft_turn", "SOFT TURN", 0.0, 0, 1, None, False),
    ("stretch_x", "STRETCH X", 0.5, 0, 1, None, False), ("stretch_y", "STRETCH Y", 0.5, 0, 1, None, False),
    ("skew", "SKEW", 0.5, 0, 1, None, False), ("morph", "MORPH TO ROUND", 0.0, 0, 1, None, False),
    ("centre_x", "CENTRE X", 0.5, 0, 1, None, False), ("centre_y", "CENTRE Y", 0.5, 0, 1, None, False),
    ("warp_x", "WARP X", 0.5, 0, 1, None, False), ("warp_y", "WARP Y", 0.5, 0, 1, None, False),
    ("focus", "FOCUS", 0, 0, 0, ["OFF", "ON"], False), ("focus_x", "FOCUS X", 0.5, 0, 1, None, False),
    ("focus_y", "FOCUS Y", 0.5, 0, 1, None, False), ("at_sub", "AFTERTOUCH TO SUBORBIT", 0.0, 0, 1, None, False)]
TRIGGERS_LATE3 = [("warp_reset", "WARP RESET"), ("randomize", "RANDOMIZE"), ("undo_random", "UNDO RANDOM")]
DIRS = ["FORWARD", "REVERSE", "PING-PONG", "LOOP BACK"]
SOUND_LATE2 = [("sh_a", "SHAPE A", 0.5, 0, 1, None, False), ("sh_b", "SHAPE B", 0.5, 0, 1, None, False),
               ("sh_c", "SHAPE C", 0.5, 0, 1, None, False), ("sh_d", "SHAPE D", 0.0, 0, 1, None, False),
               ("orb_dir", "ORBIT DIRECTION", 0, 0, 0, DIRS, False),
               ("sim_mode", "MODE", 0, 0, 0, SIM_MODES, False),
               ("sim_layer", "LAYER", 0, 0, 0, ["1", "2", "3"], False)] + [
               ("mk%d" % (i + 1), "MODE KNOB %d" % (i + 1), 0.5, 0, 1, None, False) for i in range(8)]
POPUPS_LATE = ["im%d_%s" % (i + 1, f) for i in range(4) for f in ("src", "to")] 
POPUPS_LATE2 = ["orb_dir", "sim_mode"]
POPUPS_LATE3 = ["%s_eng" % c for c in "abcd"] + ["self_scale", "drone_chord", "spd_curve"] + ["smp_len_%s" % c for c in "abcd"]

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
    for k, *_ in SOUND + SOUND_LATE + SOUND_LATE2 + SOUND_LATE3 + SOUND_LATE4:
        snd_slots.append(k)
    glob_slots = [k for k, *_ in GLOBAL + GLOBAL_LATE + GLOBAL_LATE2 + GLOBAL_LATE3 + GLOBAL_LATE4 + GLOBAL_LATE5]

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
    # 0.6.1 (appended): the logo / QR switch on MAIN
    for k, name, d, lo, hi, opts, isint in GLOBAL_LATE2:
        add(k, name, d, lo, hi, opts, isint, "GLOB", glob_slots.index(k), None)
    # 0.8 (appended): shape numbers, direction
    for k, name, d, lo, hi, opts, isint in SOUND_LATE2:
        extra = {"dynamic_display": True} if not opts else None
        if k.startswith("mk"):
            extra["dynamic_name"] = True
        add(k, name, d, lo, hi, opts, isint, "SND", snd_slots.index(k), extra)
    for i in range(8):   # the live names of the mode knobs (text on the MODE page)
        add("mn%d" % (i + 1), "MODE KNOB %d NAME" % (i + 1), 0, 0, 1, None, False, "STR", 2 + i, {"display": "string"})
    for k, name, d, lo, hi, opts, isint in SOUND_LATE3:
        extra = {"dynamic_display": True} if not opts else None
        if k[2:4] == "v" + k[3:4] and not opts:
            extra["dynamic_name"] = True
        add(k, name, d, lo, hi, opts, isint, "SND", snd_slots.index(k), extra)
    for k, name, d, lo, hi, opts, isint in SOUND_LATE4:
        add(k, name, d, lo, hi, opts, isint, "SND", snd_slots.index(k), None if opts else {"dynamic_display": True})
    for k, name in TRIGGERS_LATE3:
        add(k, name, 0, 0, 1, None, False, "TRIG", 0, {"momentary": True})
    for k, name, d, lo, hi, opts, isint in GLOBAL_LATE3:
        add(k, name, d, lo, hi, opts, isint, "GLOB", glob_slots.index(k), None if opts else {"dynamic_display": True})
    for k, name, d, lo, hi, opts, isint in GLOBAL_LATE4:
        add(k, name, d, lo, hi, opts, isint, "GLOB", glob_slots.index(k), None if opts else {"dynamic_display": True})
    for k, name, d, lo, hi, opts, isint in GLOBAL_LATE5:
        add(k, name, d, lo, hi, opts, isint, "GLOB", glob_slots.index(k), None if opts else {"dynamic_display": True})
    add("cpu", "CPU READOUT", 0, 0, 1, None, False, "STR", 26, {"display": "string"})
    for i in range(16):   # the live names of the corners' character controls
        add("cn%d" % (i + 1), "CORNER CHARACTER %d NAME" % (i + 1), 0, 0, 1, None, False, "STR", 10 + i, {"display": "string"})
    for k in POPUPS_LATE2:
        params.append({"key": k + "__open", "name": k + " list", "default": 0, "min": 0, "max": 1, "popup_of": k})
        rows.append((k + "__open", "POPUP", 0, 0, 1, 0, 0, 0, None))
    for k in POPUPS_LATE3:
        params.append({"key": k + "__open", "name": k + " list", "default": 0, "min": 0, "max": 1, "popup_of": k})
        rows.append((k + "__open", "POPUP", 0, 0, 1, 0, 0, 0, None))
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
    L.append("#define NENG %d" % len(ENGINE_TAB))
    L.append("static const signed char ENG_ZID[NENG] = {%s};" % ", ".join(str(e[1]) for e in ENGINE_TAB))
    L.append("static const signed char ENG_SL[NENG][4] = {%s};" % ", ".join("{" + ", ".join(map(str, e[2])) + "}" for e in ENGINE_TAB))
    L.append("static const char *const ENG_KNOBS[NENG][4] = {%s};" % ", ".join(
        "{" + ", ".join('"%s"' % n for n in e[3]) + "}" for e in ENGINE_TAB))
    L.append("#define NMODES_G %d" % len(SIM_MODES))
    L.append("static const char *const MODE_KNOBS[NMODES_G][8] = {%s};" % ", ".join(
        "{" + ", ".join('"%s"' % n for n in row) + "}" for row in MODE_KNOBS))
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
