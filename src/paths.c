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
    "HARMONOGRAPH", "PI LISSAJOUS", "EPICYCLES", "PULSE", "ADSR PATH", "POLYHEDRA", "HILBERT", "KOCH", "FOURIER",
    "SQUEEZE", "DE BROGLIE", "COTES", "POINSOT", "ATOMIC", "MANDALA", "STACKED SPIRO", "TIMES TABLE", "TOY GEARS"};

/* a number 0..1 mapped to lo..hi */
static float mp(float v, float lo, float hi) { return lo + (hi - lo) * v; }

/* the toy spirograph's gcd */
static int gcd_i(int a, int b) { return b ? gcd_i(b, a % b) : a; }

const path_t *paths_user = 0;   /* 0.8: the loaded shape, for FOURIER to rebuild from circles */

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
    case 22: {   /* POLYHEDRA (3D): a walk along every edge. A: tetra, cube, octa, dodeca, icosa; B: spin */
        static float vx[20], vy[20], vz[20];
        int nv = 0, kind = (int)lroundf(A * 4);
        const float ph = 1.6180339887f;
        if (kind == 0) { static const float t[4][3] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}}; for (; nv < 4; nv++) { vx[nv] = t[nv][0]; vy[nv] = t[nv][1]; vz[nv] = t[nv][2]; } }
        else if (kind == 1) { for (; nv < 8; nv++) { vx[nv] = (nv & 1) ? 1 : -1; vy[nv] = (nv & 2) ? 1 : -1; vz[nv] = (nv & 4) ? 1 : -1; } }
        else if (kind == 2) { static const float t[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}; for (; nv < 6; nv++) { vx[nv] = t[nv][0]; vy[nv] = t[nv][1]; vz[nv] = t[nv][2]; } }
        else if (kind == 3) {
            for (int i = 0; i < 8; i++) { vx[nv] = (i & 1) ? 1 : -1; vy[nv] = (i & 2) ? 1 : -1; vz[nv++] = (i & 4) ? 1 : -1; }
            for (int i = 0; i < 4; i++) { float a = (i & 1) ? 1 / ph : -1 / ph, b = (i & 2) ? ph : -ph; vx[nv] = 0; vy[nv] = a; vz[nv++] = b; }
            for (int i = 0; i < 4; i++) { float a = (i & 1) ? 1 / ph : -1 / ph, b = (i & 2) ? ph : -ph; vx[nv] = a; vy[nv] = b; vz[nv++] = 0; }
            for (int i = 0; i < 4; i++) { float a = (i & 1) ? 1 / ph : -1 / ph, b = (i & 2) ? ph : -ph; vx[nv] = b; vy[nv] = 0; vz[nv++] = a; }
        } else {
            for (int i = 0; i < 4; i++) { float a = (i & 1) ? 1 : -1, b = (i & 2) ? ph : -ph; vx[nv] = 0; vy[nv] = a; vz[nv++] = b; }
            for (int i = 0; i < 4; i++) { float a = (i & 1) ? 1 : -1, b = (i & 2) ? ph : -ph; vx[nv] = a; vy[nv] = b; vz[nv++] = 0; }
            for (int i = 0; i < 4; i++) { float a = (i & 1) ? 1 : -1, b = (i & 2) ? ph : -ph; vx[nv] = b; vy[nv] = 0; vz[nv++] = a; }
        }
        float emin = 1e9f;   /* the edge length: the shortest distance between two vertices */
        for (int i = 0; i < nv; i++) for (int j = i + 1; j < nv; j++) {
            float d = sqrtf((vx[i] - vx[j]) * (vx[i] - vx[j]) + (vy[i] - vy[j]) * (vy[i] - vy[j]) + (vz[i] - vz[j]) * (vz[i] - vz[j]));
            if (d < emin) emin = d;
        }
        static char seen[20], adj[20][20];
        memset(seen, 0, sizeof seen);
        for (int i = 0; i < nv; i++) for (int j = 0; j < nv; j++) {
            float d = sqrtf((vx[i] - vx[j]) * (vx[i] - vx[j]) + (vy[i] - vy[j]) * (vy[i] - vy[j]) + (vz[i] - vz[j]) * (vz[i] - vz[j]));
            adj[i][j] = i != j && d < emin * 1.05f;
        }
        int stack[24], sp = 0, cur = 0, ptr[20] = {0};
        n = 0;
        is3d = 1;
        seen[0] = 1; stack[sp++] = 0;
        x[n] = vx[0]; y[n] = vy[0]; z[n++] = vz[0];
        while (sp > 0 && n < NPTS - 2) {   /* depth-first walk, stepping back along the edge it came by */
            cur = stack[sp - 1];
            int nx = -1;
            while (ptr[cur] < nv) { int j = ptr[cur]++; if (adj[cur][j] && !seen[j]) { nx = j; break; } }
            if (nx >= 0) { seen[nx] = 1; stack[sp++] = nx; x[n] = vx[nx]; y[n] = vy[nx]; z[n++] = vz[nx]; }
            else { sp--; if (sp > 0) { int pv = stack[sp - 1]; x[n] = vx[pv]; y[n] = vy[pv]; z[n++] = vz[pv]; } }
        }
        break;
    }
    case 23: {   /* HILBERT curve: A order 1..5 */
        int ord = 1 + (int)lroundf(A * 4), side = 1 << ord;
        n = side * side;
        closed = 0;
        for (int i = 0; i < n; i++) {
            int t = i, rx, ry, px = 0, py = 0;
            for (int sdl = 1; sdl < side; sdl <<= 1) {
                rx = 1 & (t / 2); ry = 1 & (t ^ rx);
                if (ry == 0) { if (rx == 1) { px = sdl - 1 - px; py = sdl - 1 - py; } int tmp = px; px = py; py = tmp; }
                px += sdl * rx; py += sdl * ry; t /= 4;
            }
            x[i] = (float)px; y[i] = (float)py;
        }
        break;
    }
    case 24: {   /* KOCH snowflake: A iterations 0..4 */
        int it = (int)lroundf(A * 4);
        static float kx[NPTS], ky[NPTS], tx[NPTS], ty[NPTS];
        int cnt = 3;
        for (int i = 0; i < 3; i++) { float a = -PI / 2 + TWO_PI * i / 3; kx[i] = cosf(a); ky[i] = sinf(a); }
        for (int q = 0; q < it && cnt * 4 < NPTS; q++) {
            int m2 = 0;
            for (int i = 0; i < cnt; i++) {
                int j = (i + 1) % cnt;
                float dx = (kx[j] - kx[i]) / 3, dy = (ky[j] - ky[i]) / 3;
                float ax = kx[i] + dx, ay = ky[i] + dy, bx = kx[i] + 2 * dx, by = ky[i] + 2 * dy;
                float px = ax + (bx - ax) * 0.5f + (by - ay) * 0.8660254f, py = ay + (by - ay) * 0.5f - (bx - ax) * 0.8660254f;
                tx[m2] = kx[i]; ty[m2++] = ky[i]; tx[m2] = ax; ty[m2++] = ay; tx[m2] = px; ty[m2++] = py; tx[m2] = bx; ty[m2++] = by;
            }
            memcpy(kx, tx, sizeof(float) * (size_t)m2); memcpy(ky, ty, sizeof(float) * (size_t)m2); cnt = m2;
        }
        n = cnt;
        for (int i = 0; i < n; i++) { x[i] = kx[i]; y[i] = ky[i]; }
        break;
    }
    case 25: {   /* FOURIER DRAWING: a shape rebuilt from its first circles. A: square, triangle, star, heart, LOADED shape; B: how many circles */
        int kind = (int)lroundf(A * 4), keep = 1 + (int)lroundf(B * 23);
        enum { M = 256 };
        static float sx_[M], sy_[M];
        for (int i = 0; i < M; i++) {
            float u = (float)i / M, t = TWO_PI * u;
            if (kind == 0) { float seg = u * 4; int e = (int)seg; float f = seg - e; static const float cx[5] = {-1, 1, 1, -1, -1}, cy[5] = {-1, -1, 1, 1, -1}; sx_[i] = cx[e] + (cx[e + 1] - cx[e]) * f; sy_[i] = cy[e] + (cy[e + 1] - cy[e]) * f; }
            else if (kind == 1) { float seg = u * 3; int e = (int)seg; float f = seg - e; float ax = cosf(-PI / 2 + TWO_PI * e / 3), ay = sinf(-PI / 2 + TWO_PI * e / 3), bx = cosf(-PI / 2 + TWO_PI * (e + 1) / 3), by = sinf(-PI / 2 + TWO_PI * (e + 1) / 3); sx_[i] = ax + (bx - ax) * f; sy_[i] = ay + (by - ay) * f; }
            else if (kind == 2) { float seg = u * 10; int e = (int)seg; float f = seg - e; float r0 = (e & 1) ? 0.4f : 1.0f, r1 = ((e + 1) & 1) ? 0.4f : 1.0f; float a0 = -PI / 2 + TWO_PI * e / 10, a1 = -PI / 2 + TWO_PI * (e + 1) / 10; float ax = r0 * cosf(a0), ay = r0 * sinf(a0), bx = r1 * cosf(a1), by = r1 * sinf(a1); sx_[i] = ax + (bx - ax) * f; sy_[i] = ay + (by - ay) * f; }
            else if (kind == 4 && paths_user) { int j = (int)(u * PATHN) % PATHN; sx_[i] = paths_user->x[j]; sy_[i] = paths_user->y[j]; }
            else { sx_[i] = 16 * powf(sinf(t), 3) / 16; sy_[i] = -(13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t)) / 16; }
        }
        static float cre[64], cim[64];
        static int cfq[64];
        for (int h = 0; h < 2 * keep + 1; h++) {   /* the circles: frequencies 0, +1, -1, +2, -2 ... of the shape's own transform */
            int fq = h == 0 ? 0 : (h & 1) ? (h + 1) / 2 : -(h / 2);
            float re = 0, im = 0;
            for (int j = 0; j < M; j++) { float a = -TWO_PI * fq * j / M; re += sx_[j] * cosf(a) - sy_[j] * sinf(a); im += sx_[j] * sinf(a) + sy_[j] * cosf(a); }
            cre[h] = re / M; cim[h] = im / M; cfq[h] = fq;
        }
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, xx = 0, yy = 0;
            for (int h = 0; h < 2 * keep + 1; h++) {
                xx += cre[h] * cosf(cfq[h] * t) - cim[h] * sinf(cfq[h] * t);
                yy += cre[h] * sinf(cfq[h] * t) + cim[h] * cosf(cfq[h] * t);
            }
            x[i] = xx; y[i] = yy;
        }
        break;
    }
    case 26: {   /* SQUEEZE: x*sin(1/x) wiggling in to the centre. A wiggle rate, B passes */
        float rate = mp(A, 0.5f, 3.0f);
        closed = 0;
        int half = n / 2;
        for (int i = 0; i < half; i++) {   /* in from the left wall, then back out along the right */
            float u = (float)i / (half - 1), xx = -(1 - u) + 0.0001f;
            x[i] = xx; y[i] = fabsf(xx) * sinf(rate * 0.2f / (fabsf(xx) + 0.02f) * (0.5f + B));
            x[n - 1 - i] = -xx; y[n - 1 - i] = -y[i];
        }
        break;
    }
    case 27: {   /* DE BROGLIE: a ring with a whole number of waves. A: waves 1..12, B: amplitude */
        int w = 1 + (int)lroundf(A * 11);
        float amp = mp(B, 0.05f, 0.35f);
        for (int i = 0; i < n; i++) { float t = TWO_PI * i / n, r = 1 + amp * sinf(w * t); x[i] = r * cosf(t); y[i] = r * sinf(t); }
        break;
    }
    case 28: case 29: case 30: {   /* COTES / POINSOT / ATOMIC spirals: A turns, B tightness, C offset */
        float turns = mp(A, 1, 6), k = mp(B, 0.2f, 1.5f), eps = mp(C, 0.0f, 2.0f);
        closed = 0;
        for (int i = 0; i < n; i++) {
            float u = (float)i / (n - 1), th = mp(u, 0.2f, 0.2f + TWO_PI * turns), r;
            if (m == 28) r = 1.0f / coshf(k * th * 0.5f + eps);
            else if (m == 29) r = 1.0f / sinhf(k * th * 0.5f + 0.15f + eps * 0.2f);
            else r = th / (th - 0.1f - eps * 0.2f * (1 - u) + 0.0001f);
            if (r > 6) r = 6;
            if (r < -6) r = -6;
            x[i] = r * cosf(th); y[i] = r * sinf(th);
        }
        break;
    }
    case 31: {   /* MANDALA: layered petals. A layers 1..10, B symmetry 2..12, C morph, D depth */
        int layers = 1 + (int)lroundf(A * 9), sym = 2 + (int)lroundf(B * 10);
        float depth = mp(D, 0.05f, 0.5f);
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * i / n, r = 1;
            for (int l = 0; l < layers; l++) r += depth / (l + 1) * cosf((float)(sym * (l + 1)) * t + C * TWO_PI * l * 0.37f);
            x[i] = r * cosf(t); y[i] = r * sinf(t);
        }
        break;
    }
    case 32: {   /* STACKED SPIRO: wheels on wheels. A wheels 2..3, B ratio 1, C ratio 2, D pen */
        int wh = 2 + (int)lroundf(A);
        float r1 = mp(B, 0.5f, 3.0f), r2 = mp(C, 0.5f, 5.0f) * 1.3f, d = mp(D, 0.2f, 1.0f);
        closed = 0;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * 24 * i / n;
            float xx = cosf(t) + 0.5f * cosf(r1 * t), yy = sinf(t) + 0.5f * sinf(r1 * t);
            if (wh == 3) { xx += d * 0.5f * cosf(r2 * t); yy += d * 0.5f * sinf(r2 * t); }
            x[i] = xx; y[i] = yy;
        }
        break;
    }
    case 33: {   /* TIMES TABLE: points on a circle joined i -> i*mult. A mult 2..20 (whole), B points 20..200 */
        int pts = 20 + (int)lroundf(B * 180), mult = 2 + (int)lroundf(A * 18);
        n = 0;
        closed = 0;
        for (int i = 0; i < pts && n < NPTS - 2; i++) {
            int j = (i * mult) % pts;
            float a = TWO_PI * i / pts, b = TWO_PI * j / pts;
            x[n] = cosf(a); y[n++] = sinf(a);
            x[n] = cosf(b); y[n++] = sinf(b);
        }
        break;
    }
    case 34: {   /* TOY GEARS: the real toy's rings (96, 105, 144, 150 teeth) and wheels. A ring, B wheel, C pen hole, D in/out */
        static const int RING[4] = {96, 105, 144, 150};
        static const int WHEEL[16] = {24, 30, 32, 36, 40, 42, 45, 48, 52, 56, 60, 63, 72, 75, 80, 84};
        int R = RING[(int)lroundf(A * 3)], r = WHEEL[(int)lroundf(B * 15)];
        int g = gcd_i(R, r), laps = r / g;
        float rr = (float)r, d = mp(C, 0.2f, 0.95f) * rr, Rf = (float)R;
        for (int i = 0; i < n; i++) {
            float t = TWO_PI * laps * i / n;
            if (D < 0.5f) { x[i] = (Rf - rr) * cosf(t) + d * cosf((Rf - rr) * t / rr); y[i] = (Rf - rr) * sinf(t) - d * sinf((Rf - rr) * t / rr); }
            else { x[i] = (Rf + rr) * cosf(t) - d * cosf((Rf + rr) * t / rr); y[i] = (Rf + rr) * sinf(t) - d * sinf((Rf + rr) * t / rr); }
        }
        break;
    }
    case 21: default: {   /* ADSR PATH PATH: attack up, decay down to sustain, hold, release; A..D shape the four stages */
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
