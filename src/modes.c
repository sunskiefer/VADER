/* VADER 0.8: the MODES. A mode moves the orbiter by a simulation instead of along a shape's path. It is stepped once per
 * block per timbre (not per voice); the voices read where its balls are. Each mode reads up to eight knobs (a layer) 0..1.
 * Cheap on purpose: a few dozen flops per ball per block. Everything is deterministic from a seed (presets recall it). */
#include <math.h>
#include <string.h>
#include "vader.h"

#define PI 3.14159265358979f

const char *const MODE_NAMES[NMODES] = {
    "PATH", "CHASE", "RECAMAN", "METEOR RAIN", "KEPLER", "ROSETTE", "FIGURE 8", "LORENZ", "ROSSLER", "THOMAS",
    "AIZAWA", "PENDULUM", "BOUNCE", "RIPPLE", "MAGNET", "BOIDS", "PARTICLE WAVE", "OPTIMIZE", "TRANSFER ORBIT", "GRAVITY WELLS",
    "SERPENTINE DRAGON", "PARAMECIA", "MEDUSA", "FLATWORMS", "ETHEREAL RAY", "CLUMP SWARM", "THUNDER"};


static float frand(sim_t *s) {   /* 0..1 */
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(s->rng >> 8) * (1.0f / 16777216.0f);
}
static float clampf_(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float mp(float v, float lo, float hi) { return lo + (hi - lo) * v; }

void sim_reset(sim_t *s, int mode, const float *k) {
    unsigned seed = s->rng ? s->rng : 12345u;
    memset(s, 0, sizeof *s);
    s->rng = seed;
    s->mode = mode;
    s->n = 1;
    switch (mode) {
    case 1: {   /* CHASE: N balls on a ring, each chasing the next */
        s->n = 2 + (int)lroundf(k[1] * 6);
        for (int i = 0; i < s->n; i++) {
            float a = TWO_PI * i / s->n - PI / 2;
            s->px[i] = cosf(a);
            s->py[i] = sinf(a);
        }
        break;
    }
    case 2:  /* RECAMAN */
        s->a_prev = 0; s->a_n = 1; s->t = 0;
        memset(s->used, 0, sizeof s->used);
        s->used[0] = 1;
        break;
    case 3:  /* METEOR RAIN */
        s->n = 1;
        s->life[0] = -1;
        break;
    case 4: case 5:   /* KEPLER / ROSETTE */
        s->t = mp(k[7], 0, TWO_PI);
        break;
    case 6: {   /* FIGURE 8: the three-body choreography (Chenciner-Montgomery) */
        s->n = 3;
        s->px[0] = -0.97000436f; s->py[0] = 0.24308753f;
        s->px[1] = 0.97000436f; s->py[1] = -0.24308753f;
        s->px[2] = 0; s->py[2] = 0;
        s->vx[2] = -0.93240737f; s->vy[2] = -0.86473146f;
        s->vx[0] = s->vx[1] = 0.46620369f; s->vy[0] = s->vy[1] = 0.43236573f;
        break;
    }
    case 7: case 8: case 9: case 10: {   /* attractors: up to 4 dots from slightly different starts */
        s->n = 1 + (int)lroundf(k[6] * 3);
        for (int i = 0; i < s->n; i++) {
            float e = 0.01f * (i + 1) + 0.1f * k[7];
            if (mode == 7) { s->px[i] = 1 + e; s->py[i] = 1; s->pz[i] = 1; }
            else if (mode == 8) { s->px[i] = 0.1f + e; s->py[i] = 0; s->pz[i] = 0; }
            else if (mode == 9) { s->px[i] = 1.1f + e; s->py[i] = 1.1f; s->pz[i] = -0.01f; }
            else { s->px[i] = 0.1f + e; s->py[i] = 0; s->pz[i] = 0; }
        }
        break;
    }
    case 11:   /* PENDULUM: a double pendulum */
        s->a1 = 2.0f + k[5]; s->a2 = 2.5f - k[5]; s->w1 = s->w2 = 0;
        break;
    case 12: {   /* BOUNCE */
        s->n = 1 + (int)lroundf(k[3] * 3);
        for (int i = 0; i < s->n; i++) {
            float a = TWO_PI * frand(s);
            s->px[i] = 0.3f * cosf(a);
            s->py[i] = 0.3f * sinf(a);
            s->vx[i] = 0.5f * cosf(a + 1);
            s->vy[i] = 0.5f * sinf(a + 1);
        }
        break;
    }
    case 14: s->n = 1; s->px[0] = 0.3f; s->py[0] = -0.2f; s->vx[0] = 0.4f; s->vy[0] = 0.3f; break;
    case 15: case 21: case 24: case 25: {   /* BOIDS / PARAMECIA / ETHEREAL RAY / CLUMP: a crowd from the seed */
        s->n = 12 + (int)lroundf(k[1] * 40);
        if (s->n > SIM_N) s->n = SIM_N;
        for (int i = 0; i < s->n; i++) {
            float a = TWO_PI * frand(s), r = 0.2f + 0.7f * frand(s);
            s->px[i] = r * cosf(a); s->py[i] = r * sinf(a);
            float b = TWO_PI * frand(s);
            s->vx[i] = 0.3f * cosf(b); s->vy[i] = 0.3f * sinf(b);
            s->life[i] = frand(s);
        }
        break;
    }
    case 16: s->n = 1; s->px[0] = 0.2f; s->py[0] = 0.1f; break;
    case 17: {   /* OPTIMIZE: a few placed points and a ball */
        s->n = 1 + (int)lroundf(k[2] * 7);
        if (s->n < 2) s->n = 2;
        for (int i = 0; i < s->n; i++) { s->vx[i] = 2 * frand(s) - 1; s->vy[i] = 2 * frand(s) - 1; s->px[i] = 2 * frand(s) - 1; s->py[i] = 2 * frand(s) - 1; }
        s->a_n = 0;
        break;
    }
    case 19: s->n = 1; s->px[0] = 0.5f; s->py[0] = 0.0f; s->vx[0] = 0.0f; s->vy[0] = 0.8f; break;
    case 20: case 22: case 23: s->n = 8 + (int)lroundf(k[1] * 30); if (s->n > SIM_N) s->n = SIM_N; break;
    case 26: s->n = 1 + (int)lroundf(k[3] * 7); s->t = 0; s->rng ^= 0x9e3779b9u; break;
    default: break;
    }
}

/* 3D point turned and squeezed to the square */
static void turn3(sim_t *s, float x, float y, float z, float rate, float dt, float *ox, float *oy) {
    s->yaw += rate * dt * 0.4f;
    if (s->yaw > TWO_PI) s->yaw -= TWO_PI;
    float c = cosf(s->yaw), sn = sinf(s->yaw);
    *ox = x * c + z * sn;
    *oy = y;
}

/* step the mode by dt seconds. Fills s->px/py (the balls, -1..1) and s->n; the first ball is the one the sound follows */
void sim_step(sim_t *s, const float *k, float dt) {
    int mode = s->mode;
    float spd = 0.05f * powf(200.0f, k[0]);
    if (mode == 0) return;
    if (dt > 0.05f) dt = 0.05f;
    switch (mode) {
    case 1: {   /* CHASE */
        int n = s->n;
        float rate = spd * 0.6f * (k[2] < 0.5f ? 1.0f : -1.0f), pull = mp(k[3], 0.3f, 1.5f);
        float nx[8], ny[8];
        float sz = mp(k[6], 0.4f, 1.0f);
        float cx = 0, cy = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float dx = s->px[j] - s->px[i], dy = s->py[j] - s->py[i];
            float tx = -dy * mp(k[5], 0, 1), ty = dx * mp(k[5], 0, 1);   /* TURN: a sideways part */
            nx[i] = s->px[i] + (dx * pull + tx) * rate * dt;
            ny[i] = s->py[i] + (dy * pull + ty) * rate * dt;
        }
        for (int i = 0; i < n; i++) { s->px[i] = nx[i]; s->py[i] = ny[i]; cx += nx[i]; cy += ny[i]; }
        cx /= n; cy /= n;
        float r = 0;
        for (int i = 0; i < n; i++) { float dx = s->px[i] - cx, dy = s->py[i] - cy; r += sqrtf(dx * dx + dy * dy); }
        r /= n;
        if (r < 0.03f || k[7] > 0.5f && r < 0.2f) {   /* arrived at the middle: start again on the walls */
            sim_t keep = *s; (void)keep;
            float kk[8]; memcpy(kk, k, sizeof kk);
            sim_reset(s, 1, kk);
        }
        (void)sz;
        break;
    }
    case 2: {   /* RECAMAN: arcs along an axis; the ball rides the current semicircle */
        float steps = mp(k[0], 0.5f, 8.0f);
        s->t += steps * dt;
        while (s->t >= 1.0f) {
            s->t -= 1.0f;
            int a = s->a_n, p = (int)s->a_prev;
            int cand = p - a;
            if (cand > 0 && cand < 256 && !s->used[cand]) { /* back */ }
            else cand = p + a;
            s->a_from = (float)p;
            s->a_prev = (float)cand;
            if (cand < 256) s->used[cand] = 1;
            s->a_n++;
            if (s->a_n > 120 || cand >= 250) { sim_reset(s, 2, k); break; }
            s->a_dir = (s->a_n & 1) ? 1.0f : -1.0f;
        }
        float range = mp(k[4], 20.0f, 250.0f);
        float x0 = s->a_from, x1 = s->a_prev;
        float cxm = (x0 + x1) * 0.5f, rad = fabsf(x1 - x0) * 0.5f, dir = x1 > x0 ? 1.0f : -1.0f;
        float th = PI * s->t;
        float xx = cxm - dir * rad * cosf(th), yy = s->a_dir * rad * sinf(th) * mp(k[3], 0.3f, 1.2f);
        s->px[0] = clampf_((xx / range) * 2.0f - 1.0f, -1, 1);
        s->py[0] = clampf_(yy / range * 2.0f, -1, 1);
        break;
    }
    case 3: {   /* METEOR RAIN: meteors from the walls to the middle, the newest is the sound */
        float rate = mp(k[1], 0.3f, 8.0f), speed = mp(k[3], 0.1f, 2.0f);
        int nmax = 4 + (int)(k[2] * 60);
        if (nmax > 64) nmax = 64;
        s->spawn += rate * dt * (0.5f + spd);
        if (s->spawn >= 1.0f && s->n < nmax + 1) {
            s->spawn = 0;
            int i = -1;
            for (int j = 0; j < 64; j++) if (s->life[j] <= 0) { i = j; break; }
            if (i >= 0) {
                float a = TWO_PI * frand(s);
                s->px[i] = cosf(a); s->py[i] = sinf(a);
                s->vx[i] = -cosf(a) * speed * (0.6f + 0.8f * frand(s));
                s->vy[i] = -sinf(a) * speed * (0.6f + 0.8f * frand(s));
                s->life[i] = 1;
                s->last = i;
            }
        }
        int alive = 0;
        for (int i = 0; i < 64; i++) {
            if (s->life[i] <= 0) continue;
            s->px[i] += s->vx[i] * dt;
            s->py[i] += s->vy[i] * dt;
            if (s->px[i] * s->px[i] + s->py[i] * s->py[i] < 0.01f) s->life[i] = 0;   /* impact at the middle */
            else alive++;
        }
        s->n = 64;
        s->alive = alive;
        break;
    }
    case 4: case 5: {   /* KEPLER ellipse (4) / precessing ROSETTE (5): mean anomaly -> position */
        float e = mp(k[1], 0.0f, 0.9f), M, E;
        s->t += spd * 0.3f * dt;
        if (s->t > 1000.0f) s->t -= 1000.0f;
        M = s->t * TWO_PI * mp(1.0f, 1, 1);
        E = M;
        for (int i = 0; i < 6; i++) E = E - (E - e * sinf(E) - M) / (1 - e * cosf(E));
        float xx = cosf(E) - e, yy = sqrtf(1 - e * e) * sinf(E);
        float w = (mode == 5 ? k[2] * 0.15f : k[2] * 0.0f) * M + k[6] * TWO_PI + (mode == 4 ? k[2] * TWO_PI : 0);
        float cw = cosf(w), sw = sinf(w);
        float sz = mp(k[3], 0.3f, 1.0f) / (1 + e);
        s->px[0] = clampf_((xx * cw - yy * sw) * sz, -1, 1);
        s->py[0] = clampf_((xx * sw + yy * cw) * sz, -1, 1);
        break;
    }
    case 6: {   /* FIGURE 8 three-body (leapfrog) */
        float h = 0.002f * spd * 4, G = 1.0f;
        int steps = (int)ceilf(dt / 0.002f * spd * 4 / 1.0f);
        if (steps < 1) steps = 1;
        if (steps > 40) steps = 40;
        h = dt * spd * 4 / steps;
        for (int st = 0; st < steps; st++) {
            float ax[3] = {0}, ay[3] = {0};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    if (i == j) continue;
                    float dx = s->px[j] - s->px[i], dy = s->py[j] - s->py[i], r2 = dx * dx + dy * dy + 1e-4f, ir = 1 / (r2 * sqrtf(r2));
                    ax[i] += G * dx * ir; ay[i] += G * dy * ir;
                }
            for (int i = 0; i < 3; i++) {
                s->vx[i] += ax[i] * h * 0.5f; s->vy[i] += ay[i] * h * 0.5f;
                s->px[i] += s->vx[i] * h; s->py[i] += s->vy[i] * h;
                s->vx[i] += ax[i] * h * 0.5f; s->vy[i] += ay[i] * h * 0.5f;
            }
        }
        /* the balls shown are scaled copies; they keep the true state in cx/cy */
        for (int i = 0; i < 3; i++) { s->cx[i] = s->px[i]; s->cy[i] = s->py[i]; }
        break;
    }
    case 7: case 8: case 9: case 10: {   /* attractors */
        int steps = 1 + (int)(spd * 6);
        if (steps > 24) steps = 24;
        float h = dt * spd * 0.2f / steps;
        for (int i = 0; i < s->n; i++) {
            float x = s->px[i], y = s->py[i], z = s->pz[i];
            for (int st = 0; st < steps; st++) {
                float dx, dy, dz;
                if (mode == 7) { float sg = mp(k[1], 5, 16), rho = mp(k[2], 20, 40), be = mp(k[3], 1.5f, 4);
                    dx = sg * (y - x); dy = x * (rho - z) - y; dz = x * y - be * z; }
                else if (mode == 8) { float a = mp(k[1], 0.1f, 0.4f), b = mp(k[2], 0.1f, 0.4f), c = mp(k[3], 4, 9);
                    dx = -y - z; dy = x + a * y; dz = b + z * (x - c); }
                else if (mode == 9) { float b = mp(k[1], 0.15f, 0.3f);
                    dx = sinf(y) - b * x; dy = sinf(z) - b * y; dz = sinf(x) - b * z; h = h * 6; }
                else { float a = mp(k[1], 0.8f, 1.1f), b = mp(k[2], 0.5f, 0.9f), c = mp(k[3], 0.4f, 0.8f), d = 3.5f, e = 0.25f, f = 0.1f;
                    dx = (z - b) * x - d * y; dy = d * x + (z - b) * y;
                    dz = c + a * z - z * z * z / 3 - (x * x + y * y) * (1 + e * z) + f * z * x * x * x; }
                x += dx * h; y += dy * h; z += dz * h;
                if (x != x || fabsf(x) > 1e4f) { x = 1; y = 1; z = 1; }
            }
            s->px[i] = x; s->py[i] = y; s->pz[i] = z;
        }
        break;   /* projected below */
    }
    case 11: {   /* double pendulum */
        float g = 9.81f, l1 = mp(k[1], 0.5f, 1.5f), l2 = mp(k[2], 0.5f, 1.5f), m2 = mp(k[3], 0.3f, 2.0f), m1 = 1;
        float damp = 1 - k[4] * 0.002f;
        int steps = 8;
        float h = dt * spd * 2 / steps;
        for (int st = 0; st < steps; st++) {
            float d = s->a1 - s->a2, sd = sinf(d), cd = cosf(d);
            float den = 2 * m1 + m2 - m2 * cosf(2 * d);
            float a1 = (-g * (2 * m1 + m2) * sinf(s->a1) - m2 * g * sinf(s->a1 - 2 * s->a2) - 2 * sd * m2 * (s->w2 * s->w2 * l2 + s->w1 * s->w1 * l1 * cd)) / (l1 * den);
            float a2 = (2 * sd * (s->w1 * s->w1 * l1 * (m1 + m2) + g * (m1 + m2) * cosf(s->a1) + s->w2 * s->w2 * l2 * m2 * cd)) / (l2 * den);
            s->w1 = (s->w1 + a1 * h) * damp; s->w2 = (s->w2 + a2 * h) * damp;
            s->a1 += s->w1 * h; s->a2 += s->w2 * h;
        }
        float xx = l1 * sinf(s->a1) + l2 * sinf(s->a2), yy = -(l1 * cosf(s->a1) + l2 * cosf(s->a2));
        float sz = mp(k[6], 0.4f, 1.0f) / (l1 + l2);
        s->px[0] = clampf_(xx * sz, -1, 1);
        s->py[0] = clampf_(yy * sz, -1, 1);
        break;
    }
    case 12: {   /* BOUNCE */
        float g = (k[1] - 0.5f) * 3.0f, el = mp(k[2], 0.7f, 1.0f);
        for (int i = 0; i < s->n; i++) {
            s->vy[i] -= g * dt * spd;
            s->px[i] += s->vx[i] * dt * spd * 4;
            s->py[i] += s->vy[i] * dt * spd * 4;
            if (s->px[i] > 1) { s->px[i] = 1; s->vx[i] = -fabsf(s->vx[i]) * el; }
            if (s->px[i] < -1) { s->px[i] = -1; s->vx[i] = fabsf(s->vx[i]) * el; }
            if (s->py[i] > 1) { s->py[i] = 1; s->vy[i] = -fabsf(s->vy[i]) * el; }
            if (s->py[i] < -1) { s->py[i] = -1; s->vy[i] = fabsf(s->vy[i]) * el; if (fabsf(s->vy[i]) < 0.05f) s->vy[i] = 0.9f; }
        }
        break;
    }
    case 13: {   /* RIPPLE: a point riding a standing-wave surface's gradient */
        s->t += spd * 0.5f * dt;
        float w = 1 + roundf(k[1] * 6), dec = mp(k[2], 0.0f, 0.5f);
        float r = fabsf(sinf(s->t * 0.7f)) * expf(-dec * s->t * 0.1f);
        float a = s->t * w * 0.5f;
        s->px[0] = clampf_(r * cosf(a * 2), -1, 1);
        s->py[0] = clampf_(r * sinf(a * 2 + k[3]), -1, 1);
        if (s->t > 60) s->t = 0;
        break;
    }
    case 14: {   /* MAGNET: four corner magnets, attract or repel (PULL), the ball keeps its momentum */
        float pull = (k[1] - 0.5f) * 6.0f, damp = mp(k[2], 0.2f, 1.5f);
        static const float cxs[4] = {-1, 1, -1, 1}, cys[4] = {-1, -1, 1, 1};
        float h = dt * spd * 2;
        for (int c = 0; c < 4; c++) {
            float dx = cxs[c] - s->px[0], dy = cys[c] - s->py[0], r2 = dx * dx + dy * dy + 0.05f;
            s->vx[0] += pull * dx / r2 * h; s->vy[0] += pull * dy / r2 * h;
        }
        float dk = 1 - damp * h * 0.5f;
        s->vx[0] *= dk; s->vy[0] *= dk;
        float sp2 = s->vx[0] * s->vx[0] + s->vy[0] * s->vy[0];
        if (sp2 < 0.04f) { s->vx[0] += 0.3f * (frand(s) - 0.5f); s->vy[0] += 0.3f * (frand(s) - 0.5f); }
        s->px[0] += s->vx[0] * h; s->py[0] += s->vy[0] * h;
        if (fabsf(s->px[0]) > 1) { s->px[0] = s->px[0] > 0 ? 1 : -1; s->vx[0] = -s->vx[0]; }
        if (fabsf(s->py[0]) > 1) { s->py[0] = s->py[0] > 0 ? 1 : -1; s->vy[0] = -s->vy[0]; }
        break;
    }
    case 15: case 21: {   /* BOIDS (15) / PARAMECIA (21): cohesion, alignment, separation; paramecia add an alarm that packs the group */
        int n = s->n;
        float coh = mp(k[2], 0.0f, 1.0f), sep = mp(k[3], 0.1f, 1.0f), h = dt * spd * 2;
        float mx = 0, my = 0, ax = 0, ay = 0;
        for (int i = 0; i < n; i++) { mx += s->px[i]; my += s->py[i]; ax += s->vx[i]; ay += s->vy[i]; }
        mx /= n; my /= n; ax /= n; ay /= n;
        if (mode == 21) {   /* the alarm: every so often the whole group packs into a pill and turns */
            s->spawn += dt * (0.1f + k[4] * 0.5f);
            if (s->spawn > 1.0f) s->spawn = -0.4f;
        }
        int alarm = mode == 21 && s->spawn < 0;
        for (int i = 0; i < n; i++) {
            float fx = (mx - s->px[i]) * coh * (alarm ? 4.0f : 1.0f), fy = (my - s->py[i]) * coh * (alarm ? 4.0f : 1.0f);
            fx += (ax - s->vx[i]) * 0.5f; fy += (ay - s->vy[i]) * 0.5f;
            for (int j = 0; j < n; j += 3) {
                int q = (i + j + 1) % n;
                float dx = s->px[i] - s->px[q], dy = s->py[i] - s->py[q], d2 = dx * dx + dy * dy + 0.001f;
                if (d2 < 0.04f) { fx += dx / d2 * sep * 0.02f; fy += dy / d2 * sep * 0.02f; }
            }
            if (fabsf(s->px[i]) > 0.9f) fx -= s->px[i] * 2; if (fabsf(s->py[i]) > 0.9f) fy -= s->py[i] * 2;
            s->vx[i] += fx * h; s->vy[i] += fy * h;
            float sp = sqrtf(s->vx[i] * s->vx[i] + s->vy[i] * s->vy[i]) + 1e-6f, want = alarm ? 0.5f : 0.35f;
            s->vx[i] *= want / sp; s->vy[i] *= want / sp;
            s->px[i] += s->vx[i] * h; s->py[i] += s->vy[i] * h;
        }
        /* ball 0 is the flock's centre: the sound follows the group */
        float cx0 = 0, cy0 = 0;
        for (int i = 0; i < n; i++) { cx0 += s->px[i]; cy0 += s->py[i]; }
        s->cx[0] = cx0 / n; s->cy[0] = cy0 / n;
        break;
    }
    case 16: {   /* PARTICLE WAVE: the ball climbs a ripple-interference surface from two sources */
        s->t += spd * 0.3f * dt;
        float w = 4 + k[1] * 12, drift = mp(k[2], 0.0f, 1.0f);
        float sx1 = 0.6f * cosf(s->t * drift), sy1 = 0.6f * sinf(s->t * drift), sx2 = -sx1, sy2 = -sy1;
        float gx = 0, gy = 0, h = dt * spd * 0.4f;
        for (int q = 0; q < 2; q++) {
            float cx = q ? sx2 : sx1, cy = q ? sy2 : sy1;
            float dx = s->px[0] - cx, dy = s->py[0] - cy, r = sqrtf(dx * dx + dy * dy) + 0.02f;
            float ph = w * r - s->t * 6;
            float dr = -cosf(ph) * w / (1 + 3 * r);   /* the surface's slope */
            gx += dr * dx / r; gy += dr * dy / r;
        }
        s->vx[0] = s->vx[0] * 0.95f + gx * h; s->vy[0] = s->vy[0] * 0.95f + gy * h;
        s->px[0] += s->vx[0]; s->py[0] += s->vy[0];
        s->px[0] = clampf_(s->px[0], -1, 1); s->py[0] = clampf_(s->py[0], -1, 1);
        if (s->px[0] * s->px[0] + s->py[0] * s->py[0] < 1e-6f) s->px[0] = 0.1f;
        break;
    }
    case 17: {   /* OPTIMIZE: knob 1 picks SEEK, SETTLE, RANDOM SEARCH, SHORTEST TOUR, SPREAD */
        int type = (int)(k[1] * 4.99f), n = s->n;
        float h = dt * spd * 0.8f;
        if (type == 4) {   /* SPREAD: the points push each other apart (packing); ball 0 follows point 0 */
            for (int i = 0; i < n; i++) {
                float fx = 0, fy = 0;
                for (int j = 0; j < n; j++) { if (i == j) continue; float dx = s->px[i] - s->px[j], dy = s->py[i] - s->py[j], d2 = dx * dx + dy * dy + 0.01f; fx += dx / d2; fy += dy / d2; }
                fx -= s->px[i] * 0.6f; fy -= s->py[i] * 0.6f;
                s->px[i] = clampf_(s->px[i] + fx * h * 0.1f, -0.95f, 0.95f); s->py[i] = clampf_(s->py[i] + fy * h * 0.1f, -0.95f, 0.95f);
            }
            s->cx[0] = s->px[0]; s->cy[0] = s->py[0];
            break;
        }
        float *bx = &s->cx[0], *by = &s->cy[0];   /* the ball */
        if (s->t == 0) { *bx = 0; *by = 0; s->t = 1; s->a_n = 0; }
        int tgt = s->a_n % n;
        float tx = s->vx[tgt], ty = s->vy[tgt];
        if (type == 3) {   /* TOUR: go to the nearest point not yet visited this lap */
            if (s->used[0] == 0 && tgt == s->a_n % n) { /* choose nearest unvisited */ }
        }
        float dx = tx - *bx, dy = ty - *by, d = sqrtf(dx * dx + dy * dy);
        float ease = type == 1 ? 1.5f : type == 0 ? 1.0f : 2.0f;
        *bx += dx * ease * h; *by += dy * ease * h;
        if (type == 1) { s->w1 += dx * h; *bx += s->w1 * h * 0.3f; s->w1 *= 0.9f; }   /* SETTLE: overshoot and ring */
        if (d < 0.05f) {
            if (type == 3) {   /* nearest unvisited */
                s->used[tgt] = 1;
                int best = -1; float bd = 1e9f;
                for (int j = 0; j < n; j++) if (!s->used[j]) { float ex = s->vx[j] - *bx, ey = s->vy[j] - *by, e2 = ex * ex + ey * ey; if (e2 < bd) { bd = e2; best = j; } }
                if (best < 0) { memset(s->used, 0, 16); best = 0; }
                s->a_n = best;
            } else if (type == 2) {   /* RANDOM SEARCH: a new random point, kept if closer to the middle than the last */
                s->vx[tgt] = 2 * frand(s) - 1; s->vy[tgt] = 2 * frand(s) - 1;
                s->a_n++;
            } else { s->vx[tgt] = 2 * frand(s) - 1; s->vy[tgt] = 2 * frand(s) - 1; s->a_n++; }
        }
        *bx = clampf_(*bx, -1, 1); *by = clampf_(*by, -1, 1);
        break;
    }
    case 18: {   /* TRANSFER ORBIT: round the inner circle, out along an ellipse, round the outer, back */
        s->t += spd * 0.15f * dt;
        float u = s->t - floorf(s->t), in = mp(k[1], 0.15f, 0.5f), out = mp(k[2], 0.6f, 1.0f), a;
        float r;
        if (u < 0.25f) { a = u / 0.25f * TWO_PI; r = in; }
        else if (u < 0.5f) { float f = (u - 0.25f) / 0.25f; a = f * PI; r = (in + out) / 2 - (out - in) / 2 * cosf(f * PI); a = a; r = r; }
        else if (u < 0.75f) { a = (u - 0.5f) / 0.25f * TWO_PI + PI; r = out; }
        else { float f = (u - 0.75f) / 0.25f; a = f * PI + PI; r = (in + out) / 2 + (out - in) / 2 * cosf(f * PI); }
        a += k[3] * TWO_PI;
        s->px[0] = r * cosf(a); s->py[0] = r * sinf(a);
        break;
    }
    case 19: {   /* GRAVITY WELLS: the four corners pull; the ball swings between them */
        float G = mp(k[1], 0.2f, 2.0f), damp = mp(k[2], 0.0f, 0.3f), h = dt * spd * 1.5f;
        static const float cxs[4] = {-1, 1, -1, 1}, cys[4] = {-1, -1, 1, 1};
        int steps = 4;
        for (int st = 0; st < steps; st++) {
            float hh = h / steps;
            for (int c = 0; c < 4; c++) {
                float dx = cxs[c] * 0.9f - s->px[0], dy = cys[c] * 0.9f - s->py[0], r2 = dx * dx + dy * dy + 0.04f, ir = 1 / (r2 * sqrtf(r2));
                s->vx[0] += G * dx * ir * hh * 0.2f; s->vy[0] += G * dy * ir * hh * 0.2f;
            }
            s->vx[0] *= 1 - damp * hh; s->vy[0] *= 1 - damp * hh;
            s->px[0] += s->vx[0] * hh; s->py[0] += s->vy[0] * hh;
        }
        if (fabsf(s->px[0]) > 1 || fabsf(s->py[0]) > 1) { s->px[0] = clampf_(s->px[0], -1, 1); s->py[0] = clampf_(s->py[0], -1, 1); s->vx[0] *= -0.8f; s->vy[0] *= -0.8f; }
        break;
    }
    case 20: {   /* SERPENTINE DRAGON: a log spiral with a sine wiggle; the head leads, the body follows behind in the phase */
        s->t += spd * 0.1f * dt;
        float a = mp(k[2], 0.05f, 0.4f), b = mp(k[3], 0.05f, 0.25f), c = mp(k[4], 0.0f, 0.2f), wig = 2 + k[5] * 10;
        for (int i = 0; i < s->n; i++) {
            float th = (s->t - i * 0.012f) * TWO_PI * 3, ph = th - floorf(th / (TWO_PI * 4)) * TWO_PI * 4;
            float r = a * expf(b * ph) + c * sinf(wig * ph);
            r = clampf_(r, 0, 1);
            s->px[i] = r * cosf(ph); s->py[i] = r * sinf(ph);
        }
        break;
    }
    case 22: {   /* MEDUSA: dots pulse in and out together; the slower ones trail as tentacles */
        s->t += spd * 0.4f * dt;
        float lag = mp(k[2], 0.0f, 1.5f);
        for (int i = 0; i < s->n; i++) {
            float a = TWO_PI * i / s->n, ph = s->t * TWO_PI - lag * (float)(i % 5) * 0.6f;
            float r = 0.15f + 0.8f * (0.5f + 0.5f * sinf(ph));
            s->px[i] = r * cosf(a); s->py[i] = r * sinf(a) * mp(k[3], 0.6f, 1.0f);
        }
        break;
    }
    case 23: {   /* FLATWORMS: a chain stretches forward then pulls its tail up, walking round a ring */
        s->t += spd * 0.15f * dt;
        float len = mp(k[2], 0.3f, 1.2f), inch = 0.5f + 0.5f * sinf(s->t * TWO_PI * 4);
        for (int i = 0; i < s->n; i++) {
            float f = (float)i / (s->n - 1), pos = s->t * TWO_PI - f * len * (0.6f + 0.4f * inch);
            float r = 0.75f + 0.1f * sinf(f * 6 + s->t * 6);
            s->px[i] = r * cosf(pos); s->py[i] = r * sinf(pos);
        }
        break;
    }
    case 24: {   /* ETHEREAL RAY: a school gliding as a flapping sheet; the wings beat and the body swims across */
        s->t += spd * 0.2f * dt;
        float flap = mp(k[2], 1.0f, 6.0f), sw = mp(k[3], 0.3f, 0.9f);
        for (int i = 0; i < s->n; i++) {
            float f = 2.0f * ((float)i / (s->n - 1)) - 1.0f;
            float cx = sw * cosf(s->t * 1.3f), cy = sw * 0.6f * sinf(s->t * 0.9f);
            s->px[i] = clampf_(cx + f * 0.45f, -1, 1);
            s->py[i] = clampf_(cy + 0.3f * sinf(flap * s->t * 3 - fabsf(f) * 3) * (1 - 0.4f * fabsf(f)) * (f < 0 ? 1 : 1), -1, 1);
        }
        break;
    }
    case 25: {   /* CLUMP SWARM: pairwise pull (range L) and push; gravity drops the dots to a floor clump */
        int n = s->n;
        float F = mp(k[2], 0.1f, 2.0f), L = mp(k[3], 0.1f, 1.5f), grav = k[4] * 0.8f, h = dt * spd * 0.8f;
        for (int i = 0; i < n; i++) {
            float fx = 0, fy = -grav;
            for (int j = 0; j < n; j += 2) {
                int q = (i + j + 1) % n;
                float dx = s->px[q] - s->px[i], dy = s->py[q] - s->py[i], d = sqrtf(dx * dx + dy * dy) + 0.02f;
                float kern = -(-F * expf(-d / L) + expf(-d));   /* pull at range, push when close */
                fx += kern * dx / d * 0.15f; fy += kern * dy / d * 0.15f;
            }
            s->vx[i] = (s->vx[i] + fx * h) * 0.9f; s->vy[i] = (s->vy[i] + fy * h) * 0.9f;
        }
        for (int i = 0; i < n; i++) {
            s->px[i] = clampf_(s->px[i] + s->vx[i] * h, -1, 1); s->py[i] = clampf_(s->py[i] + s->vy[i] * h, -1, 1);
        }
        float cx0 = 0, cy0 = 0;
        for (int i = 0; i < n; i++) { cx0 += s->px[i]; cy0 += s->py[i]; }
        s->cx[0] = cx0 / n; s->cy[0] = cy0 / n;
        break;
    }
    case 26: {   /* THUNDER: a jagged bolt from the sky to the ground from a seed; the ball strikes down it, the rest follow late */
        s->t += spd * 0.5f * dt;
        unsigned seed = s->rng;
        int bolt = (int)floorf(s->t / 3.0f);
        float u = (s->t - bolt * 3.0f) / 1.0f;   /* the strike takes a third of the cycle, then the rumble */
        int segs = 24;
        float jag = mp(k[2], 0.05f, 0.4f);
        unsigned r = seed ^ (unsigned)(bolt * 2654435761u);
        float xs[25];
        xs[0] = (float)((r >> 4) & 255) / 255.0f - 0.5f;
        for (int i = 1; i <= segs; i++) {
            r = r * 1664525u + 1013904223u;
            xs[i] = clampf_(xs[i - 1] + (((float)((r >> 8) & 255) / 255.0f) - 0.5f) * jag * 2.0f, -0.9f, 0.9f);
        }
        for (int b = 0; b < s->n; b++) {
            float uu = clampf_(u - b * mp(k[4], 0.02f, 0.2f), 0, 1);
            float f = uu * segs; int i0 = (int)f; if (i0 >= segs) i0 = segs - 1;
            float fr = f - i0;
            s->px[b] = xs[i0] + (xs[i0 + 1] - xs[i0]) * fr;
            s->py[b] = 1.0f - 2.0f * uu;
        }
        break;
    }
    default: break;
    }
    /* project the 3D ones (and size/shift the balls) into the square, ball 0 first */
    if (mode >= 7 && mode <= 10) {
        for (int i = 0; i < s->n; i++) {
            float x = s->px[i], y = s->py[i], z = s->pz[i], a, b;
            if (mode == 7) { x /= 22; y = (z - 25) / 25; z = 0; }
            else if (mode == 8) { x /= 12; y /= 12; z /= 12; }
            else if (mode == 9) { x /= 4.5f; y /= 4.5f; z /= 4.5f; }
            else { x /= 1.5f; y /= 1.5f; z = (z - 0.7f) / 1.2f; }
            turn3(s, x, y, z, k[5], i == 0 ? dt : 0, &a, &b);
            float sz = mp(k[4], 0.4f, 1.0f);
            s->sx[i] = clampf_(a * sz, -1, 1);
            s->sy[i] = clampf_(b * sz, -1, 1);
        }
    } else if (mode == 6) {
        float sz = mp(k[1], 0.4f, 1.0f);
        s->yaw += (k[2] - 0.5f) * dt * 2;
        float c = cosf(s->yaw), sn = sinf(s->yaw);
        for (int i = 0; i < s->n; i++) {
            float x = s->cx[i] * 0.95f, y = s->cy[i] * 0.95f;
            s->sx[i] = clampf_((x * c - y * sn) * sz, -1, 1);
            s->sy[i] = clampf_((x * sn + y * c) * sz, -1, 1);
        }
    } else {
        for (int i = 0; i < s->n; i++) { s->sx[i] = s->px[i]; s->sy[i] = s->py[i]; }
        if (mode == 15 || mode == 21 || mode == 25) {   /* a crowd: ball 0 is its centre (the sound), the dots follow */
            for (int i = s->n - 1; i > 0; i--) { s->sx[i] = s->px[i]; s->sy[i] = s->py[i]; }
            s->sx[0] = s->cx[0]; s->sy[0] = s->cy[0];
        }
        if (mode == 17) {
            int tp = (int)(k[1] * 4.99f);
            if (tp != 4) { for (int i = s->n - 1; i > 0; i--) { s->sx[i] = s->vx[i]; s->sy[i] = s->vy[i]; } s->sx[0] = s->cx[0]; s->sy[0] = s->cy[0]; }
            else { s->sx[0] = s->cx[0]; s->sy[0] = s->cy[0]; }
        }
        if (mode == 3) {   /* the sound follows the newest live meteor */
            int i = s->last;
            if (s->life[i] <= 0) { s->sx[0] = s->sy[0] = 0; }
            else { s->sx[0] = s->px[i]; s->sy[0] = s->py[i]; }
        }
    }
}
