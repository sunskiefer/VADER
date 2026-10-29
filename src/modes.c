/* VADER 0.8: the MODES. A mode moves the orbiter by a simulation instead of along a shape's path. It is stepped once per
 * block per timbre (not per voice); the voices read where its balls are. Each mode reads up to eight knobs (a layer) 0..1.
 * Cheap on purpose: a few dozen flops per ball per block. Everything is deterministic from a seed (presets recall it). */
#include <math.h>
#include <string.h>
#include "vader.h"

#define PI 3.14159265358979f

const char *const MODE_NAMES[NMODES] = {
    "PATH", "CHASE", "RECAMAN", "METEOR RAIN", "KEPLER", "ROSETTE", "FIGURE 8", "LORENZ", "ROSSLER", "THOMAS",
    "AIZAWA", "PENDULUM", "BOUNCE", "RIPPLE"};

/* what the eight knobs of a layer-1 do, per mode (shown as the knobs' names) */
const char *const MODE_KNOBS[NMODES][8] = {
    {"", "", "", "", "", "", "", ""},
    {"SPEED", "BALLS", "DIRECTION", "PULL", "SPREAD", "TURN", "SIZE", "RESET"},
    {"STEPS/S", "SCALE", "AXIS", "ARC", "RANGE", "SEED", "SIZE", "RESET"},
    {"FALL", "RATE", "SPREAD", "SPEED", "LIFE", "SEED", "SIZE", "TURN"},
    {"SPEED", "ECCENTRIC", "PRECESS", "SIZE", "TILT", "WELL", "TURN", "PHASE"},
    {"SPEED", "ECCENTRIC", "PRECESS", "SIZE", "TILT", "WELL", "TURN", "PHASE"},
    {"SPEED", "SIZE", "TURN", "OFFSET", "BALLS", "SEED", "SPREAD", "RESET"},
    {"SPEED", "SIGMA", "RHO", "BETA", "SIZE", "TURN", "BALLS", "SEED"},
    {"SPEED", "A", "B", "C", "SIZE", "TURN", "BALLS", "SEED"},
    {"SPEED", "DAMP", "SIZE", "TURN", "BALLS", "SEED", "SPREAD", "RESET"},
    {"SPEED", "A", "B", "C", "SIZE", "TURN", "BALLS", "SEED"},
    {"SPEED", "LENGTH 1", "LENGTH 2", "MASS", "DAMP", "SEED", "SIZE", "KICK"},
    {"SPEED", "GRAVITY", "ELASTIC", "BALLS", "ANGLE", "SEED", "SIZE", "KICK"},
    {"SPEED", "WAVES", "DECAY", "SPREAD", "RATE", "SEED", "SIZE", "TURN"},
};

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
        if (mode == 3) {   /* the sound follows the newest live meteor */
            int i = s->last;
            if (s->life[i] <= 0) { s->sx[0] = s->sy[0] = 0; }
            else { s->sx[0] = s->px[i]; s->sy[0] = s->py[i]; }
        }
    }
}
