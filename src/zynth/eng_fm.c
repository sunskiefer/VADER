/* LXRXXL ZYNTH -- FM6 (six operators, the DX7's 32 algorithms) and FM4 (four operators, 12 algorithms).
 *
 * FM6's algorithm table is music-synthesizer-for-android's (fm_core.cc, Copyright 2012 Google Inc., Apache License
 * 2.0): each operator, in the order op6 .. op1, reads a bus and writes or adds to a bus; bus 0 is the output. The
 * operators, their ratios, envelopes and feedback are written here. */
#include <string.h>
#include "eng_dsp.h"

extern const float FM4_RATIOS[20];

/* ---------------------------------------------------------------- the DX7's algorithms (msfa, Apache-2.0) */
enum { OUT_BUS_ONE = 1 << 0, OUT_BUS_TWO = 1 << 1, OUT_BUS_ADD = 1 << 2, IN_BUS_ONE = 1 << 4, IN_BUS_TWO = 1 << 5,
       FB_IN = 1 << 6, FB_OUT = 1 << 7 };
static const uint8_t ALGS[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0x41, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0x41, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04},
};

/* the RATIO sets: op1 .. op6 */
static const float FM6_RATIOS[16][6] = {
    {1, 1, 1, 1, 1, 1}, {1, 2, 1, 2, 1, 2}, {1, 1, 2, 2, 3, 3}, {1, 14, 1, 1, 1, 1},
    {1, 3, 1, 3, 1, 3}, {1, 1, 3, 1, 7, 1}, {1, 2, 3, 4, 5, 6}, {.5f, 1, 1, 2, 2, 4},
    {1, 1.41f, 1, 3.5f, 1, 7}, {1, 3.5f, 1, 1.41f, 2, 5.19f}, {2, 1, 2, 1, 4, 1}, {1, .5f, 1, .5f, 1, .5f},
    {1, 7, 1, 7, 1, 7}, {1, 1, 1, 1, 1, .5f}, {1, 4, 2, 8, 3, 12}, {1, 1.005f, 2, 2.005f, .5f, 4.01f},
};
const char *FM6_RATIO_NAMES[16] = {
    "1 1 1 1 1 1", "1 2 1 2 1 2", "1 1 2 2 3 3", "1 14 1 1 1 1", "1 3 1 3 1 3", "1 1 3 1 7 1", "1 2 3 4 5 6",
    ".5 1 1 2 2 4", "1 1.41 1 3.5 1 7", "1 3.5 1 1.41 2 5.19", "2 1 2 1 4 1", "1 .5 1 .5 1 .5", "1 7 1 7 1 7",
    "1 1 1 1 1 .5", "1 4 2 8 3 12", "1 1.005 2 2.005 .5 4.01"};

#define RAD_TO_PH (PH_ONE / TWO_PI_F)

void fm6_render(FmSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const uint8_t *alg = ALGS[zone(p[0], 32)];
    const float *rs = FM6_RATIOS[zone(p[1], 16)];
    const float ks = exp2f(-p[11] * (in->note - 60.f) / 24.f);     /* KEY SCALE: high notes less bright */
    const float idx = p[2] * p[2] * 7.f * (ks < 2.f ? ks : 2.f);   /* the modulators' level: up to 7 radians */
    const float fbAmt = p[3] * p[3] * 1.6f;
    const float det = p[4] * 0.14f;                       /* up to 14 cents, spread over the operators */
    const float floorLvl = p[5] * 0.7f;
    const float decT = 0.08f + 2.5f * p[5] * p[5];
    const float tilt = (p[7] - 0.5f) * 2.f;               /* DECAY TILT: the higher operators shorter (or longer) */
    const float atk = p[6] > 0.002f ? 1.f / ((0.002f + 1.5f * p[6] * p[6]) * SR) : 1.f;
    const float inh = p[8] * 0.03f, spreadC = p[10] * 0.15f;
    const int fixed6 = p[9] > 0.01f;
    float dec[6];
    uint32_t inc[6];
    float lvl[6];
    int ncar = 0, op, k, ci = 0;
    for (op = 0; op < 6; op++) {   /* op index 0 = op6 */
        const int dxop = 5 - op;   /* op1 = 0 */
        float d = (dxop & 1 ? 1.f : -1.f) * det * (float)(dxop + 1) / 6.f;
        float r = rs[dxop] * (1.f + inh * powf((float)dxop, 1.5f));
        if ((alg[op] & 3) == 0) { lvl[op] = 1.f; ncar++; d += (ci++ & 1 ? 1.f : -1.f) * spreadC; }
        else lvl[op] = idx;
        inc[op] = (op == 0 && fixed6) ? ph_inc(20.f * powf(400.f, p[9])) : ph_inc(in->freq * r * semis(d));
        dec[op] = expf(-1.f / (decT * exp2f(-tilt * ((float)dxop - 2.5f) * 0.6f) * SR));
    }
    if (in->trig) { int i; for (i = 0; i < 6; i++) s->menv[i] = 1.f; s->matk = p[6] > 0.002f ? 0.f : 1.f; }
    {
        const float carNorm = 0.45f / sqrtf((float)(ncar ? ncar : 1));
        for (k = 0; k < n; k++) {
            float bus[3] = {0.f, 0.f, 0.f};
            const float fbIn = (s->fb[0] + s->fb[1]) * 0.5f * fbAmt;
            if (s->matk < 1.f) { s->matk += atk; if (s->matk > 1.f) s->matk = 1.f; }
            for (op = 0; op < 6; op++) {
                const uint8_t f = alg[op];
                const int inb = (f >> 4) & 3, outb = f & 3;
                float pm = inb ? bus[inb] : 0.f, y;
                if (f & FB_IN) pm += fbIn;
                s->ph[op] += inc[op];
                y = esin(s->ph[op] + (uint32_t)(int64_t)(pm * RAD_TO_PH));
                if (f & FB_OUT) { s->fb[1] = s->fb[0]; s->fb[0] = y; }
                if (outb) y *= lvl[op] * s->matk * (floorLvl + (1.f - floorLvl) * s->menv[op]);
                if (f & OUT_BUS_ADD) bus[outb] += y;
                else bus[outb] = y;
                s->menv[op] *= dec[op];
            }
            out[k] = bus[0] * carNorm;
        }
    }
}

/* ---------------------------------------------------------------- FM4: op 4 has the feedback; ops modulate lower ops */
typedef struct { uint8_t mod[4]; uint8_t car; } Alg4;   /* mod[j]: bit i = op i modulates op j (0-based); car: carriers */
static const Alg4 ALGS4[12] = {
    {{1 << 1, 1 << 2, 1 << 3, 0}, 1},                       /* 4>3>2>1 */
    {{1 << 1, (1 << 2) | (1 << 3), 0, 0}, 1},               /* (3+4)>2>1 */
    {{(1 << 1) | (1 << 3), 1 << 2, 0, 0}, 1},               /* (3>2 + 4)>1 */
    {{(1 << 1) | (1 << 2), 0, 1 << 3, 0}, 1},               /* (2 + 4>3)>1 */
    {{1 << 1, 0, 1 << 3, 0}, 1 | 4},                        /* 2>1, 4>3 */
    {{1 << 3, 1 << 3, 1 << 3, 0}, 1 | 2 | 4},               /* 4 > 1, 2, 3 */
    {{1 << 1, 0, 0, 0}, 1 | 4 | 8},                         /* 2>1, 3, 4 */
    {{0, 0, 0, 0}, 15},                                     /* additive */
    {{1 << 1, 1 << 2, 0, 0}, 1 | 8},                        /* 3>2>1, 4 */
    {{(1 << 1) | (1 << 2) | (1 << 3), 0, 0, 0}, 1},         /* 2+3+4 > 1 */
    {{1 << 2, 1 << 2, 1 << 3, 0}, 1 | 2},                   /* 4>3 > 1, 2 */
    {{1 << 1, 1 << 2, 1 << 3, 0}, 1 | 2 | 4},               /* the cascade, every stage heard */
};

void fm4_render(FmSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const Alg4 *a = &ALGS4[zone(p[0], 12)];
    const float rb = FM4_RATIOS[zone(p[1], 20)], rc = FM4_RATIOS[zone(p[2], 20)], rd = FM4_RATIOS[zone(p[6], 20)];
    const float inh = p[9] * 0.03f;
    const float ratio[4] = {1.f, rb * (1.f + inh), rc * (1.f + inh * 2.8f), rd * (1.f + inh * 5.2f)};
    const float ks = exp2f(-p[11] * (in->note - 60.f) / 24.f);
    const float idx = p[3] * p[3] * 6.f * (ks < 2.f ? ks : 2.f);
    const float fbAmt = p[4] * p[4] * 1.5f;
    const float dec = expf(-1.f / ((0.03f + 4.f * p[5] * p[5]) * SR));
    const float floorLvl = 0.15f + 0.5f * p[5];
    const float atk = p[7] > 0.002f ? 1.f / ((0.002f + 1.5f * p[7] * p[7]) * SR) : 1.f;
    const float spreadC = p[10] * 0.15f;
    float ncar = 0.f;
    uint32_t inc[4];
    int j, k, ci = 0;
    for (j = 0; j < 4; j++) {
        float d = 0.f;
        if (a->car & (1 << j)) { ncar += 1.f; d = (ci++ & 1 ? 1.f : -1.f) * spreadC; }
        inc[j] = (j == 3 && p[8] > 0.01f) ? ph_inc(20.f * powf(400.f, p[8])) : ph_inc(in->freq * ratio[j] * semis(d));
    }
    if (in->trig) { s->menv[0] = 1.f; s->matk = p[7] > 0.002f ? 0.f : 1.f; }
    {
        const float carNorm = 0.85f / sqrtf(ncar > 0.f ? ncar : 1.f);
        for (k = 0; k < n; k++) {
            float y[4], o = 0.f, me;
            if (s->matk < 1.f) { s->matk += atk; if (s->matk > 1.f) s->matk = 1.f; }
            me = idx * s->matk * (floorLvl + (1.f - floorLvl) * s->menv[0]);
            for (j = 3; j >= 0; j--) {
                float pm = 0.f;
                int i;
                for (i = j + 1; i < 4; i++) if (a->mod[j] & (1 << i)) pm += y[i] * me;
                if (j == 3) pm += (s->fb[0] + s->fb[1]) * 0.5f * fbAmt;
                s->ph[j] += inc[j];
                y[j] = esin(s->ph[j] + (uint32_t)(int64_t)(pm * RAD_TO_PH));
                if (j == 3) { s->fb[1] = s->fb[0]; s->fb[0] = y[3]; }
                if (a->car & (1 << j)) o += y[j];
            }
            s->menv[0] *= dec;
            out[k] = o * carNorm;
        }
    }
}
