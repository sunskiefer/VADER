/* The screen, made of pieces MPC can swap (it draws a plugin's screen from pictures; a plugin can't draw on it):
 *  - TILE_COLS x TILE_ROWS tiles. Each shows one straight piece of line from a point on its edge to another (0.4.3).
 *    The points sit at fixed places along each edge (EDGE_NX x EDGE_NY), the same on both sides of an edge two tiles
 *    share, so a line leaves one tile exactly where it enters the next and stays unbroken. A second layer of tiles
 *    draws the light trail the same way (TRAIL_NX x TRAIL_NY, in TRAIL_LV brightnesses).
 *  - The built-in 2D shapes are ready-drawn pictures (tools/vader_skin.py); the tiles draw only your own shapes and
 *    the 3D ones, plus the orbit line and the trail. The two balls have layers of their own (screen_balls).
 *  - ENV_COLS columns draw the envelope, each a stroke between two rows.
 * The engine works out what each piece shows; only the ones that change are sent to MPC (engine.c). */
#include <math.h>
#include <string.h>
#include "vader.h"

#define PI_F 3.14159265f

/* the place on a tile's edge nearest to (lx, ly) (tile pixels), numbered round the tile: the top edge left to right,
 * the right edge top to bottom, the bottom edge right to left, the left edge bottom to top. NX places on the top and
 * bottom, NY on the sides, each in the middle of its stretch: the tile next door has the same places on the shared edge. */
static int edge_point(float lx, float ly, int NX, int NY) {
    float dt = ly, db = TILE_H - ly, dl = lx, dr = TILE_W - lx;
    int i = (int)floorf(lx / ((float)TILE_W / NX)), j = (int)floorf(ly / ((float)TILE_H / NY));
    if (i < 0) i = 0;
    if (i > NX - 1) i = NX - 1;
    if (j < 0) j = 0;
    if (j > NY - 1) j = NY - 1;
    float m = fminf(fminf(dt, db), fminf(dl, dr));
    if (m == dt) return i;
    if (m == dr) return NX + j;
    if (m == db) return NX + NY + (NX - 1 - i);
    return 2 * NX + NY + (NY - 1 - j);
}

static int pair_index(int i, int j, int n) {
    if (i > j) { int t = i; i = j; j = t; }
    return i * (2 * n - i - 1) / 2 + (j - i - 1);
}

/* one tile's piece, a to b (screen pixels): its frame, or 0 when both ends land on the same place. The shape's and the
 * orbit line's pieces are frames of the main tiles; the trail's (trail 1..TRAIL_LV: its brightness) of its own layer. */
static int piece_frame(int cell, float ax, float ay, float bx, float by, int trail) {
    float x0 = (cell % TILE_COLS) * TILE_W, y0 = (cell / TILE_COLS) * TILE_H;
    int NX = trail ? TRAIL_NX : EDGE_NX, NY = trail ? TRAIL_NY : EDGE_NY, N = 2 * (NX + NY);
    int i = edge_point(ax - x0, ay - y0, NX, NY), j = edge_point(bx - x0, by - y0, NX, NY);
    if (i == j) return 0;
    int k = pair_index(i, j, N);
    if (trail) return 1 + (trail - 1) * (N * (N - 1) / 2) + k;
    return 1 + k;
}

typedef struct {
    unsigned short *g, *g2;                        /* g2: a second layer, for a tile the line crosses again (or NULL) */
    float best[TILE_COLS * TILE_ROWS], best2[TILE_COLS * TILE_ROWS];
    unsigned char lv[TILE_COLS * TILE_ROWS];
    int trail, cell;
    float ax, ay, bx, by;
} walk_t;

/* a tile the line crosses more than once keeps its longest piece (the trail: of its brightest, newest part); with a
 * second layer, its second longest goes there */
static void finish(walk_t *w) {
    int c = w->cell;
    if (c < 0) return;
    if (w->lv[c] && w->lv[c] != (w->trail ? w->trail : 1)) return;
    float l = hypotf(w->bx - w->ax, w->by - w->ay);
    int f = piece_frame(c, w->ax, w->ay, w->bx, w->by, w->trail);
    if (!f) return;
    if (l > w->best[c]) {
        if (w->g2 && w->g[c] && w->g[c] != f) { w->g2[c] = w->g[c]; w->best2[c] = w->best[c]; }
        w->g[c] = (unsigned short)f;
        w->best[c] = l;
        w->lv[c] = (unsigned char)(w->trail ? w->trail : 1);
    } else if (w->g2 && l > w->best2[c] && f != w->g[c]) {
        w->g2[c] = (unsigned short)f;
        w->best2[c] = l;
    }
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

void screen_frame(screen_t *s, float fx, float fy, float ox, float oy, const float *lx, const float *ly, int ln,
                  const float *tx, const float *ty, int tn, int motion) {
    memcpy(s->tiles, s->shape, sizeof s->tiles);
    memset(s->trail, 0, sizeof s->trail);
    memset(s->orbit, 0, sizeof s->orbit);
    if (!motion) return;
    if (ln > 1) {   /* the orbit line, faint: its longest piece in each tile on the main tiles (where the shape leaves
                     * room), and the second one (where it crosses itself, or the shape) on its own layer */
        static unsigned short l1[TILE_COLS * TILE_ROWS], l2[TILE_COLS * TILE_ROWS];
        static walk_t w;
        memset(l1, 0, sizeof l1);
        memset(l2, 0, sizeof l2);
        memset(&w, 0, sizeof w);
        w.g = l1;
        w.g2 = l2;
        w.cell = -1;
        for (int i = 0; i < ln - 1; i++) segment(&w, scr_x(lx[i]), scr_y(ly[i]), scr_x(lx[i + 1]), scr_y(ly[i + 1]));
        finish(&w);
        for (int i = 0; i < TILE_COLS * TILE_ROWS; i++) {
            if (!s->tiles[i]) { s->tiles[i] = l1[i] ? (unsigned short)(l1[i] + EDGE_PAIRS) : 0; s->orbit[i] = l2[i]; }
            else s->orbit[i] = l1[i];
        }
    }
    if (tn > 1) {   /* the light trail, newest first: a tile keeps the newest, brightest piece */
        walk_t w;
        memset(&w, 0, sizeof w);
        w.g = s->trail;
        w.cell = -1;
        for (int i = 0; i < tn - 1; i++) {
            int lv = 1 + i * TRAIL_LV / (tn - 1);
            if (lv > TRAIL_LV) lv = TRAIL_LV;
            if (lv != w.trail) { finish(&w); w.cell = -1; w.trail = lv; }
            segment(&w, scr_x(tx[i]), scr_y(ty[i]), scr_x(tx[i + 1]), scr_y(ty[i + 1]));
        }
        finish(&w);
    }
}

/* one ball on its layer of cells: the cell whose square holds its centre shows it at its place there, every other cell
 * is empty. The centre is kept a radius from the screen's edges, so the whole ball is always on the screen. */
static void ball(unsigned char *cells, float px, float py, int d) {
    float r = d * 0.5f, X = TILE_COLS * TILE_W, Y = TILE_ROWS * TILE_H;
    px = fminf(fmaxf(px, r), X - r);
    py = fminf(fmaxf(py, r), Y - r);
    memset(cells, 0, BALL_N * BALL_N);
    float lx = px - SQUARE_X, ly = py;   /* on the corners' square, where the cells are */
    int c = (int)floorf(lx / BALL_PITCH), rr = (int)floorf(ly / BALL_PITCH);
    if (c < 0) c = 0;
    if (c > BALL_N - 1) c = BALL_N - 1;
    if (rr < 0) rr = 0;
    if (rr > BALL_N - 1) rr = BALL_N - 1;
    int ix = (int)floorf((lx - c * BALL_PITCH) / BALL_STEP), iy = (int)floorf((ly - rr * BALL_PITCH) / BALL_STEP);
    if (ix < 0) ix = 0;
    if (ix > BALL_POS - 1) ix = BALL_POS - 1;
    if (iy < 0) iy = 0;
    if (iy > BALL_POS - 1) iy = BALL_POS - 1;
    cells[rr * BALL_N + c] = (unsigned char)(1 + iy * BALL_POS + ix);
}

static int colour_step(float size) {
    int k = (int)floorf(size * BALL_COLS);
    return k < 0 ? 0 : k > BALL_COLS - 1 ? BALL_COLS - 1 : k;
}

void screen_balls(screen_t *s, float fx, float fy, float ox, float oy, float orb_size, float sub_size, int behind, int motion) {
    if (!motion) {
        memset(s->bo, 0, sizeof s->bo);
        memset(s->bp, 0, sizeof s->bp);
    } else {
        ball(s->bp, scr_x(fx), scr_y(fy), BALL_PD);
        ball(s->bo, scr_x(ox), scr_y(oy), BALL_OD);
    }
    s->ball_p = (unsigned char)colour_step(orb_size);
    s->ball_o = (unsigned char)(colour_step(sub_size) + (behind ? BALL_COLS : 0));
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
