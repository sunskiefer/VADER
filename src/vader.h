/* VADER: an orbit-based vector synth for MPC OS (Akai Force / MPC), as a VST2 through mpc-vst-plugins.
 * Shared declarations of the engine's parts. */
#pragma once
#include <stdint.h>
#include <math.h>
#include "vader_params.h"

#define SR 44100.0f
#define INV_SR (1.0f / 44100.0f)
#define MAXV 16              /* voices */
#define NPAT 32              /* overtone patterns an OVERTONE GEN sweeps through */
#define TBL 2048             /* samples per table */
#define NMIP 7               /* band-limited versions: 64, 32, 16, ... 1 harmonics */
#define PATHN 256            /* points of an orbit path */
#define NSHAPES 16
#define SHAPE_USER 15
#define TWO_PI 6.28318530718f

/* ---- tables.c: the overtone patterns, band-limited */
void tables_init(void);
extern float g_tab[NPAT][NMIP][TBL + 1];
extern const char *const PATTERN_NAMES[NPAT];
static inline int tab_mip(float hz) {
    /* the most harmonics that stay under 19 kHz at this pitch */
    float lim = 19000.0f / (hz > 1.0f ? hz : 1.0f);
    int m = 0, h = 64;
    while (m < NMIP - 1 && (float)h > lim) { h >>= 1; m++; }
    return m;
}
static inline float tab_lerp(const float *t, float ph) {   /* ph in [0,1) */
    float x = ph * TBL;
    int i = (int)x;
    float f = x - (float)i;
    i &= TBL - 1;
    return t[i] + (t[i + 1] - t[i]) * f;
}
/* an OVERTONE GEN at morph position m (0..1): two neighbouring patterns, crossfaded */
static inline float og_read(float m, int mip, float ph) {
    float p = m * (NPAT - 1);
    int i = (int)p;
    if (i >= NPAT - 1) return tab_lerp(g_tab[NPAT - 1][mip], ph);
    float f = p - (float)i;
    float a = tab_lerp(g_tab[i][mip], ph);
    if (f < 0.002f) return a;
    return a + (tab_lerp(g_tab[i + 1][mip], ph) - a) * f;
}

/* ---- shape.c: orbit paths */
typedef struct {
    float x[PATHN], y[PATHN], z[PATHN];
    int is3d;
    char name[64];
} path_t;
void shape_builtin(int idx, path_t *p);
/* 0.8: maths shapes (src/paths.c), indices SHAPE_FIRST_MATH.., drawn from four shape numbers 0..1 */
#define SHAPE_FIRST_MATH 16
#define NMATH 22
extern const char *const MATH_NAMES[NMATH];
void paths_gen(int m, const float *k, path_t *p);
void path_resample(const float *x, const float *y, const float *z, int n, int closed, int is3d, path_t *out);
/* the 2D path the orbiter follows: rotated (in plane, or in 3D: yaw/pitch), fitted to -1..1 */
void path_project(const path_t *p, float rot, float yaw, float pitch, float *x2, float *y2);
static inline void path_at(const float *x2, const float *y2, float t, float *ox, float *oy) {
    float u = t * PATHN;
    int i = (int)u;
    float f = u - (float)i;
    i %= PATHN;
    if (i < 0) i += PATHN;
    int j = (i + 1) % PATHN;
    *ox = x2[i] + (x2[j] - x2[i]) * f;
    *oy = y2[i] + (y2[j] - y2[i]) * f;
}

/* ---- shapefile.c: SVG, OBJ, PNG/JPG -> a path. 0 on success, else -1 with err filled. A PNG/JPG also fills img (when
 * given): its colours as values 0..1 on a spectrogram's scale, IMG_N x IMG_N over the same -1..1 as the path */
#define IMG_N 64
typedef struct { int ok; float v[IMG_N * IMG_N], hue[IMG_N * IMG_N], sat[IMG_N * IMG_N]; } image_t;   /* v: spectrogram scale */
int shape_load_file(const char *path, path_t *out, image_t *img, char *err, int errlen);

/* ---- fx.c: the effects after the voices (stereo float) */
typedef struct fx_s fx_t;
fx_t *fx_new(void);
void fx_free(fx_t *f);
typedef struct {
    float drive, drive_col;
    float ch_rate_hz, ch_depth, ch_col, ch_mix;
    float dly_sec, dly_fb, dly_mix;
    float rev_size, rev_col, rev_mix;
    float volume;
} fx_params_t;
void fx_process(fx_t *f, const fx_params_t *p, float *l, float *r, int n);
void fx_clear(fx_t *f);

/* ---- screen.c: the line screen (tiles) and the envelope line */
#define LINE_PTS 640   /* most points of the orbit line */
#define TRAIL_K 14     /* tiles the light trail is long (0.4.5: the last tiles the orbiter crossed) */
#define TRAIL_STEP 3   /* most trail tiles taken up in one frame (a faster orbiter leaves a sparser trail) */
#define TRAIL_BRIGHT 6 /* of them, the newest drawn bright; the rest softer */
typedef struct { int cell; float ax, ay; unsigned short frame; } trail_piece_t;
typedef struct {
    unsigned short tiles[TILE_COLS * TILE_ROWS];
    unsigned short shape[TILE_COLS * TILE_ROWS];   /* the shape alone (when the tiles draw it) */
    unsigned short line1[TILE_COLS * TILE_ROWS];   /* the orbit line alone: its first and second piece per tile */
    unsigned short line2[TILE_COLS * TILE_ROWS];
    unsigned short trail[TILE_COLS * TILE_ROWS];   /* the light trail's layer */
    unsigned short orbit[TILE_COLS * TILE_ROWS];   /* the orbit line's second layer (0.4.3) */
    trail_piece_t tr[TRAIL_K];                     /* the trail's pieces, oldest first */
    int tr_n, tr_has;
    float tr_x, tr_y;                              /* where the orbiter was last frame (screen pixels) */
    unsigned char env[ENV_COLS];
    unsigned char bo[BALL_N * BALL_N], bp[BALL_N * BALL_N];   /* the balls' cells: the orbiter's, the path point's */
    unsigned char ball_o, ball_p;                               /* their colour steps (the orbiter's + BALL_COLS: behind) */
} screen_t;
/* x, y in -1..1 (the corners' square) -> the screen's pixels (640 x 288: the square in the middle, true proportions) */
static inline float scr_x(float x) { return SQUARE_X + (x + 1) * 0.5f * SQUARE; }
static inline float scr_y(float y) { return (y + 1) * 0.5f * SQUARE; }
/* your own shape's screen strips (src/pictures.c): size step k's frame, w x h, centred on the square */
static inline void upic_frame(int k, int *w, int *h) {
    float r = 1.4142f * k / U_SIZES * SQUARE * 0.5f + 12;
    *w = 2 * (int)ceilf(r < SQUARE_X + SQUARE * 0.5f ? r : SQUARE_X + SQUARE * 0.5f);
    *h = 2 * (int)ceilf(r < SQUARE * 0.5f ? r : SQUARE * 0.5f);
}
int write_user_pictures(const char *skin_dir, const path_t *p);   /* 1: written */
void screen_shape(screen_t *s, const float *x2, const float *y2, float size, int draw);
/* the orbit line through lx, ly (ln points on the square: the orbiter's whole path over one orbit, drawn faint and
 * thin with the shape's pieces where the shape leaves room; ln 0: none). 0.4.5: worked out only when the line's inputs
 * change (engine.c), then it stays still: no screen traffic while the orbiter travels it. */
void screen_line(screen_t *s, const float *lx, const float *ly, int ln);
/* the main tiles and the orbit layer from the shape and the line (after either changed) */
void screen_merge(screen_t *s);
/* the light trail (0.4.5): the last TRAIL_K tiles the orbiter crossed, each the piece it drew there. Called once a
 * frame with the orbiter's place on the screen (pixels); only the tile it enters and the one that falls off the end
 * change. A jump (a new note somewhere else) restarts it; motion off clears it. */
void screen_trail_step(screen_t *s, float px, float py, int motion);
/* the balls (0.4.3): the path point at fx, fy and the orbiter at ox, oy (on the square), their colours from ORBIT SIZE and
 * SUBORBIT SIZE; behind: the orbiter passes under the path point */
void screen_balls(screen_t *s, float fx, float fy, float ox, float oy, float orb_size, float sub_size, int behind, int motion);
void screen_env(screen_t *s, float a, float d, float sus, float r);
int env_pair_index(int r0, int r1);
