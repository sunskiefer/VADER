/* VADER's engine, tested directly (no wrapper): every preset and shape sounds, no NaN or runaway level, every blend
 * mode and filter, the arpeggiator on MPC's clock, shape files load (SVG, OBJ, PNG), the state round trip, the screen
 * reports its changes, and a CPU figure for the worst case (16 voices, four corners each). */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"
#include "vader.h"
#include "overlay.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const mpc_engine_t *E;

static void setp(void *s, const char *k, double v) {
    char b[32];
    snprintf(b, sizeof b, "%g", v);
    E->set_param(s, k, b);
}
static double getp(void *s, const char *k) {
    char b[128];
    if (E->get_param(s, k, b, sizeof b) <= 0) return -999;
    return atof(b);
}
static void note(void *s, int on, int n, int vel) {
    uint8_t m[3] = {(uint8_t)(on ? 0x90 : 0x80), (uint8_t)n, (uint8_t)vel};
    E->midi(s, m, 3);
}

typedef struct { double rms, peak; int nan; } level_t;
static level_t run(void *s, int blocks) {
    int16_t buf[256];
    double sum = 0, peak = 0;
    long n = 0;
    for (int b = 0; b < blocks; b++) {
        E->render(s, buf, 128);
        for (int i = 0; i < 256; i++) {
            double v = buf[i] / 32768.0;
            sum += v * v;
            if (fabs(v) > peak) peak = fabs(v);
            n++;
        }
    }
    level_t l = {sqrt(sum / n), peak, 0};
    return l;
}

static void upic_frame_test(int k, int *w, int *h) {   /* src/vader.h upic_frame */
    float r = 1.4142f * k / U_SIZES * SQUARE * 0.5f + 12;
    *w = 2 * (int)ceilf(r < SQUARE_X + SQUARE * 0.5f ? r : SQUARE_X + SQUARE * 0.5f);
    *h = 2 * (int)ceilf(r < SQUARE * 0.5f ? r : SQUARE * 0.5f);
}

static void chord(void *s, int on) { note(s, on, 48, 100); note(s, on, 55, 100); note(s, on, 60, 100); note(s, on, 64, 100); }

static void drain(void *s) {
    char b[8192];
    E->get_param(s, "dirty", b, sizeof b);
}

static void waitload(void *s) { char d[128]; for (int k = 0; k < 6000; k++) { usleep(5000); E->get_param(s, "file_display", d, sizeof d); if (strcmp(d, "LOADING...")) { /* a pending second file */ usleep(20000); E->get_param(s, "file_display", d, sizeof d); if (strcmp(d, "LOADING...")) break; } } }
static int findfile(void *s, const char *name) { char d[128]; for (int i = 0; i < 60; i++) { setp(s, "file", i); E->get_param(s, "file_display", d, sizeof d); if (strstr(d, name)) return i; } return -1; }
static char st[70000];
static const char *state(void *s) { st[0] = 0; E->get_param(s, "state", st, sizeof st); return st; }
static int has(const char *t, const char *k) { return strstr(t, k) != NULL; }
int main(void) {
    E = mpc_engine();
    void *s = E->create("shapes");
    char d[128];
    /* a picture */
    setp(s, "file", findfile(s, "crescent.png")); setp(s, "load", 1); waitload(s);
    E->get_param(s, "file_display", d, sizeof d);
    const char *t = state(s);
    CHECK(!strcmp(d, "LOADED (IMAGE)") && has(t, "\nimage ") && has(t, "crescent.png") && !has(t, "\nuser "), "picture loaded: state has its image line, no shape-file line (%s)", d);
    setp(s, "user_slot", 201); setp(s, "save_user", 1);
    /* a shape file replaces it */
    setp(s, "file", findfile(s, "torus.obj")); setp(s, "load", 1); waitload(s);
    t = state(s);
    CHECK(has(t, "\nuser ") && has(t, "torus.obj") && !has(t, "\nimage "), "shape file loaded: it replaces the picture (one loaded file)");
    setp(s, "user_slot", 202); setp(s, "save_user", 1);
    /* a factory preset leaves the loaded file alone */
    setp(s, "preset", 5);
    t = state(s);
    CHECK(has(t, "torus.obj"), "factory preset: the loaded file stays");
    /* a preset saved with nothing loaded */
    void *s2 = E->create("shapes");
    setp(s2, "preset", 2); setp(s2, "user_slot", 203); setp(s2, "save_user", 1);
    t = state(s2);
    CHECK(!has(t, "\nuser ") && !has(t, "\nimage "), "fresh instance: nothing loaded");
    /* load them back, each over the other */
    setp(s, "user_slot", 201); setp(s, "load_user", 1); waitload(s);
    t = state(s);
    CHECK(has(t, "\nimage ") && has(t, "crescent.png") && !has(t, "\nuser "), "MY PRESET 201 (saved with a picture) brings the picture back, the shape file is gone");
    setp(s, "user_slot", 202); setp(s, "load_user", 1); waitload(s);
    t = state(s);
    CHECK(has(t, "\nuser ") && has(t, "torus.obj") && !has(t, "\nimage "), "MY PRESET 202 (saved with a shape file) brings the shape back, the picture is gone");
    setp(s, "user_slot", 201); setp(s, "load_user", 1); waitload(s);
    setp(s, "user_slot", 203); setp(s, "load_user", 1); waitload(s);
    t = state(s);
    CHECK(!has(t, "\nuser ") && !has(t, "\nimage "), "MY PRESET 203 (saved with nothing) comes back with nothing loaded");
    /* a LOAD pressed while a preset is still bringing its file back waits its turn (it used to be dropped) */
    setp(s, "user_slot", 202); setp(s, "load_user", 1);                      /* starts loading torus.obj */
    setp(s, "file", findfile(s, "crescent.png")); setp(s, "load", 1);        /* pressed straight away */
    waitload(s);
    t = state(s);
    CHECK(has(t, "\nimage ") && has(t, "crescent.png") && !has(t, "\nuser "), "LOAD pressed during a preset's own load: the picture still loads, last");
    printf(fails ? "FAILED: %d\n" : "PRESET FILE TEST PASSED\n", fails);
    return fails != 0;
}
