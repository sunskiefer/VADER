/* The overtone patterns an OVERTONE GEN sweeps through: 32 harmonic spectra (up to 64 harmonics), each built in
 * seven band-limited versions (64, 32, 16, 8, 4, 2 and 1 harmonics) so a high note never folds back. Additive, as
 * the generators on the hardware are, but computed once at load: playing them is a table read. Shared by every
 * instance (built once). */
#include <math.h>
#include <pthread.h>
#include <string.h>
#include "vader.h"

float g_tab[NPAT][NMIP][TBL + 1];

const char *const PATTERN_NAMES[NPAT] = {
    "SINE", "SOFT", "SQUARE", "SAW", "BRIGHT", "HOLLOW", "PULSE 10", "PULSE 20", "PULSE 30", "PULSE 40",
    "VOX A", "VOX E", "VOX I", "VOX O", "VOX U", "ORGAN 1", "ORGAN 2", "ORGAN 3", "ORGAN 4", "BELL",
    "COMB", "OCTAVES", "FIFTHS", "GLASS", "REED", "BUZZ", "AIR", "HIGHS", "PRIMES", "SPREAD", "FORMANT", "METAL"};

static float sine_tbl[TBL];
static unsigned rng_state = 12345u;
static float frand(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 8) / 16777216.0f;
}

static float formant(int h, const float *f, const float *g, int n, float f0) {
    float a = 0, hz = h * f0;
    for (int k = 0; k < n; k++) {
        float d = (hz - f[k]) / (f[k] * 0.12f + 60.0f);
        a += g[k] * expf(-d * d);
    }
    return a / powf((float)h, 0.25f);
}

/* amplitude and phase (in table steps) of harmonic h (1..64) of pattern k */
static void pattern(int k, float *amp, int *ph) {
    static const float va[5][3] = {{800, 1150, 2900}, {400, 1700, 2600}, {300, 2100, 3000}, {450, 800, 2830}, {325, 700, 2530}};
    static const float vg[3] = {1.0f, 0.6f, 0.3f};
    memset(amp, 0, sizeof(float) * 65);
    memset(ph, 0, sizeof(int) * 65);
    for (int h = 1; h <= 64; h++) {
        float H = (float)h, a = 0;
        int odd = h & 1;
        switch (k) {
        case 0: a = h == 1; break;
        case 1: a = odd ? 1.0f / (H * H) : 0; break;
        case 2: a = odd ? 1.0f / H : 0; break;
        case 3: a = 1.0f / H; break;
        case 4: a = 1.0f / sqrtf(H); break;
        case 5: a = h == 1 ? 0.6f : (odd ? 0 : 1.0f / H); break;
        case 6: case 7: case 8: case 9: {
            float d = 0.1f * (k - 5);
            a = fabsf(sinf(3.14159265f * H * d)) / H;
            break;
        }
        case 10: case 11: case 12: case 13: case 14: a = formant(h, va[k - 10], vg, 3, 110.0f); break;
        case 15: a = (h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8) ? 1.0f / sqrtf(H) : 0; break;
        case 16: a = (h == 1 || h == 3 || h == 5 || h == 8 || h == 16) ? 0.8f : 0; break;
        case 17: a = (h == 1 || h == 2 || h == 4 || h == 8 || h == 16 || h == 32) ? 1.0f / sqrtf(H) : 0; break;
        case 18: a = (h <= 12) ? 1.0f / sqrtf(H) : 0; break;
        case 19: a = (h == 1 ? 1.0f : 0) + ((h == 5 || h == 7 || h == 11 || h == 13 || h == 19) ? 0.7f / sqrtf(H) : 0); break;
        case 20: a = (h % 3) ? 1.0f / H : 0; break;
        case 21: a = (h == 1 || h == 2 || h == 4 || h == 8 || h == 16 || h == 32 || h == 64) ? 1.0f : 0; break;
        case 22: a = (h == 1 || h == 3 || h == 6 || h == 9 || h == 12 || h == 18 || h == 24 || h == 36 || h == 48) ? 1.0f / powf(H, 0.4f) : 0; break;
        case 23: a = (h == 1 ? 0.7f : 0) + ((h >= 9 && h <= 15) ? 0.5f : 0) + ((h >= 30 && h <= 34) ? 0.3f : 0); break;
        case 24: a = (odd ? 1.0f : 0.35f) / powf(H, 0.8f) * (1.0f + 1.5f * expf(-(H - 7) * (H - 7) / 8.0f)); break;
        case 25: a = 1.0f / powf(H, 0.6f); break;
        case 26: a = (h == 1 ? 0.5f : 0) + 0.25f * expf(-(H - 24) * (H - 24) / 200.0f); break;
        case 27: a = (h >= 16 && h <= 40) ? 0.5f : (h == 1 ? 0.6f : 0); break;
        case 28: {
            int prime = h > 1;
            for (int d = 2; d * d <= h; d++) if (h % d == 0) prime = 0;
            a = (h == 1 || prime) ? 1.0f / sqrtf(H) : 0;
            break;
        }
        case 29: a = (0.3f + frand()) / H; break;
        case 30: a = formant(h, (const float[]){1200, 2600}, (const float[]){1.0f, 0.6f}, 2, 110.0f) + (h == 1 ? 0.5f : 0); break;
        case 31: a = (h == 1 || h == 4 || h == 9 || h == 16 || h == 25 || h == 36 || h == 49 || h == 64) ? 1.0f / powf(H, 0.3f) : 0; break;
        }
        amp[h] = a;
        if (k >= 23 && k != 28) ph[h] = (int)(frand() * TBL);   /* the denser spectra: scattered phases, less peaky */
    }
}

static void build(void) {
    for (int i = 0; i < TBL; i++) sine_tbl[i] = sinf(TWO_PI * (float)i / TBL);
    float amp[65];
    int ph[65];
    for (int k = 0; k < NPAT; k++) {
        rng_state = 777u + (unsigned)k * 31u;
        pattern(k, amp, ph);
        float peak = 0;
        for (int m = 0; m < NMIP; m++) {
            int hmax = 64 >> m;
            float *t = g_tab[k][m];
            for (int i = 0; i < TBL; i++) {
                float s = 0;
                for (int h = 1; h <= hmax; h++)
                    if (amp[h] != 0) s += amp[h] * sine_tbl[(h * i + ph[h]) & (TBL - 1)];
                t[i] = s;
                if (m == 0 && fabsf(s) > peak) peak = fabsf(s);
            }
        }
        float g = peak > 1e-6f ? 0.9f / peak : 1.0f;
        for (int m = 0; m < NMIP; m++) {
            for (int i = 0; i < TBL; i++) g_tab[k][m][i] *= g;
            g_tab[k][m][TBL] = g_tab[k][m][0];
        }
    }
}

static pthread_once_t once = PTHREAD_ONCE_INIT;
void tables_init(void) { pthread_once(&once, build); }
