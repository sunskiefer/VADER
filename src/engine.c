/* VADER's engine: mpc_engine() for mpc-vst-plugins' wrapper (wrapper/engine.h).
 *
 * A note plays four CORNERS at once. Each corner is two OVERTONE GENerators (additive spectra, tables.c) joined by a
 * BLEND (mix, AM, PM, both, phase distortion, ring, fold) and a filter (LP12, LP24, BP, HP, ladder) placed after the
 * blend or on one generator. What you hear is a mix of the four corners, and the mix is set by where the ORBITER is:
 * a point travelling round a SHAPE (built in, or your own SVG/OBJ/PNG) with a SUBORBIT circling it. Corner A sits top
 * left, B top right, C bottom left, D bottom right.
 * Two TIMBREs (A and B) can play layered or split; an arpeggiator with a 16-step pattern; drive, chorus, delay and
 * reverb. The screen (screen.c) is drawn from parameters MPC swaps pictures for. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"
#include "eng.h"   /* 0.8: the ZYNTH voice engines (src/zynth) */
#include "vader.h"
#include "overlay.h"
#include "web.h"

/* ------------------------------------------------------------------------------------------------ parameters */
#define HSIZE 4096   /* more than twice the parameters (a full table would loop) */
static short g_hash[HSIZE];
static pthread_once_t g_hash_once = PTHREAD_ONCE_INIT;
static unsigned hkey(const char *s, int n) {
    unsigned h = 2166136261u;
    for (int i = 0; i < n && s[i]; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}
static void hash_build(void) {
    for (int i = 0; i < HSIZE; i++) g_hash[i] = -1;
    for (int i = 0; i < NP; i++) {
        unsigned h = hkey(VP[i].key, 64) & (HSIZE - 1);
        while (g_hash[h] >= 0) h = (h + 1) & (HSIZE - 1);
        g_hash[h] = (short)i;
    }
}
/* the parameter whose key is the first n characters of s, or -1 */
static int find_n(const char *s, int n) {
    unsigned h = hkey(s, n) & (HSIZE - 1);
    while (g_hash[h] >= 0) {
        const char *k = VP[g_hash[h]].key;
        if ((int)strlen(k) == n && !strncmp(k, s, (size_t)n)) return g_hash[h];
        h = (h + 1) & (HSIZE - 1);
    }
    return -1;
}
static int find(const char *s) { return find_n(s, (int)strlen(s)); }

/* ------------------------------------------------------------------------------------------------ the instance */
#define MAXFILES 500
#define MAXHELD 32
typedef struct { char *path, *label; } shapefile_t;   /* a shape file found: where it is, how it's listed */

/* 0.8: a corner's sample (one WAV per corner), loaded off the audio thread; two buffers so the swap never frees what plays */
typedef struct {
    float *d[2];
    int n[2], cur;
    float sr;
    char path[600], want[600];
    int busy, joinable;
    pthread_t th;
    pthread_mutex_t lk;
} smp_t;

typedef struct {
    int active, timbre, note, key, released, held_by_pedal;   /* key: the key that played it (note: with the timbre's octave) */
    float vel;
    float freq, target;          /* Hz, with glide */
    float cents, pan;            /* unison spread */
    int stage;                   /* 0 attack, 1 decay, 2 sustain, 3 release */
    float env;
    float ph[4][2];              /* generator phases */
    double sp[4];                /* 0.8: each corner's sample position */
    float s1[4][2], s2[4][2];    /* SVF states (two stages) */
    float lad[4][4], lad_y[4];   /* ladder */
    float sh_ph, sh_lp[2];       /* the shape sound */
    float orb_t, orb_o;          /* its own orbit (RETRIG) */
    float w[4];                  /* corner weights at the end of the last block */
    float ox, oy, fx, fy;        /* where its orbiter / path point is */
    float shown_t, shown_o;      /* the orbit and suborbit phases it last played (for the screen) */
    float im[4], im_src[4];      /* the IMAGE mods: glided value; the source as last read (for the page's meters) */
    int im_init;
    Eng eng[4];                  /* 0.8: a ZYNTH voice engine per corner (when its ENGINE is not SYNTH) */
    unsigned char eng_type[4];   /* the engine type + 1 each runs (0: none yet) */
    unsigned char eng_new[4];
    unsigned age;
} voice_t;

typedef struct {
    path_t base;                 /* the shape as built or loaded */
    float x2[PATHN], y2[PATHN];  /* as the orbiter follows it now (turned, tilted) */
    float yaw, last_rot, last_tilt;
    int shape_built;
    float orb_t, orb_o;          /* the free-running orbit */
    float size_s, sub_s, rot_s;  /* ORBIT SIZE, SUBORBIT SIZE, ROTATE, glided (-1: not yet) */
    float hz_now, sub_now;       /* orbit and suborbit turns per second, as last played (for the light trail) */
    float vib_ph, trem_ph;
    float lfo_ph, spd_mul;       /* 0.8: the orbit speed's LFO phase; the speed factor now */
} timbre_t;

typedef struct {
    float snd[2][NSND];
    float g[NGLOB];
    timbre_t tm[2];
    sim_t sim[2];
    float mv[2][NMODES][3][8];   /* 0.8: each mode's knobs, per timbre and layer */
    voice_t v[MAXV];
    unsigned age;
    fx_t *fx;
    /* host time */
    int playing;
    char cpu_text[48];           /* 0.8: the CPU readout (render time over block time) */
    double cpu_acc, cpu_blk;
    int cpu_n;
    double ppq;                  /* at the start of the current render() */
    float tempo;
    /* MIDI */
    float bend, modwheel, aftertouch;
    int pedal;
    /* arpeggiator */
    int held[MAXHELD], nheld;
    int latch_fresh;             /* LATCH: the next key pressed after all were released starts a new chord */
    double arp_next;             /* beats: the next step */
    double arp_free_beats;       /* beats counted while MPC is stopped */
    int arp_count, arp_idx, arp_dir;
    int chain_pos, chain_n;      /* 0.8: ARP CHAIN: which direction of the chain, notes played in it */
    int arp_sounding[MAXHELD], narp;    /* notes the arp is holding */
    long arp_off_at;             /* frames until they stop (-1: tied / none) */
    unsigned rng;
    double self_next;            /* 0.8: SELF PLAY's next step (beats) */
    float undo_snd[NSND], undo_mv[NMODES][3][8];   /* 0.8: the sound before RANDOMIZE */
    int undo_t;
    int drone_notes[4], ndrone;  /* 0.8: the DRONE's held notes */
    /* screen */
    screen_t scr;
    unsigned short sent_tiles[TILE_COLS * TILE_ROWS], sent_trail[TILE_COLS * TILE_ROWS], sent_orbit[TILE_COLS * TILE_ROWS];
    unsigned char sent_env[ENV_COLS];
    unsigned char sent_bo[BALL_N * BALL_N], sent_bp[BALL_N * BALL_N], sent_ball_o, sent_ball_p;
    int sent_pic;
    int grid_next;               /* where the screen's tile reports take their turn from */
    unsigned char sent_w[4], sent_rd;
    int scr_wait;                /* frames until the next screen update */
    int shape_wait;
    unsigned line_key;           /* what the orbit line was last worked out from (0.4.5) */
    int bar_wait;                /* screen frames until the corner bars and meters report again */
    /* 0.4.6: pieces that just went empty are told to MPC a second time a few polls later (with budget to spare): a
     * report MPC drops while busy would otherwise leave that piece on the screen for good (GLASS STAR: trail pieces
     * piled up over the whole screen) */
#define HEAL_N 48
    int heal_p[HEAL_N], heal_at[HEAL_N], heal_n, poll_no;
    unsigned char dirty[NP];
    char readout[96];
    float rd_norm;
    int last_voice;
    /* files */
    char dir[512];
    shapefile_t files[MAXFILES];
    int nfiles;
    shapefile_t wavs[MAXFILES];  /* 0.8: the WAV files found (samples) */
    int nwavs;
    smp_t smp[4];
    path_t user;
    int has_user;
    image_t img, load_img;        /* the picture under the shape (IMAGE page), from the last PNG/JPG loaded */
    char skin_dir[600];           /* <plugin folder>/Plugin Skins: where your shape's screen pictures are written */
    char base_dir[600];           /* the plugin folder */
    overlay_t ovl;                /* 0.5.0: the live screen (overlay.c), off unless overlay.on exists */
    web_t web;                    /* 0.6.0: the companion screen (web.c) */
    unsigned web_seen;            /* the address version last shown (the readout and the QR code) */
    unsigned char sent_qr[QR_N * QR_BYTES];
    char web_text[96];
    int shape_ver, pub_key, rested, screen_on;
    int upics;                    /* 1: the pictures of the loaded shape are written */
    int load_upics;
    int sent_r[3];
    unsigned char sent_im[4];
    char user_file[1024];
    pthread_t loader;
    int loader_joinable;
    volatile int load_state;     /* 0 idle, 1 loading, 2 done ok, 3 failed */
    path_t load_buf;
    char load_err[96];
    char load_name[1024];
    char status[96];             /* the file field's message (loaded / failed) */
    int load_select;             /* a LOAD press: the loaded shape becomes the edited timbre's (a restore keeps its own) */
    double tap_last;             /* seconds, for TAP */
    double clock_s;
    int preset;
    /* your own presets: <plugin folder>/User Presets/USER 001.vader ... */
    char pdir[600];
    unsigned char uknown[1000];   /* 0 not looked at yet, 1 empty, 2 saved */
    char ustatus[48];
} vader_t;

/* ------------------------------------------------------------------------------------------------ helpers */
#define T(t) (s->snd[t])
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float norm_of(int p, float v) {
    const vp_t *q = &VP[p];
    return q->max > q->min ? (v - q->min) / (q->max - q->min) : 0;
}
#define SCREEN_BUDGET 10   /* screen pieces reported to MPC per poll (25 polls a second): 0.4.5, was 48 (crackles) */
static int is_grid(int p) {
    return (p >= P_T000 && p < P_T000 + TILE_COLS * TILE_ROWS) || (p >= P_R000 && p < P_R000 + TILE_COLS * TILE_ROWS) ||
           (p >= P_L000 && p < P_L000 + TILE_COLS * TILE_ROWS);
}
/* every parameter the screen moves by itself: counted against SCREEN_BUDGET, in this order of urgency */
static int is_screen(int p) {
    return is_grid(p) || (p >= P_BO00 && p < P_BO00 + 2 * BALL_N * BALL_N) || p == P_BALL_O || p == P_BALL_P ||
           (p >= P_WA && p <= P_WD) || (p >= P_IM_M1 && p <= P_IM_M4) || (p >= P_E00 && p < P_E00 + ENV_COLS);
}
static void mark(vader_t *s, int p) {
    if (p >= 0 && p < NP) s->dirty[p] = 1;
}
static void heal_later(vader_t *s, int p) {
    if (s->heal_n == HEAL_N) { memmove(s->heal_p, s->heal_p + 1, (HEAL_N - 1) * sizeof(int)); memmove(s->heal_at, s->heal_at + 1, (HEAL_N - 1) * sizeof(int)); s->heal_n--; }
    s->heal_p[s->heal_n] = p;
    s->heal_at[s->heal_n] = s->poll_no;
    s->heal_n++;
}
static int edit(vader_t *s) { return (int)s->g[G_TIMBRE] & 1; }
static float div_beats(int d) {
    static const float b[7] = {16, 8, 4, 2, 1, 0.5f, 0.25f};
    return b[d < 0 ? 0 : d > 6 ? 6 : d];
}
static float tempo_of(vader_t *s) { return s->tempo > 20 ? s->tempo : 120.0f; }

/* the value a front knob shows: the selected corner's (all corners: corner A's) */
static float front_value(vader_t *s, int field) {
    int c = (int)s->g[G_CORNER];
    if (c > 3) c = 0;
    return T(edit(s))[c * NCF + field];
}

static void mark_corner_view(vader_t *s) {
    for (int p = 0; p < NP; p++)
        if (VP[p].kind == K_FRONT) mark(s, p);
}
static void mark_sound(vader_t *s) {
    for (int p = 0; p < NP; p++)
        if (VP[p].kind == K_SND || VP[p].kind == K_FRONT) mark(s, p);
    for (int i = 0; i < 8; i++) mark(s, P_MN1 + i);
    for (int i = 0; i < 16; i++) mark(s, P_CN1 + i);
}

/* 0.8: the mode knobs show the knobs of the current mode and layer */
static void mv_defaults(vader_t *s) {
    for (int t = 0; t < 2; t++)
        for (int m = 0; m < NMODES; m++)
            for (int l = 0; l < 3; l++)
                for (int i = 0; i < 8; i++) s->mv[t][m][l][i] = 0.5f;
}
static void mode_load(vader_t *s, int t, int reset) {
    int m = (int)T(t)[S_SIM_MODE], l = (int)T(t)[S_SIM_LAYER];
    for (int i = 0; i < 8; i++) {
        T(t)[S_MK1 + i] = s->mv[t][m][l][i];
        if (t == edit(s)) { mark(s, SND_P[S_MK1 + i]); mark(s, P_MN1 + i); }
    }
    if (reset) sim_reset(&s->sim[t], m, s->mv[t][m][0]);
}

/* the name of a corner's character control k (0..3): the ZYNTH voice's own */
static const char *cn_name(vader_t *s, int t, int c, int k, char *buf, int len) {
    int ei = (int)T(t)[S_A_ENG + c * 5];
    if (ei > 0) snprintf(buf, (size_t)len, "%s", ENG_KNOBS[ei - 1][k]);
    else buf[0] = 0;
    return buf;
}

static const char *mk_name(vader_t *s, int t, int i, char *buf, int len) {
    int m = (int)T(t)[S_SIM_MODE], l = (int)T(t)[S_SIM_LAYER];
    if (m > 0 && l == 0 && MODE_KNOBS[m][i][0]) snprintf(buf, (size_t)len, "%s", MODE_KNOBS[m][i]);
    else if (m > 0) snprintf(buf, (size_t)len, "L%d K%d", l + 1, i + 1);
    else buf[0] = 0;
    return buf;
}

/* ------------------------------------------------------------------------------------------------ shapes */
/* 0.8: the orbiter's way of travelling: the running phase 0..1 -> the place on the path */
static inline float dir_t(const float *P, float ph) {
    switch ((int)P[S_ORB_DIR]) {
    case 1: return 1.0f - ph;                                   /* REVERSE */
    case 2: return 1.0f - fabsf(2.0f * ph - 1.0f);              /* PING-PONG: out and back */
    default: return ph;                                         /* FORWARD, LOOP BACK */
    }
}

/* 0.8: the suborbit: its own direction (or the orbiter's, when linked) and its own shape (circle, ellipse, figure 8, square, line) */
static inline void sub_pos(const float *P, float ph, float *cx, float *cy) {
    int d = (int)P[S_SUB_LINK] ? (int)P[S_ORB_DIR] : (int)P[S_SUB_DIR];
    if (d == 1) ph = 1.0f - ph;
    else if (d == 2) ph = 1.0f - fabsf(2.0f * ph - 1.0f);
    float a = TWO_PI * ph, c = cosf(a), sn = sinf(a);
    switch ((int)P[S_SUB_SHAPE]) {
    case 1: *cx = c; *cy = 0.5f * sn; break;                                          /* ELLIPSE */
    case 2: *cx = sn; *cy = sinf(2.0f * a) * 0.5f; break;                              /* FIGURE 8 */
    case 3: *cx = clampf(1.35f * c, -1, 1); *cy = clampf(1.35f * sn, -1, 1); break;   /* SQUARE-ish */
    case 4: *cx = c; *cy = 0; break;                                                   /* LINE */
    default: *cx = c; *cy = sn; break;
    }
}

/* 0.8: the part of the path used (PATH START..END), turning softly (SOFT TURN) */
static inline float path_u(const float *P, float ph) {
    float u = dir_t(P, ph);
    float soft = P[S_SOFT_TURN];
    if (soft > 0.001f && (int)P[S_ORB_DIR] == 2) {   /* PING-PONG turning on a cosine instead of a point */
        float c = 0.5f - 0.5f * cosf(TWO_PI * ph);
        u = u * (1 - soft) + c * soft;
    }
    float a = P[S_PATH_START], b = P[S_PATH_END];
    if (a != 0.0f || b != 1.0f) u = a + (b - a) * u;
    return u;
}

/* 0.8: stretch, skew, morph to round, the centre point and the Warp pad on the orbiter's place (-1..1) */
static inline void orb_xform(const float *P, float *x, float *y) {
    float px = *x, py = *y;
    float mo = P[S_MORPH];
    if (mo > 0.001f) {
        float r = sqrtf(px * px + py * py);
        if (r > 0.05f) { float k = (1 - mo) + mo * 0.85f / r; px *= k; py *= k; }
    }
    float sx = powf(2.0f, (P[S_STRETCH_X] - 0.5f) * 2 + (P[S_WARP_X] - 0.5f) * 2);
    float sy = powf(2.0f, (P[S_STRETCH_Y] - 0.5f) * 2);
    float sk = (P[S_SKEW] - 0.5f) * 2 + (P[S_WARP_Y] - 0.5f) * 2;
    float nx = px * sx + sk * py, ny = py * sy;
    *x = clampf(nx + (P[S_CENTRE_X] - 0.5f) * 2, -1, 1);
    *y = clampf(ny + (P[S_CENTRE_Y] - 0.5f) * 2, -1, 1);
}

static void timbre_shape(vader_t *s, int t) {
    int idx = (int)T(t)[S_SHAPE];
    if (idx == SHAPE_USER && s->has_user) s->tm[t].base = s->user;
    else if (idx >= SHAPE_FIRST_MATH) paths_gen(idx - SHAPE_FIRST_MATH, &T(t)[S_SH_A], &s->tm[t].base);
    else shape_builtin(idx == SHAPE_USER ? 0 : idx, &s->tm[t].base);
    s->tm[t].shape_built = 1;
    s->tm[t].last_rot = -99;   /* project again */
    if (t == edit(s)) s->shape_wait = 0;
    s->shape_ver++;
}

static void project(vader_t *s, int t, float dt) {
    timbre_t *m = &s->tm[t];
    float rot = m->rot_s * TWO_PI, tilt = T(t)[S_TILT] * 1.5707963f;
    if (m->base.is3d) {
        float spin = T(t)[S_SPIN];
        m->yaw += spin * spin * TWO_PI * 0.6f * dt;
        if (m->yaw > TWO_PI) m->yaw -= TWO_PI;
        path_project(&m->base, rot, m->yaw, tilt, m->x2, m->y2);
    } else if (fabsf(rot - m->last_rot) > 1e-5f) {
        path_project(&m->base, rot, 0, 0, m->x2, m->y2);
    }
    m->last_rot = rot;
    m->last_tilt = tilt;
}

/* ------------------------------------------------------------------------------------------------ files */
static int is_shape_file(const char *n) {
    const char *e = strrchr(n, '.');
    return e && (!strcasecmp(e, ".svg") || !strcasecmp(e, ".obj") || !strcasecmp(e, ".png") || !strcasecmp(e, ".jpg") ||
                 !strcasecmp(e, ".jpeg"));
}
/* the shape files: the plugin's own Shapes folder, then a "VADER Shapes" folder on the internal card and on any SD card
 * or USB drive (/media/<drive>/VADER Shapes), so new shapes can be copied on a computer and plugged in */
static int cmp_file(const void *a, const void *b) {
    const shapefile_t *x = a, *y = b;
    int ex = x->label[0] == '[', ey = y->label[0] == '[';   /* the plugin's own first, then the drives' */
    if (ex != ey) return ex - ey;
    return strcasecmp(x->label, y->label);
}

static int is_wav_file(const char *n) {
    const char *e = strrchr(n, '.');
    return e && (!strcasecmp(e, ".wav") || !strcasecmp(e, ".wave"));
}
/* names in order, with numbers by value: "vader 2" before "vader 10" */
static int cmp_nat(const void *a, const void *b) {
    const shapefile_t *x = a, *y = b;
    int ex = x->label[0] == '[', ey = y->label[0] == '[';
    if (ex != ey) return ex - ey;
    const char *p = x->label, *q = y->label;
    while (*p && *q) {
        if (*p >= '0' && *p <= '9' && *q >= '0' && *q <= '9') {
            long m = strtol(p, (char **)&p, 10), n = strtol(q, (char **)&q, 10);
            if (m != n) return m < n ? -1 : 1;
        } else {
            int cp = tolower((unsigned char)*p), cq = tolower((unsigned char)*q);
            if (cp != cq) return cp - cq;
            p++; q++;
        }
    }
    return (*p != 0) - (*q != 0);
}
static int g_scan_wav;   /* 1: scan_dir lists WAV files into s->wavs instead of the shapes */
static void scan_dir(vader_t *s, const char *root, const char *rel, const char *tag, int depth) {
    shapefile_t *arr = g_scan_wav ? s->wavs : s->files;
    int *cnt = g_scan_wav ? &s->nwavs : &s->nfiles;
    char path[1024];
    snprintf(path, sizeof path, "%s%s%s", root, rel[0] ? "/" : "", rel);
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && *cnt < MAXFILES) {
        if (e->d_name[0] == '.') continue;
        char r2[512], full[1024];
        snprintf(r2, sizeof r2, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", root, r2);
        struct stat st;
        if (lstat(full, &st)) continue;   /* lstat: links are not followed */
        if (S_ISDIR(st.st_mode)) {
            if (depth < 2) scan_dir(s, root, r2, tag, depth + 1);
        } else if (S_ISREG(st.st_mode) && (g_scan_wav ? is_wav_file(e->d_name) : is_shape_file(e->d_name))) {
            char label[600];
            snprintf(label, sizeof label, "%s%s", tag, r2);
            arr[*cnt].path = strdup(full);
            arr[*cnt].label = strdup(label);
            (*cnt)++;
        }
    }
    closedir(d);
}

#ifndef VADER_MEDIA
#define VADER_MEDIA "/media"
#endif
#ifndef VADER_SDCARD
#define VADER_SDCARD "/sdcard"
#endif

static void smp_sync_all(vader_t *s);
static void rescan(vader_t *s) {
    for (int i = 0; i < s->nfiles; i++) { free(s->files[i].path); free(s->files[i].label); }
    s->nfiles = 0;
    if (s->dir[0]) scan_dir(s, s->dir, "", "", 0);
    char root[600];
    snprintf(root, sizeof root, "%s/VADER Shapes", VADER_SDCARD);
    scan_dir(s, root, "", "[CARD] ", 0);
    DIR *m = opendir(VADER_MEDIA);
    if (m) {
        struct dirent *e;
        while ((e = readdir(m))) {
            if (e->d_name[0] == '.') continue;
            char tag[96];
            snprintf(root, sizeof root, "%s/%s/VADER Shapes", VADER_MEDIA, e->d_name);
            snprintf(tag, sizeof tag, "[%.80s] ", e->d_name);
            scan_dir(s, root, "", tag, 0);
        }
        closedir(m);
    }
    qsort(s->files, (size_t)s->nfiles, sizeof(shapefile_t), cmp_file);
    /* the samples: WAV files in the plugin folder, "VADER Samples" on the card and on drives */
    for (int i = 0; i < s->nwavs; i++) { free(s->wavs[i].path); free(s->wavs[i].label); }
    s->nwavs = 0;
    g_scan_wav = 1;
    if (s->dir[0]) scan_dir(s, s->dir, "", "", 0);
    if (s->pdir[0]) {   /* <plugin folder>/Samples */
        snprintf(root, sizeof root, "%s", s->pdir);
        char *sl_ = strrchr(root, '/');
        if (sl_) { snprintf(sl_, sizeof root - (size_t)(sl_ - root), "/Samples"); scan_dir(s, root, "", "", 0); }
    }
    snprintf(root, sizeof root, "%s/VADER Samples", VADER_SDCARD);
    scan_dir(s, root, "", "[CARD] ", 0);
    DIR *m2 = opendir(VADER_MEDIA);
    if (m2) {
        struct dirent *e;
        while ((e = readdir(m2))) {
            if (e->d_name[0] == '.') continue;
            char tag[96];
            snprintf(root, sizeof root, "%s/%s/VADER Samples", VADER_MEDIA, e->d_name);
            snprintf(tag, sizeof tag, "[%.80s] ", e->d_name);
            scan_dir(s, root, "", tag, 0);
        }
        closedir(m2);
    }
    g_scan_wav = 0;
    qsort(s->wavs, (size_t)s->nwavs, sizeof(shapefile_t), cmp_nat);
    s->status[0] = 0;
    mark(s, P_FILE);
    for (int c = 0; c < 4; c++) mark(s, P_SMP_A + c);
    smp_sync_all(s);
}

/* ---- 0.8: samples */
static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }
/* a WAV (PCM 8/16/24/32 or float 32, any channels, mixed down to mono, up to 30 s) -> floats */
static int wav_load(const char *path, float **out, int *nout, float *sr) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 44 || sz > 64 * 1024 * 1024) { fclose(f); return -1; }
    unsigned char *b = malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return -1; }
    fclose(f);
    int rc = -1, fmt = 0, ch = 0, bits = 0;
    unsigned rate = 0;
    const unsigned char *data = NULL;
    unsigned dlen = 0;
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return -1; }
    for (long p = 12; p + 8 <= sz;) {
        unsigned cl = rd32(b + p + 4);
        const unsigned char *c = b + p + 8;
        if (!memcmp(b + p, "fmt ", 4) && p + 8 + 16 <= sz) {
            fmt = (int)rd16(c); ch = (int)rd16(c + 2); rate = rd32(c + 4); bits = (int)rd16(c + 14);
            if (fmt == 0xFFFE && cl >= 26) fmt = (int)rd16(c + 24);   /* extensible: the sub-format */
        } else if (!memcmp(b + p, "data", 4)) {
            data = c; dlen = cl;
            if ((long)(p + 8 + dlen) > sz) dlen = (unsigned)(sz - p - 8);
            break;
        }
        p += 8 + (long)cl + (cl & 1);
    }
    if (data && ch >= 1 && ch <= 8 && rate >= 8000 && rate <= 192000 && ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) {
        int bps = bits / 8;
        long frames = (long)(dlen / (unsigned)(bps * ch));
        long maxf = (long)rate * 30;
        if (frames > maxf) frames = maxf;
        float *d = frames > 1 ? malloc(sizeof(float) * (size_t)frames) : NULL;
        if (d) {
            for (long i = 0; i < frames; i++) {
                float acc = 0;
                for (int k = 0; k < ch; k++) {
                    const unsigned char *q = data + ((size_t)i * (size_t)ch + (size_t)k) * (size_t)bps;
                    float x;
                    if (fmt == 3) { float fv; memcpy(&fv, q, 4); x = fv; }
                    else if (bits == 8) x = ((int)q[0] - 128) / 128.0f;
                    else if (bits == 16) x = (short)rd16(q) / 32768.0f;
                    else if (bits == 24) x = (int)((unsigned)(q[0] << 8 | q[1] << 16 | (unsigned)q[2] << 24)) / 2147483648.0f;
                    else x = (int)rd32(q) / 2147483648.0f;
                    acc += x;
                }
                d[i] = acc / (float)ch;
            }
            *out = d; *nout = (int)frames; *sr = (float)rate;
            rc = 0;
        }
    }
    free(b);
    return rc;
}

static void *smp_worker(void *arg) {
    smp_t *sl = arg;
    for (;;) {
        char w[600];
        pthread_mutex_lock(&sl->lk);
        if (!strcmp(sl->want, sl->path)) { sl->busy = 0; pthread_mutex_unlock(&sl->lk); return NULL; }
        snprintf(w, sizeof w, "%s", sl->want);
        pthread_mutex_unlock(&sl->lk);
        float *d = NULL; int n = 0; float sr = 48000;
        if (w[0] && wav_load(w, &d, &n, &sr)) { d = NULL; n = 0; }
        int nx = 1 - sl->cur;
        free(sl->d[nx]);
        sl->d[nx] = d; sl->n[nx] = n; sl->sr = sr;
        __atomic_store_n(&sl->cur, nx, __ATOMIC_RELEASE);
        pthread_mutex_lock(&sl->lk);
        snprintf(sl->path, sizeof sl->path, "%s", w);
        pthread_mutex_unlock(&sl->lk);
    }
}

/* the corner's SAMPLE choice (0: off, else the n-th WAV found): load it (or drop it) in the background */
static void smp_request(vader_t *s, int c) {
    smp_t *sl = &s->smp[c];
    int i = (int)s->g[G_SMP_A + c] - 1;
    const char *want = i >= 0 && i < s->nwavs ? s->wavs[i].path : "";
    pthread_mutex_lock(&sl->lk);
    snprintf(sl->want, sizeof sl->want, "%s", want);
    if (!sl->busy && strcmp(sl->want, sl->path)) {
        sl->busy = 1;
        if (sl->joinable) pthread_join(sl->th, NULL);
        sl->joinable = pthread_create(&sl->th, NULL, smp_worker, sl) == 0;
        if (!sl->joinable) sl->busy = 0;
    }
    pthread_mutex_unlock(&sl->lk);
}
static void smp_sync_all(vader_t *s) { for (int c = 0; c < 4; c++) smp_request(s, c); }

/* a corner's sample for n frames: ONE SHOT follows the key (the ROOT NOTE plays it at its own speed); a LOOP runs the
 * whole sample over 1-4 bars (4/4) of the tempo at the root note, and the keys pitch it from there. */
static void smp_render(vader_t *s, voice_t *v, int c, float freq, float *out, int n) {
    smp_t *sl = &s->smp[c];
    int cu = __atomic_load_n(&sl->cur, __ATOMIC_ACQUIRE), len = sl->n[cu];
    const float *d = sl->d[cu];
    int mode = (int)s->g[G_SMP_LEN_A + c];
    double rate, p = v->sp[c];
    float fr = 440.0f * powf(2.0f, (s->g[G_SMP_ROOT_A + c] - 69.0f) / 12.0f);
    if (mode == 0) rate = (double)sl->sr * INV_SR * freq / fr;
    else rate = (double)len / ((double)mode * (4.0 * 60.0 / tempo_of(s)) / INV_SR) * (freq / fr);   /* in time at the root note, pitched by the keys */
    for (int i = 0; i < n; i++) {
        if (mode == 0 && p >= len - 1) { out[i] = 0; continue; }
        while (p >= len) p -= len;
        int i0 = (int)p, i1 = i0 + 1;
        float fr = (float)(p - i0);
        if (i1 >= len) i1 = mode ? 0 : i0;
        out[i] = (d[i0] + (d[i1] - d[i0]) * fr) * 0.8f;
        p += rate;
    }
    v->sp[c] = p;
}

static void *load_thread(void *arg) {
    vader_t *s = arg;
    s->load_img.ok = 0;
    int rc = shape_load_file(s->load_name, &s->load_buf, &s->load_img, s->load_err, sizeof s->load_err);
    s->load_upics = !rc && !s->load_buf.is3d && s->skin_dir[0] ? write_user_pictures(s->skin_dir, &s->load_buf) : 0;
    __atomic_store_n(&s->load_state, rc ? 3 : 2, __ATOMIC_RELEASE);
    return NULL;
}

/* path: the file's full path (a saved project from 0.1.0 has it relative to the Shapes folder) */
static void start_load(vader_t *s, const char *path) {
    if (__atomic_load_n(&s->load_state, __ATOMIC_ACQUIRE) == 1) return;   /* one at a time */
    if (s->loader_joinable) { pthread_join(s->loader, NULL); s->loader_joinable = 0; }
    if (path[0] == '/' || access(path, R_OK) == 0) snprintf(s->load_name, sizeof s->load_name, "%s", path);
    else snprintf(s->load_name, sizeof s->load_name, "%s/%s", s->dir, path);
    __atomic_store_n(&s->load_state, 1, __ATOMIC_RELEASE);
    snprintf(s->status, sizeof s->status, "LOADING...");
    mark(s, P_FILE);
    if (pthread_create(&s->loader, NULL, load_thread, s) == 0) s->loader_joinable = 1;
    else { __atomic_store_n(&s->load_state, 3, __ATOMIC_RELEASE); snprintf(s->load_err, sizeof s->load_err, "no thread"); }
}

/* a finished load lands here (from render or a parameter call: whoever looks first) */
static void collect_load(vader_t *s) {
    int st = __atomic_load_n(&s->load_state, __ATOMIC_ACQUIRE);
    if (st < 2) return;
    if (st == 2) {
        s->user = s->load_buf;
        s->has_user = 1;
        if (s->load_img.ok) s->img = s->load_img;   /* a picture: it is the IMAGE too (an SVG or OBJ keeps the last one) */
        s->shape_ver++;
        s->upics = s->load_upics;
        snprintf(s->user_file, sizeof s->user_file, "%s", s->load_name);
        snprintf(s->status, sizeof s->status, "LOADED");
        for (int t = 0; t < 2; t++) {
            if (t == edit(s) && s->load_select) T(t)[S_SHAPE] = SHAPE_USER;
            if ((int)T(t)[S_SHAPE] == SHAPE_USER) timbre_shape(s, t);
        }
        mark(s, P_SHAPE);
    } else {
        snprintf(s->status, sizeof s->status, "%s", s->load_err[0] ? s->load_err : "FAILED");
    }
    __atomic_store_n(&s->load_state, 0, __ATOMIC_RELEASE);
    mark(s, P_FILE);
}

static const shapefile_t *file_at(vader_t *s) {
    int i = (int)s->g[G_FILE];
    if (s->nfiles <= 0) return NULL;
    if (i >= s->nfiles) i = s->nfiles - 1;
    return &s->files[i];
}

static void release_all(vader_t *s);
/* ------------------------------------------------------------------------------------------------ presets */
typedef struct { const char *key; float v; } kv_t;
typedef struct { const char *name; kv_t a[40]; kv_t b[24]; kv_t g[24]; } preset_t;
#define END {0, 0}
static const preset_t PRESETS[] = {
    {"INIT ORBIT", {END}, {END}, {END}},
    {"GLASS STAR", {{"shape", 2}, {"a_og1", 0.75f}, {"a_og2", 0.95f}, {"b_og1", 0.0f}, {"b_og2", 0.32f}, {"b_mode", 2},
                    {"c_og1", 0.68f}, {"c_tune", 19}, {"d_og1", 0.9f}, {"d_mode", 5}, {"orb_speed", 0.25f}, {"attack", 0.2f},
                    {"release", 0.6f}, {"sub_size", 0.4f}, END}, {END}, {{"mix_rev", 0.45f}, {"rev_size", 0.8f}, {"mix_ch", 0.3f}, END}},
    {"DARK FIGURE", {{"shape", 1}, {"a_og1", 0.1f}, {"b_og1", 0.06f}, {"c_og1", 0.15f}, {"d_og1", 0.13f}, {"a_cutoff", 0.35f},
                     {"b_cutoff", 0.5f}, {"c_cutoff", 0.3f}, {"d_cutoff", 0.55f}, {"a_ftype", 4}, {"b_ftype", 4}, {"c_ftype", 4},
                     {"d_ftype", 4}, {"a_reso", 0.5f}, {"d_reso", 0.6f}, {"orb_speed", 0.15f}, {"octave", -1}, {"decay", 0.6f},
                     {"sustain", 0.5f}, END}, {END}, {{"drive", 0.35f}, {"mix_rev", 0.15f}, END}},
    {"VOX CIRCLE", {{"shape", 0}, {"a_og1", 0.32f}, {"b_og1", 0.36f}, {"c_og1", 0.42f}, {"d_og1", 0.45f}, {"a_og2", 0.1f},
                    {"orb_speed", 0.3f}, {"sub_size", 0.15f}, {"attack", 0.35f}, {"release", 0.55f}, {"vib_depth", 0.15f},
                    END}, {END}, {{"mix_ch", 0.35f}, {"mix_rev", 0.35f}, END}},
    {"BELL CUBE", {{"shape", 13}, {"a_og1", 0.61f}, {"a_og2", 0.0f}, {"a_tune", 24}, {"a_mode", 2}, {"a_blend", 0.5f},
                   {"b_og1", 0.0f}, {"b_tune", 17}, {"b_mode", 2}, {"b_blend", 0.6f}, {"c_og1", 0.71f}, {"d_og1", 0.94f},
                   {"spin", 0.35f}, {"decay", 0.55f}, {"sustain", 0.1f}, {"release", 0.6f}, END}, {END},
     {{"mix_rev", 0.4f}, {"rev_size", 0.75f}, {"mix_dly", 0.2f}, END}},
    {"PWM ROSE", {{"shape", 7}, {"a_og1", 0.19f}, {"b_og1", 0.23f}, {"c_og1", 0.26f}, {"d_og1", 0.29f}, {"detune", 0.35f},
                  {"unison", 2}, {"orb_speed", 0.4f}, {"attack", 0.15f}, {"release", 0.4f}, END}, {END}, {{"mix_ch", 0.25f}, END}},
    {"FM SPIRAL", {{"shape", 9}, {"a_mode", 2}, {"b_mode", 3}, {"c_mode", 2}, {"d_mode", 3}, {"a_blend", 0.2f}, {"b_blend", 0.5f},
                   {"c_blend", 0.75f}, {"d_blend", 0.95f}, {"a_tune", 12}, {"b_tune", 7}, {"c_tune", 19}, {"d_tune", 5},
                   {"orb_speed", 0.3f}, {"decay", 0.45f}, {"sustain", 0.4f}, END}, {END}, {{"mix_dly", 0.25f}, END}},
    {"ARP HEART", {{"shape", 10}, {"a_og1", 0.06f}, {"b_og1", 0.2f}, {"c_og1", 0.12f}, {"d_og1", 0.5f}, {"decay", 0.4f},
                   {"sustain", 0.5f}, {"release", 0.3f}, {"orb_sync", 1}, {"orb_div", 2}, END}, {END},
     {{"arp_on", 1}, {"arp_mode", 2}, {"arp_rate", 3}, {"arp_oct", 2}, {"note_len", 0.6f}, {"step3", 2}, {"step7", 0},
      {"step11", 2}, {"step14", 3}, {"mix_dly", 0.3f}, {"dly_div", 5}, END}},
    {"DUAL PAD", {{"shape", 0}, {"attack", 0.55f}, {"release", 0.65f}, {"orb_speed", 0.12f}, {"a_og1", 0.3f}, {"c_og1", 0.6f},
                  END}, {{"shape", 12}, {"attack", 0.6f}, {"release", 0.7f}, {"octave", 1}, {"a_og1", 0.75f}, {"b_og1", 0.85f},
                         {"orb_speed", 0.2f}, {"t_volume", 0.6f}, END}, {{"keys", 1}, {"tmode", 0}, {"mix_rev", 0.5f}, {"rev_size", 0.9f}, END}},
    {"SPLIT BASS/LEAD", {{"shape", 3}, {"octave", -1}, {"a_og1", 0.1f}, {"b_og1", 0.09f}, {"c_og1", 0.07f}, {"d_og1", 0.1f},
                         {"a_ftype", 1}, {"b_ftype", 1}, {"c_ftype", 1}, {"d_ftype", 1}, {"a_cutoff", 0.45f}, {"b_cutoff", 0.5f},
                         {"decay", 0.3f}, {"sustain", 0.5f}, {"glide", 0.15f}, END},
     {{"shape", 5}, {"a_og1", 0.95f}, {"b_og1", 0.2f}, {"vib_depth", 0.2f}, {"glide", 0.2f}, END},
     {{"keys", 1}, {"tmode", 1}, {"split", 55}, {"mix_dly", 0.2f}, END}},
    {"METAL ICOSA", {{"shape", 14}, {"a_mode", 5}, {"b_mode", 6}, {"c_mode", 5}, {"d_mode", 6}, {"a_tune", 11}, {"b_tune", 13},
                     {"c_tune", -5}, {"d_tune", 23}, {"a_og1", 0.9f}, {"spin", 0.5f}, {"decay", 0.5f}, {"sustain", 0.2f}, END},
     {END}, {{"mix_rev", 0.3f}, {"drive", 0.2f}, END}},
    {"SWEEP LISSA", {{"shape", 6}, {"a_cutoff", 0.25f}, {"b_cutoff", 0.85f}, {"c_cutoff", 0.55f}, {"d_cutoff", 0.95f},
                     {"a_reso", 0.7f}, {"b_reso", 0.4f}, {"a_ftype", 1}, {"d_ftype", 2}, {"orb_speed", 0.5f}, {"sub_speed", 0.6f},
                     END}, {END}, {{"mix_dly", 0.2f}, {"mres", 0.2f}, END}},
    {"ORGAN SQUARE", {{"shape", 3}, {"a_og1", 0.48f}, {"b_og1", 0.52f}, {"c_og1", 0.55f}, {"d_og1", 0.58f}, {"trem_depth", 0.3f},
                      {"trem_speed", 0.55f}, {"attack", 0.0f}, {"release", 0.15f}, {"sustain", 1.0f}, {"orb_speed", 0.2f}, END},
     {END}, {{"mix_ch", 0.4f}, {"mix_rev", 0.2f}, END}},
    {"FOLD ZIGZAG", {{"shape", 11}, {"a_mode", 6}, {"b_mode", 6}, {"c_mode", 6}, {"d_mode", 6}, {"a_blend", 0.2f}, {"b_blend", 0.5f},
                     {"c_blend", 0.8f}, {"d_blend", 1.0f}, {"orb_speed", 0.45f}, {"decay", 0.35f}, {"sustain", 0.6f}, END},
     {END}, {{"drive", 0.25f}, {"drive_col", 0.7f}, END}},
    {"AIR SPIRO", {{"shape", 12}, {"a_og1", 0.84f}, {"b_og1", 0.87f}, {"c_og1", 0.0f}, {"d_og1", 0.81f}, {"attack", 0.5f},
                   {"release", 0.7f}, {"sub_size", 0.5f}, {"sub_speed", 0.3f}, END}, {END}, {{"mix_rev", 0.55f}, {"rev_size", 0.95f}, END}},
    {"SHAPE SCOPE", {{"shape", 2}, {"shape_osc", 0.7f}, {"a_level", 0.3f}, {"b_level", 0.3f}, {"c_level", 0.3f}, {"d_level", 0.3f},
                     {"orb_speed", 0.3f}, {"rotate", 0.1f}, END}, {END}, {{"mix_rev", 0.2f}, END}},
    {"SAMPLE DRONES", {{"shape", 0}, {"a_level", 0.8f}, {"b_level", 0.8f}, {"c_level", 0.7f}, {"d_level", 0.7f}, {"orb_speed", 0.1f},
                       {"sub_size", 0.3f}, {"attack", 0.6f}, {"release", 0.75f}, {"sustain", 1.0f}, END}, {END},
     {{"smp_a", 39}, {"smp_b", 43}, {"smp_c", 44}, {"smp_d", 41}, {"smp_len_a", 4}, {"smp_len_b", 4}, {"smp_len_c", 4},
      {"smp_len_d", 4}, {"mix_rev", 0.5f}, {"rev_size", 0.9f}, END}},
    {"SAMPLE ATMOS", {{"shape", 2}, {"orb_speed", 0.15f}, {"sub_size", 0.35f}, {"attack", 0.5f}, {"release", 0.7f}, {"sustain", 1.0f}, END},
     {END}, {{"smp_a", 46}, {"smp_b", 47}, {"smp_c", 48}, {"smp_d", 49}, {"smp_len_a", 4}, {"smp_len_b", 4}, {"smp_len_c", 4},
             {"smp_len_d", 4}, {"mix_dly", 0.25f}, {"mix_rev", 0.4f}, END}},
    {"SAMPLE PADS", {{"shape", 1}, {"orb_speed", 0.2f}, {"attack", 0.4f}, {"release", 0.6f}, {"sustain", 0.9f}, END}, {END},
     {{"smp_a", 22}, {"smp_b", 23}, {"smp_c", 25}, {"smp_d", 35}, {"smp_len_a", 4}, {"smp_len_b", 4}, {"smp_len_c", 2},
      {"smp_len_d", 4}, {"mix_ch", 0.3f}, {"mix_rev", 0.45f}, END}},
    {"SAMPLE BELLS", {{"shape", 13}, {"orb_speed", 0.3f}, {"decay", 0.6f}, {"sustain", 0.4f}, {"release", 0.5f}, END}, {END},
     {{"smp_a", 17}, {"smp_b", 26}, {"smp_c", 31}, {"smp_d", 38}, 
      {"mix_rev", 0.5f}, END}},
    {"SAMPLE PLAY", {{"shape", 5}, {"orb_speed", 0.35f}, {"decay", 0.4f}, {"sustain", 0.5f}, {"release", 0.4f}, END}, {END},
     {{"smp_a", 28}, {"smp_b", 18}, {"smp_c", 29}, {"smp_d", 34}, 
      {"mix_dly", 0.2f}, END}},
    {"SAMPLE TEXTURES", {{"shape", 12}, {"orb_speed", 0.25f}, {"attack", 0.3f}, {"release", 0.6f}, {"sustain", 0.8f}, END}, {END},
     {{"smp_a", 51}, {"smp_b", 52}, {"smp_c", 54}, {"smp_d", 27}, {"smp_len_a", 4}, {"smp_len_b", 2}, {"smp_len_c", 4},
      {"smp_len_d", 2}, {"mix_rev", 0.5f}, {"mix_dly", 0.3f}, END}},
};
#define NPRESET ((int)(sizeof PRESETS / sizeof PRESETS[0]))

static void apply_kv(vader_t *s, int t, const kv_t *kv) {
    for (; kv->key; kv++) {
        int p = find(kv->key);
        if (p < 0) continue;
        if (VP[p].kind == K_SND) T(t)[VP[p].slot] = kv->v;
        else if (VP[p].kind == K_GLOB) s->g[VP[p].slot] = kv->v;
    }
}

/* every parameter but the screen's own pieces (those are reported as they differ from what MPC was last told) */
static void all_dirty(vader_t *s) {
    for (int p = 0; p < NP; p++)
        if (VP[p].kind != K_POPUP && VP[p].kind != K_TRIG && !is_screen(p)) s->dirty[p] = 1;
}

/* MPC shows every screen piece empty (a fresh instance, or one whose state was just set): report what was lit so
 * it clears, and the next screen update reports everything that shows something */
static void screen_forget(vader_t *s) {
    for (int i = 0; i < TILE_COLS * TILE_ROWS; i++) {
        if (s->sent_tiles[i]) mark(s, P_T000 + i);
        if (s->sent_trail[i]) mark(s, P_R000 + i);
        if (s->sent_orbit[i]) mark(s, P_L000 + i);
    }
    for (int i = 0; i < BALL_N * BALL_N; i++) {
        if (s->sent_bo[i]) mark(s, P_BO00 + i);
        if (s->sent_bp[i]) mark(s, P_BP00 + i);
    }
    memset(s->sent_tiles, 0, sizeof s->sent_tiles);
    memset(s->sent_trail, 0, sizeof s->sent_trail);
    memset(s->sent_orbit, 0, sizeof s->sent_orbit);
    memset(s->sent_bo, 0, sizeof s->sent_bo);
    memset(s->sent_bp, 0, sizeof s->sent_bp);
    s->sent_ball_o = s->sent_ball_p = 0xff;
}

static void load_preset(vader_t *s, int n) {
    if (n < 0 || n >= NPRESET) return;
    release_all(s);   /* nothing keeps sounding from the sound before */
    const preset_t *P = &PRESETS[n];
    /* keep how the instance is being used: what's edited, the screen, the file */
    float keep[] = {s->g[G_TIMBRE], s->g[G_CORNER], s->g[G_ALT], s->g[G_MOTION], s->g[G_FILE], s->g[G_VOICES], s->g[G_QR_SHOW]};
    memcpy(s->g, GLOB_DEF, sizeof s->g);
    s->g[G_TIMBRE] = keep[0]; s->g[G_CORNER] = keep[1]; s->g[G_ALT] = keep[2]; s->g[G_MOTION] = keep[3];
    s->g[G_FILE] = keep[4]; s->g[G_VOICES] = keep[5]; s->g[G_QR_SHOW] = keep[6];
    mv_defaults(s);
    memset(s->sim, 0, sizeof s->sim);
    s->ndrone = 0;
    s->self_next = -1;
    memcpy(T(0), SND_DEF, sizeof(float) * NSND);
    apply_kv(s, 0, P->a);
    memcpy(T(1), T(0), sizeof(float) * NSND);
    apply_kv(s, 1, P->b);
    apply_kv(s, 0, P->g);   /* the globals (K_GLOB entries only) */
    smp_sync_all(s);
    s->g[G_PRESET] = (float)n;
    s->preset = n;
    for (int t = 0; t < 2; t++) timbre_shape(s, t);
    all_dirty(s);
}

/* ------------------------------------------------------------------------------------------------ voices */
static float note_hz(float n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

static int max_voices(vader_t *s) { int m = (int)s->g[G_VOICES] ? 16 : 8, c = (int)s->g[G_VOICE_CAP]; return c >= 1 && c < m ? c : m; }

/* which timbres a key plays: bit 0 A, bit 1 B */
static int route(vader_t *s, int note) {
    int k = (int)s->g[G_KEYS];
    if (k == 0) return 1;
    if (k == 2) return 2;
    if ((int)s->g[G_TMODE] == 0) return 3;
    return note < (int)s->g[G_SPLIT] ? 1 : 2;
}

static int timbre_limit(vader_t *s) {
    int both = (int)s->g[G_KEYS] == 1;
    return both ? max_voices(s) / 2 : max_voices(s);
}

static voice_t *alloc_voice(vader_t *s, int t) {
    int mv = max_voices(s), lim = timbre_limit(s), used = 0;
    int vm = (int)s->g[G_VOICE_MODE];   /* 0.8: POLY, DUAL (2 notes), MONO (1) */
    if (vm) {
        int cap = (vm == 1 ? 2 : 1) * (1 << ((int)T(t)[S_UNISON] & 3));
        if (cap < lim) lim = cap;
    }
    voice_t *free_v = NULL, *oldest = NULL, *oldest_rel = NULL;
    for (int i = 0; i < mv; i++) {
        voice_t *v = &s->v[i];
        if (!v->active) { if (!free_v) free_v = v; continue; }
        if (v->timbre != t) continue;
        used++;
        if (!oldest || v->age < oldest->age) oldest = v;
        if (v->released && (!oldest_rel || v->age < oldest_rel->age)) oldest_rel = v;
    }
    if (used < lim && free_v) return free_v;
    if (oldest_rel && !vm) return oldest_rel;
    if (oldest) return oldest;
    return free_v;
}

static void voice_on(vader_t *s, int t, int note, int key, float vel) {
    static const int UNI[4] = {1, 2, 4, 8};
    int u = UNI[(int)T(t)[S_UNISON] & 3];
    float det = T(t)[S_DETUNE];
    for (int k = 0; k < u; k++) {
        voice_t *v = alloc_voice(s, t);
        if (!v) return;
        int legato = v->active && T(t)[S_GLIDE] > 0.001f;
        float prev = v->freq;
        float ph[4][2];
        memcpy(ph, v->ph, sizeof ph);
        void *keep_mi[4];
        for (int c = 0; c < 4; c++) keep_mi[c] = v->eng[c].mi;
        memset(v, 0, sizeof *v);
        for (int c = 0; c < 4; c++) v->eng[c].mi = keep_mi[c];
        v->active = 1;
        v->timbre = t;
        v->note = note;
        v->key = key;
        v->vel = vel;
        v->age = ++s->age;
        float spread = u > 1 ? (2.0f * k / (u - 1) - 1) : 0;
        v->cents = spread * det * 40.0f + (u == 1 ? 0 : 0);
        v->pan = spread * 0.8f;
        v->target = note_hz((float)note);
        v->freq = (T(t)[S_GLIDE] > 0.001f && prev > 0) ? prev : v->target;
        (void)legato;
        if (!(int)T(t)[S_RETRIG]) memcpy(v->ph, ph, sizeof ph);   /* free-running phases: no click on a fast trill */
        for (int c = 0; c < 4; c++) v->w[c] = -1;   /* first block: no ramp from zero */
        v->im_init = 0;
        s->last_voice = (int)(v - s->v);
    }
}

/* a key let go: every voice it started stops, whatever has changed since (octave, KEYS, split, a preset) */
static void voice_off(vader_t *s, int key) {
    for (int i = 0; i < MAXV; i++) {
        voice_t *v = &s->v[i];
        if (v->active && !v->released && v->key == key) {
            if (s->pedal) v->held_by_pedal = 1;
            else { v->released = 1; v->stage = 3; }
        }
    }
}

static void all_off(vader_t *s, int hard) {
    for (int i = 0; i < MAXV; i++) {
        if (hard) s->v[i].active = 0;
        else if (s->v[i].active) { s->v[i].released = 1; s->v[i].stage = 3; }
    }
}

/* a note into the instrument (from the keys or the arp) */
static void play_on(vader_t *s, int note, float vel) {
    int r = route(s, note);
    for (int t = 0; t < 2; t++)
        if (r & (1 << t)) {
            int n = note + 12 * (int)T(t)[S_OCTAVE];
            if (n >= 0 && n < 128) voice_on(s, t, n, note, vel);
        }
}
static void play_off(vader_t *s, int note) { voice_off(s, note); }

/* ------------------------------------------------------------------------------------------------ arpeggiator */
static float arp_step_beats(vader_t *s) {
    static const float b[6] = {1.0f, 0.5f, 1.0f / 3, 0.25f, 1.0f / 6, 0.125f};
    return b[(int)s->g[G_ARP_RATE] % 6];
}

static void arp_release(vader_t *s) {
    for (int i = 0; i < s->narp; i++) play_off(s, s->arp_sounding[i]);
    s->narp = 0;
}

static int arp_order(vader_t *s, int *out) {
    int n = 0, oct = (int)s->g[G_ARP_WALK] ? 1 : (int)s->g[G_ARP_OCT];   /* RISE and RANDOM walk the octaves themselves */
    int sorted[MAXHELD];
    memcpy(sorted, s->held, sizeof(int) * (size_t)s->nheld);
    if ((int)s->g[G_ARP_MODE] != 4)
        for (int i = 1; i < s->nheld; i++)
            for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) { int t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
    for (int o = 0; o < oct; o++)
        for (int i = 0; i < s->nheld && n < 128; i++) out[n++] = sorted[i] + 12 * o;
    return n;
}

static void arp_step(vader_t *s, float step_frames) {
    int steps = (int)s->g[G_ARP_STEPS];
    if (steps < 1) steps = 1;
    int st = (int)s->g[G_STEP1 + (s->arp_count % steps)];
    s->arp_count++;
    if (st == 3 && s->narp) {   /* TIE: the note goes on */
        s->arp_off_at += (long)step_frames;
        return;
    }
    arp_release(s);
    if (st == 0 || s->nheld == 0) return;
    int seq[128];
    int n = arp_order(s, seq), mode = (int)s->g[G_ARP_MODE];
    if (!n) return;
    int chain = (int)s->g[G_ARP_CHAIN];
    if (chain > 0 && mode < 4) mode = (mode + s->chain_pos) % 4;   /* ARP CHAIN: UP, DOWN, UP-DOWN, RANDOM, then round again */
    else s->chain_pos = 0;
    float vel = st == 2 ? 1.0f : 0.75f;
    int walk = (int)s->g[G_ARP_WALK], octs = (int)s->g[G_ARP_OCT], so = (int)s->g[G_STEPO1 + ((s->arp_count - 1) % steps)];
    float hum = s->g[G_ARP_HUMAN];
    if (hum > 0.001f) {
        s->rng = s->rng * 1664525u + 1013904223u;
        vel = clampf(vel + (((s->rng >> 8) & 1023) / 1023.0f - 0.5f) * 0.5f * hum, 0.1f, 1.0f);
    }
    if (mode == 5) {   /* CHORD */
        for (int i = 0; i < s->nheld && s->narp < MAXHELD; i++) {
            s->arp_sounding[s->narp++] = s->held[i];
            play_on(s, s->held[i], vel);
        }
    } else {
        int note;
        if (mode == 3) {
            s->rng = s->rng * 1664525u + 1013904223u;
            note = seq[(s->rng >> 8) % (unsigned)n];
        }
        else if (mode == 2 && n > 1) {   /* UP-DOWN, the ends once */
            int per = 2 * n - 2, k = s->arp_idx % per;
            note = seq[k < n ? k : per - k];
            s->arp_idx++;
        } else {
            int k = s->arp_idx % n;
            note = seq[mode == 1 ? n - 1 - k : k];
            s->arp_idx++;
        }
        if (walk == 1 && octs > 1) note += 12 * ((s->arp_idx / (n > 0 ? n : 1)) % octs);
        else if (walk == 2 && octs > 1) { s->rng = s->rng * 1664525u + 1013904223u; note += 12 * (int)((s->rng >> 8) % (unsigned)octs); }
        note += 12 * so;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        s->arp_sounding[s->narp++] = note;
        play_on(s, note, vel);
    }
    if (chain > 0 && mode < 4) {
        int per = mode == 2 && n > 1 ? 2 * n - 2 : n;
        if (++s->chain_n >= chain * per) { s->chain_n = 0; s->chain_pos = (s->chain_pos + 1) % 4; s->arp_idx = 0; }
    }
    float len = s->g[G_NOTE_LEN];
    if (hum > 0.001f) { s->rng = s->rng * 1664525u + 1013904223u; len = clampf(len * (1 + (((s->rng >> 8) & 1023) / 1023.0f - 0.5f) * 0.6f * hum), 0.02f, 1.0f); }
    s->arp_off_at = (long)(step_frames * (0.05f + 0.93f * len));
}

/* ------------------------------------------------------------------------------------------------ 0.8 self play, drone */
static const unsigned char SC_DEG[8][12] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 7, 9},
                                           {0, 3, 5, 7, 10}, {0, 2, 4, 6, 8, 10}, {0, 3, 5, 6, 7, 10}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
static const unsigned char SC_N[8] = {7, 7, 7, 5, 5, 6, 6, 12};

static void orbiter_pos(vader_t *s, int t, float *x, float *y) {
    timbre_t *m = &s->tm[t];
    if ((int)T(t)[S_SIM_MODE] > 0) { *x = s->sim[t].sx[0]; *y = s->sim[t].sy[0]; }
    else path_at(m->x2, m->y2, path_u(T(t), m->orb_t), x, y);
    orb_xform(T(t), x, y);
    *x *= m->size_s < 0 ? T(t)[S_ORB_SIZE] : m->size_s;
    *y *= m->size_s < 0 ? T(t)[S_ORB_SIZE] : m->size_s;
}

/* SELF PLAY: a note from the scale on the orbiter's place: x picks the degree, y the velocity */
static void self_step(vader_t *s, float step_frames) {
    arp_release(s);
    int sc = (int)s->g[G_SELF_SCALE] & 7, span = (int)s->g[G_SELF_RANGE];
    if (span < 1) span = 1;
    float x, y;
    orbiter_pos(s, 0, &x, &y);
    int total = SC_N[sc] * span;
    int idx = (int)floorf((clampf(x, -1, 1) * 0.5f + 0.5f) * total);
    if (idx >= total) idx = total - 1;
    int note = 36 + (int)s->g[G_SELF_ROOT] + 12 * (idx / SC_N[sc]) + SC_DEG[sc][idx % SC_N[sc]];
    float vel = 0.45f + 0.5f * (clampf(y, -1, 1) * 0.5f + 0.5f);
    if (s->narp < MAXHELD) { s->arp_sounding[s->narp++] = note; play_on(s, note, vel); }
    s->arp_off_at = (long)(step_frames * (0.05f + 0.93f * s->g[G_NOTE_LEN]));
}

/* DRONE: the chord's notes held on until it is switched off */
static void drone_apply(vader_t *s) {
    for (int i = 0; i < s->ndrone; i++) play_off(s, s->drone_notes[i]);
    s->ndrone = 0;
    if (!(int)s->g[G_DRONE]) return;
    static const signed char CH[6][3] = {{0, -1, -1}, {0, 7, -1}, {0, 12, -1}, {0, 3, 7}, {0, 4, 7}, {0, 5, 7}};
    int root = 36 + (int)s->g[G_SELF_ROOT], ch = (int)s->g[G_DRONE_CHORD] % 6;
    for (int k = 0; k < 3; k++)
        if (CH[ch][k] >= 0) {
            int n = root + CH[ch][k];
            s->drone_notes[s->ndrone++] = n;
            play_on(s, n, 0.7f);
        }
}

/* ------------------------------------------------------------------------------------------------ 0.8 randomizer */
static float rnd01(vader_t *s) {
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(s->rng >> 8) * (1.0f / 16777216.0f);
}
static void param_set_slot(vader_t *s, int t, int slot, float v) {
    T(t)[slot] = v;
    if (t == edit(s)) mark(s, SND_P[slot]);
}
/* new random values for the sound of the edited timbre, AMOUNT of the way from the present ones; the old ones are kept for UNDO */
static void randomize(vader_t *s) {
    int t = edit(s);
    memcpy(s->undo_snd, T(t), sizeof s->undo_snd);
    memcpy(s->undo_mv, s->mv[t], sizeof s->undo_mv);
    s->undo_t = t + 1;
    float amt = s->g[G_RAND_AMT];
    float *P = T(t);
    for (int c = 0; c < 4; c++) {
        int b = c * NCF;
        static const int F[] = {CF_OG1, CF_OG2, CF_BLEND, CF_CUTOFF, CF_RESO};
        for (int k = 0; k < 5; k++) param_set_slot(s, t, b + F[k], P[b + F[k]] + (rnd01(s) - P[b + F[k]]) * amt);
        param_set_slot(s, t, b + CF_MODE, rnd01(s) < amt ? (float)(int)(rnd01(s) * 7) : P[b + CF_MODE]);
        param_set_slot(s, t, b + CF_FTYPE, rnd01(s) < amt * 0.5f ? (float)(int)(rnd01(s) * 5) : P[b + CF_FTYPE]);
        param_set_slot(s, t, b + CF_TUNE, rnd01(s) < amt ? (float)((int)(rnd01(s) * 4) * 5 - 5 + (rnd01(s) < 0.5f ? 0 : 7)) : P[b + CF_TUNE]);
    }
    static const int O[] = {S_ORB_SPEED, S_ORB_SIZE, S_SUB_SIZE, S_SUB_SPEED, S_ROTATE, S_SH_A, S_SH_B, S_SH_C};
    for (int k = 0; k < 8; k++) param_set_slot(s, t, O[k], P[O[k]] + (rnd01(s) - P[O[k]]) * amt);
    if (rnd01(s) < amt) {   /* a shape: one of the built-in ones or a maths one (not the picture USER slot) */
        int n = 15 + NMATH;
        int idx = (int)(rnd01(s) * n);
        if (idx >= 15) idx += 1;
        param_set_slot(s, t, S_SHAPE, (float)idx);
        timbre_shape(s, t);
    }
    timbre_shape(s, t);
    s->shape_wait = 0;
}
static void undo_random(vader_t *s) {
    if (!s->undo_t) return;
    int t = s->undo_t - 1;
    memcpy(T(t), s->undo_snd, sizeof s->undo_snd);
    memcpy(s->mv[t], s->undo_mv, sizeof s->undo_mv);
    s->undo_t = 0;
    timbre_shape(s, t);
    mode_load(s, t, 1);
    if (t == edit(s)) mark_sound(s);
    s->shape_wait = 0;
}

/* every note lets go (they fade with their release), the arpeggiator forgets its keys */
static void release_all(vader_t *s) {
    arp_release(s);
    s->nheld = 0;
    s->latch_fresh = 0;
    s->pedal = 0;
    all_off(s, 0);
    for (int i = 0; i < MAXV; i++) s->v[i].held_by_pedal = 0;
}

static void key_on(vader_t *s, int note, float vel) {
    if ((int)s->g[G_ARP_ON]) {
        if ((int)s->g[G_LATCH] && s->latch_fresh) { s->nheld = 0; s->latch_fresh = 0; }
        for (int i = 0; i < s->nheld; i++)
            if (s->held[i] == note) return;
        if (s->nheld < MAXHELD) s->held[s->nheld++] = note;
        if (s->nheld == 1 && !s->playing) { s->arp_next = s->arp_free_beats; s->arp_idx = 0; s->arp_count = 0; }
        return;
    }
    play_on(s, note, vel);
}

static void key_off(vader_t *s, int note) {
    if ((int)s->g[G_ARP_ON]) {
        if ((int)s->g[G_LATCH]) { s->latch_fresh = 1; return; }
        for (int i = 0; i < s->nheld; i++)
            if (s->held[i] == note) {
                memmove(&s->held[i], &s->held[i + 1], sizeof(int) * (size_t)(s->nheld - i - 1));
                s->nheld--;
                break;
            }
        if (!s->nheld) arp_release(s);
        return;
    }
    play_off(s, note);
}

/* ------------------------------------------------------------------------------------------------ rendering */
static inline float svf(float x, float *ic1, float *ic2, float a1, float a2, float a3, float k, int type) {
    float v3 = x - *ic2, v1 = a1 * *ic1 + a2 * v3, v2 = *ic2 + a2 * *ic1 + a3 * v3;
    *ic1 = 2 * v1 - *ic1;
    *ic2 = 2 * v2 - *ic2;
    return type == 2 ? v1 : type == 3 ? x - k * v1 - v2 : v2;
}

static inline float fast_tanh(float x) {
    if (x > 3) return 1;
    if (x < -3) return -1;
    float x2 = x * x;
    return x * (27 + x2) / (27 + 9 * x2);
}

typedef struct { float a1, a2, a3, k, G, kl; int type; } filt_t;

/* 0.8: a corner's limiter: a soft ceiling that comes down as the amount goes up (0 = off) */
static inline float corner_lim(float x, float amt) {
    if (amt < 0.001f) return x;
    float thr = 1.0f - 0.85f * amt;
    return thr * tanhf(x / thr);
}
static inline float corner_filter(voice_t *v, int c, const filt_t *f, float x) {
    switch (f->type) {
    case 0: case 2: case 3: return svf(x, &v->s1[c][0], &v->s1[c][1], f->a1, f->a2, f->a3, f->k, f->type == 0 ? 0 : f->type);
    case 1: {
        float y = svf(x, &v->s1[c][0], &v->s1[c][1], f->a1, f->a2, f->a3, f->k, 0);
        return svf(y, &v->s2[c][0], &v->s2[c][1], f->a1, f->a2, f->a3, 1.41f, 0);
    }
    default: {   /* ladder: four one-poles, feedback from the last sample */
        float u = fast_tanh(x - f->kl * v->lad_y[c]);
        for (int k = 0; k < 4; k++) {
            float w = (u - v->lad[c][k]) * f->G, y = w + v->lad[c][k];
            v->lad[c][k] = y + w;
            u = y;
        }
        v->lad_y[c] = u;
        return u * (1 + f->kl * 0.25f);
    }
    }
}

/* ---- the IMAGE page: read the picture under the orbiter */
static float img_at(const float *g, float x, float y) {   /* x, y: -1..1 over the picture (bilinear; 0 outside) */
    float u = (x + 1) * 0.5f * IMG_N - 0.5f, w = (y + 1) * 0.5f * IMG_N - 0.5f;
    if (u < -0.5f || w < -0.5f || u > IMG_N - 0.5f || w > IMG_N - 0.5f) return 0;
    int i = (int)floorf(u), j = (int)floorf(w);
    float fu = u - i, fw = w - j;
    float a[4];
    for (int k = 0; k < 4; k++) {
        int ii = i + (k & 1), jj = j + (k >> 1);
        if (ii < 0) ii = 0;
        if (jj < 0) jj = 0;
        if (ii > IMG_N - 1) ii = IMG_N - 1;
        if (jj > IMG_N - 1) jj = IMG_N - 1;
        a[k] = g[jj * IMG_N + ii];
    }
    return (a[0] * (1 - fu) + a[1] * fu) * (1 - fw) + (a[2] * (1 - fu) + a[3] * fu) * fw;
}

/* the six sources at the orbiter (ox, oy: on the square) for a voice: BRIGHT, HUE, COLOUR 0..1; DEPTH -1..1 (the slope
 * it travels on); TURN 0..1 (how sharply the outline turns at the path point, t); HEADING -1..1 (up +1, down -1) */
static void img_sources(vader_t *s, voice_t *v, timbre_t *m, float size, float rot, float ox, float oy, float t, float *src) {
    float x = ox, y = oy;
    if (!m->base.is3d) {   /* back onto the picture: undo ROTATE and ORBIT SIZE */
        float c = cosf(-rot * TWO_PI), sn = sinf(-rot * TWO_PI);
        float xx = x * c - y * sn, yy = x * sn + y * c;
        x = xx; y = yy;
    }
    if (size > 0.02f) { x /= size; y /= size; }
    int ok = s->img.ok;
    src[0] = ok ? img_at(s->img.v, x, y) : 0;
    src[1] = ok ? img_at(s->img.hue, x, y) : 0;
    src[2] = ok ? img_at(s->img.sat, x, y) : 0;
    /* DEPTH: the brightness as height, its slope along where the orbiter is going */
    float dx = ox - v->ox, dy = oy - v->oy, d = sqrtf(dx * dx + dy * dy);
    src[3] = 0;
    if (ok && d > 1e-5f) {
        float e = 2.0f / IMG_N, ux = dx / d, uy = dy / d;
        if (!m->base.is3d) { float c = cosf(-rot * TWO_PI), sn = sinf(-rot * TWO_PI), a = ux * c - uy * sn; uy = ux * sn + uy * c; ux = a; }
        float h1 = img_at(s->img.v, x + ux * e, y + uy * e), h0 = img_at(s->img.v, x - ux * e, y - uy * e);
        src[3] = clampf((h1 - h0) * 3.0f, -1, 1);
    }
    /* the outline at the path point */
    float ax, ay, bx, by, cx, cy;
    path_at(m->x2, m->y2, t - 3.0f / PATHN, &ax, &ay);
    path_at(m->x2, m->y2, t, &bx, &by);
    path_at(m->x2, m->y2, t + 3.0f / PATHN, &cx, &cy);
    float t1 = atan2f(by - ay, bx - ax), t2 = atan2f(cy - by, cx - bx), dt_ = fabsf(t2 - t1);
    if (dt_ > 3.14159265f) dt_ = TWO_PI - dt_;
    src[4] = clampf(dt_ / 3.14159265f * 1.5f, 0, 1);
    src[5] = (fabsf(cx - ax) + fabsf(cy - ay)) > 1e-6f ? -sinf(atan2f(cy - ay, cx - ax)) : 0;
}

/* 0.8: render a corner's ZYNTH voice engine for n frames (blocks of 8) */
static void corner_engine(vader_t *s, voice_t *v, int c, const float *P, int ei, float freq, float *out, int n) {
    int type = ENG_ZID[ei - 1];
    Eng *en = &v->eng[c];
    if (v->eng_type[c] != type + 1) {
        if ((type == ENG_MACRO || type == ENG_WAVE) && !en->mi) en->mi = mi_new();
        eng_reset(en, type);
        v->eng_type[c] = (unsigned char)(type + 1);
        v->eng_new[c] = 1;
    }
    EngIn in;
    memset(&in, 0, sizeof in);
    for (int j = 0; j < ENG_SLIDERS; j++) in.p[j] = ENG_DEFS[type].def[j];
    for (int k = 0; k < 4; k++) in.p[ENG_SL[ei - 1][k]] = P[S_A_V1 + c * 5 + k];
    in.freq = freq;
    in.note = 69.0f + 12.0f * log2f(freq / 440.0f);
    in.vel = v->vel;
    in.env = v->env;
    in.bpm = tempo_of(s);
    in.gate = v->stage != 3;
    in.wave = 1;
    in.fmRatio = 1.0f;
    in.trig = v->eng_new[c];
    v->eng_new[c] = 0;
    for (int i = 0; i < n; i += ENG_MAX_BLOCK) {
        int m = n - i < ENG_MAX_BLOCK ? n - i : ENG_MAX_BLOCK;
        eng_render(en, &in, out + i, m);
        in.trig = 0;
    }
}

static void voice_render(vader_t *s, voice_t *v, float *L, float *R, int n, float vib, float trem) {
    int t = v->timbre;
    const float *P = T(t);
    timbre_t *m = &s->tm[t];
    float dt = n * INV_SR;
    /* pitch: glide, bend, vibrato, unison spread */
    if (P[S_GLIDE] > 0.001f) {
        float gt = 0.002f + 2.0f * P[S_GLIDE] * P[S_GLIDE];
        float k = 1 - expf(-dt / gt);
        v->freq *= powf(v->target / v->freq, k);
    } else v->freq = v->target;
    float semis = s->bend * 2 + vib + v->cents / 100.0f;
    float base = v->freq * powf(2.0f, semis / 12.0f);
    /* envelope coefficients */
    float at = 0.001f * powf(6000.0f, P[S_ATTACK]), dc = 0.005f * powf(2000.0f, P[S_DECAY]), rl = 0.005f * powf(2000.0f, P[S_RELEASE]);
    float a_inc = 1.0f / (at * SR), d_k = expf(-6.9f / (dc * SR)), r_k = expf(-6.9f / (rl * SR)), sus = P[S_SUSTAIN];
    /* the orbiter */
    float env_now = v->env;
    float size = m->size_s;
    float oe = P[S_ORB_ENV];
    if (oe > 0) size *= 1 - oe + oe * env_now;
    else if (oe < 0) size *= 1 + oe * env_now;
    float ot = (int)P[S_RETRIG] ? v->orb_t : m->orb_t, oo = (int)P[S_RETRIG] ? v->orb_o : m->orb_o;
    if ((int)P[S_RETRIG]) {   /* its own orbit, from the start of the shape at each note */
        float hz, sub;
        if ((int)P[S_ORB_SYNC]) { hz = tempo_of(s) / 60.0f / div_beats((int)P[S_ORB_DIV]); sub = hz * (1 + floorf(P[S_SUB_SPEED] * 15)); }
        else { hz = 0.02f * powf(1000.0f, P[S_ORB_SPEED]); sub = 0.05f * powf(600.0f, P[S_SUB_SPEED]); }
        v->orb_t = fmodf(v->orb_t + hz * dt * m->spd_mul, 1.0f);
        v->orb_o = fmodf(v->orb_o + sub * dt, 1.0f);
        ot = v->orb_t;
        oo = v->orb_o;
    }
    float px, py;
    if ((int)P[S_FOCUS]) { px = (P[S_FOCUS_X] - 0.5f) * 2; py = (P[S_FOCUS_Y] - 0.5f) * 2; }   /* steered by hand */
    else {
        if ((int)P[S_SIM_MODE] > 0) { px = s->sim[v->timbre].sx[0]; py = s->sim[v->timbre].sy[0]; }
        else path_at(m->x2, m->y2, path_u(P, ot), &px, &py);
        orb_xform(P, &px, &py);
    }
    px *= size;
    py *= size;
    float sr_ = m->sub_s * 0.6f;
    float sc_, ss_;
    sub_pos(P, oo, &sc_, &ss_);
    float ox = clampf(px + sr_ * sc_, -1, 1), oy = clampf(py + sr_ * ss_, -1, 1);
    /* the IMAGE page: four MODs, each a source under the orbiter moving a sound control */
    float im_cut = 0, im_res = 0, im_oct = 0, im_semi = 0, im_og1 = 0, im_og2 = 0, im_blend = 0, im_lvl = 1;
    {
        float src[6];
        img_sources(s, v, m, size, m->rot_s, ox, oy, ot, src);
        for (int k = 0; k < 4; k++) {
            const float *Q = &P[S_IM1_SRC + k * 4];   /* SRC, TO, AMT, SMOOTH */
            int si = (int)Q[0], to = (int)Q[1];
            float amt = Q[2], target = amt * src[si < 0 ? 0 : si > 5 ? 5 : si];
            v->im_src[k] = src[si < 0 ? 0 : si > 5 ? 5 : si];
            float tau = 0.003f + 1.2f * Q[3] * Q[3];
            if (!v->im_init) v->im[k] = target;
            else v->im[k] += (1 - expf(-dt / tau)) * (target - v->im[k]);
            float mv = v->im[k];
            if (fabsf(amt) < 0.0005f) continue;
            switch (to) {
            case 0: im_cut += mv * 6; break;                        /* CUTOFF: up to 6 octaves */
            case 1: im_res += mv; break;                            /* RESO */
            case 2: im_oct += roundf(mv * 2); break;                /* OCTAVE: whole octaves, -2..+2 */
            case 3: im_semi += roundf(mv * 12); break;              /* PITCH: semitones, -12..+12 */
            case 4: im_og1 += mv; break;                            /* GEN 1 */
            case 5: im_og2 += mv; break;                            /* GEN 2 */
            case 6: im_blend += mv; break;                          /* BLEND */
            default: im_lvl *= amt > 0 ? 1 - amt + mv : 1 + mv;     /* LEVEL: bright = loud (or quiet, AMOUNT below 0) */
            }
        }
        v->im_init = 1;
        if (im_lvl < 0) im_lvl = 0;
        if (im_oct != 0 || im_semi != 0) base *= powf(2.0f, im_oct + im_semi / 12.0f);
    }
    v->fx = px; v->fy = py; v->ox = ox; v->oy = oy;
    v->shown_t = ot; v->shown_o = oo;
    float u = (ox + 1) * 0.5f, w_ = (oy + 1) * 0.5f;
    float wt[4] = {(1 - u) * (1 - w_), u * (1 - w_), (1 - u) * w_, u * w_};
    {   /* 0.8: several balls on this one sound: the simulation's balls and the kaleidoscope's copies, their corner weights averaged */
        float ex[18], ey[18];
        int ne = 0, nb = (int)P[S_SOUND_BALLS], kn = (int)P[S_KAL_N];
        if ((int)P[S_SIM_MODE] > 0) for (int k = 1; k < nb && k < s->sim[t].n && ne < 3; k++) { ex[ne] = s->sim[t].sx[k]; ey[ne++] = s->sim[t].sy[k]; }
        if (kn > 1 && (int)P[S_KAL_SOUND]) for (int j = 1; j < kn && ne < 17; j++) {
            float a = TWO_PI * j / kn, ca = cosf(a), sa = sinf(a);
            ex[ne] = clampf(ox * ca - oy * sa, -1, 1); ey[ne++] = clampf(ox * sa + oy * ca, -1, 1);
        }
        if (ne > 0) {
            for (int k = 0; k < ne; k++) {
                float uu = (ex[k] + 1) * 0.5f, ww = (ey[k] + 1) * 0.5f;
                wt[0] += (1 - uu) * (1 - ww); wt[1] += uu * (1 - ww); wt[2] += (1 - uu) * ww; wt[3] += uu * ww;
            }
            for (int c = 0; c < 4; c++) wt[c] /= (float)(ne + 1);
        }
    }
    float w0[4], wd[4];
    for (int c = 0; c < 4; c++) {
        w0[c] = v->w[c] < 0 ? wt[c] : v->w[c];
        wd[c] = (wt[c] - w0[c]) / n;
        v->w[c] = wt[c];
    }
    /* the corners' settings for this block */
    float inc[4][2], og[4][2], blend[4], level[4];
    int mip[4][2], mode[4], fpos[4], on[4];
    filt_t F[4];
    float mres = s->g[G_MRES];
    for (int c = 0; c < 4; c++) {
        const float *C = &P[c * NCF];
        float f1 = base * powf(2.0f, C[CF_OCT]);
        float f2 = f1 * powf(2.0f, (C[CF_TUNE] + C[CF_FINE] / 100.0f) / 12.0f);
        {   /* 0.8: GEN 2 TUNE as a ratio: musical (just intervals) or prime (each prime folded into one octave) */
            static const float MUS[16] = {1, 1.125f, 1.2f, 1.25f, 1.3333334f, 1.5f, 1.6f, 1.6666666f, 1.75f, 2, 2.5f, 3, 3.5f, 4, 5, 8};
            static const int PRM[16] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};
            int rm = (int)P[S_A_RMODE + c * 3], ri = (int)P[S_A_RATIO + c * 3];
            if (ri < 0) ri = 0;
            if (ri > 15) ri = 15;
            if (rm == 1) f2 = f1 * MUS[ri] * powf(2.0f, C[CF_FINE] / 1200.0f);
            else if (rm == 2) {
                float r = (float)PRM[ri];
                while (r >= 2.0f) r *= 0.5f;
                f2 = f1 * r * powf(2.0f, C[CF_FINE] / 1200.0f);
            }
        }
        inc[c][0] = f1 * INV_SR;
        inc[c][1] = f2 * INV_SR;
        mip[c][0] = tab_mip(f1);
        mip[c][1] = tab_mip(f2);
        og[c][0] = clampf(C[CF_OG1] + im_og1, 0, 1);
        og[c][1] = clampf(C[CF_OG2] + im_og2, 0, 1);
        blend[c] = clampf(C[CF_BLEND] + im_blend, 0, 1);
        mode[c] = (int)C[CF_MODE];
        fpos[c] = (int)C[CF_FPOS];
        level[c] = C[CF_LEVEL];
        on[c] = level[c] > 0.0005f && (w0[c] > 0.0005f || wt[c] > 0.0005f);
        /* filter: cutoff (with a little key tracking), resonance plus MASTER RESONANCE */
        float hz = 20.0f * powf(2.0f, C[CF_CUTOFF] * 9.9f + im_cut) * powf(base / 261.6f, 0.3f);
        if (hz > 18000) hz = 18000;
        if (hz < 20) hz = 20;
        float res = clampf(C[CF_RESO] + im_res + mres * (1 - C[CF_RESO]), 0, 1);
        float g = tanf(3.14159265f * hz * INV_SR);
        F[c].type = (int)C[CF_FTYPE];
        F[c].k = 2 - 1.96f * res;
        F[c].a1 = 1 / (1 + g * (g + F[c].k));
        F[c].a2 = g * F[c].a1;
        F[c].a3 = g * F[c].a2;
        F[c].G = g / (1 + g);
        F[c].kl = res * 3.9f;
    }
    float sh_lvl = P[S_SHAPE_OSC], sh_inc = base * INV_SR, sh_lp = clampf(base * 40 * INV_SR * TWO_PI, 0.05f, 1);
    float amp = (1 - P[S_VELOCITY] * (1 - v->vel)) * P[S_T_VOLUME] * 0.6f * (1 - trem) * im_lvl;
    float gl = sqrtf(0.5f * (1 - v->pan)), gr = sqrtf(0.5f * (1 + v->pan));
    static float ebuf[4][128];
    int eng_on[4] = {0, 0, 0, 0};
    for (int c = 0; c < 4; c++) {
        int ei = (int)P[S_A_ENG + c * 5];
        float cfreq = base * powf(2.0f, P[c * NCF + CF_OCT]);
        if ((int)s->g[G_SMP_A + c] > 0 && on[c] && s->smp[c].n[__atomic_load_n(&s->smp[c].cur, __ATOMIC_ACQUIRE)] > 1) {
            eng_on[c] = 1;   /* a sample: through the corner's filter like a voice */
            smp_render(s, v, c, cfreq, ebuf[c], n);
        } else if (ei > 0 && on[c]) {
            eng_on[c] = 1;
            corner_engine(s, v, c, P, ei, base * powf(2.0f, P[c * NCF + CF_OCT]), ebuf[c], n);
        }
    }
    for (int i = 0; i < n; i++) {
        /* envelope */
        switch (v->stage) {
        case 0: v->env += a_inc; if (v->env >= 1) { v->env = 1; v->stage = 1; } break;
        case 1: v->env = sus + (v->env - sus) * d_k; break;
        case 3: v->env *= r_k; break;
        }
        float out = 0;
        for (int c = 0; c < 4; c++) {
            float wc = w0[c] + wd[c] * i;
            if (!on[c]) continue;
            if (eng_on[c]) {   /* a ZYNTH voice: its tone through the corner's filter */
                out += corner_lim(corner_filter(v, c, &F[c], ebuf[c][i]), P[S_A_LIM + c * 3]) * wc * level[c];
                continue;
            }
            float p1 = v->ph[c][0], p2 = v->ph[c][1];
            float o2 = og_read(og[c][1], mip[c][1], p2);
            float o1, x, b = blend[c];
            if (fpos[c] == 2) o2 = corner_filter(v, c, &F[c], o2);
            switch (mode[c]) {
            case 2: case 3: {   /* PM (and AM+PM) */
                float pm = p1 + b * 0.8f * o2;
                pm -= floorf(pm);
                o1 = og_read(og[c][0], mip[c][0], pm);
                if (fpos[c] == 1) o1 = corner_filter(v, c, &F[c], o1);
                x = mode[c] == 3 ? o1 * (1 - 0.5f * b + 0.5f * b * o2) : o1;
                break;
            }
            case 4: {           /* phase distortion of GEN 1 by GEN 2 */
                float d = 0.5f - 0.45f * b * (0.5f + 0.5f * o2);
                float q = p1 < d ? p1 * 0.5f / d : 0.5f + (p1 - d) * 0.5f / (1 - d);
                o1 = og_read(og[c][0], mip[c][0], q);
                if (fpos[c] == 1) o1 = corner_filter(v, c, &F[c], o1);
                x = o1;
                break;
            }
            default:
                o1 = og_read(og[c][0], mip[c][0], p1);
                if (fpos[c] == 1) o1 = corner_filter(v, c, &F[c], o1);
                switch (mode[c]) {
                case 1: x = o1 * (1 - b + b * o2); break;                         /* AM */
                case 5: x = (1 - b) * o1 + b * o1 * o2 * 1.4f; break;             /* RING */
                case 6: { float y = (o1 + b * o2) * (1 + 3 * b); x = sinf(y * 1.5707963f); break; }   /* FOLD */
                default: x = (1 - b) * o1 + b * o2;                               /* MIX */
                }
            }
            if (fpos[c] == 0) x = corner_filter(v, c, &F[c], x);
            out += corner_lim(x, P[S_A_LIM + c * 3]) * wc * level[c];
            p1 += inc[c][0]; if (p1 >= 1) p1 -= 1;
            p2 += inc[c][1]; if (p2 >= 1) p2 -= 1;
            v->ph[c][0] = p1;
            v->ph[c][1] = p2;
        }
        float e = v->env * amp;
        float l = out * e * gl, r = out * e * gr;
        if (sh_lvl > 0.001f) {   /* the shape itself as sound: x left, y right */
            float sx, sy;
            path_at(m->x2, m->y2, v->sh_ph, &sx, &sy);
            v->sh_lp[0] += sh_lp * (sx - v->sh_lp[0]);
            v->sh_lp[1] += sh_lp * (sy - v->sh_lp[1]);
            v->sh_ph += sh_inc;
            if (v->sh_ph >= 1) v->sh_ph -= 1;
            l += v->sh_lp[0] * sh_lvl * e * 0.8f;
            r += v->sh_lp[1] * sh_lvl * e * 0.8f;
        }
        L[i] += l;
        R[i] += r;
    }
    if (v->stage == 3 && v->env < 0.0001f) v->active = 0;
}

/* ------------------------------------------------------------------------------------------------ the screen */
/* The screen shows a ready-drawn picture of the shape when there is one (tools/vader_skin.py; your own 2D shape: the
 * strips src/pictures.c wrote), else the tiles draw it. A picture is a strip per shape and size step, its frames the
 * turns (over the shape's own symmetry) or, for CUBE and ICOSA, the spin. Returns the selector (0: none) and, through
 * the pointers, the frame and its parameter, and the path as the picture draws it (x, y on the square) for the dots. */
static int shape_picture(vader_t *s, int t, int *frame, int *fparam, float *px, float *py) {
    const float *P = T(t);
    timbre_t *m = &s->tm[t];
    int idx = (int)P[S_SHAPE];
    float size = P[S_ORB_SIZE], rot = m->rot_s;
    rot -= floorf(rot);
    *frame = 0;
    *fparam = -1;
    if (idx < PIC_SHAPES && !m->base.is3d) {
        int k = (int)lroundf(size * PIC_SIZES);
        if (k < 1) k = 1;
        if (k > PIC_SIZES) k = PIC_SIZES;
        int sym = PIC_SYM[idx], rots = PIC_ROT[idx];
        float ang = rot;   /* a round shape: one frame; its path point still turns */
        if (sym > 0) {
            float stepw = 1.0f / sym / rots;
            long q = lroundf(rot / stepw);
            ang = q * stepw;
            *frame = (int)(q % rots);
            *fparam = rots == 6 ? 0 : rots == 8 ? 1 : 2;
        }
        path_project(&m->base, ang * TWO_PI, 0, 0, px, py);
        for (int i = 0; i < PATHN; i++) { px[i] *= (float)k / PIC_SIZES; py[i] *= (float)k / PIC_SIZES; }
        return SEL_2D + idx * PIC_SIZES + (k - 1);
    }
    if ((idx == 13 || idx == 14) && (rot < 0.01f || rot > 0.99f)) {   /* CUBE, ICOSA (not turned with ROTATE) */
        int j = idx - 13, k = (int)lroundf(size * P3_SIZES), ti = (int)lroundf(P[S_TILT] * (P3_TILT - 1));
        if (k < 1) k = 1;
        if (k > P3_SIZES) k = P3_SIZES;
        float yt = m->yaw / TWO_PI, stepw = 1.0f / P3_SYM[j] / P3_YAW;
        yt -= floorf(yt);
        long q = lroundf(yt / stepw);
        *frame = (int)(q % P3_YAW);
        *fparam = 2;
        path_project(&m->base, 0, q * stepw * TWO_PI, (float)ti / (P3_TILT - 1) * 1.5707963f, px, py);
        for (int i = 0; i < PATHN; i++) { px[i] *= (float)k / P3_SIZES; py[i] *= (float)k / P3_SIZES; }
        return SEL_3D + (j * P3_SIZES + (k - 1)) * P3_TILT + ti;
    }
    if (idx == SHAPE_USER && s->has_user && s->upics && !m->base.is3d) {
        int k = (int)lroundf(size * U_SIZES);
        if (k < 1) k = 1;
        if (k > U_SIZES) k = U_SIZES;
        long q = lroundf(rot * U_ROT);
        *frame = (int)(q % U_ROT);
        *fparam = 1;
        path_project(&m->base, q * TWO_PI / U_ROT, 0, 0, px, py);
        for (int i = 0; i < PATHN; i++) { px[i] *= (float)k / U_SIZES; py[i] *= (float)k / U_SIZES; }
        return SEL_USER + (k - 1);
    }
    return 0;
}

static void screen_update(vader_t *s, int force_shape) {
    if (!s->screen_on) return;   /* 0.6.0: the MAIN page has no tile screen any more (the companion screen draws) */
    int t = edit(s);
    const float *P = T(t);
    timbre_t *m = &s->tm[t];
    int motion = (int)s->g[G_MOTION];
    /* where to show: the newest sounding voice of this timbre, else the free orbit */
    voice_t *best = NULL;
    for (int i = 0; i < MAXV; i++)
        if (s->v[i].active && s->v[i].timbre == t && (!best || s->v[i].age > best->age)) best = &s->v[i];
    float ot = dir_t(P, best ? best->shown_t : m->orb_t), oo = best ? best->shown_o : m->orb_o;
    float sr_ = m->sub_s * 0.6f, size = P[S_ORB_SIZE];
    static float qx[PATHN], qy[PATHN];
    int frame = 0, fparam = -1, pic = shape_picture(s, t, &frame, &fparam, qx, qy);
    float fx, fy;
    if (pic) path_at(qx, qy, ot, &fx, &fy);   /* on the picture's path */
    else {
        path_at(m->x2, m->y2, ot, &fx, &fy);
        fx *= size;
        fy *= size;
    }
    float sc_, ss_;
    sub_pos(P, oo, &sc_, &ss_);
    float ox = clampf(fx + sr_ * sc_, -1, 1), oy = clampf(fy + sr_ * ss_, -1, 1);
    if (overlay_live(&s->ovl)) {   /* 0.5.0: the live screen draws it all: the tiles, balls and picture rest (empty) */
        if (!s->rested) {
            s->rested = 1;
            memset(&s->scr.tiles, 0, sizeof s->scr.tiles);
            memset(&s->scr.trail, 0, sizeof s->scr.trail);
            memset(&s->scr.orbit, 0, sizeof s->scr.orbit);
            memset(&s->scr.bo, 0, sizeof s->scr.bo);
            memset(&s->scr.bp, 0, sizeof s->scr.bp);
            s->scr.tr_n = 0;
            for (int i = 0; i < TILE_COLS * TILE_ROWS; i++) {
                if (s->sent_tiles[i]) { s->sent_tiles[i] = 0; mark(s, P_T000 + i); }
                if (s->sent_trail[i]) { s->sent_trail[i] = 0; mark(s, P_R000 + i); }
                if (s->sent_orbit[i]) { s->sent_orbit[i] = 0; mark(s, P_L000 + i); }
            }
            for (int i = 0; i < BALL_N * BALL_N; i++) {
                if (s->sent_bo[i]) { s->sent_bo[i] = 0; mark(s, P_BO00 + i); }
                if (s->sent_bp[i]) { s->sent_bp[i] = 0; mark(s, P_BP00 + i); }
            }
            if (s->sent_pic) { s->sent_pic = 0; mark(s, P_SHAPE_PIC); }
        }
        goto bars;
    } else if (s->rested) { s->rested = 0; force_shape = 1; s->line_key = ~0u; }
    if (pic != s->sent_pic) { s->sent_pic = pic; mark(s, P_SHAPE_PIC); force_shape = 1; }
    if (fparam >= 0 && frame != s->sent_r[fparam]) { s->sent_r[fparam] = frame; mark(s, P_PIC_R6 + fparam); }
    int redraw = 0;
    if (force_shape || s->shape_wait <= 0) {
        screen_shape(&s->scr, m->x2, m->y2, size, !pic);
        s->shape_wait = m->base.is3d && P[S_SPIN] > 0.01f && !pic ? 10 : 1 << 30;   /* a turning 3D shape the tiles draw: 2.5 times a second */
        redraw = 1;
    } else s->shape_wait--;
    /* the orbit line: the orbiter's whole path over one orbit (with its suborbit loops), from the orbit and suborbit
     * speeds. 0.4.5: worked out only when what it depends on changes (the shape, its picture frame, size, turn, the
     * suborbit's size, the speed ratio, motion) and kept still otherwise, so the orbiter travelling it costs no screen
     * traffic at all (0.4.4 redrew it as its start phase drifted: most of the crackle with MOTION on). */
    static float lx[LINE_PTS], ly[LINE_PTS];
    float sub = m->sub_now, hz = m->hz_now;
    float ratio = 0;
    int line_on = motion && fabsf(hz) > 0.05f && sr_ > 0.004f;
    if (line_on) {
        /* suborbit turns per orbit, rounded to a whole number so the line closes on itself (no loose ends), at most
         * 12 loops so it doesn't fill the screen */
        ratio = roundf(sub / hz);
        if (ratio > 12) ratio = 12;
        if (ratio < -12) ratio = -12;
    }
    unsigned key = line_on ? 1u + (unsigned)(pic * 977) + (unsigned)(frame * 131) + (unsigned)(lroundf(size * 64) * 7)
                             + (unsigned)(lroundf((m->rot_s - floorf(m->rot_s)) * 64) * 1009) + (unsigned)(lroundf(sr_ * 128) * 3001)
                             + (unsigned)((int)ratio + 16) * 65537u + (unsigned)(int)P[S_SHAPE] * 7919u
                           : 0;   /* a spinning 3D shape the tiles draw redraws the line with the shape (redraw) */
    if (key != s->line_key || redraw) {
        s->line_key = key;
        int ln = 0;
        if (line_on) {
            float b0 = oo - ratio * ot;   /* the suborbit phase where the orbit starts, as it is now */
            ln = (int)(96 + fabsf(ratio) * 40);
            if (ln > LINE_PTS) ln = LINE_PTS;
            for (int k = 0; k < ln; k++) {
                float a = (float)k / (ln - 1), b = b0 + ratio * a, px_, py_;
                a -= floorf(a);
                b -= floorf(b);
                if (pic) path_at(qx, qy, a, &px_, &py_);
                else { path_at(m->x2, m->y2, a, &px_, &py_); px_ *= size; py_ *= size; }
                lx[k] = clampf(px_ + sr_ * cosf(TWO_PI * b), -1, 1);
                ly[k] = clampf(py_ + sr_ * sinf(TWO_PI * b), -1, 1);
            }
        }
        screen_line(&s->scr, lx, ly, ln);
        screen_merge(&s->scr);
    }
    /* the light trail: the last tiles the orbiter crossed */
    screen_trail_step(&s->scr, scr_x(ox), scr_y(oy), motion);
    for (int i = 0; i < TILE_COLS * TILE_ROWS; i++) {
        if (s->scr.tiles[i] != s->sent_tiles[i]) { s->sent_tiles[i] = s->scr.tiles[i]; mark(s, P_T000 + i); }
        if (s->scr.trail[i] != s->sent_trail[i]) { s->sent_trail[i] = s->scr.trail[i]; mark(s, P_R000 + i); if (!s->scr.trail[i]) heal_later(s, P_R000 + i); }
        if (s->scr.orbit[i] != s->sent_orbit[i]) { s->sent_orbit[i] = s->scr.orbit[i]; mark(s, P_L000 + i); }
    }
    /* the balls: the orbiter passes behind the path point on the upper half of its suborbit */
    screen_balls(&s->scr, fx, fy, ox, oy, P[S_ORB_SIZE], P[S_SUB_SIZE], sinf(TWO_PI * oo) < 0, motion);
    for (int i = 0; i < BALL_N * BALL_N; i++) {
        if (s->scr.bo[i] != s->sent_bo[i]) { s->sent_bo[i] = s->scr.bo[i]; mark(s, P_BO00 + i); if (!s->scr.bo[i]) heal_later(s, P_BO00 + i); }
        if (s->scr.bp[i] != s->sent_bp[i]) { s->sent_bp[i] = s->scr.bp[i]; mark(s, P_BP00 + i); if (!s->scr.bp[i]) heal_later(s, P_BP00 + i); }
    }
    if (s->scr.ball_o != s->sent_ball_o) { s->sent_ball_o = s->scr.ball_o; mark(s, P_BALL_O); }
    if (s->scr.ball_p != s->sent_ball_p) { s->sent_ball_p = s->scr.ball_p; mark(s, P_BALL_P); }
bars:
    if (motion && --s->bar_wait <= 0) {   /* 0.4.5: about 8 times a second, in steps of 5, so they cost little */
        s->bar_wait = 3;
        /* the corner bars: what the sound really hears */
        float sx = best ? best->ox : ox, sy = best ? best->oy : oy;
        float u = (sx + 1) * 0.5f, w = (sy + 1) * 0.5f;
        float wt[4] = {(1 - u) * (1 - w), u * (1 - w), (1 - u) * w, u * w};
        for (int c = 0; c < 4; c++) {
            unsigned char qv = (unsigned char)(5 * lroundf(wt[c] * 20));
            if (qv != s->sent_w[c]) { s->sent_w[c] = qv; mark(s, P_WA + c); }
        }
        /* the IMAGE page's meters: each MOD's source as the newest voice reads it */
        for (int k = 0; k < 4; k++) {
            float v = best ? best->im_src[k] : 0;
            int si = (int)P[S_IM1_SRC + k * 4];
            if (si == 3 || si == 5) v = 0.5f + 0.5f * v;   /* DEPTH, HEADING: -1..1, 50 in the middle */
            unsigned char qv = (unsigned char)(5 * lroundf(clampf(v, 0, 1) * 20));
            if (qv != s->sent_im[k]) { s->sent_im[k] = qv; mark(s, P_IM_M1 + k); }
        }
    }
}

static void env_update(vader_t *s) {
    const float *P = T(edit(s));
    screen_env(&s->scr, P[S_ATTACK], P[S_DECAY], P[S_SUSTAIN], P[S_RELEASE]);
    for (int c = 0; c < ENV_COLS; c++)
        if (s->scr.env[c] != s->sent_env[c]) { s->sent_env[c] = s->scr.env[c]; mark(s, P_E00 + c); }
}

/* ------------------------------------------------------------------------------------------------ the readout */
static int user_saved(vader_t *s, int n);
static void value_text(vader_t *s, int p, float v, char *buf, int len) {
    const vp_t *q = &VP[p];
    if (p == P_SHAPE && (int)v == SHAPE_USER && s->has_user) {
        snprintf(buf, (size_t)len, "USER: %s", s->user.name);
        return;
    }
    if (q->nopts) { int i = (int)v; snprintf(buf, (size_t)len, "%s", q->opts[i < 0 ? 0 : i >= q->nopts ? q->nopts - 1 : i]); return; }
    if (p == P_FILE) {
        const shapefile_t *f = file_at(s);
        snprintf(buf, (size_t)len, "%s", s->status[0] ? s->status : f ? f->label : "NO SHAPE FILES FOUND");
        return;
    }
    if (p >= P_SMP_A && p <= P_SMP_D) {
        int i = (int)lroundf(v) - 1;
        snprintf(buf, (size_t)len, "%s", i < 0 ? "OFF" : i < s->nwavs ? s->wavs[i].label : "NOT FOUND");
        return;
    }
    if (p == P_USER_SLOT) {
        int n = (int)lroundf(v);
        snprintf(buf, (size_t)len, "USER %03d   %s", n, s->ustatus[0] ? s->ustatus : user_saved(s, n) ? "saved" : "empty");
        return;
    }
    if (p == P_PRESET) { int i = (int)v; snprintf(buf, (size_t)len, "%s", PRESETS[i < 0 ? 0 : i >= NPRESET ? NPRESET - 1 : i].name); return; }
    if (q->isint) { snprintf(buf, (size_t)len, "%s%d", (q->min < 0 && v > 0) ? "+" : "", (int)lroundf(v)); return; }
    snprintf(buf, (size_t)len, "%s%d", (q->min < 0 && v > 0.004f) ? "+" : "", (int)lroundf(v * 100));
}

static void set_readout(vader_t *s, int p, float v) {
    char val[64], name[48];
    int k = VP[p].kind;
    if (k == K_DISP || k == K_STR || k == K_POPUP) return;
    value_text(s, p, v, val, sizeof val);
    if (k == K_FRONT) {
        int c = (int)s->g[G_CORNER];
        snprintf(name, sizeof name, "%s %s", VP[p].name, c > 3 ? "ALL" : (const char *[]){"A", "B", "C", "D"}[c]);
    } else if (k == K_SND && VP[p].slot >= S_MK1 && VP[p].slot <= S_MK8) {
        char nm[32];
        mk_name(s, edit(s), VP[p].slot - S_MK1, nm, sizeof nm);
        snprintf(name, sizeof name, "%s", nm[0] ? nm : VP[p].name);
    } else snprintf(name, sizeof name, "%s", VP[p].name);
    int timbre = k == K_SND || k == K_FRONT;
    snprintf(s->readout, sizeof s->readout, "%s%s   %s", name, timbre ? (edit(s) ? "  [B]" : "  [A]") : "", val);
    float nv = k == K_TRIG ? 1.0f : k == K_FRONT ? v : norm_of(p, v);
    unsigned char q = (unsigned char)lroundf(clampf(nv, 0, 1) * 100);
    if (q != s->sent_rd) { s->sent_rd = q; mark(s, P_RD_LINE); }
}

/* ------------------------------------------------------------------------------------------------ the engine API */
static void *create(const char *data_dir) {
    tables_init();
    eng_tables_init();
    pthread_once(&g_hash_once, hash_build);
    vader_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->fx = fx_new();
    if (!s->fx) { free(s); return NULL; }
    for (int c = 0; c < 4; c++) pthread_mutex_init(&s->smp[c].lk, NULL);
    if (data_dir) {
        snprintf(s->dir, sizeof s->dir, "%s", data_dir);
        /* the presets go next to the Shapes folder, in the plugin's own folder */
        char base[512];
        snprintf(base, sizeof base, "%s", data_dir);
        size_t bl = strlen(base);
        if (bl > 7 && !strcmp(base + bl - 7, "/Shapes")) base[bl - 7] = 0;
        else if (!strcmp(base, "Shapes") || !strcmp(base, "shapes")) snprintf(base, sizeof base, ".");
        snprintf(s->pdir, sizeof s->pdir, "%s/User Presets", base);
        snprintf(s->skin_dir, sizeof s->skin_dir, "%s/Plugin Skins", base);
        if (access(s->skin_dir, W_OK) != 0) s->skin_dir[0] = 0;   /* no skin folder here (tests): don't write */
        if (s->skin_dir[0]) snprintf(s->base_dir, sizeof s->base_dir, "%s", base);
    }
    memcpy(s->g, GLOB_DEF, sizeof s->g);
    mv_defaults(s);
    memcpy(T(0), SND_DEF, sizeof(float) * NSND);
    memcpy(T(1), SND_DEF, sizeof(float) * NSND);
    s->tempo = 120;
    s->rng = 0x1234567u;
    for (int t = 0; t < 2; t++) timbre_shape(s, t);
    for (int t = 0; t < 2; t++) {
        s->tm[t].size_s = T(t)[S_ORB_SIZE];
        s->tm[t].sub_s = T(t)[S_SUB_SIZE];
        s->tm[t].rot_s = T(t)[S_ROTATE];
        project(s, t, 0);
    }
    /* 0.4.5: MPC starts every screen piece at 0 (empty), so only the pieces that show something need reporting (a
     * burst of 1500 reports at the start crackled) */
    screen_forget(s);
    s->sent_pic = -1;
    for (int k = 0; k < 3; k++) s->sent_r[k] = -1;
    memset(s->sent_im, 0xff, sizeof s->sent_im);
    memset(s->sent_env, 0xff, sizeof s->sent_env);
    memset(s->sent_w, 0xff, sizeof s->sent_w);
    s->sent_rd = 0xff;
    snprintf(s->readout, sizeof s->readout, "VADER");
    rescan(s);
    screen_update(s, 1);
    env_update(s);
    overlay_start(&s->ovl, s->base_dir);   /* 0.5.0: the live screen: only with overlay.on in the plugin folder */
    if (s->base_dir[0] || getenv("VADER_WEB")) web_start(&s->web, &s->ovl, s->dir);   /* 0.6.0: the companion screen */
    snprintf(s->web_text, sizeof s->web_text, "%s", s->web.started ? "STARTING" : "OFF");
    return s;
}

static void destroy(void *p) {
    vader_t *s = p;
    if (!s) return;
    web_stop(&s->web);
    overlay_stop(&s->ovl);
    if (s->loader_joinable) pthread_join(s->loader, NULL);
    for (int c = 0; c < 4; c++) {
        pthread_mutex_lock(&s->smp[c].lk); s->smp[c].want[0] = 0; strcpy(s->smp[c].path, "\x01"); pthread_mutex_unlock(&s->smp[c].lk);
        if (s->smp[c].joinable) pthread_join(s->smp[c].th, NULL);
        free(s->smp[c].d[0]); free(s->smp[c].d[1]);
    }
    for (int i = 0; i < s->nwavs; i++) { free(s->wavs[i].path); free(s->wavs[i].label); }
    for (int i = 0; i < s->nfiles; i++) { free(s->files[i].path); free(s->files[i].label); }
    fx_free(s->fx);
    for (int i = 0; i < MAXV; i++)
        for (int c = 0; c < 4; c++) if (s->v[i].eng[c].mi) mi_free(s->v[i].eng[c].mi);
    free(s);
}

static void midi(void *p, const uint8_t *m, int len) {
    vader_t *s = p;
    if (len < 1) return;
    int st = m[0] & 0xF0;
    int d1 = len > 1 ? m[1] & 127 : 0, d2 = len > 2 ? m[2] & 127 : 0;
    switch (st) {
    case 0x90:
        if (d2) { key_on(s, d1, d2 / 127.0f); break; }
        /* fall through: velocity 0 is a note off */
    case 0x80: key_off(s, d1); break;
    case 0xD0: s->aftertouch = d1 / 127.0f; break;   /* channel pressure */
    case 0xA0: s->aftertouch = d2 / 127.0f; break;   /* polyphonic pressure (MPE pressure comes the same way) */
    case 0xE0: s->bend = (((d2 << 7) | d1) - 8192) / 8192.0f; break;
    case 0xB0:
        if (d1 == 1) s->modwheel = d2 / 127.0f;
        else if (d1 == 20 || d1 == 21) {   /* CC 20 / 21: the centre point of the orbit (both timbres) */
            for (int t = 0; t < 2; t++) {
                int sl = d1 == 20 ? S_CENTRE_X : S_CENTRE_Y;
                T(t)[sl] = d2 / 127.0f;
                if (t == edit(s)) mark(s, SND_P[sl]);
            }
        }
        else if (d1 == 64) {
            s->pedal = d2 >= 64;
            if (!s->pedal)
                for (int i = 0; i < MAXV; i++)
                    if (s->v[i].held_by_pedal) { s->v[i].held_by_pedal = 0; s->v[i].released = 1; s->v[i].stage = 3; }
        } else if (d1 == 120 || d1 == 123) {
            s->nheld = 0;
            arp_release(s);
            all_off(s, d1 == 120);
        }
        break;
    }
}

static void tap(vader_t *s) {
    double now = s->clock_s;
    double d = now - s->tap_last;
    s->tap_last = now;
    if (d < 0.05 || d > 2.0) return;
    if ((int)s->g[G_DLY_SYNC]) {   /* synced: the division nearest the tapped time */
        float beats = (float)d * tempo_of(s) / 60.0f, best = 1e9f;
        int bi = (int)s->g[G_DLY_DIV];
        for (int i = 0; i < 7; i++)
            if (fabsf(div_beats(i) - beats) < best) { best = fabsf(div_beats(i) - beats); bi = i; }
        s->g[G_DLY_DIV] = (float)bi;
        mark(s, P_DLY_DIV);
    } else {
        s->g[G_DLY_TIME] = clampf(sqrtf(((float)d - 0.01f) / 1.49f), 0, 1);
        mark(s, P_DLY_TIME);
    }
}

/* a saved state (MPC's project chunk, or one of your preset files): every value of both timbres and the instance */
static void restore_state(vader_t *s, const char *val) {   /* restore (the wrapper passes the chunk text) */
        memcpy(s->g, GLOB_DEF, sizeof s->g);
        memcpy(T(0), SND_DEF, sizeof(float) * NSND);
        memcpy(T(1), SND_DEF, sizeof(float) * NSND);
        char line[1100];
        const char *q = val;
        char user[1024] = "";
        mv_defaults(s);
        while (*q) {
            int n = 0;
            while (q[n] && q[n] != '\n' && n < (int)sizeof line - 1) { line[n] = q[n]; n++; }
            line[n] = 0;
            q += n;
            if (*q == '\n') q++;
            char k[96];
            float v;
            if (!strncmp(line, "user ", 5)) { snprintf(user, sizeof user, "%s", line + 5); continue; }
            if (line[0] == 'v') {
                int vt, vm, vl, vi;
                float vv;
                if (sscanf(line + 1, "%d %d %d %d %f", &vt, &vm, &vl, &vi, &vv) == 5 && vt >= 0 && vt < 2 && vm >= 0 &&
                    vm < NMODES && vl >= 0 && vl < 3 && vi >= 0 && vi < 8)
                    s->mv[vt][vm][vl][vi] = vv;
                continue;
            }
            if (sscanf(line + 2, "%95s %f", k, &v) != 2) continue;
            int i = find(k);
            if (i < 0) continue;
            if (line[0] == 'g' && VP[i].kind == K_GLOB) s->g[VP[i].slot] = v;
            else if ((line[0] == 'a' || line[0] == 'b') && VP[i].kind == K_SND) s->snd[line[0] - 'a'][VP[i].slot] = v;
        }
        s->has_user = 0;
        s->load_select = 0;
        if (user[0]) start_load(s, user);   /* the shape file again, by name */
        for (int t = 0; t < 2; t++) { timbre_shape(s, t); mode_load(s, t, 1); }
        release_all(s);
        all_off(s, 1);
        s->ndrone = 0;
        s->self_next = -1;
        drone_apply(s);
        all_dirty(s);
        smp_sync_all(s);
        screen_forget(s);
        }

static int state_text(vader_t *s, char *buf, int len);

static void user_path(vader_t *s, int n, char *out, int len) { snprintf(out, (size_t)len, "%s/USER %03d.vader", s->pdir, n); }

static int user_saved(vader_t *s, int n) {
    if (n < 1 || n > 999 || !s->pdir[0]) return 0;
    if (!s->uknown[n]) {
        char path[700];
        struct stat st;
        user_path(s, n, path, sizeof path);
        s->uknown[n] = stat(path, &st) == 0 ? 2 : 1;
    }
    return s->uknown[n] == 2;
}

static void save_user(vader_t *s) {
    int n = (int)s->g[G_USER_SLOT];
    char path[700];
    if (!s->pdir[0]) { snprintf(s->ustatus, sizeof s->ustatus, "NO FOLDER"); return; }
    mkdir(s->pdir, 0755);
    user_path(s, n, path, sizeof path);
    int cap = 64 * 1024;
    char *buf = malloc((size_t)cap);
    if (!buf) return;
    int len = state_text(s, buf, cap) - 1;
    FILE *f = fopen(path, "wb");
    if (f && len > 0 && fwrite(buf, 1, (size_t)len, f) == (size_t)len) {
        snprintf(s->ustatus, sizeof s->ustatus, "SAVED");
        s->uknown[n] = 2;
    } else snprintf(s->ustatus, sizeof s->ustatus, "COULDN'T SAVE");
    if (f) fclose(f);
    free(buf);
}

static void load_user(vader_t *s) {
    int n = (int)s->g[G_USER_SLOT];
    char path[700];
    user_path(s, n, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(s->ustatus, sizeof s->ustatus, "EMPTY SLOT"); return; }
    char *buf = malloc(256 * 1024);
    size_t got = buf ? fread(buf, 1, 256 * 1024 - 1, f) : 0;
    fclose(f);
    if (!buf) return;
    buf[got] = 0;
    float keep_slot = s->g[G_USER_SLOT], keep_file = s->g[G_FILE], keep_qr = s->g[G_QR_SHOW];
    restore_state(s, buf);
    s->g[G_USER_SLOT] = keep_slot;
    s->g[G_FILE] = keep_file;
    s->g[G_QR_SHOW] = keep_qr;
    free(buf);
    snprintf(s->ustatus, sizeof s->ustatus, "LOADED");
}

static void set_param(void *p, const char *key, const char *val) {
    vader_t *s = p;
    collect_load(s);
    if (!strcmp(key, "tile_screen")) { s->screen_on = atoi(val); s->shape_wait = 0; s->line_key = ~0u; return; }   /* the tests: the old tile screen */
    if (!strcmp(key, "host_time")) {
        int pl = 0;
        double ppq = 0, tempo = 0;
        if (sscanf(val, "%d %lf %lf", &pl, &ppq, &tempo) == 3) {
            if (pl != s->playing) { s->arp_next = -1; if (pl) { s->arp_count = 0; s->arp_idx = 0; } }
            s->playing = pl;
            s->ppq = ppq;
            if (tempo > 0) s->tempo = (float)tempo;
        }
        return;
    }
    if (!strcmp(key, "state")) {   /* restore (the wrapper passes the chunk text) */
        restore_state(s, val);
        return;
    }
    int i = find(key);
    if (i < 0) return;
    const vp_t *q = &VP[i];
    float v = strtof(val, NULL);
    if (q->nopts) {
        int k = (int)lroundf(v);
        v = (float)(k < 0 ? 0 : k >= q->nopts ? q->nopts - 1 : k);
    } else {
        v = clampf(v, q->min, q->max);
        if (q->isint) v = (float)lroundf(v);
    }
    int t = edit(s);
    switch (q->kind) {
    case K_FRONT: {
        int c = (int)s->g[G_CORNER];
        for (int k = 0; k < 4; k++)
            if (c > 3 || c == k) { T(t)[k * NCF + q->slot] = v; mark(s, SND_P[k * NCF + q->slot]); }
        break;
    }
    case K_SND:
        T(t)[q->slot] = v;
        if (q->slot == S_SHAPE || (q->slot >= S_SH_A && q->slot <= S_SH_D)) timbre_shape(s, t);
        if (q->slot < 4 * NCF) mark_corner_view(s);
        if (q->slot >= S_MK1 && q->slot <= S_MK8)
            s->mv[t][(int)T(t)[S_SIM_MODE]][(int)T(t)[S_SIM_LAYER]][q->slot - S_MK1] = v;
        if (q->slot >= S_A_ENG && q->slot <= S_D_V4 && (q->slot - S_A_ENG) % 5 == 0) {   /* a corner's ENGINE: its characters start at the voice's own */
            int c = (q->slot - S_A_ENG) / 5, ei = (int)v;
            for (int k = 0; k < 4; k++) {
                T(t)[S_A_V1 + c * 5 + k] = ei > 0 ? ENG_DEFS[ENG_ZID[ei - 1]].def[ENG_SL[ei - 1][k]] : 0.5f;
                mark(s, SND_P[S_A_V1 + c * 5 + k]);
            }
            for (int k = 0; k < 4; k++) mark(s, P_CN1 + c * 4 + k);
        }
        if (q->slot == S_SIM_MODE) { T(t)[S_SIM_LAYER] = 0; mark(s, P_SIM_LAYER); mode_load(s, t, 1); }
        if (q->slot == S_SIM_LAYER) mode_load(s, t, 0);
        if (q->slot == S_ORB_SIZE || q->slot == S_ROTATE || q->slot == S_TILT) s->shape_wait = 0;
        if (q->slot >= S_ATTACK && q->slot <= S_RELEASE) env_update(s);
        break;
    case K_GLOB:
        if (q->slot == G_PRESET) {
            if ((int)v != s->preset) load_preset(s, (int)v);
            break;
        }
        s->g[q->slot] = v;
        if (q->slot == G_TIMBRE) { mark_sound(s); s->shape_wait = 0; env_update(s); }
        if (q->slot == G_CORNER) mark_corner_view(s);
        if (q->slot == G_VOICES) all_off(s, 1);
        if (q->slot == G_FILE) s->status[0] = 0;
        if (q->slot >= G_SMP_A && q->slot <= G_SMP_D) smp_request(s, q->slot - G_SMP_A);
        if (q->slot == G_USER_SLOT) s->ustatus[0] = 0;
        if (q->slot == G_ARP_ON && !(int)v) { s->nheld = 0; arp_release(s); }
        if (q->slot == G_MOTION) s->shape_wait = 0;
        if (q->slot == G_SELF_PLAY) { s->self_next = -1; if (!(int)v) arp_release(s); }
        if (q->slot == G_DRONE || q->slot == G_DRONE_CHORD || (q->slot == G_SELF_ROOT && (int)s->g[G_DRONE])) drone_apply(s);
        break;
    case K_TRIG:
        if (v > 0.5f) {
            if (i == P_TAP) tap(s);
            else if (i == P_LOAD) {
                const shapefile_t *f = file_at(s);
                s->load_select = 1;
                if (f) start_load(s, f->path);
                else snprintf(s->status, sizeof s->status, "NO SHAPE FILES FOUND");
                mark(s, P_FILE);
            }
            else if (i == P_WARP_RESET) {
                for (int t = 0; t < 2; t++) { T(t)[S_WARP_X] = 0.5f; T(t)[S_WARP_Y] = 0.5f; }
                mark(s, SND_P[S_WARP_X]); mark(s, SND_P[S_WARP_Y]);
            }
            else if (i == P_RANDOMIZE) randomize(s);
            else if (i == P_UNDO_RANDOM) undo_random(s);
            else if (i == P_RESCAN) { rescan(s); memset(s->uknown, 0, sizeof s->uknown); }
            else if (i == P_SAVE_USER) { save_user(s); mark(s, P_USER_SLOT); }
            else if (i == P_LOAD_USER) { load_user(s); mark(s, P_USER_SLOT); }
        }
        break;
    default:
        return;
    }
    set_readout(s, i, q->kind == K_FRONT ? front_value(s, q->slot) : v);
}

static int put_num(char *buf, int len, float v, const vp_t *q) {
    if (q->nopts || q->isint) return snprintf(buf, (size_t)len, "%d", (int)lroundf(v));
    return snprintf(buf, (size_t)len, "%.4f", v);
}

static int state_text(vader_t *s, char *buf, int len) {
    int n = snprintf(buf, (size_t)len, "VADER 1\n");
    for (int i = 0; i < NP; i++) {
        const vp_t *q = &VP[i];
        if (q->kind == K_GLOB && n < len)
            n += snprintf(buf + n, (size_t)(len - n), "g %s %.5g\n", q->key, s->g[q->slot]);
        if (q->kind == K_SND)
            for (int t = 0; t < 2 && n < len; t++)
                n += snprintf(buf + n, (size_t)(len - n), "%c %s %.5g\n", 'a' + t, q->key, s->snd[t][q->slot]);
    }
    for (int t = 0; t < 2; t++)
        for (int m = 0; m < NMODES; m++)
            for (int l = 0; l < 3; l++)
                for (int i = 0; i < 8; i++)
                    if (fabsf(s->mv[t][m][l][i] - 0.5f) > 1e-6f && n < len)
                        n += snprintf(buf + n, (size_t)(len - n), "v%d %d %d %d %.5g\n", t, m, l, i, s->mv[t][m][l][i]);
    if (s->has_user && n < len) n += snprintf(buf + n, (size_t)(len - n), "user %s\n", s->user_file);
    return n + 1;   /* with its NUL: the wrapper asks again with a bigger buffer when this is larger */
}

static int get_param(void *p, const char *key, char *buf, int len) {
    vader_t *s = p;
    collect_load(s);
    if (len <= 0) {
        if (!strcmp(key, "state")) return 64 * 1024;   /* the wrapper grows its buffer to this and asks again */
        return 0;
    }
    int i = find(key);
    if (i >= 0) {
        const vp_t *q = &VP[i];
        switch (q->kind) {
        case K_FRONT: return put_num(buf, len, front_value(s, q->slot), q);
        case K_SND: return put_num(buf, len, T(edit(s))[q->slot], q);
        case K_GLOB: return put_num(buf, len, s->g[q->slot], q);
        case K_TRIG: return snprintf(buf, (size_t)len, "0");
        case K_STR:
            if (q->slot >= 10 && q->slot < 26) { char nm[32]; cn_name(s, edit(s), (q->slot - 10) / 4, (q->slot - 10) % 4, nm, sizeof nm); return snprintf(buf, (size_t)len, "%s", nm) + 1; }
            if (q->slot == 26) return snprintf(buf, (size_t)len, "%s", s->cpu_text) + 1;
            if (q->slot >= 2 && q->slot < 10) { char nm[32]; mk_name(s, edit(s), q->slot - 2, nm, sizeof nm); return snprintf(buf, (size_t)len, "%s", nm) + 1; }
            return snprintf(buf, (size_t)len, "%s", i == P_WEB_URL ? s->web_text : s->readout);
        case K_DISP:
            if (i >= P_Q000 && i < P_Q000 + QR_N * QR_BYTES) return snprintf(buf, (size_t)len, "%d", s->sent_qr[i - P_Q000]);
            /* fall through */
            if (i == P_RD_LINE) return snprintf(buf, (size_t)len, "%d", s->sent_rd == 0xff ? 0 : s->sent_rd);
            if (i >= P_WA && i <= P_WD) return snprintf(buf, (size_t)len, "%d", s->sent_w[i - P_WA] == 0xff ? 25 : s->sent_w[i - P_WA]);
            if (i >= P_E00 && i < P_E00 + ENV_COLS) return snprintf(buf, (size_t)len, "%d", s->sent_env[i - P_E00]);
            if (i >= P_R000 && i < P_R000 + TILE_COLS * TILE_ROWS)
                return snprintf(buf, (size_t)len, "%d", s->sent_trail[i - P_R000] == 0xffff ? 0 : s->sent_trail[i - P_R000]);
            if (i >= P_L000 && i < P_L000 + TILE_COLS * TILE_ROWS)
                return snprintf(buf, (size_t)len, "%d", s->sent_orbit[i - P_L000] == 0xffff ? 0 : s->sent_orbit[i - P_L000]);
            if (i >= P_T000 && i < P_T000 + TILE_COLS * TILE_ROWS) return snprintf(buf, (size_t)len, "%d", s->sent_tiles[i - P_T000]);
            if (i >= P_PIC_R6 && i <= P_PIC_R12) return snprintf(buf, (size_t)len, "%d", s->sent_r[i - P_PIC_R6] < 0 ? 0 : s->sent_r[i - P_PIC_R6]);
            if (i >= P_IM_M1 && i <= P_IM_M4) return snprintf(buf, (size_t)len, "%d", s->sent_im[i - P_IM_M1] == 0xff ? 0 : s->sent_im[i - P_IM_M1]);
            if (i == P_SHAPE_PIC) return snprintf(buf, (size_t)len, "%d", s->sent_pic < 0 ? 0 : s->sent_pic);
            if (i >= P_BO00 && i < P_BO00 + BALL_N * BALL_N) return snprintf(buf, (size_t)len, "%d", s->sent_bo[i - P_BO00] == 0xff ? 0 : s->sent_bo[i - P_BO00]);
            if (i >= P_BP00 && i < P_BP00 + BALL_N * BALL_N) return snprintf(buf, (size_t)len, "%d", s->sent_bp[i - P_BP00] == 0xff ? 0 : s->sent_bp[i - P_BP00]);
            if (i == P_BALL_O) return snprintf(buf, (size_t)len, "%d", s->sent_ball_o == 0xff ? 0 : s->sent_ball_o);
            if (i == P_BALL_P) return snprintf(buf, (size_t)len, "%d", s->sent_ball_p == 0xff ? 0 : s->sent_ball_p);
            return snprintf(buf, (size_t)len, "0");
        default: return 0;
        }
    }
    if (!strcmp(key, "dirty")) {
        /* What changed, for the wrapper to tell MPC (one host call each, on the audio thread). Everything but the screen's
         * tiles goes out at once; the tiles at most SCREEN_BUDGET a poll, taking turns, so a big redraw (a new shape, a
         * new size) spreads over a few polls instead of one burst MPC can't keep up with (crackles). */
        collect_load(s);
        int n = 0, budget = SCREEN_BUDGET;
        buf[0] = 0;
        for (int k = 0; k < NP && n < len - 8; k++)
            if (s->dirty[k] && !is_screen(k)) {
                s->dirty[k] = 0;
                n += snprintf(buf + n, (size_t)(len - n), "%d ", k);
            }
        /* the screen, SCREEN_BUDGET pieces a poll (0.4.5: about 250 a second at most; MPC answers each with a redraw
         * on its message thread, and more than that crackled the audio): the balls first (they move every frame), then
         * the light trail's tiles, then the bars, meters and envelope, then the shape's and the orbit line's tiles in
         * turn, so a big redraw (a new shape, a new size) spreads over a few polls */
#define TAKE(k) do { if (budget > 0 && n < len - 8 && s->dirty[k]) { s->dirty[k] = 0; budget--; \
                     n += snprintf(buf + n, (size_t)(len - n), "%d ", (k)); } } while (0)
        for (int k = P_BO00; k < P_BO00 + 2 * BALL_N * BALL_N; k++) TAKE(k);
        TAKE(P_BALL_O);
        TAKE(P_BALL_P);
        /* the trail: first the tiles it left (else, on a fast orbiter, MPC keeps showing them: pieces piled up over the
         * whole screen on GLASS STAR), then its newest pieces, then the older ones' change of brightness, which may lag */
        for (int k = P_R000; k < P_R000 + TILE_COLS * TILE_ROWS; k++) if (!s->sent_trail[k - P_R000]) TAKE(k);
        for (int i = s->scr.tr_n - 1; i >= 0; i--) TAKE(P_R000 + s->scr.tr[i].cell);
        for (int k = P_WA; k <= P_WD; k++) TAKE(k);
        for (int k = P_IM_M1; k <= P_IM_M4; k++) TAKE(k);
        for (int k = P_E00; k < P_E00 + ENV_COLS; k++) TAKE(k);
        for (int j = 0; j < 2 * TILE_COLS * TILE_ROWS && budget > 0; j++) {
            int g = (s->grid_next + j) % (2 * TILE_COLS * TILE_ROWS);
            TAKE(g < TILE_COLS * TILE_ROWS ? P_T000 + g : P_L000 + g - TILE_COLS * TILE_ROWS);
        }
#undef TAKE
        /* then, with budget to spare, the pieces that went empty 3 or more polls ago, once more (see heal_later) */
        s->poll_no++;
        for (int i = 0; i < s->heal_n && budget > 0 && n < len - 8;) {
            if (s->poll_no - s->heal_at[i] < 3) { i++; continue; }
            int p = s->heal_p[i];
            int still = p >= P_R000 && p < P_R000 + TILE_COLS * TILE_ROWS ? !s->sent_trail[p - P_R000]
                      : p >= P_BO00 && p < P_BO00 + BALL_N * BALL_N ? !s->sent_bo[p - P_BO00]
                      : p >= P_BP00 && p < P_BP00 + BALL_N * BALL_N ? !s->sent_bp[p - P_BP00] : 0;
            if (still && !s->dirty[p]) { budget--; n += snprintf(buf + n, (size_t)(len - n), "%d ", p); }
            memmove(s->heal_p + i, s->heal_p + i + 1, (size_t)(s->heal_n - i - 1) * sizeof(int));
            memmove(s->heal_at + i, s->heal_at + i + 1, (size_t)(s->heal_n - i - 1) * sizeof(int));
            s->heal_n--;
        }
        s->grid_next = (s->grid_next + 97) % (2 * TILE_COLS * TILE_ROWS);
        return n > 0 ? n : snprintf(buf, (size_t)len, " ");
    }
    if (!strcmp(key, "state")) {
        int need = state_text(s, buf, len);
        return need;
    }
    size_t kl = strlen(key);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        int j = find_n(key, (int)kl - 8);
        if (j < 0) return 0;
        float v = VP[j].kind == K_SND ? T(edit(s))[VP[j].slot] : VP[j].kind == K_GLOB ? s->g[VP[j].slot]
                : VP[j].kind == K_FRONT ? front_value(s, VP[j].slot) : 0;
        value_text(s, j, v, buf, len);
        return (int)strlen(buf) + 1;
    }
    if (kl > 5 && !strcmp(key + kl - 5, "_name")) {
        int j = find_n(key, (int)kl - 5);
        if (j >= 0 && VP[j].kind == K_SND && VP[j].slot >= S_A_ENG && VP[j].slot <= S_D_V4 && (VP[j].slot - S_A_ENG) % 5) {
            char nm[32];
            int off = VP[j].slot - S_A_ENG;
            cn_name(s, edit(s), off / 5, off % 5 - 1, nm, sizeof nm);
            return snprintf(buf, (size_t)len, "%s", nm[0] ? nm : VP[j].name) + 1;
        }
        if (j >= 0 && VP[j].kind == K_SND && VP[j].slot >= S_MK1 && VP[j].slot <= S_MK8) {
            char nm[32];
            mk_name(s, edit(s), VP[j].slot - S_MK1, nm, sizeof nm);
            return snprintf(buf, (size_t)len, "%s", nm[0] ? nm : VP[j].name) + 1;
        }
        if (j < 0 || VP[j].kind != K_FRONT) return 0;
        int c = (int)s->g[G_CORNER];
        return snprintf(buf, (size_t)len, "%s %s", VP[j].name, c > 3 ? "ALL" : (const char *[]){"A", "B", "C", "D"}[c]) + 1;
    }
    if (!strcmp(key, "arp_count")) return snprintf(buf, (size_t)len, "%d", s->arp_count) + 1;   /* for the tests */
    if (!strncmp(key, "preset:", 7)) {
        int n = atoi(key + 7);
        if (n < 0 || n >= NPRESET) return 0;
        return snprintf(buf, (size_t)len, "%s", PRESETS[n].name) + 1;
    }
    return 0;
}

/* 0.7.0: one block. out (int16, interleaved) for the MPC/Force wrapper; fl/fr (float, -1..1) for the desktop plugin,
 * which has no 16-bit stage. Exactly one of the two is used; the sound is the same. */
static void render_ex(vader_t *s, int16_t *out, float *fl, float *fr, int frames) {
    float L[128], R[128];
    struct timespec ts0;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    collect_load(s);
    memset(L, 0, sizeof(float) * (size_t)frames);
    memset(R, 0, sizeof(float) * (size_t)frames);
    float dt = frames * INV_SR;
    s->clock_s += dt;
    float bps = tempo_of(s) / 60.0f;
    /* ---- the arpeggiator's steps in this block */
    if ((int)s->g[G_ARP_ON] && s->nheld > 0) {
        double now = s->playing ? s->ppq : s->arp_free_beats;
        double step = arp_step_beats(s), end = now + frames * bps * INV_SR;
        if (s->arp_next < 0 || s->arp_next < now - step) s->arp_next = ceil(now / step) * step;   /* onto the grid */
        while (s->arp_next < end) {
            arp_step(s, (float)(step / bps * SR));
            s->arp_next += step;
        }
    }
    if ((int)s->g[G_SELF_PLAY] && !((int)s->g[G_ARP_ON] && s->nheld > 0)) {
        double now = s->playing ? s->ppq : s->arp_free_beats;
        double step = arp_step_beats(s), end = now + frames * bps * INV_SR;
        if (s->self_next < 0 || s->self_next < now - step) s->self_next = ceil(now / step) * step;
        while (s->self_next < end) {
            self_step(s, (float)(step / bps * SR));
            s->self_next += step;
        }
    }
    if (s->narp && s->arp_off_at >= 0) {
        s->arp_off_at -= frames;
        if (s->arp_off_at <= 0) arp_release(s);
    }
    s->arp_free_beats += frames * bps * INV_SR;
    if (s->playing) s->ppq += frames * bps * INV_SR;   /* for the sub-blocks of a sample-accurate render */
    /* ---- each timbre: its orbit, LFOs, path */
    float vib[2], trem[2];
    for (int t = 0; t < 2; t++) {
        timbre_t *m = &s->tm[t];
        const float *P = T(t);
        float hz, sub;
        if ((int)P[S_ORB_SYNC]) {
            float per = div_beats((int)P[S_ORB_DIV]);
            hz = bps / per;
            sub = hz * (1 + floorf(P[S_SUB_SPEED] * 15));
            if (s->playing) m->orb_t = (float)fmod(s->ppq / per, 1.0);
            else m->orb_t = fmodf(m->orb_t + hz * dt, 1.0f);
        } else {
            hz = 0.02f * powf(1000.0f, P[S_ORB_SPEED]);
            sub = 0.05f * powf(600.0f, P[S_SUB_SPEED]) * (1.0f + s->aftertouch * P[S_AT_SUB] * 6.0f);
            /* 0.8: the speed's shape: LFO, curve, a pause near the corners */
            float mul = 1.0f;
            m->lfo_ph = fmodf(m->lfo_ph + 0.05f * powf(100.0f, P[S_SPD_LFO_RATE]) * dt, 1.0f);
            mul += P[S_SPD_LFO_DEPTH] * 0.9f * sinf(TWO_PI * m->lfo_ph);
            int crv = (int)P[S_SPD_CURVE];
            float cx_, cy_;
            path_at(m->x2, m->y2, path_u(P, m->orb_t), &cx_, &cy_);
            if (crv == 1) mul *= 0.35f + 0.65f * fminf(1.0f, sqrtf(cx_ * cx_ + cy_ * cy_));
            else if (crv == 2) mul *= 1.6f - 0.6f * fminf(1.0f, sqrtf(cx_ * cx_ + cy_ * cy_));
            else if (crv == 3) mul *= 0.25f + 1.5f * (0.5f + 0.5f * sinf(TWO_PI * 4 * m->orb_t));
            float cs = P[S_CORNER_STOP];
            if (cs > 0.001f) {
                float dx = 1 - fabsf(cx_), dy = 1 - fabsf(cy_), d = sqrtf(dx * dx + dy * dy);   /* distance to the nearest corner */
                float prox = fmaxf(0.0f, 1.0f - d / 0.5f);
                mul *= 1 - cs * 0.95f * prox;
            }
            m->spd_mul = mul < 0.02f ? 0.02f : mul;
            m->orb_t = fmodf(m->orb_t + hz * dt * m->spd_mul, 1.0f);
        }
        m->orb_o = fmodf(m->orb_o + sub * dt, 1.0f);
        {
            int sm = (int)P[S_SIM_MODE];
            if (sm > 0) {
                if (s->sim[t].mode != sm) sim_reset(&s->sim[t], sm, s->mv[t][sm][0]);
                sim_step(&s->sim[t], s->mv[t][sm][0], dt);
            }
        }
        m->hz_now = hz;
        m->sub_now = sub;
        float vr, tr;
        if ((int)P[S_VT_SYNC]) {
            static const float b[8] = {4, 2, 1, 0.5f, 0.25f, 0.125f, 1.0f / 6, 1.0f / 12};
            vr = bps / b[(int)(P[S_VIB_SPEED] * 7.99f)];
            tr = bps / b[(int)(P[S_TREM_SPEED] * 7.99f)];
        } else {
            vr = 0.1f * powf(120.0f, P[S_VIB_SPEED]);
            tr = 0.1f * powf(120.0f, P[S_TREM_SPEED]);
        }
        m->vib_ph = fmodf(m->vib_ph + vr * dt, 1.0f);
        m->trem_ph = fmodf(m->trem_ph + tr * dt, 1.0f);
        float vd = clampf(P[S_VIB_DEPTH] + s->modwheel * 0.5f, 0, 1);
        vib[t] = sinf(TWO_PI * m->vib_ph) * vd * vd * 1.0f;   /* semitones */
        trem[t] = (0.5f + 0.5f * sinf(TWO_PI * m->trem_ph)) * P[S_TREM_DEPTH];
        /* ORBIT SIZE, SUBORBIT SIZE and ROTATE glide to their new values (about 40 ms): a knob or Q-Link moves them in
         * steps, and a step would jump the orbiter and click */
        {
            float kz = 1 - expf(-dt / 0.04f);
            if (m->size_s < 0) { m->size_s = P[S_ORB_SIZE]; m->sub_s = P[S_SUB_SIZE]; m->rot_s = P[S_ROTATE]; }
            m->size_s += kz * (P[S_ORB_SIZE] - m->size_s);
            m->sub_s += kz * (P[S_SUB_SIZE] - m->sub_s);
            float dr = P[S_ROTATE] - m->rot_s;   /* ROTATE wraps: 0 and 1 are the same turn */
            if (dr > 0.5f) dr -= 1;
            if (dr < -0.5f) dr += 1;
            m->rot_s += kz * dr;
            if (m->rot_s < 0) m->rot_s += 1;
            if (m->rot_s >= 1) m->rot_s -= 1;
        }
        project(s, t, dt);
    }
    /* ---- the voices */
    for (int i = 0; i < MAXV; i++)
        if (s->v[i].active) voice_render(s, &s->v[i], L, R, frames, vib[s->v[i].timbre], trem[s->v[i].timbre]);
    /* ---- effects */
    fx_params_t fp;
    fp.drive = s->g[G_DRIVE];
    fp.drive_col = s->g[G_DRIVE_COL];
    if ((int)s->g[G_CH_SYNC]) {
        static const float b[6] = {8, 4, 2, 1, 0.5f, 0.25f};
        fp.ch_rate_hz = bps / b[(int)(s->g[G_CH_SPEED] * 5.99f)];
    } else fp.ch_rate_hz = 0.05f * powf(100.0f, s->g[G_CH_SPEED]);
    fp.ch_depth = s->g[G_CH_DEPTH];
    fp.ch_col = s->g[G_CH_COL];
    fp.ch_mix = s->g[G_MIX_CH];
    fp.dly_sec = (int)s->g[G_DLY_SYNC] ? div_beats((int)s->g[G_DLY_DIV]) / bps : 0.01f + 1.49f * s->g[G_DLY_TIME] * s->g[G_DLY_TIME];
    if (fp.dly_sec > 1.95f) fp.dly_sec = 1.95f;
    fp.dly_fb = s->g[G_DLY_FB];
    fp.dly_mix = s->g[G_MIX_DLY];
    fp.dly_tape = (int)s->g[G_DLY_MODE] == 0;
    fp.dly_hold = (int)s->g[G_DLY_HOLD];
    fp.dly_add = (int)s->g[G_DLY_ADD];
    fp.dly_rev = (int)s->g[G_DLY_REV];
    fp.rev_size = s->g[G_REV_SIZE];
    fp.rev_col = s->g[G_REV_COL];
    fp.rev_mix = s->g[G_MIX_REV];
    fp.volume = s->g[G_VOLUME];
    fx_process(s->fx, &fp, L, R, frames);
    if (fl) {
        memcpy(fl, L, sizeof(float) * (size_t)frames);
        memcpy(fr, R, sizeof(float) * (size_t)frames);
    } else
    for (int i = 0; i < frames; i++) {
        float l = L[i] * 32767.0f, r = R[i] * 32767.0f;
        out[2 * i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2 * i + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    /* ---- the screen, 25 times a second */
    s->scr_wait -= frames;
    if (s->scr_wait <= 0) {
        s->scr_wait += 1764;   /* 25 times a second (the wrapper reports changes as often) */
        screen_update(s, 0);
        /* the companion screen's address, when it changes: the readout text and the QR code's bytes */
        if (s->web.started) {
            unsigned wv = web_version(&s->web);
            if (wv != s->web_seen) {
                unsigned char qr[QR_N * QR_BYTES];
                char st[96];
                s->web_seen = web_get(&s->web, st, sizeof st, qr);
                snprintf(s->web_text, sizeof s->web_text, "%s", st);
                mark(s, P_WEB_URL);
                for (int i = 0; i < QR_N * QR_BYTES; i++)
                    if (qr[i] != s->sent_qr[i]) { s->sent_qr[i] = qr[i]; mark(s, P_Q000 + i); }
            }
            /* a shape sent from the phone: into the list, and loaded as the edited timbre's */
            char up[1100];
            if (web_take_load(&s->web, up, sizeof up)) {
                rescan(s);
                const char *base = strrchr(up, '/');
                base = base ? base + 1 : up;
                for (int i = 0; i < s->nfiles; i++) {
                    const char *fb = strrchr(s->files[i].path, '/');
                    if (!strcmp(fb ? fb + 1 : s->files[i].path, base)) { s->g[G_FILE] = (float)i; mark(s, P_FILE); break; }
                }
                s->load_select = 1;
                start_load(s, up);
            }
        }
    }
    {   /* the CPU readout: the share of the block's time the render took, a short average */
        struct timespec ts1;
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        s->cpu_acc += (ts1.tv_sec - ts0.tv_sec) + (ts1.tv_nsec - ts0.tv_nsec) * 1e-9;
        s->cpu_blk += frames * INV_SR;
        if (s->cpu_blk > 0.5) {
            int nv = 0;
            for (int i = 0; i < MAXV; i++) nv += s->v[i].active;
            snprintf(s->cpu_text, sizeof s->cpu_text, "CPU %d%%   VOICES %d", (int)(100.0 * s->cpu_acc / s->cpu_blk + 0.5), nv);
            s->cpu_acc = s->cpu_blk = 0;
            mark(s, P_CPU);
        }
    }
    /* ---- what to draw (the companion screen, web.c; the live screen, overlay.c): every block; the shape when it changes */
    if (s->ovl.started || s->web.started) {
        int t = edit(s);
        timbre_t *m = &s->tm[t];
        int key = s->shape_ver * 2 + t;
        if (key != s->pub_key) {
            s->pub_key = key;
            static ovl_shape_t sh;   /* 19 KB: not on the stack */
            memcpy(sh.px, m->base.x, sizeof sh.px);
            memcpy(sh.py, m->base.y, sizeof sh.py);
            memcpy(sh.pz, m->base.z, sizeof sh.pz);
            sh.is3d = m->base.is3d;
            sh.shape_user = (int)T(t)[S_SHAPE] == SHAPE_USER && s->has_user;
            sh.has_img = s->img.ok;
            if (sh.has_img) memcpy(sh.img, s->img.v, sizeof sh.img);
            overlay_publish_shape(&s->ovl, &sh);
        }
        voice_t *best = NULL;
        for (int i = 0; i < MAXV; i++)
            if (s->v[i].active && s->v[i].timbre == t && (!best || s->v[i].age > best->age)) best = &s->v[i];
        ovl_view_t v;
        v.motion = (int)s->g[G_MOTION];
        v.rot = m->rot_s;
        v.yaw = m->yaw;
        v.tilt = T(t)[S_TILT] * 1.5707963f;
        v.size = m->size_s < 0 ? T(t)[S_ORB_SIZE] : m->size_s;
        v.sub_size = m->sub_s < 0 ? T(t)[S_SUB_SIZE] : m->sub_s;
        v.ot = dir_t(T(t), best ? best->shown_t : m->orb_t);
        v.oo = best ? best->shown_o : m->orb_o;
        v.hz = m->hz_now;
        v.sub = m->sub_now;
        v.env = best ? best->env : 0;
        {
            float sx = best ? best->ox : 0, sy = best ? best->oy : 0;
            float u = (sx + 1) * 0.5f, ww = (sy + 1) * 0.5f;
            v.w[0] = (1 - u) * (1 - ww); v.w[1] = u * (1 - ww); v.w[2] = (1 - u) * ww; v.w[3] = u * ww;
        }
        for (int k = 0; k < 4; k++) {   /* 0.7.0: the IMAGE page's meters, as the desktop screen shows them */
            float x = best ? best->im_src[k] : 0;
            int si = (int)T(t)[S_IM1_SRC + k * 4];
            if (si == 3 || si == 5) x = 0.5f + 0.5f * x;
            v.im[k] = clampf(x, 0, 1);
        }
        v.use_pos = best ? 1 : 0;
        v.fx = best ? best->fx : 0; v.fy = best ? best->fy : 0;
        v.ox = best ? best->ox : 0; v.oy = best ? best->oy : 0;
        v.nballs = 0;
        if ((int)T(t)[S_SIM_MODE] > 0) {
            int nb = s->sim[t].n; if (nb > 8) nb = 8;
            for (int k = 1; k < nb; k++) { v.bx[v.nballs] = s->sim[t].sx[k]; v.by[v.nballs] = s->sim[t].sy[k]; v.nballs++; }
        }
        {   /* the kaleidoscope's copies of the orbiter: turned about the centre */
            int kn = (int)T(t)[S_KAL_N];
            for (int j = 1; j < kn && v.nballs < 16; j++) {
                float a = TWO_PI * j / kn, ca = cosf(a), sa = sinf(a);
                v.bx[v.nballs] = clampf(v.ox * ca - v.oy * sa, -1, 1); v.by[v.nballs] = clampf(v.ox * sa + v.oy * ca, -1, 1); v.nballs++;
            }
        }
        overlay_publish(&s->ovl, &v);
    }
}

static void render(void *p, int16_t *out, int frames) { render_ex(p, out, NULL, NULL, frames); }

/* ---- 0.7.0: the desktop plugin's doors (VST3 / VST2 for Windows and Linux, desktop/). Not used by the MPC build. */
void vader_render_f(void *inst, float *l, float *r, int frames) { render_ex(inst, NULL, l, r, frames); }
void *vader_view_bus(void *inst) { return &((vader_t *)inst)->ovl; }   /* the overlay_t the screen's data is published to */

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
