/* LXRXXL ZYNTH -- the voice list (src/voice_defs.h, from tools/voices.py), the controls' texts, and the dispatch to
 * the engines. */
#include <stdio.h>
#include <string.h>
#include "eng_dsp.h"

float ENG_SIN[SIN_SIZE + 1];
float ENG_HANN[257];

/* the engines (eng_osc.c, eng_fm.c, eng_more.c; mi_bridge.cc for MACRO, WAVE and PHYS) */
void analog_render(AnalogSt *s, const EngIn *in, float *out, int n);
void trio_render(TrioSt *s, const EngIn *in, float *out, int n);
void chip_render(ChipSt *s, const EngIn *in, float *out, int n);
void phase_render(PdSt *s, const EngIn *in, float *out, int n);
void lxrtone_render(LxrToneSt *s, const EngIn *in, float *out, int n);
void lxrmetal_render(LxrMetalSt *s, const EngIn *in, float *out, int n);
void bazz_render(BazzSt *s, const EngIn *in, float *out, int n);
void fm6_render(FmSt *s, const EngIn *in, float *out, int n);
void fm4_render(FmSt *s, const EngIn *in, float *out, int n);
void wheel_render(WheelSt *s, const EngIn *in, float *out, int n);
void voice_fmt_render(VoiceSt *s, const EngIn *in, float *out, int n);
void grain_render(GrainSt *s, const EngIn *in, float *out, int n);
void noise_render(NoiseSt *s, const EngIn *in, float *out, int n);
void drone_render(DroneSt *s, const EngIn *in, float *out, int n);
void texture_render(TextureSt *s, const EngIn *in, float *out, int n);
void fx_render_voice(FxSt *s, const EngIn *in, float *out, int n);
void grain_tables_init(void);
void bazz_tables_init(void);

const EngDef ENG_DEFS[NUM_ENGINES] = {
#include "voice_defs.h"
};

static const char *const AN_INT[9] = {"0", "+3", "+4", "+5", "+7", "+12", "+19", "+24", "-12"};
static const char *const PD_WAVE[8] = {"SAW", "SQUARE", "PULSE", "DOUBLE SINE", "SAW PULSE", "RESO SAW", "RESO TRI",
                                       "RESO TRAP"};
static const char *const PD_WAVE3[8] = {"SAW", "SQR", "PLS", "DSN", "SPL", "RS1", "RS2", "RS3"};
static const char *const WT_WARP[4] = {"FOLD", "SHAPE", "CRUSH", "RING"};
static const char *const WT_WARP3[4] = {"FLD", "SHP", "CRS", "RNG"};
static const char *const CH_WAVE[7] = {"PULSE 12", "PULSE 25", "PULSE 50", "TRIANGLE", "SAW", "NOISE", "METAL NOISE"};
static const char *const CH_WAVE3[7] = {"P12", "P25", "P50", "TRI", "SAW", "NOI", "MTL"};
static const char *const CH_ARP[8] = {"OFF", "OCTAVE", "MAJOR", "MINOR", "MAJ 7", "MIN 7", "SUS 4", "POWER"};
static const char *const CH_ARP3[8] = {"OFF", "OCT", "MAJ", "MIN", "MA7", "MI7", "SU4", "PWR"};
const char *const MACRO_NAMES[44] = {
    "CSAW", "MORPH", "SAW SQUARE", "SINE TRI FOLD", "BUZZ", "SQUARE SUB", "SAW SUB", "SQUARE SYNC", "SAW SYNC",
    "TRIPLE SAW", "TRIPLE SQUARE", "TRIPLE TRI", "TRIPLE SINE", "TRIPLE RING", "SAW SWARM", "SAW COMB", "TOY",
    "FILTER LP", "FILTER PEAK", "FILTER BP", "FILTER HP", "VOSIM", "VOWEL", "VOWEL FOF", "HARMONICS", "FM",
    "FEEDBACK FM", "CHAOTIC FM", "PLUCKED", "BOWED", "BLOWN", "FLUTED", "STRUCK BELL", "STRUCK DRUM", "WAVETABLE",
    "WAVE MAP", "WAVE LINE", "WAVE CHORD", "FILTER NOISE", "TWIN PEAKS", "CLOCKED NOISE", "GRAIN CLOUD",
    "PARTICLES", "DIGITAL MOD"};
static const char *const MACRO3[44] = {
    "CSW", "MRP", "SSQ", "FLD", "BUZ", "SQS", "SWS", "SQY", "SWY", "3SW", "3SQ", "3TR", "3SN", "3RM", "SWM", "CMB",
    "TOY", "FLP", "FPK", "FBP", "FHP", "VSM", "VWL", "FOF", "HRM", "FM", "FFM", "CFM", "PLK", "BOW", "BLW", "FLT",
    "BEL", "DRM", "WTB", "WMP", "WLN", "WTC", "FNS", "TWN", "CKN", "CLD", "PRT", "DIG"};
static const char *const WH_PERC[5] = {"OFF", "2ND", "3RD", "2ND FAST", "3RD FAST"};
static const char *const WH_PERC3[5] = {"OFF", "2ND", "3RD", "2NF", "3RF"};
static const char *const PH_MODEL[2] = {"STRING", "MODAL"};
static const char *const PH_MODEL3[2] = {"STR", "MOD"};
static const char *const NO_TYPE[5] = {"WHITE", "PINK", "CRACKLE", "DIGITAL", "DUST"};
static const char *const NO_TYPE3[5] = {"WHT", "PNK", "CRK", "DIG", "DST"};
static const char *const LX_WAVE[4] = {"SINE", "TRIANGLE", "SAW", "SQUARE"};
static const char *const LX_WAVE3[4] = {"SIN", "TRI", "SAW", "SQR"};
const float LXR_FM_RATIOS[16] = {.25f, .5f, .75f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f, 5.f, 6.f, 7.f, 8.f, 11.f, 16.f};
const float FM4_RATIOS[20] = {.5f, .75f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 3.5f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f,
                              12.f, 13.f, 14.f, 16.f};
static const char *const BZ_MODE[3] = {"VCO", "FM", "WAVE"};
static const char *const BZ_OCT[5] = {"-2", "-1", "0", "+1", "+2"};
static const char *const BZ_SUB[2] = {"-1", "-2"};
static const char *const CH_AMODE[4] = {"UP", "DOWN", "UP DOWN", "RANDOM"};
static const char *const CH_AMODE3[4] = {"UP", "DWN", "U-D", "RND"};
static const char *const DR_STACK[8] = {"UNISON", "OCTAVES", "FIFTHS", "OCT + 5TH", "MINOR", "MAJOR", "SUS", "CLUSTER"};
static const char *const DR_STACK3[8] = {"UNI", "OCT", "5TH", "O+5", "MIN", "MAJ", "SUS", "CLS"};
const char *const FX_TYPES[8] = {"LASER", "ZAP", "RISER", "FALL", "SIREN", "ALARM", "GLITCH", "BOMB"};
static const char *const FX_TYPES3[8] = {"LSR", "ZAP", "RIS", "FAL", "SIR", "ALM", "GLT", "BMB"};

/* a 0.1.0 sound's V7..V20: the values that keep it sounding as it did */
void eng_upgrade(int type, float *p)
{
    int k;
    if (type < 0 || type >= NUM_ENGINES) return;
    for (k = ENG_SLIDERS_V1; k < ENG_SLIDERS; k++) {
        const int c = ENG_DEFS[type].upCopy[k];
        p[k] = c >= 0 ? p[c] : ENG_DEFS[type].up[k];
    }
}

static void signed_text(float x, const char *unit, char *buf, int n, int full)
{
    const int v = (int)(x + (x >= 0 ? .5f : -.5f));
    if (full && unit) snprintf(buf, (size_t)n, v > 0 ? "+%d %s" : "%d %s", v, unit);
    else snprintf(buf, (size_t)n, v > 0 ? "+%d" : "%d", v);
}

int eng_step(int type, int slider, float v)
{
    const int n = ENG_DEFS[type].steps[slider];
    return n ? zone(v, n) : 0;
}

static void ratio_text(float r, char *buf, int n, int full)
{
    if (r < 1.f) {
        if (full) snprintf(buf, (size_t)n, "x %.2f", r);
        else snprintf(buf, (size_t)n, ".%02d", (int)(r * 100.f + .5f));
    } else if (r == (float)(int)r) snprintf(buf, (size_t)n, full ? "x %d" : "X%d", (int)r);
    else snprintf(buf, (size_t)n, full ? "x %.1f" : "%.1f", r);
}

extern const char *FM6_RATIO_NAMES[16];

void eng_value_text(int type, int sl, float v, char *buf, int n, int full)
{
    const int k = eng_step(type, sl, v);
    const char *s = NULL;
    /* the play row (V13..V20), the same on every voice */
    if (sl == PR_OCT) { signed_text((float)(k - 2), "OCT", buf, n, full); return; }
    if (sl == PR_FINE) { signed_text((v - .5f) * 200.f, "CENTS", buf, n, full); return; }
    if (sl == PR_ENV_A || sl == PR_ENV_B || sl == PR_VEL) { signed_text((v - .5f) * 200.f, "%", buf, n, full); return; }
    switch (type) {
    case ENG_ANALOG:
        if (sl == 3) s = AN_INT[k];
        else if (sl == 1) { snprintf(buf, (size_t)n, full ? "%d %%" : "%d", (int)(50.f - 45.f * v + .5f)); return; }
        break;
    case ENG_TRIO:
        if (sl == 1 || sl == 2) {
            const int semi = sl == 1 ? k - 12 : k - 24;
            snprintf(buf, (size_t)n, semi > 0 ? "+%d" : "%d", semi);
            return;
        }
        break;
    case ENG_FM6:
        if (sl == 0) { snprintf(buf, (size_t)n, full ? "ALGORITHM %d" : "A%d", k + 1); return; }
        if (sl == 1) {
            if (full) snprintf(buf, (size_t)n, "%s", FM6_RATIO_NAMES[k]);
            else snprintf(buf, (size_t)n, "R%d", k + 1);
            return;
        }
        break;
    case ENG_FM4:
        if (sl == 0) { snprintf(buf, (size_t)n, full ? "ALGORITHM %d" : "A%d", k + 1); return; }
        if (sl == 1 || sl == 2 || sl == 6) { ratio_text(FM4_RATIOS[k], buf, n, full); return; }
        break;
    case ENG_PHASE:
        if (sl == 0 || sl == 6) s = full ? PD_WAVE[k] : PD_WAVE3[k];
        else if (sl == 9) { signed_text((float)(k - 1), "OCT", buf, n, full); return; }
        break;
    case ENG_WAVE:
        if (sl == 3) s = full ? WT_WARP[k] : WT_WARP3[k];
        else if (sl == 0) {
            if (full) snprintf(buf, (size_t)n, "BANK %.1f", 1.f + v * 3.f);
            else snprintf(buf, (size_t)n, "B%d", (int)(1.f + v * 3.f + .5f));
            return;
        }
        break;
    case ENG_CHIP:
        if (sl == 0) s = full ? CH_WAVE[k] : CH_WAVE3[k];
        else if (sl == 3) s = full ? CH_ARP[k] : CH_ARP3[k];
        else if (sl == 7) { snprintf(buf, (size_t)n, full ? "%d OCT" : "%d", k + 1); return; }
        else if (sl == 8) s = full ? CH_AMODE[k] : CH_AMODE3[k];
        else if (sl == 1) { snprintf(buf, (size_t)n, full ? "%d BITS" : "%d", (int)(2.f + 14.f * v + .5f)); return; }
        else if (sl == 2) {
            const float hz = 2000.f * exp2f(v * 4.4627f);
            if (full) snprintf(buf, (size_t)n, "%.1f kHz", hz / 1000.f);
            else snprintf(buf, (size_t)n, "%dK", (int)(hz / 1000.f + .5f));
            return;
        } else if (sl == 5) {
            const int sw = (int)((v - .5f) * 100.f + (v >= .5f ? .5f : -.5f));
            snprintf(buf, (size_t)n, sw > 0 ? "+%d" : "%d", sw);
            return;
        }
        break;
    case ENG_MACRO: if (sl == 0) s = full ? MACRO_NAMES[k] : MACRO3[k]; break;
    case ENG_WHEEL:
        if (sl == 3) s = full ? WH_PERC[k] : WH_PERC3[k];
        else if (sl == 5) {
            if (v < .02f) s = "OFF";
            else if (v <= .5f) { snprintf(buf, (size_t)n, full ? "SLOW %d" : "S%d", (int)(v * 200.f + .5f)); return; }
            else { snprintf(buf, (size_t)n, full ? "FAST %d" : "F%d", (int)((v - .5f) * 200.f + .5f)); return; }
        }
        break;
    case ENG_PHYS: if (sl == 0) s = full ? PH_MODEL[k] : PH_MODEL3[k]; break;
    case ENG_NOISE: if (sl == 0) s = full ? NO_TYPE[k] : NO_TYPE3[k]; break;
    case ENG_LXR_TONE:
        if (sl == 0 || sl == 3) s = full ? LX_WAVE[k] : LX_WAVE3[k];
        else if (sl == 2) { ratio_text(LXR_FM_RATIOS[k], buf, n, full); return; }
        break;
    case ENG_BAZZ:
        if (sl == 0) s = BZ_MODE[k];
        else if (sl == 2) s = BZ_OCT[k];
        else if (sl == 3) s = BZ_SUB[k];
        else if (sl == 8) s = BZ_OCT[k];
        break;
    case ENG_DRONE:
        if (sl == 1) { snprintf(buf, (size_t)n, full ? "%d VOICES" : "%d", k + 2); return; }
        if (sl == 3) s = full ? DR_STACK[k] : DR_STACK3[k];
        break;
    case ENG_FX: if (sl == 0) s = full ? FX_TYPES[k] : FX_TYPES3[k]; break;
    default: break;
    }
    if (s) { snprintf(buf, (size_t)n, "%s", s); return; }
    snprintf(buf, (size_t)n, "%d", (int)(clampe(v, 0, 1) * 100.f + .5f));
}

float eng_sub_octave(int type, const float *p) { return type == ENG_BAZZ && zone(p[3], 2) == 1 ? 2.f : 1.f; }
float eng_tone(int type, const float *p) { return type == ENG_BAZZ ? p[5] : 1.f; }

/* ---------------------------------------------------------------- dispatch */
void eng_tables_init(void)
{
    static int done;
    int i;
    if (done) return;
    for (i = 0; i <= SIN_SIZE; i++) ENG_SIN[i] = sinf(TWO_PI_F * (float)i / SIN_SIZE);
    for (i = 0; i <= 256; i++) ENG_HANN[i] = 0.5f - 0.5f * cosf(TWO_PI_F * (float)i / 256.f);
    grain_tables_init();
    bazz_tables_init();
    done = 1;
}

void eng_reset(Eng *e, int type)
{
    void *mi = e->mi;
    memset(e, 0, sizeof *e);
    e->mi = mi;
    e->type = (uint8_t)(type < 0 || type >= NUM_ENGINES ? ENG_BAZZ : type);
    e->fresh = 1;
    if (mi && (e->type == ENG_MACRO || e->type == ENG_WAVE || e->type == ENG_PHYS)) mi_reset(mi, e->type);
}

void eng_render(Eng *e, const EngIn *in, float *out, int n)
{
    switch (e->type) {
    case ENG_ANALOG: analog_render(&e->u.an, in, out, n); break;
    case ENG_TRIO: trio_render(&e->u.tr, in, out, n); break;
    case ENG_FM6: fm6_render(&e->u.fm, in, out, n); break;
    case ENG_FM4: fm4_render(&e->u.fm, in, out, n); break;
    case ENG_PHASE: phase_render(&e->u.pd, in, out, n); break;
    case ENG_CHIP: chip_render(&e->u.ch, in, out, n); break;
    case ENG_WHEEL: wheel_render(&e->u.wh, in, out, n); break;
    case ENG_VOICE: voice_fmt_render(&e->u.vo, in, out, n); break;
    case ENG_GRAIN: grain_render(&e->u.gr, in, out, n); break;
    case ENG_NOISE: noise_render(&e->u.no, in, out, n); break;
    case ENG_LXR_TONE: lxrtone_render(&e->u.lt, in, out, n); break;
    case ENG_LXR_METAL: lxrmetal_render(&e->u.lm, in, out, n); break;
    case ENG_BAZZ: bazz_render(&e->u.bz, in, out, n); break;
    case ENG_DRONE: drone_render(&e->u.dr, in, out, n); break;
    case ENG_TEXTURE: texture_render(&e->u.tx, in, out, n); break;
    case ENG_FX: fx_render_voice(&e->u.fx, in, out, n); break;
    case ENG_MACRO: case ENG_WAVE: case ENG_PHYS:
        if (e->mi) { mi_render(e->mi, e->type, in, out, n); break; }
        memset(out, 0, sizeof(float) * (size_t)n);
        break;
    default: memset(out, 0, sizeof(float) * (size_t)n); break;
    }
    e->fresh = 0;
}
