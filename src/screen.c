/* The screen, made of pieces MPC can swap (it draws a plugin's screen from pictures; a plugin can't draw on it):
 *  - TILE_COLS x TILE_ROWS tiles. Each shows one straight piece of line, stored by its angle and its distance from the
 *    tile's middle, both rounded (SHAPE_ANG x SHAPE_OFF for the shape, dim; TRAIL_ANG x TRAIL_OFF for the orbiter's
 *    trail, bright), or a dot at one of DOT_NX x DOT_NY places in the tile (the path point, the orbiter). A straight
 *    line has the same angle in every tile it crosses, so it stays straight from tile to tile.
 *  - The built-in 2D shapes are ready-drawn pictures (tools/vader_skin.py); the tiles draw only your own shapes and
 *    the 3D ones, plus the trail and the dots.
 *  - ENV_COLS columns draw the envelope, each a stroke between two rows.
 * The engine works out what each piece shows; only the ones that change are sent to MPC (engine.c). */
#include <math.h>
#include <string.h>
#include "vader.h"

#define PI_F 3.14159265f

/* one tile's piece, a to b (screen pixels): its frame, or 0 when it's too short to draw */
static int piece_frame(int cell, float ax, float ay, float bx, float by, int trail) {
    float dx = bx - ax, dy = by - ay;
    if (dx * dx + dy * dy < 0.64f) return 0;
    int A = trail ? TRAIL_ANG : SHAPE_ANG, OFF = trail ? TRAIL_OFF : SHAPE_OFF;
    float step = trail ? TRAIL_DSTEP : SHAPE_DSTEP;
    float th = atan2f(dy, dx);
    if (th < 0) th += PI_F;
    if (th >= PI_F) th -= PI_F;
    int ai = (int)lroundf(th / (PI_F / A)) % A;
    float tq = ai * (PI_F / A), nx = -sinf(tq), ny = cosf(tq);
    float cx = (cell % TILE_COLS) * TILE_W + TILE_W * 0.5f, cy = (cell / TILE_COLS) * TILE_H + TILE_H * 0.5f;
    float d = ((ax + bx) * 0.5f - cx) * nx + ((ay + by) * 0.5f - cy) * ny;
    int half = (OFF - 1) / 2, k = (int)lroundf(d / step);
    if (k < -half) k = -half;
    if (k > half) k = half;
    return (trail ? FR_TRAIL : FR_SHAPE) + ai * OFF + (k + half);
}

static int dot_frame(float px, float py, int base, int *cell) {
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > TILE_COLS * TILE_W - 0.01f) px = TILE_COLS * TILE_W - 0.01f;
    if (py > TILE_ROWS * TILE_H - 0.01f) py = TILE_ROWS * TILE_H - 0.01f;
    int c = (int)(px / TILE_W), r = (int)(py / TILE_H);
    int sx = (int)((px - c * TILE_W) / ((float)TILE_W / DOT_NX)), sy = (int)((py - r * TILE_H) / ((float)TILE_H / DOT_NY));
    if (sx > DOT_NX - 1) sx = DOT_NX - 1;
    if (sy > DOT_NY - 1) sy = DOT_NY - 1;
    *cell = r * TILE_COLS + c;
    return base + sy * DOT_NX + sx;
}

typedef struct { unsigned short *g; unsigned char done[TILE_COLS * TILE_ROWS]; int trail, cell; float ax, ay, bx, by; } walk_t;

static void finish(walk_t *w) {
    if (w->cell < 0 || w->done[w->cell]) return;
    int f = piece_frame(w->cell, w->ax, w->ay, w->bx, w->by, w->trail);
    if (f) { w->g[w->cell] = (unsigned short)f; w->done[w->cell] = 1; }
}

/* a straight segment in screen pixels, cut where it crosses tile edges: each tile's first visit is its piece */
static void segment(walk_t *w, float x0, float y0, float x1, float y1) {
    const float X = TILE_COLS * TILE_W - 0.001f, Y = TILE_ROWS * TILE_H - 0.001f;
    x0 = fminf(fmaxf(x0, 0), X); x1 = fminf(fmaxf(x1, 0), X);
    y0 = fminf(fmaxf(y0, 0), Y); y1 = fminf(fmaxf(y1, 0), Y);
    float dx = x1 - x0, dy = y1 - y0;
    for (int guard = 0; guard < 128; guard++) {
        int c = (int)((x0 + (dx > 0 ? 1e-4f : dx < 0 ? -1e-4f : 0)) / TILE_W);
        int r = (int)((y0 + (dy > 0 ? 1e-4f : dy < 0 ? -1e-4f : 0)) / TILE_H);
        if (c < 0) c = 0;
        if (c >= TILE_COLS) c = TILE_COLS - 1;
        if (r < 0) r = 0;
        if (r >= TILE_ROWS) r = TILE_ROWS - 1;
        int cell = r * TILE_COLS + c;
        float tx = dx > 0 ? ((c + 1) * TILE_W - x0) / dx : dx < 0 ? (c * TILE_W - x0) / dx : 2;
        float ty = dy > 0 ? ((r + 1) * TILE_H - y0) / dy : dy < 0 ? (r * TILE_H - y0) / dy : 2;
        float t = fminf(fminf(tx, ty), 1.0f);
        if (t < 0) t = 0;
        float xn = x0 + dx * t, yn = y0 + dy * t;
        if (cell != w->cell) {
            finish(w);
            w->cell = cell;
            w->ax = x0;
            w->ay = y0;
        }
        w->bx = xn;
        w->by = yn;
        if (t >= 1.0f) return;
        dx = x1 - xn; dy = y1 - yn;
        x0 = xn; y0 = yn;
        if (fabsf(dx) < 1e-5f && fabsf(dy) < 1e-5f) return;
    }
}

void screen_shape(screen_t *s, const float *x2, const float *y2, float size, int draw) {
    memset(s->shape, 0, sizeof s->shape);
    if (!draw) return;
    walk_t w;
    memset(&w, 0, sizeof w);
    w.g = s->shape;
    w.cell = -1;
    for (int i = 0; i < PATHN; i++) {
        int j = (i + 1) % PATHN;
        segment(&w, scr_x(x2[i] * size), scr_y(y2[i] * size), scr_x(x2[j] * size), scr_y(y2[j] * size));
    }
    finish(&w);
}

void screen_frame(screen_t *s, float fx, float fy, float ox, float oy, int motion) {
    memcpy(s->tiles, s->shape, sizeof s->tiles);
    int cell;
    if (!motion) {
        s->trail_n = 0;
        return;
    }
    if (s->trail_n < TRAIL_N) s->trail_n++;
    memmove(s->trail_x + 1, s->trail_x, sizeof(float) * (TRAIL_N - 1));
    memmove(s->trail_y + 1, s->trail_y, sizeof(float) * (TRAIL_N - 1));
    s->trail_x[0] = scr_x(ox);
    s->trail_y[0] = scr_y(oy);
    if (s->trail_n > 1) {   /* the suborbit's trail: bright line pieces */
        walk_t w;
        memset(&w, 0, sizeof w);
        w.g = s->tiles;
        w.trail = 1;
        w.cell = -1;
        for (int i = s->trail_n - 1; i > 0; i--) segment(&w, s->trail_x[i], s->trail_y[i], s->trail_x[i - 1], s->trail_y[i - 1]);
        finish(&w);
    }
    int f = dot_frame(scr_x(fx), scr_y(fy), FR_PATHDOT, &cell);
    s->tiles[cell] = (unsigned short)f;
    /* the orbiter: every tile it reaches draws its part of the dot */
    float px = scr_x(ox), py = scr_y(oy);
    for (int r = (int)floorf(py / TILE_H) - 1; r <= (int)floorf(py / TILE_H) + 1; r++)
        for (int c = (int)floorf(px / TILE_W) - 1; c <= (int)floorf(px / TILE_W) + 1; c++) {
            if (r < 0 || r >= TILE_ROWS || c < 0 || c >= TILE_COLS) continue;
            int ix = (int)lroundf((px - c * TILE_W - ORB_X0) / ORB_STEP), iy = (int)lroundf((py - r * TILE_H - ORB_Y0) / ORB_STEP);
            if (ix < 0 || ix >= ORB_NX || iy < 0 || iy >= ORB_NY) continue;
            s->tiles[r * TILE_COLS + c] = (unsigned short)(FR_ORBDOT + iy * ORB_NX + ix);
        }
}

int env_pair_index(int r0, int r1) {
    if (r0 > r1) { int t = r0; r0 = r1; r1 = t; }
    if (r0 < 0) r0 = 0;
    if (r1 >= ENV_ROWS) r1 = ENV_ROWS - 1;
    return r0 * ENV_ROWS - r0 * (r0 - 1) / 2 + (r1 - r0);
}

static float env_at(float u, float a, float d, float sus, float hold, float r) {
    if (u < a) return u / a;
    u -= a;
    if (u < d) return 1 - (1 - sus) * u / d;
    u -= d;
    if (u < hold) return sus;
    u -= hold;
    return u < r ? sus * (1 - u / r) : 0;
}

void screen_env(screen_t *s, float a, float d, float sus, float r) {
    /* stage widths: a little always, more with longer times (the knobs' own curve) */
    float A = 0.04f + a * a * 1.2f, D = 0.04f + d * d * 1.2f, H = 0.5f, R = 0.04f + r * r * 1.2f;
    float tot = A + D + H + R;
    for (int c = 0; c < ENV_COLS; c++) {
        float u0 = tot * c / ENV_COLS, u1 = tot * (c + 1) / ENV_COLS;
        float lo = 2, hi = -1;
        for (int k = 0; k <= 4; k++) {
            float v = env_at(u0 + (u1 - u0) * k / 4, A, D, sus, H, R);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        int r0 = (int)lroundf((1 - hi) * (ENV_ROWS - 1)), r1 = (int)lroundf((1 - lo) * (ENV_ROWS - 1));
        s->env[c] = (unsigned char)env_pair_index(r0, r1);
    }
}
