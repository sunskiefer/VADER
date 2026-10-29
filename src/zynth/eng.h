/* LXRXXL ZYNTH -- the voice engines: one per voice type, chosen per track with the VOICE selector.
 *
 * Every engine makes the raw tone of a track (one monophonic voice): the track's voice then runs it through the same
 * chain as LXRXXL BAZZLINE (FM LVL, SUB, NOISE, SWARM, DRIVE, FILTER, VCA). Each voice has twenty controls (the
 * VOICE page's faders V1..V20, 0..1, whose labels change with the voice): an engine reads V1..V12 itself; V13..V20
 * (OCTAVE, FINE, VIBRATO and a modulation envelope and the velocity aimed at the voice's own controls) are applied
 * around it by voice.c. Names, labels and defaults: tools/voices.py (src/voice_defs.h).
 *
 * Engines written here (C): ANALOG, TRIO, FM6, FM4, PHASE, CHIP, WHEEL, VOICE, GRAIN, NOISE, LXR TONE, LXR METAL and
 * BAZZ (LXRXXL BAZZLINE's oscillator). Engines from Mutable Instruments (MIT, third_party/mutable, through
 * mi_bridge.cc): MACRO (Braids' macro oscillator), WAVE (Plaits' wavetable engine) and PHYS (Plaits' string and modal
 * voices). None of the FM-1 firmwares' code is used: their voice list only inspired the choice of voices. */
#pragma once
#include <stdint.h>

enum {
    ENG_ANALOG, ENG_TRIO, ENG_FM6, ENG_FM4, ENG_PHASE, ENG_WAVE, ENG_CHIP, ENG_MACRO,
    ENG_WHEEL, ENG_VOICE, ENG_PHYS, ENG_GRAIN, ENG_NOISE, ENG_LXR_TONE, ENG_LXR_METAL, ENG_BAZZ,
    ENG_DRONE, ENG_TEXTURE, ENG_FX,   /* 0.2.0 */
    NUM_ENGINES
};
#define NUM_ENGINES_V1 16        /* 0.1.0's voices */
#define ENG_SLIDERS 20           /* the controls of a voice (V1..V20) */
#define ENG_SLIDERS_V1 6         /* 0.1.0's */
#define ENG_DEEP 12              /* the ones an engine reads itself (V1..V12) */
/* V13..V20: the play row (voice.c) */
enum { PR_OCT = 12, PR_FINE, PR_VIB, PR_ENV_ATK, PR_ENV_DEC, PR_ENV_A, PR_ENV_B, PR_VEL };
#define ENG_MAX_BLOCK 8          /* the voice renders its engine in blocks of at most this many frames */

/* what an engine gets for one block (the voice's control rate) */
typedef struct {
    float note;                  /* the pitch in semitones (MIDI), with glide, pitch envelope and pitch modulation */
    float freq;                  /* the same in Hz */
    float p[ENG_SLIDERS];        /* the controls, 0..1 (the modulator's WAVE destination, the modulation envelope and
                                    the velocity already added) */
    float vel, accent;           /* this note's velocity 0..1, accent 0/1 */
    float env;                   /* the amp envelope now, 0..1 (engines with their own envelopes follow it) */
    float fmIdx, fmRatio;        /* FM LVL as a phase-modulation index and FM RATIO (BAZZ uses them inside) */
    float bpm;
    uint8_t gate;                /* the note is held */
    uint8_t trig;                /* a new note started (not a slide) since the last block */
    uint8_t wave;                /* the panel's SQR / SAW / TRI switch (0, 1, 2): BAZZ's VCO wave */
} EngIn;

/* ---------------------------------------------------------------- states of the engines written in C */
typedef struct { uint32_t ph[2]; float last[2]; float pwmPh; float drift[2], driftT; uint32_t rnd; float fmZ; } AnalogSt;
typedef struct { uint32_t ph[3]; float lfoPh; float fb; float drift[3], driftT; uint32_t rnd; } TrioSt;
typedef struct { uint32_t ph[6]; float fb[2]; float menv[6]; float lvl[6]; float matk; } FmSt;
typedef struct { uint32_t ph[2]; float env; float z; uint8_t stage; float nz; uint32_t rnd; } PdSt;
typedef struct {
    uint32_t ph, ph2; uint32_t lfsr, lfsr2; float hold; float holdPh; float sweep; float arpPh; int arpI; int arpDir;
    float noiseOut, noiseOut2; float pwmPh; float blip; uint32_t rnd;
} ChipSt;
#define LESLIE_LEN 512
typedef struct {
    uint32_t ph[9];
    float scanPh; float scanBuf[64]; int scanW;
    float perc, click, clickLp;
    float rotH, rotD, spdH, spdD;
    float lsl[LESLIE_LEN];
    int lw;
    float split;
    uint32_t noise;
} WheelSt;
typedef struct {
    uint32_t ph, chPh[2]; float lp; float s1[6], s2[6]; float vibPh, vibAmt; uint32_t noise; float tilt; float jit, jitT;
    float batk;
} VoiceSt;
#define MAX_GRAINS 20
typedef struct { uint32_t ph; int32_t inc; float pos; float t, dt; uint8_t on; } Grain;
typedef struct { Grain g[MAX_GRAINS]; float next; uint32_t rnd; float lp; float scan; } GrainSt;
typedef struct {
    uint32_t rnd; float b[7]; float s1[3], s2[3]; float hp, lp; float dust; float hold, holdPh; uint32_t lfsr; float crack;
    float sweep; float pulsePh; uint32_t tonePh; float lc;
} NoiseSt;
typedef struct { uint32_t ph, phMod, phSync; float fmEnv; float hold; float holdPh; float penv; float click; uint32_t rnd; } LxrToneSt;
typedef struct { uint32_t ph[6]; float s1, s2; uint32_t noise; float hp; uint32_t tonePh; float shim[6], shimT; float hold, holdPh; } LxrMetalSt;
typedef struct { uint32_t ph, phMod, ph2; float drift, driftT; uint32_t rnd; float hold, holdPh; } BazzSt;
#define DRONE_OSC 7
typedef struct {
    uint32_t ph[DRONE_OSC + 2]; float drift[DRONE_OSC], driftTo[DRONE_OSC]; float motPh[DRONE_OSC]; float bloom; float lp;
    uint32_t rnd; float driftT;
} DroneSt;
typedef struct {
    uint32_t rnd; float s1[4], s2[4]; float lp, lp2, hp; float motion, motTo, motT; float wind, windTo, windT;
    uint32_t ph[6]; float dust; float comb[1024]; int cw; float rumble; float spark, sparkEnv; uint32_t sparkPh;
} TextureSt;
typedef struct {
    uint32_t ph, ph2, rnd; float t, rep; float wobPh; float hold, holdPh; float lp; float fbBuf[2048]; int fw; float chaos, chaosTo;
    float glitchNote; float noiseLp; float env;
} FxSt;

typedef struct {
    uint8_t type;                /* ENG_* */
    uint8_t fresh;               /* reset: the next block starts the engine from rest */
    union {
        AnalogSt an; TrioSt tr; FmSt fm; PdSt pd; ChipSt ch; WheelSt wh; VoiceSt vo; GrainSt gr; NoiseSt no;
        LxrToneSt lt; LxrMetalSt lm; BazzSt bz; DroneSt dr; TextureSt tx; FxSt fx;
    } u;
    void *mi;                    /* the Mutable engines' state (mi_bridge.cc), allocated once per engine slot */
} Eng;

/* ---------------------------------------------------------------- the voices' names and sliders (engines.c) */
typedef struct {
    const char *name;            /* the VOICE selector: the name as given ("LXR METAL") */
    const char *shortName;       /* three characters for the panel's display */
    const char *slider[ENG_SLIDERS];   /* the readout's names of V1..V20 */
    const char *label[ENG_SLIDERS];    /* the VOICE page's label windows ("MOD|INDEX": two lines) */
    float def[ENG_SLIDERS];      /* each control's default */
    int8_t timbre;               /* the control the modulator's WAVE destination moves */
    uint8_t steps[ENG_SLIDERS];  /* > 0: the control picks one of this many options */
    int8_t macro[2];             /* the front panel's TIMBRE and MORPH: the controls that change the voice the most */
    int8_t target[3];            /* the modulation envelope's A and B, the velocity's (V1..V12, 0-based) */
    float up[ENG_SLIDERS];       /* a 0.1.0 sound's value for a control it did not have (so it sounds as it did) */
    int8_t upCopy[ENG_SLIDERS];  /* >= 0: ... or the value of that control */
} EngDef;
extern const EngDef ENG_DEFS[NUM_ENGINES];

void eng_tables_init(void);                     /* the shared tables (sine, windows, grain waves): once */
void eng_reset(Eng *e, int type);               /* a new voice type, or the voice from rest */
void eng_render(Eng *e, const EngIn *in, float *out, int n);   /* writes n <= ENG_MAX_BLOCK frames */
int eng_step(int type, int slider, float v);    /* a stepped slider's option index */
void eng_value_text(int type, int slider, float v, char *buf, int n, int full);   /* the display (3 chars) / readout */
void eng_upgrade(int type, float *p);           /* a 0.1.0 sound's V7..V20 (V1..V6 set): as it sounded */
float eng_sub_octave(int type, const float *p);   /* the SUB oscillator's octave below the note (1 or 2) */
float eng_tone(int type, const float *p);       /* the post-filter TONE, 0..1 (1 = open) */

/* mi_bridge.cc: the Mutable Instruments engines (C++) */
void *mi_new(void);
void mi_free(void *m);
void mi_reset(void *m, int type);
void mi_render(void *m, int type, const EngIn *in, float *out, int n);
