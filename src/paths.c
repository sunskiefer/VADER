/* VADER 0.8: the maths shapes. Each one is computed once into a path (PATHN points at equal spacing) from four shape
 * numbers A..D (0..1). Shape indices from SHAPE_FIRST_MATH up; the old ones (0..15) stay where they were. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "vader.h"

#define PI 3.14159265358979f
#define NPTS 2048

const char *const MATH_NAMES[NMATH] = {
    "N-GON", "BUTTERFLY", "SUPERFORMULA", "LISSAJOUS", "ROSE", "LIMACON", "CARDIOID", "SPIROGRAPH", "ARCHIMEDES",
    "FERMAT", "HYPERBOLIC", "LITUUS", "LOG SPIRAL", "GOLDEN SPIRAL", "EULER SPIRAL", "TORUS KNOT", "MOBIUS",
    "HARMONOGRAPH", "PI LISSAJOUS", "EPICYCLES", "PULSE", "ADSR PATH"};

/* a number 0..1 mapped to lo..hi */
static float mp(float v, float lo, float hi) { return lo + (hi - lo) * v; }

/* the toy spirograph's gcd */
static int gcd_i(int a, int b) { return b ? gcd_i(b, a % b) : a; }

void paths_gen(int m, const float *k, path_t *p) {
    static float x[NPTS], y[NPTS], z[NPTS];
    int n = NPTS, closed = 1, is3d = 0;
    memset(p, 0, sizeof *p);
    if (m < 0 || m >= NMATH) m = 0;
    snprintf(p->name, sizeof p->name, "%s", MATH_NAMES[m]);
    float A = k[0], B = k[1], C = k[2], D = k[3];
    switch (m) {
    case 0: {   /* N-GON: A sides 3..16 */
        int sides = 3 + (int)lroundf(A * 13);
        n = sides;
        for (int i = 0; i < n; i++) {
            float a = -PI / 2 + TWO_PI * i / n;
            x[i] = cosf(a);
            y[i] = sinf(a);
        }
        break;
    }
    case 1: {   /* BUTTERFLY (Fay): t 0..12 pi */
        for (int i = 0; i < n; i++) {
            float t = 12 * PI * i / n;
            float q = expf(cosf(t)) - 2 * cosf(4 * t) - powf(sinf(t / 12), 5);
            x[i] = sinf(t) * q;
            y[i] = -cosf(t) * q;
        }
        break;
    }
    case 2: {   /* SUPERFORMULA: A = m, B = n1, C = n2/n3 */
        float mm = 1 + roundf(A * 11), n1 = mp(B, 0.3f, 6), n2 = mp(C, 0.3f, 6);
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n;
            float r = powf(powf(fabsf(cosf(mm * t / 4)), n2) + powf(fabsf(sinf(mm * t / 4)), n2), -1 / n1);
            x[i] = r * cosf(t);
            y[i] = r * sinf(t);
        }
        break;
    }
    case 3: {   /* LISSAJOUS: A,B frequencies 1..9, C phase, D phase drift per cycle */
        float fx = 1 + roundf(A * 8), fy = 1 + roundf(B * 8), ph = C * PI, dr = D * 0.5f;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n;
            x[i] = sinf(fx * t);
            y[i] = sinf(fy * t + ph + dr * t);
        }
        closed = D < 0.001f;
        break;
    }
    case 4: {   /* ROSE: petals A 1..12 over B 1..4 */
        float kk = (1 + roundf(A * 11)) / (1 + roundf(B * 3));
        int turns = 1 + (int)roundf(B * 3);
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * turns * i / n, r = cosf(kk * t);
            x[i] = r * cosf(t);
            y[i] = r * sinf(t);
        }
        break;
    }
    case 5: {   /* LIMACON: r = b + a cos t, A inner loop amount */
        float b = mp(A, 0.2f, 1.0f);
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, r = b + cosf(t) * mp(B, 0.5f, 1.5f);
            x[i] = r * cosf(t);
            y[i] = r * sinf(t);
        }
        break;
    }
    case 6: {   /* CARDIOID: r = 1 - cos t (A is the dent: 0 round .. 1 cardioid) */
        float a = mp(A, 0.2f, 1.0f);
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, r = 1 - a * cosf(t);
            x[i] = r * cosf(t);
            y[i] = r * sinf(t);
        }
        break;
    }
    case 7: {   /* SPIROGRAPH (rolling inside / outside): A ring R, B wheel r, C pen d, D inside(0) .. outside(1) */
        int R = 3 + (int)lroundf(A * 21), r = 1 + (int)lroundf(B * (R - 2));
        if (r >= R) r = R - 1;
        float d = mp(C, 0.1f, 1.5f) * r;
        int g = gcd_i(R, r), laps = r / g;   /* closes after r/g laps of the ring */
        float rr = (float)r;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * laps * i / n;
            if (D < 0.5f) {   /* hypotrochoid */
                x[i] = (R - rr) * cosf(t) + d * cosf((R - rr) * t / rr);
                y[i] = (R - rr) * sinf(t) - d * sinf((R - rr) * t / rr);
            } else {          /* epitrochoid */
                x[i] = (R + rr) * cosf(t) - d * cosf((R + rr) * t / rr);
                y[i] = (R + rr) * sinf(t) - d * sinf((R + rr) * t / rr);
            }
        }
        break;
    }
    case 8: case 9: case 10: case 11: case 12: case 13: {   /* the spirals: A turns 1..8, B tightness */
        float turns = mp(A, 1, 8), th0 = 0.15f;
        closed = 0;
        for (int i = 0; i < n; i++) {
            float u = (float)i / (n - 1), th = th0 + TWO_PI * turns * u, r;
            switch (m) {
            case 8: r = th; break;                                         /* Archimedes r = a + b th */
            case 9: r = sqrtf(th); break;                                  /* Fermat r^2 = a^2 th */
            case 10: r = 1.0f / (th0 * 4 + th * mp(B, 0.3f, 1.5f)); break; /* hyperbolic r = a / th */
            case 11: r = 1.0f / sqrtf(th0 * 3 + th); break;                /* lituus r^2 th = k */
            case 12: r = expf(mp(B, 0.05f, 0.25f) * th); break;           /* logarithmic r = a e^(b th) */
            default: r = powf(1.6180339887f, 2 * th / PI); break;          /* golden r = phi^(2 th / pi) */
            }
            x[i] = r * cosf(th);
            y[i] = r * sinf(th);
        }
        break;
    }
    case 14: {   /* EULER / CORNU spiral: Fresnel integrals, A length */
        float L = mp(A, 2, 6), sx = 0, sy = 0;
        closed = 0;
        int sub = 8;
        float dt = L / (n * sub / 2);
        for (int i = 0; i < n; i++) {   /* from -L to +L through the middle */
            for (int j = 0; j < sub / 2; j++) {
                float t = -L + 2 * L * ((float)i * (sub / 2) + j) / (n * (sub / 2));
                sx += cosf(PI * t * t / 2) * dt * 2;
                sy += sinf(PI * t * t / 2) * dt * 2;
            }
            x[i] = sx;
            y[i] = sy;
        }
        break;
    }
    case 15: {   /* TORUS KNOT (3D): A p, B q */
        float pp = 2 + roundf(A * 5), qq = 1 + roundf(B * 4);
        is3d = 1;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, r = cosf(qq * t) + 2;
            x[i] = r * cosf(pp * t) / 3;
            y[i] = r * sinf(pp * t) / 3;
            z[i] = -sinf(qq * t);
        }
        break;
    }
    case 16: {   /* MOBIUS strip's centre line with a half twist along the edge (3D): B width */
        float w = mp(B, 0.1f, 0.5f);
        is3d = 1;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, s = w * sinf(t * 2 * (1 + roundf(A * 2)) / 2);
            x[i] = (1 + s * cosf(t / 2)) * cosf(t) / 1.5f;
            y[i] = (1 + s * cosf(t / 2)) * sinf(t) / 1.5f;
            z[i] = s * sinf(t / 2);
        }
        break;
    }
    case 17: {   /* HARMONOGRAPH: two decaying pendulums, A,B ratios, C decay, D phase */
        float f1 = 1 + roundf(A * 5), f2 = 2 + roundf(B * 5), dec = mp(C, 0.0f, 0.04f), ph = D * PI;
        closed = 0;
        for (int i = 0; i < n; i++) {
            float t = 40 * (float)i / n, e = expf(-dec * t);
            x[i] = e * (sinf(f1 * t) + sinf(f2 * t + ph)) / 2;
            y[i] = e * (sinf(f2 * t) + sinf(f1 * t + ph * 0.5f)) / 2;
        }
        break;
    }
    case 18: {   /* PI LISSAJOUS: frequency ratio pi, never closes (A the length) */
        float turns = mp(A, 8, 40);
        closed = 0;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * turns * i / n;
            x[i] = sinf(t);
            y[i] = sinf(PI * t / 1.0f + mp(B, 0, 1));
        }
        break;
    }
    case 19: {   /* EPICYCLES: circles on circles, A count 2..5, B tightness */
        int cnt = 2 + (int)lroundf(A * 3);
        float tight = mp(B, 0.3f, 0.7f);
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, rad = 1, xx = 0, yy = 0, f = 1;
            for (int c = 0; c < cnt; c++) {
                xx += rad * cosf(f * t);
                yy += rad * sinf(f * t);
                rad *= tight;
                f = (c & 1) ? f * -3 : f * 2 + 1;
            }
            x[i] = xx;
            y[i] = yy;
        }
        break;
    }
    case 20: {   /* PULSE: a square wave drawn as a path; A pulse width */
        float w = mp(A, 0.1f, 0.9f);
        n = 0;
        x[n] = -1; y[n++] = -0.7f;
        x[n] = -1; y[n++] = 0.7f;
        x[n] = -1 + 2 * w; y[n++] = 0.7f;
        x[n] = -1 + 2 * w; y[n++] = -0.7f;
        x[n] = 1; y[n++] = -0.7f;
        x[n] = 1; y[n++] = 0.0f;
        x[n] = -1; y[n++] = 0.0f;
        closed = 0;
        break;
    }
    default: {   /* ADSR PATH: attack up, decay down to sustain, hold, release; A..D shape the four stages */
        float a = mp(A, 0.1f, 0.4f), d = mp(B, 0.1f, 0.4f), s = mp(C, -0.6f, 0.6f), r = mp(D, 0.1f, 0.4f);
        n = 0;
        float xx = -1;
        x[n] = xx; y[n++] = -0.7f;
        xx += a * 2; x[n] = xx; y[n++] = 0.8f;
        xx += d * 2; x[n] = xx; y[n++] = s;
        x[n] = 0.4f; y[n++] = s;
        x[n] = 1; y[n++] = -0.7f;
        closed = 1;
        break;
    }
    }
    path_resample(x, y, is3d ? z : NULL, n, closed, is3d, p);
    /* centre and fit */
    float mx = 0, my = 0, mz = 0;
    for (int i = 0; i < PATHN; i++) { mx += p->x[i]; my += p->y[i]; mz += p->z[i]; }
    mx /= PATHN; my /= PATHN; mz /= PATHN;
    float big = 1e-6f;
    for (int i = 0; i < PATHN; i++) {
        p->x[i] -= mx; p->y[i] -= my; p->z[i] -= is3d ? mz : 0;
        float a = fabsf(p->x[i]), b = fabsf(p->y[i]), c = fabsf(p->z[i]);
        if (a > big) big = a;
        if (b > big) big = b;
        if (c > big) big = c;
    }
    for (int i = 0; i < PATHN; i++) { p->x[i] /= big; p->y[i] /= big; p->z[i] /= big; }
}
