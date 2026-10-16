/* The screen, made of pieces MPC can swap: a grid of TILE_COLS x TILE_ROWS line tiles (each one picture of a filmstrip:
 * empty, a short line in four directions dim for the shape and bright for the orbiter's trail, the path dot and the
 * orbiter dot), and ENV_COLS columns that draw the envelope as a line (each column a short vertical stroke between two
 * rows). The engine works out which picture each piece shows; only the ones that change are sent to MPC. */
#include <math.h>
#include <string.h>
#include "vader.h"

/* x, y in -1..1 (the corners' square, shown wide on the screen) -> tile column / row */
static inline int tcol(float x) {
    int c = (int)floorf((x + 1) * 0.5f * TILE_COLS);
    return c < 0 ? 0 : c >= TILE_COLS ? TILE_COLS - 1 : c;
}
static inline int trow(float y) {
    int r = (int)floorf((y + 1) * 0.5f * TILE_ROWS);
    return r < 0 ? 0 : r >= TILE_ROWS ? TILE_ROWS - 1 : r;
}

/* the direction of a line on the screen (tiles are square): 0 -, 1 |, 2 /, 3 \ */
static int dir_of(float dx, float dy) {
    float ax = dx * 0.5f * TILE_COLS, ay = dy * 0.5f * TILE_ROWS;   /* in tile units */
    float a = atan2f(-ay, ax);                                      /* screen y points down */
    if (a < 0) a += 3.14159265f;                                    /* a line has no arrow: 0..pi */
    int k = (int)floorf(a / (3.14159265f / 4) + 0.5f) & 3;          /* 0 = 0, 1 = 45, 2 = 90, 3 = 135 degrees */
    static const int map[4] = {0, 2, 1, 3};
    return map[k];
}

static void line(unsigned char *g, float x0, float y0, float x1, float y1, int base, int over) {
    float dx = x1 - x0, dy = y1 - y0;
    int d = dir_of(dx, dy);
    float len = fmaxf(fabsf(dx) * 0.5f * TILE_COLS, fabsf(dy) * 0.5f * TILE_ROWS);
    int steps = (int)(len * 3) + 1;
    for (int s = 0; s <= steps; s++) {
        float t = (float)s / steps;
        int i = trow(y0 + dy * t) * TILE_COLS + tcol(x0 + dx * t);
        if (over || g[i] == TL_EMPTY) g[i] = (unsigned char)(base + d);
    }
}

void screen_shape(screen_t *s, const float *x2, const float *y2, float size) {
    memset(s->shape, TL_EMPTY, sizeof s->shape);
    for (int i = 0; i < PATHN; i += 2) {
        int j = (i + 2) % PATHN;
        line(s->shape, x2[i] * size, y2[i] * size, x2[j] * size, y2[j] * size, TL_SHAPE, 0);
    }
}

void screen_frame(screen_t *s, float fx, float fy, float ox, float oy, int motion) {
    memcpy(s->tiles, s->shape, sizeof s->tiles);
    if (!motion) {
        s->trail_n = 0;
        return;
    }
    if (s->trail_n < TRAIL_N) s->trail_n++;
    memmove(s->trail_x + 1, s->trail_x, sizeof(float) * (TRAIL_N - 1));
    memmove(s->trail_y + 1, s->trail_y, sizeof(float) * (TRAIL_N - 1));
    s->trail_x[0] = ox;
    s->trail_y[0] = oy;
    for (int i = s->trail_n - 1; i > 0; i--)
        line(s->tiles, s->trail_x[i], s->trail_y[i], s->trail_x[i - 1], s->trail_y[i - 1], TL_TRAIL, 1);
    s->tiles[trow(fy) * TILE_COLS + tcol(fx)] = TL_PATHDOT;
    s->tiles[trow(oy) * TILE_COLS + tcol(ox)] = TL_ORBDOT;
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
