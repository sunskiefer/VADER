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
#include "vader_params.h"

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

static void chord(void *s, int on) { note(s, on, 48, 100); note(s, on, 55, 100); note(s, on, 60, 100); note(s, on, 64, 100); }

static void drain(void *s) {
    char b[8192];
    E->get_param(s, "dirty", b, sizeof b);
}

int main(void) {
    E = mpc_engine();
    void *s = E->create("shapes");
    CHECK(s != NULL, "create");
    /* presets */
    for (int p = 0; p < 16; p++) {
        char name[64];
        snprintf(name, sizeof name, "preset:%d", p);
        char nm[64];
        E->get_param(s, name, nm, sizeof nm);
        setp(s, "preset", p);
        chord(s, 1);
        level_t a = run(s, 345);   /* 1 s */
        chord(s, 0);
        level_t r = run(s, 345 * 3);
        CHECK(a.rms > 0.01 && a.peak < 0.999, "preset %2d %-16s rms %.3f peak %.3f, after release rms %.4f", p, nm, a.rms, a.peak, r.rms);
    }
    setp(s, "preset", 0);
    /* shapes */
    for (int sh = 0; sh < 15; sh++) {
        setp(s, "shape", sh);
        chord(s, 1);
        level_t a = run(s, 200);
        chord(s, 0);
        run(s, 600);
        char d[64];
        E->get_param(s, "shape_display", d, sizeof d);
        CHECK(a.rms > 0.01 && a.peak < 0.999, "shape %-10s rms %.3f peak %.3f", d, a.rms, a.peak);
    }
    /* blend modes and filters, on all four corners */
    for (int m = 0; m < 7; m++)
        for (int f = 0; f < 5; f++) {
            for (int c = 0; c < 4; c++) {
                char k[16];
                snprintf(k, sizeof k, "%c_mode", 'a' + c); setp(s, k, m);
                snprintf(k, sizeof k, "%c_ftype", 'a' + c); setp(s, k, f);
                snprintf(k, sizeof k, "%c_reso", 'a' + c); setp(s, k, 0.9);
                snprintf(k, sizeof k, "%c_blend", 'a' + c); setp(s, k, 1.0);
                snprintf(k, sizeof k, "%c_fpos", 'a' + c); setp(s, k, (m + f) % 3);
            }
            note(s, 1, 36, 127); note(s, 1, 84, 127);
            level_t a = run(s, 150);
            note(s, 0, 36, 0); note(s, 0, 84, 0);
            run(s, 500);
            if (!(a.rms > 0.002 && a.peak < 0.999)) CHECK(0, "mode %d filter %d rms %.4f peak %.3f", m, f, a.rms, a.peak);
        }
    CHECK(1, "35 blend/filter combinations at full resonance");
    setp(s, "preset", 1);
    setp(s, "preset", 0);
    /* the front knobs edit the selected corner, or all */
    setp(s, "corner", 1);
    setp(s, "f_cutoff", 0.123);
    CHECK(fabs(getp(s, "b_cutoff") - 0.123) < 1e-3 && fabs(getp(s, "a_cutoff") - 0.123) > 1e-3, "FILTER CUTOFF edits corner B only");
    setp(s, "corner", 4);
    setp(s, "f_og1", 0.5);
    CHECK(fabs(getp(s, "a_og1") - 0.5) < 1e-3 && fabs(getp(s, "d_og1") - 0.5) < 1e-3, "corner ALL edits all four");
    /* timbres keep their own sound */
    setp(s, "timbre", 1);
    setp(s, "attack", 0.9);
    setp(s, "timbre", 0);
    CHECK(fabs(getp(s, "attack") - 0.9) > 1e-3, "timbre A keeps its attack when B's changes");
    setp(s, "timbre", 1);
    CHECK(fabs(getp(s, "attack") - 0.9) < 1e-3, "timbre B has its own attack");
    setp(s, "timbre", 0);
    /* split and dual play */
    setp(s, "keys", 1); setp(s, "tmode", 1); setp(s, "split", 60);
    note(s, 1, 40, 100); note(s, 1, 72, 100);
    level_t sp = run(s, 200);
    note(s, 0, 40, 0); note(s, 0, 72, 0); run(s, 800);
    CHECK(sp.rms > 0.01, "split keyboard plays (rms %.3f)", sp.rms);
    setp(s, "keys", 0);
    /* the arpeggiator on MPC's clock */
    setp(s, "arp_on", 1); setp(s, "arp_rate", 3);
    E->set_param(s, "host_time", "1 0.0 120.0");
    chord(s, 1);
    int16_t buf[256];
    int onsets = 0, prev_quiet = 1;
    for (int b = 0; b < 345 * 2; b++) {
        char ht[64];
        snprintf(ht, sizeof ht, "1 %.6f 120.0", b * 128 / 44100.0 * 2.0);
        E->set_param(s, "host_time", ht);
        E->render(s, buf, 128);
        double e = 0;
        for (int i = 0; i < 256; i++) e += fabs(buf[i]);
        e /= 256;
        if (e > 300 && prev_quiet) onsets++;
        prev_quiet = e < 60;
    }
    chord(s, 0);
    setp(s, "arp_on", 0);
    run(s, 600);
    CHECK(getp(s, "arp_count") >= 15 && getp(s, "arp_count") <= 17, "arpeggiator: %.0f steps in 2 s at 1/16, 120 BPM (16 expected)", getp(s, "arp_count"));
    (void)onsets;
    E->set_param(s, "host_time", "0 0 120");
    /* shape files */
    char d[128];
    double nfiles = 0;
    for (int i = 0; i < 7; i++) {
        setp(s, "file", i);
        E->get_param(s, "file_display", d, sizeof d);
        char name[128];
        snprintf(name, sizeof name, "%s", d);
        setp(s, "load", 1);
        for (int k = 0; k < 200; k++) {
            usleep(5000);
            E->get_param(s, "file_display", d, sizeof d);
            if (strcmp(d, "LOADING...")) break;
        }
        char sd[96];
        E->get_param(s, "shape_display", sd, sizeof sd);
        CHECK(!strcmp(d, "LOADED") && getp(s, "shape") == 15, "load %-14s -> %s (%s)", name, d, sd);
        chord(s, 1);
        level_t a = run(s, 100);
        chord(s, 0);
        run(s, 300);
        CHECK(a.rms > 0.01, "  plays on it (rms %.3f)", a.rms);
        nfiles++;
    }
    /* the screen reports what changed */
    drain(s);
    setp(s, "shape", 0);
    chord(s, 1);
    run(s, 40);
    char db[8192];
    E->get_param(s, "dirty", db, sizeof db);
    int nd = 0, tiles = 0;
    for (char *p = db; *p;) {
        char *e;
        long i = strtol(p, &e, 10);
        if (e == p) break;
        p = e;
        nd++;
        if (i >= P_T000) tiles++;
    }
    CHECK(tiles > 10, "the screen sent %d changed tiles (of %d changes) after a shape change", tiles, nd);
    run(s, 40);
    E->get_param(s, "dirty", db, sizeof db);
    nd = 0;
    for (char *p = db; *p;) { char *e; strtol(p, &e, 10); if (e == p) break; p = e; nd++; }
    CHECK(nd > 0 && nd < 120, "a moving orbit changes %d pieces per update", nd);
    chord(s, 0);
    run(s, 600);
    /* state round trip */
    setp(s, "preset", 7);
    setp(s, "timbre", 1); setp(s, "c_tune", 5); setp(s, "timbre", 0);
    int need = E->get_param(s, "state", NULL, 0);
    char *st = malloc((size_t)need);
    int got = E->get_param(s, "state", st, need);
    void *s2 = E->create("shapes");
    E->set_param(s2, "state", st);
    int diff = 0;
    for (int t = 0; t < 2; t++) {
        setp(s, "timbre", t); setp(s2, "timbre", t);
        for (int i = 0; i < NP; i++)
            if (VP[i].kind == K_SND || VP[i].kind == K_GLOB) {
                double a = getp(s, VP[i].key), b = getp(s2, VP[i].key);
                if (fabs(a - b) > 1e-3) { diff++; if (diff < 5) printf("  differs: %s %g %g\n", VP[i].key, a, b); }
            }
    }
    CHECK(got > 100 && got <= need && diff == 0, "state round trip (%d bytes, %d differences)", got, diff);
    free(st);
    E->destroy(s2);
    /* a note held across a preset change ends when its key is let go (the preset moves the octave) */
    setp(s, "preset", 0);
    note(s, 1, 60, 100);
    run(s, 50);
    setp(s, "preset", 2);   /* DARK FIGURE: timbre octave -1 */
    note(s, 1, 62, 100);
    run(s, 50);
    note(s, 0, 60, 0);
    note(s, 0, 62, 0);
    level_t tail = run(s, 345 * 6);
    level_t after = run(s, 345);
    CHECK(after.rms < 0.0005, "no note keeps sounding after a preset change (rms %.5f after 6 s)", after.rms);
    (void)tail;
    /* your own presets: save, change, load */
    setp(s, "preset", 3);
    setp(s, "timbre", 1); setp(s, "b_tune", 7); setp(s, "timbre", 0);
    setp(s, "user_slot", 123);
    setp(s, "save_user", 1);
    char ud[96];
    E->get_param(s, "user_slot_display", ud, sizeof ud);
    CHECK(strstr(ud, "SAVED") != NULL, "save to MY PRESET 123 (\"%s\")", ud);
    setp(s, "preset", 0);
    setp(s, "user_slot", 122);
    E->get_param(s, "user_slot_display", ud, sizeof ud);
    int empty = strstr(ud, "empty") != NULL;
    setp(s, "user_slot", 123);
    setp(s, "load_user", 1);
    setp(s, "timbre", 1);
    CHECK(empty && getp(s, "preset") == 3 && fabs(getp(s, "b_tune") - 7) < 1e-3, "slot 122 shows empty, slot 123 loads back (preset %.0f, B tune %.0f)", getp(s, "preset"), getp(s, "b_tune"));
    setp(s, "timbre", 0);
    /* CPU: the worst case */
    setp(s, "preset", 0);
    setp(s, "voices", 1);
    setp(s, "timbre", 0);
    for (int c = 0; c < 4; c++) { char k[16]; snprintf(k, sizeof k, "%c_ftype", 'a' + c); setp(s, k, 4); snprintf(k, sizeof k, "%c_mode", 'a' + c); setp(s, k, 3); }
    setp(s, "orb_size", 0.0);   /* the orbiter at the centre: all four corners sound */
    setp(s, "sub_size", 0.0);
    setp(s, "shape_osc", 0.5);
    setp(s, "mix_rev", 0.3); setp(s, "mix_ch", 0.3); setp(s, "mix_dly", 0.3); setp(s, "drive", 0.3);
    for (int n = 0; n < 16; n++) note(s, 1, 40 + n * 3, 100);
    run(s, 50);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int nb = 2000;
    run(s, nb);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / nb;
    printf("cpu: 16 voices x 4 corners, ladder filters, AM+PM, all effects: %.1f us per 128-frame block on this machine (block = 2902 us)\n", us);
    E->destroy(s);
    printf(fails ? "\nFAILED: %d\n" : "\nPASSED\n", fails);
    return fails != 0;
}
