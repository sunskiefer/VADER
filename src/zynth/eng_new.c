/* LXRXXL ZYNTH -- the voices added in 0.2.0, all written for LXRXXL ZYNTH:
 *   DRONE    BAZZ's oscillator idea grown into a stack: two to seven oscillators on an interval stack, detuned, drifting,
 *            beating, their waves moving slowly, the upper ones blooming in, a sub and a shimmer
 *   TEXTURE  the LXR's noise and metal voices as beds: noise, metallic partials and dust in one SOURCE, resonators on
 *            the note, slow motion, wind, sparkles, a rumble and a little space
 *   FX       the LXR voice restructured for effects: lasers, zaps, risers, falls, sirens, alarms, glitches and bombs,
 *            with repeats, chaos, wobble, crush, noise, ring and a feedback comb */
#include <string.h>
#include "eng_dsp.h"

/* ================================================================ DRONE */
static const int8_t STACKS[8][DRONE_OSC] = {
    {0, 0, 0, 0, 0, 0, 0},          /* UNISON */
    {0, 12, -12, 24, 0, 12, -12},   /* OCTAVES */
    {0, 7, 12, 19, -5, 24, 7},      /* FIFTHS */
    {0, 12, 7, 19, -12, 24, 0},     /* OCT + 5TH */
    {0, 3, 7, 12, 15, 19, -12},     /* MINOR */
    {0, 4, 7, 12, 16, 19, -12},     /* MAJOR */
    {0, 5, 7, 12, 17, 19, -12},     /* SUS */
    {0, 2, 3, 5, 7, 8, 10},         /* CLUSTER */
};

static inline float drone_wave(float w, uint32_t ph, float dt)
{
    /* SINE -> TRIANGLE -> SAW -> SQUARE along w */
    const float t = ph_unit(ph);
    const float wp = clampe(w, 0.f, 1.f) * 2.999f;
    const int i = (int)wp;
    const float f = wp - (float)i;
    float a, b;
    const float sn = esin(ph), tr = 4.f * fabsf(t - 0.5f) - 1.f, sw = saw_blep(t, dt), sq = sq_blep(t, dt, 0.5f) * 0.8f;
    switch (i) {
    case 0: a = sn; b = -tr; break;
    case 1: a = -tr; b = sw; break;
    default: a = sw; b = sq; break;
    }
    return a + f * (b - a);
}

void drone_render(DroneSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const int nv = zone(p[1], 6) + 2;
    const int8_t *st = STACKS[zone(p[3], 8)];
    const float det = p[2] * 0.4f, drift = p[4], motion = p[5];
    const float motRate = 0.02f * powf(100.f, p[6]);
    const float beat = p[7] * 2.f, sub = p[8], shim = p[9];
    const float bloomT = 0.01f + 8.f * p[10] * p[10];
    const float lpA = 1.f - expf(-TWO_PI_F * 150.f * powf(120.f, p[11]) / SR);
    float g[DRONE_OSC], wv[DRONE_OSC];
    uint32_t inc[DRONE_OSC];
    float dt[DRONE_OSC];
    int o, k;
    if (!s->rnd) s->rnd = 0x2545F491u;
    if (in->trig) {
        s->bloom = 0.f;
        if (!s->ph[0]) for (o = 0; o < DRONE_OSC + 2; o++) s->ph[o] = xrand(&s->rnd);
        for (o = 0; o < DRONE_OSC; o++) s->motPh[o] = frand(&s->rnd);
    }
    /* DRIFT: each oscillator wanders on its own */
    s->driftT -= (float)n;
    if (s->driftT <= 0.f) {
        s->driftT = SR * 0.25f;
        for (o = 0; o < DRONE_OSC; o++) s->driftTo[o] = brand(&s->rnd);
    }
    s->bloom += (1.f - s->bloom) * (1.f - expf(-(float)n / (bloomT * SR)));
    for (o = 0; o < nv; o++) {
        const float spreadPos = nv > 1 ? (float)o / (float)(nv - 1) * 2.f - 1.f : 0.f;
        float semi = (float)st[o] + det * spreadPos + 0.3f * drift * s->drift[o];
        float f = in->freq * semis(semi) + beat * (float)(o & 1 ? o : -o) * 0.5f;
        s->drift[o] += 0.002f * (float)n / 8.f * (s->driftTo[o] - s->drift[o]);
        s->motPh[o] += motRate * (1.f + 0.13f * (float)o) * (float)n / SR;
        s->motPh[o] -= floorf(s->motPh[o]);
        wv[o] = clampe(p[0] + motion * 0.45f * esinf(s->motPh[o]), 0.f, 1.f);
        inc[o] = ph_inc(f);
        dt[o] = (float)inc[o] / PH_ONE;
        g[o] = o == 0 ? 1.f : s->bloom;   /* BLOOM: the stack above the first oscillator swells in */
    }
    {
        const float norm = 0.85f / sqrtf(1.f + (float)(nv - 1) * 0.8f);
        const uint32_t incSub = ph_inc(in->freq * 0.5f), incSh = ph_inc(in->freq * 2.f * semis(0.07f));
        for (k = 0; k < n; k++) {
            float x = 0.f;
            for (o = 0; o < nv; o++) {
                s->ph[o] += inc[o];
                x += g[o] * drone_wave(wv[o], s->ph[o], dt[o]);
            }
            x *= norm;
            if (sub > 0.001f) { s->ph[DRONE_OSC] += incSub; x += sub * 0.6f * esin(s->ph[DRONE_OSC]); }
            if (shim > 0.001f) {   /* SHIMMER: a folded octave above */
                s->ph[DRONE_OSC + 1] += incSh;
                x += shim * 0.35f * s->bloom * sinf(3.f * esin(s->ph[DRONE_OSC + 1]));
            }
            s->lp += lpA * (x - s->lp);
            out[k] = s->lp;
        }
    }
}

/* ================================================================ TEXTURE */
static const float TX_METAL[6] = {1.f, 1.4471f, 1.617f, 1.9265f, 2.5028f, 2.6637f};

void texture_render(TextureSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const float src = clampe(p[0], 0, 1) * 2.f;      /* 0 noise, 1 metal, 2 dust */
    const float wN = clampe(1.f - src, 0.f, 1.f), wM = 1.f - fabsf(src - 1.f), wD = clampe(src - 1.f, 0.f, 1.f);
    const float dens = p[1], track = p[2], motion = p[5];
    const float motRate = 0.05f * powf(80.f, p[6]);
    const float grit = p[7], wind = p[8], spark = p[9], rumble = p[10], space = p[11];
    float col, reso, centre;
    SvfCo c[3], cw;
    uint32_t inc[6];
    int j, k;
    if (!s->rnd) s->rnd = 0x9E3779B1u;
    /* MOTION and WIND: slow random targets, glided to */
    s->motT -= (float)n;
    if (s->motT <= 0.f) { s->motT = SR / motRate; s->motTo = brand(&s->rnd); }
    s->motion += (s->motTo - s->motion) * (1.f - expf(-(float)n * motRate / SR * 2.f));
    s->windT -= (float)n;
    if (s->windT <= 0.f) { s->windT = SR * (0.4f + 1.2f * frand(&s->rnd)); s->windTo = frand(&s->rnd); }
    s->wind += (s->windTo - s->wind) * (1.f - expf(-(float)n / (0.5f * SR)));
    col = clampe(p[3] + motion * 0.3f * s->motion, 0.f, 1.f);
    reso = clampe(p[4] + motion * 0.2f * s->motion, 0.f, 1.f);
    centre = (track * in->freq + (1.f - track) * 700.f) * exp2f(motion * 0.7f * s->motion);
    for (j = 0; j < 3; j++) c[j] = svf_co(centre * (float)(j + 1), 1.f + reso * reso * 40.f);
    cw = svf_co(300.f + 2500.f * s->wind, 2.f);
    for (j = 0; j < 6; j++) inc[j] = ph_inc((track * in->freq + (1.f - track) * 420.f) * TX_METAL[j] * 2.f);
    {
        const float lpA = 1.f - expf(-TWO_PI_F * 200.f * powf(80.f, col) / SR);
        const float hpA = 1.f - expf(-TWO_PI_F * 30.f * powf(60.f, col) / SR);
        const float dustP = (2.f + 400.f * dens * dens) / SR;
        const float rumA = 1.f - expf(-TWO_PI_F * 90.f / SR);
        const float levels = exp2f(15.f - grit * 11.f);
        const float sparkDec = expf(-1.f / (0.04f * SR));
        const int clen = 400 + (int)(space * 600.f);
        for (k = 0; k < n; k++) {
            const float w = brand(&s->rnd);
            float x = 0.f, y = 0.f, sq = 0.f;
            if (wN > 0.f) x += wN * w;
            if (wM > 0.f) {
                for (j = 0; j < 6; j++) { s->ph[j] += inc[j]; sq += (int32_t)s->ph[j] < 0 ? 1.f : -1.f; }
                x += wM * (sq * (1.f / 6.f) * (0.6f + 0.4f * dens) + 0.2f * w * dens);
            }
            if (wD > 0.f) {
                if (frand(&s->rnd) < dustP) s->dust = brand(&s->rnd) * 3.f;
                x += wD * s->dust;
                s->dust *= 0.7f;
            }
            /* COLOR: low-pass below the middle, high-pass above */
            s->lp += lpA * (x - s->lp);
            s->hp += hpA * (s->lp - s->hp);
            x = col < 0.5f ? s->lp : s->lp - s->hp * (col - 0.5f) * 1.6f;
            /* RESO: resonators on the note (or the fixed centre) */
            if (reso > 0.001f) {
                for (j = 0; j < 3; j++) y += c[j].k * svf_bp(c[j], &s->s1[j], &s->s2[j], x, NULL) * (j ? 0.6f : 1.f);
                x = x * (1.f - reso) + y * reso * (1.f + reso * 4.f);
            }
            if (wind > 0.001f) x += wind * (0.4f + s->wind) * cw.k * svf_bp(cw, &s->s1[3], &s->s2[3], w, NULL) * 1.5f;
            if (rumble > 0.001f) { s->rumble += rumA * (w - s->rumble); x += rumble * s->rumble * 4.f; }
            if (spark > 0.001f) {   /* SPARKLE: high metallic pings, here and there */
                if (frand(&s->rnd) < spark * spark * 12.f / SR) { s->sparkEnv = 1.f; s->spark = 3000.f + 5000.f * frand(&s->rnd); }
                if (s->sparkEnv > 0.0005f) {
                    s->sparkPh += ph_inc(s->spark);
                    x += spark * 0.5f * s->sparkEnv * esin(s->sparkPh);
                    s->sparkEnv *= sparkDec;
                }
            }
            if (grit > 0.005f) x = floorf(x * levels + 0.5f) / levels;
            if (space > 0.001f) {   /* SPACE: a short feedback comb, a little room */
                const int r = (s->cw - clen) & 1023;
                const float d = s->comb[r];
                s->comb[s->cw] = x + d * space * 0.6f;
                s->cw = (s->cw + 1) & 1023;
                x = x + d * space * 0.7f;
            }
            s->lp2 += 0.5f * (x - s->lp2);
            out[k] = clampe(s->lp2 * 1.2f, -2.f, 2.f);
        }
    }
}

/* ================================================================ FX */
void fx_render_voice(FxSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const int type = zone(p[0], 8);
    const float swT = 0.01f + 6.f * p[1] * p[1];                    /* SWEEP TIME: 10 ms .. 6 s */
    const float depth = p[2];
    const float repHz = p[3] > 0.01f ? 0.5f * powf(60.f, p[3]) : 0.f;   /* REPEAT: 0.5 .. 30 a second */
    const float chaos = p[4], wob = p[5], wobHz = 0.5f * powf(40.f, p[6]);
    const float crush = p[7], noise = p[8], ring = p[9], fb = p[10];
    const float lpA = 1.f - expf(-TWO_PI_F * 150.f * powf(120.f, p[11]) / SR);
    const float holdHz = SR * exp2f(-crush * 6.f);
    int k;
    if (!s->rnd) s->rnd = 0x85EBCA77u;
    if (in->trig) { s->t = 0.f; s->rep = 0.f; s->env = 1.f; s->glitchNote = 0.f; }
    for (k = 0; k < n; k++) {
        float u = s->t / swT, semi = 0.f, x, amp = 1.f;
        float f;
        int sq = 0, sine = 0;
        if (u > 1.f) u = 1.f;
        switch (type) {
        case 0: semi = depth * 48.f * (1.f - u) * (1.f - u); sq = 1; break;                       /* LASER: down fast */
        case 1: semi = depth * 60.f * expf(-u * 8.f); sine = 1; amp = 1.f - u * 0.5f; break;      /* ZAP */
        case 2: semi = -depth * 36.f * (1.f - u); amp = 0.3f + 0.7f * u; break;                   /* RISER */
        case 3: semi = depth * 36.f * (1.f - u) - depth * 24.f * u; break;                       /* FALL */
        case 4: semi = depth * 12.f * (0.5f - 0.5f * esinf(s->t / swT + 0.25f)); sine = 1; break;   /* SIREN */
        case 5: semi = fmodf(s->t / swT * 4.f, 2.f) < 1.f ? 0.f : depth * 12.f; sq = 1; break;      /* ALARM */
        case 6: semi = s->glitchNote; sq = 1; break;                                                /* GLITCH */
        default:                                                                                    /* BOMB */
            if (u < 1.f) { semi = depth * 36.f * (1.f - u); sine = 1; }
            else { semi = -24.f; amp = s->env; s->env *= expf(-1.f / (0.6f * SR)); }
            break;
        }
        s->t += 1.f / SR;
        if (repHz > 0.f) {   /* REPEAT: the sweep again, and again */
            s->rep += repHz / SR;
            if (s->rep >= 1.f) {
                s->rep -= 1.f;
                s->t = 0.f; s->env = 1.f;
                s->glitchNote = (float)((int)(xrand(&s->rnd) % 37u) - 12) * depth;
            }
        } else if (type == 6 && fmodf(s->t, 0.06f) < 1.f / SR) s->glitchNote = (float)((int)(xrand(&s->rnd) % 37u) - 12) * depth;
        if (chaos > 0.001f) {   /* CHAOS: the pitch thrown about */
            if ((xrand(&s->rnd) & 511u) == 0u) s->chaosTo = brand(&s->rnd) * 24.f;
            s->chaos += 0.002f * (s->chaosTo - s->chaos);
            semi += chaos * s->chaos;
        }
        if (wob > 0.001f) {
            s->wobPh += wobHz / SR;
            if (s->wobPh >= 1.f) s->wobPh -= 1.f;
            semi += wob * 7.f * esinf(s->wobPh);
        }
        f = in->freq * semis(semi);
        if (f > 16000.f) f = 16000.f;
        {
            const uint32_t inc = ph_inc(f);
            const float dt = (float)inc / PH_ONE;
            s->ph += inc;
            if (sine) x = esin(s->ph);
            else if (sq) x = sq_blep(ph_unit(s->ph), dt, 0.5f) * 0.8f;
            else x = saw_blep(ph_unit(s->ph), dt);
            if (type == 7 && u >= 1.f) { s->noiseLp += 0.08f * (brand(&s->rnd) - s->noiseLp); x = s->noiseLp * 3.f; }
            if (ring > 0.001f) { s->ph2 += ph_inc(f * 1.5f + 30.f); x *= 1.f - ring + ring * esin(s->ph2); }
        }
        if (noise > 0.001f) x = x * (1.f - noise * 0.6f) + noise * brand(&s->rnd) * 0.7f;
        x *= amp;
        if (crush > 0.005f) {
            s->holdPh += holdHz / SR;
            if (s->holdPh >= 1.f) { s->holdPh -= floorf(s->holdPh); s->hold = floorf(x * 16.f) / 16.f; }
            x = s->hold;
        }
        if (fb > 0.001f) {   /* FEEDBACK: a comb on the sound, 30 ms */
            const int r = (s->fw - 1323) & 2047;
            const float d = s->fbBuf[r];
            s->fbBuf[s->fw] = etanh(x + d * fb * 0.85f);
            s->fw = (s->fw + 1) & 2047;
            x = x + d * fb * 0.6f;
        }
        s->lp += lpA * (x - s->lp);
        out[k] = clampe(s->lp, -2.f, 2.f);
    }
}
