/* The effects after the voices, in this order: drive (amount, colour), chorus (speed, depth, colour), a stereo
 * ping-pong delay (time, feedback; synced or free), a reverb (size, colour), then the master volume and a soft
 * limiter so nothing clips on MPC's mixer. Stereo float, 44.1 kHz. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vader.h"

#define CH_MAX 2048          /* chorus line: 46 ms */
#define DLY_MAX 88200        /* delay line: 2 s */
#define NREV 8

static const int REV_LEN[NREV] = {1597, 1801, 2053, 2287, 2579, 2861, 3109, 3389};   /* x size (0.35 .. 1.4) */
#define REV_MAXLEN 4800
static const int AP_LEN[4] = {142, 107, 379, 277};

struct fx_s {
    float drv_lp[2];
    float ch[2][CH_MAX];
    int ch_w;
    float ch_ph, ch_lp[2], ch_hp[2];
    float dly[2][DLY_MAX];
    int dly_w;
    float dly_time_s;          /* the delay time, slewed (a change bends instead of clicking) */
    float dly_lp[2];
    float rev[NREV][REV_MAXLEN];
    int rev_w[NREV];
    float rev_lp[NREV];
    float ap[4][400];
    int ap_w[4];
    float rev_mod;
};

fx_t *fx_new(void) { return calloc(1, sizeof(fx_t)); }
void fx_free(fx_t *f) { free(f); }
void fx_clear(fx_t *f) { memset(f, 0, sizeof *f); }

static inline float soft(float x) {   /* a cheap tanh */
    if (x > 3) return 1;
    if (x < -3) return -1;
    float x2 = x * x;
    return x * (27 + x2) / (27 + 9 * x2);
}

static inline float read_frac(const float *buf, int len, int w, float d) {
    float r = (float)w - d;
    while (r < 0) r += (float)len;
    int i = (int)r;
    float fr = r - (float)i;
    int j = i + 1 >= len ? 0 : i + 1;
    return buf[i] + (buf[j] - buf[i]) * fr;
}

void fx_process(fx_t *f, const fx_params_t *p, float *L, float *R, int n) {
    /* ---- drive: a tilt (colour) into a soft clipper, level kept */
    if (p->drive > 0.001f) {
        float g = 1 + p->drive * p->drive * 24, mk = 1.0f / soft(g * 0.5f) * 0.5f;
        float tilt = (p->drive_col - 0.5f) * 2;   /* -1 dark .. +1 bright */
        for (int i = 0; i < n; i++) {
            float *s[2] = {&L[i], &R[i]};
            for (int c = 0; c < 2; c++) {
                float x = *s[c];
                f->drv_lp[c] += 0.08f * (x - f->drv_lp[c]);
                float lo = f->drv_lp[c], hi = x - lo;
                x = lo * (1 - 0.6f * tilt) + hi * (1 + 0.9f * tilt);
                *s[c] = soft(x * g) * mk;
            }
        }
    }
    /* ---- chorus: two lines, quadrature LFOs */
    if (p->ch_mix > 0.001f) {
        float rate = p->ch_rate_hz * INV_SR, depth = 0.5f + p->ch_depth * 7.5f, base = 9.0f;
        float lpc = 0.05f + 0.9f * p->ch_col, fb = p->ch_col > 0.5f ? (p->ch_col - 0.5f) * 0.6f : 0;
        for (int i = 0; i < n; i++) {
            f->ch_ph += rate;
            if (f->ch_ph >= 1) f->ch_ph -= 1;
            float m0 = sinf(TWO_PI * f->ch_ph), m1 = cosf(TWO_PI * f->ch_ph);
            float d0 = (base + depth * (1 + m0)) * SR * 0.001f, d1 = (base + depth * (1 + m1)) * SR * 0.001f;
            float wl = read_frac(f->ch[0], CH_MAX, f->ch_w, d0), wr = read_frac(f->ch[1], CH_MAX, f->ch_w, d1);
            f->ch_lp[0] += lpc * (wl - f->ch_lp[0]);
            f->ch_lp[1] += lpc * (wr - f->ch_lp[1]);
            f->ch[0][f->ch_w] = L[i] + f->ch_lp[1] * fb;
            f->ch[1][f->ch_w] = R[i] + f->ch_lp[0] * fb;
            if (++f->ch_w >= CH_MAX) f->ch_w = 0;
            L[i] += p->ch_mix * f->ch_lp[0];
            R[i] += p->ch_mix * f->ch_lp[1];
        }
    }
    /* ---- delay: ping-pong, damped feedback */
    {
        float target = p->dly_sec * SR;
        if (target < 32) target = 32;
        if (target > DLY_MAX - 4) target = DLY_MAX - 4;
        if (f->dly_time_s <= 0) f->dly_time_s = target;
        float fb = p->dly_fb * 0.95f;
        for (int i = 0; i < n; i++) {
            f->dly_time_s += 0.0005f * (target - f->dly_time_s);
            float a = read_frac(f->dly[0], DLY_MAX, f->dly_w, f->dly_time_s);
            float b = read_frac(f->dly[1], DLY_MAX, f->dly_w, f->dly_time_s);
            f->dly_lp[0] += 0.35f * (a - f->dly_lp[0]);
            f->dly_lp[1] += 0.35f * (b - f->dly_lp[1]);
            float in = (L[i] + R[i]) * 0.5f;
            f->dly[0][f->dly_w] = in + f->dly_lp[1] * fb;   /* left takes the right's echo: ping-pong */
            f->dly[1][f->dly_w] = f->dly_lp[0] * fb;
            if (++f->dly_w >= DLY_MAX) f->dly_w = 0;
            L[i] += p->dly_mix * a;
            R[i] += p->dly_mix * b;
        }
    }
    /* ---- reverb: two allpasses into an 8-line feedback network (Hadamard) */
    if (p->rev_mix > 0.001f) {
        float size = 0.35f + 1.05f * p->rev_size;
        float rt = 0.5f + 7.5f * p->rev_size * p->rev_size;           /* seconds */
        float damp = 0.12f + 0.8f * p->rev_col;
        float len[NREV], g[NREV];
        for (int k = 0; k < NREV; k++) {
            len[k] = REV_LEN[k] * size;
            if (len[k] > REV_MAXLEN - 8) len[k] = REV_MAXLEN - 8;
            g[k] = powf(10.0f, -3.0f * len[k] / (rt * SR));
        }
        /* the lines' slow wobble, worked out once a block */
        f->rev_mod += 0.00002f * n;
        if (f->rev_mod > 1) f->rev_mod -= 1;
        float dl[NREV];
        for (int k = 0; k < NREV; k++) dl[k] = len[k] + (k & 1 ? 3.0f : -3.0f) * sinf(TWO_PI * (f->rev_mod + k * 0.125f));
        for (int i = 0; i < n; i++) {
            float x = (L[i] + R[i]) * 0.3f;
            for (int a = 0; a < 2; a++) {
                float *b = f->ap[a];
                int w = f->ap_w[a], l = AP_LEN[a];
                int r = w - l;
                if (r < 0) r += 400;
                float d = b[r], y = d - 0.6f * x;
                b[w] = x + 0.6f * y;
                f->ap_w[a] = (w + 1) % 400;
                x = y;
            }
            float o[NREV];
            for (int k = 0; k < NREV; k++) {
                float v = read_frac(f->rev[k], REV_MAXLEN, f->rev_w[k], dl[k]);
                f->rev_lp[k] += damp * (v - f->rev_lp[k]);
                o[k] = f->rev_lp[k] * g[k];
            }
            /* Hadamard 8 */
            float h[NREV];
            for (int k = 0; k < NREV; k++) h[k] = o[k];
            for (int s = 1; s < NREV; s <<= 1)
                for (int k = 0; k < NREV; k += 2 * s)
                    for (int j = k; j < k + s; j++) {
                        float a = h[j], b = h[j + s];
                        h[j] = a + b;
                        h[j + s] = a - b;
                    }
            for (int k = 0; k < NREV; k++) {
                f->rev[k][f->rev_w[k]] = h[k] * 0.35355339f + x;
                if (++f->rev_w[k] >= REV_MAXLEN) f->rev_w[k] = 0;
            }
            float wl = o[0] + o[2] + o[4] + o[6], wr = o[1] + o[3] + o[5] + o[7];
            L[i] += p->rev_mix * wl * 0.5f;
            R[i] += p->rev_mix * wr * 0.5f;
        }
    }
    /* ---- master volume and a soft limit */
    float v = p->volume * p->volume * 1.6f;
    for (int i = 0; i < n; i++) {
        L[i] = soft(L[i] * v);
        R[i] = soft(R[i] * v);
    }
}
