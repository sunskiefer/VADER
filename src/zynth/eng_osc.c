/* LXRXXL ZYNTH -- the oscillator voices: ANALOG, TRIO, CHIP, PHASE, LXR TONE, LXR METAL and BAZZ.
 *
 * LXR TONE, LXR METAL and BAZZ read the LXR's band-limited tables (third_party/lxr02open wavetable.c, LXR02 Open-Source
 * license), one table per octave of the note, as the LXR's oscillator does. Everything else here is written for
 * LXRXXL ZYNTH. */
#include <string.h>
#include "eng_dsp.h"
#include "wavetable.h"

extern const float LXR_FM_RATIOS[16];

/* ================================================================ the LXR tables (as in LXRXXL BAZZLINE) */
static uint32_t wave_offset[4];   /* each table's phase so SQR, SAW, TRI and the sine start together */

static inline float rd1024(const int16_t *t, uint32_t ph)
{
    const uint32_t i = ph >> 22;
    const float fr = (float)(ph & 0x3FFFFFu) * (1.f / 4194304.f);
    const int a = t[i], b = t[(i + 1) & 1023];
    return ((float)a + fr * (float)(b - a)) * (1.f / 32767.f);
}

static inline float rd_sine(uint32_t ph)
{
    const uint32_t i = ph >> 20;   /* TABLESIZE 4096 (+1 guard) */
    const float fr = (float)(ph & 0xFFFFFu) * (1.f / 1048576.f);
    const int a = sine_table[i], b = sine_table[i + 1];
    return ((float)a + fr * (float)(b - a)) * (1.f / 32767.f);
}

void bazz_tables_init(void)
{
    const int16_t *tabs[3] = {recTable[0], sawTable[0], triTable[0]};
    int w, i;
    double ref, re = 0, im = 0;
    for (i = 0; i < TABLESIZE; i++) {
        re += sine_table[i] * cos(6.283185307 * i / TABLESIZE);
        im += sine_table[i] * sin(6.283185307 * i / TABLESIZE);
    }
    ref = atan2(im, re);
    for (w = 0; w < 3; w++) {
        double f;
        re = im = 0;
        for (i = 0; i < 1024; i++) { re += tabs[w][i] * cos(6.283185307 * i / 1024.0); im += tabs[w][i] * sin(6.283185307 * i / 1024.0); }
        f = (atan2(im, re) - ref) / 6.283185307;
        f -= floor(f);
        wave_offset[w] = (uint32_t)(f * 4294967296.0);
    }
    wave_offset[3] = 0;
}

/* wave 0 SQR, 1 SAW, 2 TRI, 3 SINE */
static inline float lxr_wave(int w, int ti, uint32_t ph)
{
    switch (w) {
    case 0: return rd1024(recTable[ti], ph + wave_offset[0]);
    case 1: return rd1024(sawTable[ti], ph + wave_offset[1]);
    case 2: return rd1024(triTable[ti], ph + wave_offset[2]);
    default: return rd_sine(ph);
    }
}

/* the LXR's table for the octave of a note (Oscillator.c freqToTableIndex), from the note */
static inline int lxr_table(float note)
{
    return note < 0.f ? 0 : note >= 132.f ? 10 : (int)(note / 12.f);
}

/* WAVE morph: SINE -> TRI -> SAW -> SQR along pos 0..1 */
static inline float lxr_morph(float pos, int ti, uint32_t ph)
{
    static const int order[4] = {3, 2, 1, 0};
    const float p = clampe(pos, 0, 1) * 2.999f;
    const int i = (int)p;
    const float fr = p - (float)i;
    const float a = lxr_wave(order[i], ti, ph);
    return fr < 0.002f ? a : a + fr * (lxr_wave(order[i + 1], ti, ph) - a);
}

/* ================================================================ shared: a slow random walk (DRIFT) */
static inline void drift_walk(float *d, int k, float *t, uint32_t *rnd, int n)
{
    int i;
    *t -= (float)n;
    if (*t <= 0.f) {
        *t = SR * (0.2f + 0.4f * frand(rnd));
        for (i = 0; i < k; i++) d[i] += 0.5f * (brand(rnd) - d[i]);
    }
}

/* ================================================================ ANALOG: two polyBLEP oscillators, sync */
static const float AN_INT[9] = {0, 3, 4, 5, 7, 12, 19, 24, -12};

void analog_render(AnalogSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const float w = p[0], w2 = p[6];
    float pw = p[1];
    const float det = p[2] * 0.5f;                      /* up to 50 cents */
    const float iv = AN_INT[zone(p[3], 9)];
    const float sync = p[4];
    const float mix = p[5];
    const float ring = p[9], xfm = p[10] * p[10] * 2.f, drift = p[11];
    const int synced = sync > 0.01f;
    float f1 = in->freq, f2;
    if (!s->rnd) s->rnd = 0x5BD1E995u;
    if (drift > 0.001f) drift_walk(s->drift, 2, &s->driftT, &s->rnd, n);
    f1 *= semis(drift * 0.25f * s->drift[0]);
    f2 = synced ? f1 * (1.f + sync * 5.f) * semis(iv) : in->freq * semis(iv + det + drift * 0.25f * s->drift[1]);
    if (p[7] > 0.001f) {   /* PWM: an internal triangle on the pulse width */
        const float tri = 4.f * fabsf(s->pwmPh - 0.5f) - 1.f;
        s->pwmPh += (float)n * 0.1f * powf(120.f, p[8]) / SR;
        s->pwmPh -= floorf(s->pwmPh);
        pw = clampe(pw + p[7] * 0.5f * tri, 0.f, 1.f);
    }
    {
        const float pwv = 0.5f - 0.45f * pw;
        const float g1 = cosf(mix * 1.5707963f) * 1.15f, g2 = sinf(mix * 1.5707963f) * 1.15f;
        const int os = synced || xfm > 0.f ? 2 : 1;          /* sync and cross FM: twice the rate, then averaged */
        const uint32_t i1 = ph_inc(f1) / (uint32_t)os, i2 = ph_inc(f2) / (uint32_t)os;
        const float dt1 = (float)i1 / PH_ONE, dt2 = (float)i2 / PH_ONE;
        int k, o;
        if (in->trig && s->ph[0] == 0 && s->ph[1] == 0) s->ph[1] = 0x40000000u;
        for (k = 0; k < n; k++) {
            float acc = 0.f;
            for (o = 0; o < os; o++) {
                const uint32_t old = s->ph[0];
                float t1, t2, a, b;
                s->ph[0] += xfm > 0.f ? (uint32_t)((float)i1 * clampe(1.f + xfm * s->fmZ, 0.f, 3.f)) : i1;
                s->ph[1] += i2;
                if (synced && s->ph[0] < old) s->ph[1] = (uint32_t)((float)s->ph[0] * (dt2 / dt1));   /* osc 1 restarts osc 2 */
                t1 = ph_unit(s->ph[0]);
                t2 = ph_unit(s->ph[1]);
                a = (1.f - w) * saw_blep(t1, dt1) + w * sq_blep(t1, dt1, pwv);
                b = (1.f - w2) * saw_blep(t2, dt2) + w2 * sq_blep(t2, dt2, pwv);
                s->fmZ = b;
                acc += (1.f - ring) * (g1 * a + g2 * b) + ring * a * b * 1.3f;
            }
            out[k] = acc * (0.5f / (float)os) * 1.3f;
        }
    }
}

/* ================================================================ TRIO: three oscillators, Minimoog style */
static inline float trio_osc(float w, float t, float dt) { return (1.f - w) * saw_blep(t, dt) + w * sq_blep(t, dt, 0.5f); }

void trio_render(TrioSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const float wv[3] = {p[0], p[6], p[7]};
    const float i2 = (float)(zone(p[1], 25) - 12), i3 = (float)(zone(p[2], 49) - 24);
    const float det = p[3] * 0.35f;
    const float l2 = p[4];
    const int low = p[9] > 0.01f;                          /* OSC 3 LOW: a modulator below the audio range */
    const float l3 = low ? 0.f : p[5];
    const float o3mod = p[8] * p[8] * 12.f;                /* OSC 3 TO PITCH: up to an octave */
    const float fb = p[10], drift = p[11];
    const float norm = 1.3f / (1.f + l2 + l3);
    float f3, modNow;
    uint32_t inc[3];
    float dt[3];
    const float lv[3] = {1.f, l2, l3};
    int k, o;
    if (!s->rnd) s->rnd = 0x27D4EB2Fu;
    if (drift > 0.001f) drift_walk(s->drift, 3, &s->driftT, &s->rnd, n);
    f3 = low ? 0.1f * powf(200.f, p[9]) : in->freq * semis(i3 + det + drift * 0.3f * s->drift[2]);
    /* osc 3's value now: what it bends the other two by */
    {
        const float t = ph_unit(s->ph[2]);
        modNow = o3mod > 0.f ? trio_osc(wv[2], t, 0.f) * o3mod : 0.f;
    }
    inc[0] = ph_inc(in->freq * semis(-det + modNow + drift * 0.3f * s->drift[0]));
    inc[1] = ph_inc(in->freq * semis(i2 + det * 0.6f + modNow + drift * 0.3f * s->drift[1]));
    inc[2] = low ? (uint32_t)(f3 * (PH_ONE / SR)) : ph_inc(f3);
    for (o = 0; o < 3; o++) dt[o] = (float)inc[o] / PH_ONE;
    if (in->trig && !s->ph[0] && !s->ph[1] && !s->ph[2]) { s->ph[1] = 0x55555555u; s->ph[2] = 0xAAAAAAAAu; }
    for (k = 0; k < n; k++) {
        float acc = 0.f;
        for (o = 0; o < 3; o++) {
            s->ph[o] += inc[o];
            if (lv[o] > 0.f) acc += lv[o] * trio_osc(wv[o], ph_unit(s->ph[o]), dt[o]);
        }
        acc *= norm;
        if (fb > 0.001f) {   /* FEEDBACK: the output back into the mixer, as the Minimoog's patch: it thickens and drives */
            s->fb = etanh(acc + fb * 1.6f * s->fb);
            acc = acc * (1.f - fb) + s->fb * fb;
        }
        out[k] = acc;
    }
}

/* ================================================================ CHIP: console and computer chips */
static const int8_t ARPS[8][4] = {{0, 0, 0, 0}, {0, 12, 0, 12}, {0, 4, 7, 12}, {0, 3, 7, 12},
                                  {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, 12}, {0, 7, 12, 7}};

static inline float chip_tone(int wave, float t, float dt, float duty)
{
    switch (wave) {
    case 0: case 1: case 2: {
        static const float D[3] = {0.125f, 0.25f, 0.5f}, OFS[3] = {0.65f, 0.45f, 0.f}, G[3] = {0.9f, 0.9f, 0.8f};
        const float pw = clampe(D[wave] + duty, 0.05f, 0.95f);
        return sq_blep(t, dt, pw) * G[wave] + OFS[wave] * (1.f - 2.f * fabsf(duty));
    }
    case 3: {   /* the NES's 32-step triangle */
        const int st = (int)(t * 32.f);
        const int lv = st < 16 ? 15 - st : st - 16;
        return (float)lv / 7.5f - 1.f;
    }
    default: return (float)((int)(t * 16.f)) / 7.5f - 1.f;   /* a 16-step saw */
    }
}

static inline float chip_noise(uint32_t *ph, uint32_t *lfsr, float *outv, uint32_t inc, int metal)
{
    const uint32_t ni = (inc > 0x1FFFFFFFu ? 0x1FFFFFFFu : inc) * 8u;
    const uint64_t sum = (uint64_t)*ph + ni;
    *ph = (uint32_t)sum;
    if (sum >> 32) {
        const int tap = metal ? 6 : 1;
        const uint32_t fb = (*lfsr ^ (*lfsr >> tap)) & 1u;
        *lfsr = (*lfsr >> 1) | (fb << 14);
        *outv = (*lfsr & 1u) ? 0.8f : -0.8f;
    }
    return *outv;
}

void chip_render(ChipSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const int wave = zone(p[0], 7);
    const float bits = 2.f + 14.f * p[1];
    const float q = exp2f(bits - 1.f);
    const float rate = 2000.f * exp2f(p[2] * 4.4627f);   /* 2 kHz .. 44.1 kHz */
    const int arp = zone(p[3], 8);
    const float speed = (6.f + p[4] * 54.f) / SR;       /* the arpeggio: 6 .. 60 notes a second */
    const float sweep = (p[5] - 0.5f) * 2.f;           /* semitones a second: -/+ 96 */
    const float pwm = p[6];
    const int octs = zone(p[7], 3) + 1, amode = zone(p[8], 4);
    const float nmix = p[9], dual = p[10], blip = p[11];
    const int len = 4 * octs;
    int k;
    if (!s->rnd) s->rnd = 0x165667B1u;
    if (in->trig) {
        s->sweep = 0.f; s->arpI = amode == 1 ? len - 1 : 0; s->arpDir = 1; s->arpPh = 0.f; s->blip = 1.f;
        if (!s->lfsr) s->lfsr = 1;
    }
    if (!s->lfsr) s->lfsr = 1;
    if (!s->lfsr2) s->lfsr2 = 0x3A5;
    for (k = 0; k < n; k++) {
        float note = in->note, x;
        float duty = 0.f;
        if (arp) {
            s->arpPh += speed;
            if (s->arpPh >= 1.f) {
                s->arpPh -= 1.f;
                switch (amode) {
                case 1: s->arpI = (s->arpI + len - 1) % len; break;
                case 2:
                    if (len > 1) { if (s->arpI + s->arpDir >= len || s->arpI + s->arpDir < 0) s->arpDir = -s->arpDir; s->arpI += s->arpDir; }
                    break;
                case 3: s->arpI = (int)(xrand(&s->rnd) % (uint32_t)len); break;
                default: s->arpI = (s->arpI + 1) % len; break;
                }
            }
            note += ARPS[arp][s->arpI & 3] + 12.f * (float)(s->arpI >> 2);
        }
        s->sweep = clampe(s->sweep + sweep * fabsf(sweep) * 96.f / SR, -48.f, 48.f);
        note += s->sweep;
        if (blip > 0.001f) { note += blip * 24.f * s->blip; s->blip *= 0.9985f; }   /* BLIP: a quick drop into the note */
        if (pwm > 0.001f) {
            s->pwmPh += 4.f / SR;
            if (s->pwmPh >= 1.f) s->pwmPh -= 1.f;
            duty = pwm * 0.35f * (4.f * fabsf(s->pwmPh - 0.5f) - 1.f);
        }
        {
            const float f = 440.f * exp2f((note - 69.f) / 12.f);
            const uint32_t inc = ph_inc(f);
            const float dt = (float)inc / PH_ONE;
            if (wave >= 5) x = chip_noise(&s->ph, &s->lfsr, &s->noiseOut, inc, wave == 6);
            else {
                s->ph += inc;
                x = chip_tone(wave, ph_unit(s->ph), dt, duty);
                if (dual > 0.001f) {   /* DUAL: a second channel a little sharp */
                    const uint32_t inc2 = ph_inc(f * semis(0.04f + dual * 0.3f));
                    s->ph2 += inc2;
                    x = (x + dual * chip_tone(wave, ph_unit(s->ph2), (float)inc2 / PH_ONE, -duty)) / (1.f + dual * 0.7f);
                }
                if (nmix > 0.001f) x = x * (1.f - nmix * 0.7f) + nmix * chip_noise(&s->ph2, &s->lfsr2, &s->noiseOut2, inc, 0);
            }
        }
        /* bits, then the sample rate */
        if (bits < 15.9f) x = floorf(x * q + 0.5f) / q;
        s->holdPh += rate / SR;
        if (s->holdPh >= 1.f || rate >= 44000.f) { s->holdPh -= floorf(s->holdPh); s->hold = x; }
        out[k] = s->hold * 0.8f;
    }
}

/* ================================================================ PHASE: Casio CZ style phase distortion */
static inline float pd_phase(int w, float t, float d)
{
    const float m = 0.5f - 0.495f * d;      /* the knee */
    const float q = 1.f - 0.985f * d;       /* the squeeze */
    switch (w) {
    case 0: return t < m ? 0.5f * t / m : 0.5f + 0.5f * (t - m) / (1.f - m);                     /* SAW */
    case 1: {                                                                                   /* SQUARE */
        const float h = t < 0.5f ? 2.f * t : 2.f * t - 1.f;
        const float u = h / q < 1.f ? h / q : 1.f;
        return t < 0.5f ? 0.5f * u : 0.5f + 0.5f * u;
    }
    case 2: return t / q < 1.f ? t / q : 1.f;                                                   /* PULSE */
    case 3: {                                                                                   /* DOUBLE SINE */
        const float h = t < 0.5f ? 2.f * t : 2.f * t - 1.f;
        const float u = h < m ? 0.5f * h / m : 0.5f + 0.5f * (h - m) / (1.f - m);
        return t < 0.5f ? u : 1.f - u;
    }
    default: {                                                                                  /* SAW PULSE */
        if (t < 0.5f) { const float h = 2.f * t; return 0.5f * (h < m ? 0.5f * h / m : 0.5f + 0.5f * (h - m) / (1.f - m)); }
        { const float h = 2.f * t - 1.f; return 0.5f + 0.5f * (h / q < 1.f ? h / q : 1.f); }
    }
    }
}

static inline float pd_sample(int w, float t, float d)
{
    if (w >= 5) {   /* the resonance waves: a cosine at a rising ratio inside a window */
        const float r = 1.f + d * 14.f;
        float win;
        if (w == 5) win = 1.f - t;
        else if (w == 6) win = 1.f - fabsf(2.f * t - 1.f);
        else win = (1.f - t) * 2.f < 1.f ? (1.f - t) * 2.f : 1.f;
        return win * (1.f - esinf(t * r + 0.25f)) - 0.5f * win;
    }
    return -esinf(pd_phase(w, t, d) + 0.25f);   /* -cos */
}

void phase_render(PdSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const int w = zone(p[0], 8), w2 = zone(p[6], 8);
    const float dec = expf(-1.f / ((0.02f + 3.f * p[3] * p[3]) * SR));
    const float atk = 1.f / ((0.0005f + 1.5f * p[7] * p[7]) * SR);
    const float sus = p[8];
    const float l2 = p[5];
    const float oct2 = (float)(zone(p[9], 3) - 1) * 12.f;
    const float ring = p[10], nmod = p[11];
    const uint32_t inc1 = ph_inc(in->freq) >> 1;            /* 2x oversampled */
    const uint32_t inc2 = ph_inc(in->freq * semis(p[4] * 0.4f + oct2)) >> 1;
    const float norm = 1.f / (1.f + 0.7f * l2);
    int k;
    if (!s->rnd) s->rnd = 0x61C88647u;
    if (in->trig) { s->stage = p[7] > 0.002f ? 1 : 2; if (s->stage == 2) s->env = 1.f; }
    for (k = 0; k < n; k++) {
        float d, acc = 0.f;
        int o;
        if (s->stage == 1) { s->env += atk; if (s->env >= 1.f) { s->env = 1.f; s->stage = 2; } }
        else s->env = sus + (s->env - sus) * dec;     /* DCW: attack, decay to SUSTAIN */
        d = clampe(p[1] + p[2] * s->env, 0.f, 1.f);
        if (nmod > 0.001f) s->nz += 0.2f * (brand(&s->rnd) - s->nz);
        for (o = 0; o < 2; o++) {
            float a, b = 0.f;
            s->ph[0] += nmod > 0.001f ? (uint32_t)((float)inc1 * (1.f + nmod * 0.6f * s->nz)) : inc1;
            a = pd_sample(w, ph_unit(s->ph[0]), d);
            if (l2 > 0.001f || ring > 0.001f) { s->ph[1] += inc2; b = pd_sample(w2, ph_unit(s->ph[1]), d); }
            acc += (1.f - ring) * (a + 0.7f * l2 * b) + ring * a * b * 1.4f;
        }
        s->z += 0.6f * (acc * 0.5f - s->z);   /* the decimation's smoothing */
        out[k] = s->z * norm * 0.9f;
    }
}

/* ================================================================ LXR TONE: the LXR drum voice's oscillator, pitched */
void lxrtone_render(LxrToneSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    static const int WMAP[4] = {3, 2, 1, 0};   /* SINE TRI SAW SQR -> the table's order */
    const int w = WMAP[zone(p[0], 4)], fw = WMAP[zone(p[3], 4)];
    const float ratio = LXR_FM_RATIOS[zone(p[2], 16)];
    const float amt = p[1] * p[1] * 8.f;                   /* the FM index, radians */
    const float envAmt = p[4];
    const float fdec = expf(-1.f / ((0.015f + 0.6f * (1.f - envAmt) * (1.f - envAmt)) * SR));
    const float hz = SR * exp2f(-p[5] * 6.f);              /* DECIMATE: 44.1 kHz down to 690 Hz */
    const float pAmt = 48.f * p[6] * p[6];                 /* PITCH ENV: the LXR drum's sweep, down into the note */
    const float pDec = expf(-(float)n / ((0.005f + 1.f * p[7] * p[7]) * SR));
    const float click = p[8], body = p[9], sync = p[10], fold = p[11];
    const float note = in->note + pAmt * s->penv;
    const float f = 440.f * exp2f((note - 69.f) / 12.f);
    const float sr = 1.f + sync * 7.f;
    const int ti = lxr_table(note + (sync > 0.01f ? 12.f * log2f(sr) : 0.f)), tiM = lxr_table(note + 12.f * log2f(ratio));
    const uint32_t inc = ph_inc(f), incM = ph_inc(f * ratio), incS = ph_inc(f * sr);
    const float pmScale = PH_ONE / TWO_PI_F;
    const float clickDec = expf(-1.f / (0.003f * SR));
    int k;
    if (!s->rnd) s->rnd = 0x7F4A7C15u;
    if (in->trig) { s->fmEnv = 1.f; s->ph = 0; s->phMod = 0; s->phSync = 0; s->penv = 1.f; s->click = click; }
    s->penv *= pDec;
    for (k = 0; k < n; k++) {
        const float idx = amt * (1.f - envAmt + envAmt * s->fmEnv);
        const float m = lxr_wave(fw, tiM, s->phMod);
        const uint32_t pm = (uint32_t)(int64_t)(m * idx * pmScale);
        const uint32_t old = s->ph;
        float x;
        s->ph += inc;
        if (sync > 0.01f) {   /* SYNC: a slave at a higher ratio, restarted by the note's cycle */
            s->phSync += incS;
            if (s->ph < old) s->phSync = 0;
            x = lxr_wave(w, ti, s->phSync + pm);
        } else x = lxr_wave(w, ti, old + pm);
        if (body > 0.001f) x = x * (1.f - body) + body * rd_sine(old);
        if (fold > 0.001f) x = sinf(1.5707963f * x * (1.f + fold * 5.f));
        if (s->click > 0.0005f) { x += s->click * (0.6f * brand(&s->rnd) + 0.8f); s->click *= clickDec; }
        s->phMod += incM;
        s->fmEnv *= fdec;
        s->holdPh += hz / SR;
        if (s->holdPh >= 1.f || p[5] < 0.005f) { s->holdPh -= floorf(s->holdPh); s->hold = x; }
        out[k] = s->hold;
    }
}

/* ================================================================ LXR METAL: the LXR's hi-hat / cymbal, pitched */
static const float METAL[4][6] = {
    {1.f, 1.4471f, 1.617f, 1.9265f, 2.5028f, 2.6637f},   /* the 808's hats */
    {1.f, 1.3420f, 1.2312f, 1.6532f, 1.9523f, 2.1523f},   /* a cymbal */
    {1.f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f},        /* a bell's modes */
    {1.f, 1.183f, 1.506f, 1.741f, 2.0f, 2.473f},          /* a gong */
};
static const float MDET[6] = {0.f, 0.61f, -0.37f, 0.83f, -0.71f, 0.29f};

void lxrmetal_render(LxrMetalSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const float c = clampe(p[0], 0, 1) * 2.999f;
    const int ci = (int)c;
    const float cf = c - (float)ci;
    const float spread = 0.3f + p[1] * 1.4f;
    const float fm = p[2] * p[2] * 0.9f;
    const float noise = p[3];
    const float ring = p[5];
    const float det = p[6] * 0.02f, tone = p[7], shim = p[10], crush = p[11];
    const SvfCo bp = svf_co(in->freq * exp2f(1.f + p[4] * 4.f), 0.7f + 1.75f * p[8]);
    const float hpA = p[9] > 0.001f ? 1.f - expf(-TWO_PI_F * 20.f * powf(200.f, p[9]) / SR) : 0.f;
    const float holdHz = SR * exp2f(-crush * 6.f);
    const uint32_t incT = ph_inc(in->freq);
    uint32_t inc[6];
    int o, k;
    if (shim > 0.001f) {
        s->shimT -= (float)n;
        if (s->shimT <= 0.f) {
            s->shimT = SR * 0.08f;
            for (o = 0; o < 6; o++) s->shim[o] += 0.3f * (brand(&s->noise) - s->shim[o]);
        }
    }
    for (o = 0; o < 6; o++) {
        float r = METAL[ci][o] + cf * (METAL[ci + 1 < 4 ? ci + 1 : 3][o] - METAL[ci][o]);
        r = 1.f + (r - 1.f) * spread;
        r *= 1.f + det * MDET[o] + shim * 0.01f * s->shim[o];
        inc[o] = ph_inc(in->freq * r);
    }
    if (in->trig && !s->noise) s->noise = 0x6C8E9CF5u;
    if (!s->noise) s->noise = 0x6C8E9CF5u;
    for (k = 0; k < n; k++) {
        float sq[6], sum = 0.f, rm, x, lp;
        const float mod = (int32_t)s->ph[0] < 0 ? 1.f : -1.f;
        for (o = 0; o < 6; o++) {
            sq[o] = (int32_t)s->ph[o] < 0 ? 1.f : -1.f;
            sum += sq[o];
            s->ph[o] += o ? (uint32_t)((float)inc[o] * (1.f + fm * mod)) : inc[o];
        }
        rm = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * (1.f / 3.f);
        x = (1.f - ring) * sum * (1.f / 6.f) + ring * rm;
        x = x * (1.f - noise) + noise * brand(&s->noise);
        x = svf_bp(bp, &s->s1, &s->s2, x, &lp) * 2.f;
        if (tone > 0.001f) { s->tonePh += incT; x = x * (1.f - 0.5f * tone) + tone * 0.8f * esin(s->tonePh); }
        if (hpA > 0.f) { s->hp += hpA * (x - s->hp); x -= s->hp; }
        if (crush > 0.005f) {
            s->holdPh += holdHz / SR;
            if (s->holdPh >= 1.f) { s->holdPh -= floorf(s->holdPh); s->hold = x; }
            x = s->hold;
        }
        out[k] = x;
    }
}

/* ================================================================ BAZZ: LXRXXL BAZZLINE's oscillator */
static inline float bazz_osc(int mode, int wave, float pos, int ti, uint32_t pm)
{
    if (mode == 0) return lxr_wave(wave == 0 ? 0 : wave == 1 ? 1 : 2, ti, pm);
    if (mode == 1) return rd_sine(pm);
    return lxr_morph(pos, ti, pm);
}

void bazz_render(BazzSt *s, const EngIn *in, float *out, int n)
{
    const float *p = in->p;
    const int mode = zone(p[0], 3);
    const float pos = p[1];
    const float oct = (float)(zone(p[2], 5) - 2) * 12.f;
    const float l2 = p[6], det2 = p[7] * 0.5f, oct2 = (float)(zone(p[8], 5) - 2) * 12.f;
    const float fold = p[9], crush = p[10], sat = p[11];
    const float holdHz = SR * exp2f(-crush * 6.f);
    float note;
    uint32_t inc, incMod, inc2 = 0;
    int ti, ti2 = 0, k;
    const float pmScale = PH_ONE / TWO_PI_F;
    /* DRIFT: a slow random walk of the pitch, up to 30 cents */
    if (!s->rnd) s->rnd = 0x2F6B9A13u;
    s->driftT -= (float)n;
    if (s->driftT <= 0.f) { s->driftT = SR * (0.15f + 0.3f * frand(&s->rnd)); s->drift = brand(&s->rnd); }
    note = in->note + oct + p[4] * 0.3f * s->drift;
    {
        const float f = 440.f * exp2f((note - 69.f) / 12.f);
        inc = ph_inc(f);
        incMod = ph_inc(f * in->fmRatio);
        if (l2 > 0.001f) { inc2 = ph_inc(f * semis(det2 + oct2)); ti2 = lxr_table(note + det2 + oct2); }
    }
    ti = lxr_table(note);
    if (in->trig && !s->ph) s->phMod = 0;
    for (k = 0; k < n; k++) {
        uint32_t pm = s->ph, d = 0;
        float o;
        if (in->fmIdx > 0.f) { d = (uint32_t)(int64_t)(rd_sine(s->phMod) * in->fmIdx * pmScale); pm += d; }
        o = bazz_osc(mode, in->wave, pos, ti, pm);
        if (l2 > 0.001f) { s->ph2 += inc2; o = (o + l2 * bazz_osc(mode, in->wave, pos, ti2, s->ph2 + d)) / (1.f + 0.6f * l2); }
        if (fold > 0.001f) o = sinf(1.5707963f * o * (1.f + fold * 5.f));
        if (sat > 0.001f) o = etanh(o * (1.f + sat * 6.f)) / etanh(1.f + sat * 6.f) * (1.f + sat * 0.3f);
        if (crush > 0.005f) {
            s->holdPh += holdHz / SR;
            if (s->holdPh >= 1.f) { s->holdPh -= floorf(s->holdPh); s->hold = o; }
            o = s->hold;
        }
        s->ph += inc;
        s->phMod += incMod;
        out[k] = o;
    }
}
