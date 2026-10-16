/* VADER: an orbit-based vector synth for MPC OS (Akai Force / MPC), as a VST2 through mpc-vst-plugins.
 * Shared declarations of the engine's parts. */
#pragma once
#include <stdint.h>
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

/* ---- shapefile.c: SVG, OBJ, PNG/JPG -> a path. 0 on success, else -1 with err filled */
int shape_load_file(const char *path, path_t *out, char *err, int errlen);

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
#define TRAIL_N 14
#define FR_SHAPE 1
#define FR_TRAIL (FR_SHAPE + SHAPE_ANG * SHAPE_OFF)
#define FR_PATHDOT (FR_TRAIL + TRAIL_ANG * TRAIL_OFF)
#define FR_ORBDOT (FR_PATHDOT + DOT_NX * DOT_NY)
typedef struct {
    unsigned short tiles[TILE_COLS * TILE_ROWS];
    unsigned short shape[TILE_COLS * TILE_ROWS];   /* the shape alone (when the tiles draw it) */
    float trail_x[TRAIL_N], trail_y[TRAIL_N];      /* screen pixels */
    int trail_n;
    unsigned char env[ENV_COLS];
    /* the stipple layer: density at each corner of its tiles */
    float glow[(FOG_ROWS + 1) * (FOG_COLS + 1)];   /* around the shape's line */
    float heat[(FOG_ROWS + 1) * (FOG_COLS + 1)];   /* where the orbiter has just been, fading */
    unsigned char fog[FOG_COLS * FOG_ROWS];        /* each tile's frame: its 4 corners' levels */
} screen_t;
/* x, y in -1..1 (the corners' square) -> the screen's pixels (640 x 288: the square in the middle, true proportions) */
static inline float scr_x(float x) { return SQUARE_X + (x + 1) * 0.5f * SQUARE; }
static inline float scr_y(float y) { return (y + 1) * 0.5f * SQUARE; }
void screen_shape(screen_t *s, const float *x2, const float *y2, float size, int draw);
void screen_frame(screen_t *s, float fx, float fy, float ox, float oy, int motion);
void screen_env(screen_t *s, float a, float d, float sus, float r);
int env_pair_index(int r0, int r1);
/* the stipple layer: the glow around a path (x, y in -1..1, already sized and turned); then each frame: the fading
 * heat where the orbiter (ox, oy) passes and a cloud at each corner as loud as it is heard (wt[4]) */
void screen_glow(screen_t *s, const float *x, const float *y, int n, int draw);
void screen_fog(screen_t *s, float ox, float oy, const float *wt, int motion);
