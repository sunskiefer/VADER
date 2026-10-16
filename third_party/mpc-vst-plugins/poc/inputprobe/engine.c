/* Engine for the input probe (poc/inputprobe): silent, keeps each param's value and logs every raw call the host makes
 * into the wrapper (WRAP_TRACE: wrap_trace below), so the log shows exactly what MPC sends for a Q-Link turn, a data wheel
 * click or a touch drag, and what it reads back. One line per setParameter; getParameter only when the value it returns
 * changed. Columns: ms since the plugin loaded, S|G, param key, the host's normalized value (S) or the value read back (G),
 * and the step of that value in the param's own units (option index, whole number, or the 0..1 value). MARK lines come
 * from the MARK button. The log is /tmp/inputprobe.log (set INPUTPROBE_LOG to move it); it stops at 2 MB. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params.h"
#include "../../wrapper/engine.h"

#define LOG_MAX (2L << 20)
static FILE *g_log;
static long g_bytes, g_marks;
static struct timespec g_t0;
static float g_last_get[NPARAMS > 0 ? NPARAMS : 1];

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - g_t0.tv_sec) * 1000L + (t.tv_nsec - g_t0.tv_nsec) / 1000000L;
}

static void open_log(void) {
    if (g_log) return;
    const char *path = getenv("INPUTPROBE_LOG");
    g_log = fopen(path && *path ? path : "/tmp/inputprobe.log", "a");
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    for (int i = 0; i < NPARAMS; i++) g_last_get[i] = -2.0f;
    if (g_log) g_bytes += fprintf(g_log, "# inputprobe start, %d params (key, host value, value in the param's own units)\n", NPARAMS);
}

static void logf_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_line(const char *fmt, ...) {
    if (!g_log || g_bytes > LOG_MAX) return;
    va_list ap;
    va_start(ap, fmt);
    g_bytes += vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* the host's normalized value in the param's own units: option index, whole number, or the value itself */
static double units(int i, float v) {
    const param_t *p = &PARAMS[i];
    if (p->nopts > 1) return v * (p->nopts - 1);
    return p->min + v * (p->max - p->min);
}

void wrap_trace(int kind, int idx, float v) {
    open_log();
    if (idx < 0 || idx >= NPARAMS) return;
    if (kind == 1) {   /* a read back: only when it changed */
        if (fabsf(v - g_last_get[idx]) < 1e-6f) return;
        g_last_get[idx] = v;
    }
    logf_line("%ld %c %s %.6f %.4f\n", now_ms(), kind ? 'G' : 'S', PARAMS[idx].key, v, units(idx, v));
}

typedef struct { char val[NPARAMS > 0 ? NPARAMS : 1][24]; } st_t;

static void *create(const char *d) {
    (void)d;
    st_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    open_log();
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        if (p->nopts) snprintf(s->val[i], sizeof s->val[i], "%d", (int)lroundf(p->def * (p->nopts - 1)));
        else snprintf(s->val[i], sizeof s->val[i], "%g", p->min + p->def * (p->max - p->min));
    }
    return s;
}
static void destroy(void *i) { free(i); }
static void midi(void *i, const uint8_t *m, int n) { (void)i, (void)m, (void)n; }

static void set_param(void *inst, const char *key, const char *val) {
    st_t *s = inst;
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) {
            if (!strcmp(key, "mark")) {
                if (atof(val) > 0.5) logf_line("%ld MARK %ld\n", now_ms(), ++g_marks);
            } else {
                snprintf(s->val[i], sizeof s->val[i], "%s", val);
                logf_line("%ld E %s %s\n", now_ms(), key, val);   /* what the wrapper handed the engine after rounding/stepping */
            }
            return;
        }
}

static int get_param(void *inst, const char *key, char *b, int n) {
    st_t *s = inst;
    for (int i = 0; i < NPARAMS; i++)
        if (!strcmp(PARAMS[i].key, key)) return snprintf(b, n, "%s", s->val[i]);
    return 0;
}

static void render(void *inst, int16_t *out, int frames) { (void)inst; memset(out, 0, sizeof(int16_t) * 2 * frames); }

static const mpc_engine_t ENGINE = {create, destroy, midi, set_param, get_param, render, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
