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
    /* the screen reports what changed (the tile screen: off since 0.6.0, on here for its own checks) */
    setp(s, "tile_screen", 1);
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
    run(s, 14);
    E->get_param(s, "dirty", db, sizeof db);
    for (char *p = db; *p;) { char *e; long i = strtol(p, &e, 10); if (e == p) break; p = e; nd++; if (i >= P_T000) tiles++; }
    CHECK(tiles > 10, "the screen sent %d changed tiles (of %d changes) after a shape change", tiles, nd);
    run(s, 40);
    E->get_param(s, "dirty", db, sizeof db);
    nd = 0;
    for (char *p = db; *p;) { char *e; strtol(p, &e, 10); if (e == p) break; p = e; nd++; }
    CHECK(nd > 0 && nd < 120, "a moving orbit changes %d pieces per update", nd);
    /* 0.4.5: in steady play the screen costs MPC little: at most 10 reports a poll, well under that on average (0.4.4
     * sent 35..47 a poll, 1000 a second, and MPC's redraws crackled the audio) */
    {
        int tot = 0, mx = 0;
        for (int u = 0; u < 150; u++) { run(s, 14); drain(s); }   /* the shape change's redraw (spread over polls) has gone out */
        for (int u = 0; u < 60; u++) {
            run(s, 14);
            E->get_param(s, "dirty", db, sizeof db);
            int k = 0;
            for (char *p = db; *p;) { char *e; strtol(p, &e, 10); if (e == p) break; p = e; k++; }
            tot += k;
            if (k > mx) mx = k;
        }
        CHECK(mx <= 10 && tot < 60 * 8, "steady play: the screen reports %.1f pieces a poll on average, %d at most", tot / 60.0, mx);
    }
    /* 0.5.0: the live screen (overlay.c), offline: a frame draws (lit pixels in the box, the balls in their colours),
     * fast; the box finder finds the skin's markers on a picture of a display */
    {
        static ovl_shape_t sh; static ovl_view_t v; static path_t p;
        shape_builtin(2, &p);
        memcpy(sh.px, p.x, sizeof sh.px); memcpy(sh.py, p.y, sizeof sh.py); memcpy(sh.pz, p.z, sizeof sh.pz);
        v.motion = 1; v.size = 0.7f; v.sub_size = 0.5f; v.ot = 0.3f; v.oo = 0.2f; v.hz = 0.8f; v.sub = 4.9f;
        static uint8_t buf[OVL_W * OVL_H * 4];
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int rc = 0;
        for (int i = 0; i < 10; i++) rc |= overlay_render_test(&sh, &v, buf, OVL_W * 4);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / 10;
        long lit = 0;
        for (int i = 0; i < OVL_W * OVL_H; i++) if (buf[i * 4 + 2] > 60 || buf[i * 4 + 1] > 60) lit++;
        int slow_ok = getenv("VADER_SLOW") != NULL;   /* under valgrind or qemu (test/run_tests.sh, test/arm_sim.sh) a frame takes far longer */
        CHECK(rc == 0 && lit > 2000 && lit < 60000 && (ms < 40 || slow_ok), "live screen: a frame draws %ld lit pixels in %.1f ms%s", lit, ms, slow_ok ? " (timing not checked)" : "");
        /* a 1280 x 800 display with the box at 312, 286 */
        static uint8_t disp[1280 * 800 * 4];
        memset(disp, 0x10, sizeof disp);
        const uint8_t TL[4][3] = {{23, 7, 91}, {91, 7, 23}, {7, 91, 23}, {23, 91, 7}}, BR[4][3] = {{91, 23, 7}, {7, 23, 91}, {61, 7, 61}, {7, 61, 61}};
        for (int k = 0; k < 4; k++) {
            uint8_t *q = disp + ((size_t)(286 + 4 + k / 2) * 1280 + 312 + 8 + k % 2) * 4;
            q[2] = TL[k][0]; q[1] = TL[k][1]; q[0] = TL[k][2];
            q = disp + ((size_t)(286 + 4 + 288 - 2 + k / 2) * 1280 + 312 + 8 + 640 - 2 + k % 2) * 4;
            q[2] = BR[k][0]; q[1] = BR[k][1]; q[0] = BR[k][2];
        }
        int bx = -1, by = -1;
        int found = overlay_find_test(disp, 1280, 800, 1280 * 4, &bx, &by);
        CHECK(found && bx == 312 && by == 286, "live screen: the box found at %d,%d on a display picture (found %d)", bx, by, found);
        disp[((size_t)(286 + 4) * 1280 + 312 + 8) * 4 + 2] = 0;   /* a list over the corner */
        CHECK(!overlay_find_test(disp, 1280, 800, 1280 * 4, &bx, &by), "live screen: not found with the corner covered");
    }
    /* 0.4.6: a fast orbiter under the report cap: the tiles the trail left are reported before new pieces, so what MPC
     * shows never piles up (GLASS STAR on the device: trail pieces over the whole screen) */
    {
        setp(s, "preset", 1);   /* GLASS STAR: 3D SPIN, a fast suborbit */
        setp(s, "motion", 1);
        setp(s, "sub_speed", 1);
        setp(s, "orb_speed", 1);
        chord(s, 1);
        int worst = 0;
        for (int u = 0; u < 200; u++) {
            run(s, 14);
            drain(s);
            int lit = 0;
            char k[16], b[32];
            for (int i = 0; i < TILE_COLS * TILE_ROWS; i++) {
                snprintf(k, sizeof k, "r%03d", i);
                if (E->get_param(s, k, b, sizeof b) > 0 && atoi(b)) lit++;
            }
            if (lit > worst) worst = lit;
        }
        CHECK(worst <= 16, "a fast orbiter: MPC shows at most %d trail tiles (the trail is 14 long)", worst);
        chord(s, 0);
        run(s, 100);
    }
    /* the balls (0.4.3): one cell each at a time, always whole on the screen, colours following the sizes */
    {
        setp(s, "shape", 3);   /* SQUARE: the path reaches the square's edges */
        setp(s, "orb_size", 1);
        setp(s, "sub_size", 1);
        setp(s, "orb_speed", 0.8);
        int bad_count = 0, bad_edge = 0, polls = 0, behind = 0, front = 0;
        char k[16];
        for (int u = 0; u < 400; u++) {
            run(s, 14);
            polls++;
            int lit[2] = {0, 0};
            for (int w = 0; w < 2; w++)
                for (int i = 0; i < BALL_N * BALL_N; i++) {
                    snprintf(k, sizeof k, "b%c%02d", w ? 'p' : 'o', i);
                    int f = (int)getp(s, k);
                    if (!f) continue;
                    lit[w]++;
                    int ix = (f - 1) % BALL_POS, iy = (f - 1) / BALL_POS;
                    float r = (w ? BALL_PD : BALL_OD) * 0.5f;
                    float cx = SQUARE_X + (i % BALL_N) * BALL_PITCH + ix * BALL_STEP + BALL_STEP * 0.5f;
                    float cy = (i / BALL_N) * BALL_PITCH + iy * BALL_STEP + BALL_STEP * 0.5f;
                    if (cx - r < -0.01f || cy - r < -0.01f || cx + r > TILE_COLS * TILE_W + 0.01f || cy + r > TILE_ROWS * TILE_H + 0.01f) bad_edge++;
                }
            if (lit[0] != 1 || lit[1] != 1) bad_count++;
            int o = (int)getp(s, "ball_o");
            if (o >= BALL_COLS) behind++; else front++;
        }
        CHECK(bad_count == 0, "the balls: one cell each in every one of %d updates (%d wrong)", polls, bad_count);
        CHECK(bad_edge == 0, "the balls stay whole inside the screen, on the SQUARE at full size (%d outside)", bad_edge);
        CHECK(behind > 20 && front > 20, "the orbiter passes behind the path point and in front of it (%d / %d updates)", behind, front);
        CHECK((int)getp(s, "ball_o") % BALL_COLS == BALL_COLS - 1 && (int)getp(s, "ball_p") == BALL_COLS - 1,
              "full ORBIT SIZE and SUBORBIT SIZE: both balls at their last colour");
        setp(s, "orb_size", 0.05);
        setp(s, "sub_size", 0.5);
        run(s, 30);
        CHECK((int)getp(s, "ball_p") == 0 && (int)getp(s, "ball_o") % BALL_COLS == 3,
              "small ORBIT SIZE: the path point's first colour; SUBORBIT SIZE 50: the orbiter's colour 4 (%d, %d)",
              (int)getp(s, "ball_p"), (int)getp(s, "ball_o"));
        setp(s, "motion", 0);
        run(s, 30);
        int any = 0;
        for (int w = 0; w < 2; w++) for (int i = 0; i < BALL_N * BALL_N; i++) { snprintf(k, sizeof k, "b%c%02d", w ? 'p' : 'o', i); any += getp(s, k) != 0; }
        CHECK(any == 0, "MOTION off: no balls");
        setp(s, "motion", 1);
        setp(s, "orb_size", 0.7);
        setp(s, "sub_size", 0.3);
        setp(s, "orb_speed", 0.35);
    }
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
    /* the screen's pictures: a STAR at full size shows strip 2D + its size, a turn frame in 0..5 */
    setp(s, "preset", 0);
    setp(s, "shape", 2); setp(s, "orb_size", 1.0); setp(s, "rotate", 0.1);
    run(s, 200);
    {
        double sel = getp(s, "shape_pic"), fr = getp(s, "pic_r6");
        CHECK(sel == SEL_2D + 2 * PIC_SIZES + PIC_SIZES - 1 && fr >= 0 && fr <= 5, "STAR, size 100, turned: picture %.0f, frame %.0f", sel, fr);
    }
    /* the IMAGE page: a picture under the orbiter moves the sound */
    {
        int fi = -1;
        for (int i = 0; i < 40 && fi < 0; i++) {
            setp(s, "file", i);
            E->get_param(s, "file_display", d, sizeof d);
            if (strstr(d, "crescent.png")) fi = i;
        }
        setp(s, "file", fi);
        setp(s, "load", 1);
        for (int k = 0; k < 400; k++) { usleep(5000); E->get_param(s, "file_display", d, sizeof d); if (strcmp(d, "LOADING...")) break; }
        setp(s, "shape", 0); setp(s, "orb_size", 0.6); setp(s, "sub_size", 0.3); setp(s, "orb_speed", 0.6);
        setp(s, "im1_amt", 0.0);
        chord(s, 1);
        level_t a0 = run(s, 400);
        chord(s, 0); run(s, 400);
        setp(s, "im1_src", 0); setp(s, "im1_to", 7); setp(s, "im1_amt", 1.0); setp(s, "im1_smooth", 0.1);   /* BRIGHT -> LEVEL */
        chord(s, 1);
        double mx = 0, mn = 100;
        level_t a1 = {0, 0, 0};
        for (int b = 0; b < 40; b++) {
            level_t l = run(s, 10);
            a1.rms += l.rms / 40;
            double m = getp(s, "im_m1");
            if (m > mx) mx = m;
            if (m < mn) mn = m;
        }
        chord(s, 0); run(s, 400);
        CHECK(fi >= 0 && fabs(a1.rms - a0.rms) > 0.002 && mx > mn, "IMAGE: crescent.png BRIGHT -> LEVEL changes the sound (rms %.3f -> %.3f), meter %.0f..%.0f", a0.rms, a1.rms, mn, mx);
        setp(s, "im1_amt", 0.0);
        /* every source and destination runs clean */
        int bad = 0;
        for (int src = 0; src < 6; src++)
            for (int to = 0; to < 8; to++) {
                setp(s, "im2_src", src); setp(s, "im2_to", to); setp(s, "im2_amt", to % 2 ? -1.0 : 1.0);
                note(s, 1, 60, 100);
                level_t l = run(s, 60);
                note(s, 0, 60, 0);
                run(s, 30);
                if (l.nan || l.peak >= 1.0 || l.rms != l.rms) bad++;
            }
        setp(s, "im2_amt", 0.0);
        CHECK(bad == 0, "IMAGE: all 6 sources x 8 destinations play clean (%d bad)", bad);
    }
    /* your own shape's pictures, written into the skin folder (the experiment) */
    {
        char root[] = "/tmp/vader_pics_XXXXXX";
        if (mkdtemp(root)) {
            char cmd[512], sk[300], f[400];
            snprintf(cmd, sizeof cmd, "mkdir -p '%s/Plugin Skins' && cp -r shapes '%s/Shapes'", root, root);
            if (system(cmd) != 0) printf("(could not set up %s)\n", root);
            snprintf(sk, sizeof sk, "%s/Shapes", root);
            void *s3 = E->create(sk);
            E->set_param(s3, "tile_screen", "1");
            int li = -1;
            for (int i = 0; i < 40 && li < 0; i++) {
                setp(s3, "file", i);
                E->get_param(s3, "file_display", d, sizeof d);
                if (strstr(d, "lightning.svg")) li = i;
            }
            setp(s3, "file", li);
            struct timespec w0, w1;
            clock_gettime(CLOCK_MONOTONIC, &w0);
            setp(s3, "load", 1);
            for (int k = 0; k < 6000; k++) { usleep(5000); E->get_param(s3, "file_display", d, sizeof d); if (strcmp(d, "LOADING...")) break; }
            int ok = 0;
            for (int k = 1; k <= U_SIZES; k++) {
                snprintf(f, sizeof f, "%s/Plugin Skins/vd_user_%d.png", root, k);
                FILE *fp = fopen(f, "rb");
                unsigned char h[24] = {0};
                if (fp) {
                    if (fread(h, 1, 24, fp) == 24 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') {
                        int w = h[16] << 24 | h[17] << 16 | h[18] << 8 | h[19], hh = h[20] << 24 | h[21] << 16 | h[22] << 8 | h[23], ew, eh;
                        upic_frame_test(k, &ew, &eh);
                        ok += w == ew && hh == eh * U_ROT;
                    }
                    fclose(fp);
                }
            }
            clock_gettime(CLOCK_MONOTONIC, &w1);
            printf("(your shape's pictures: loaded and written in %.2f s)\n", (w1.tv_sec - w0.tv_sec) + (w1.tv_nsec - w0.tv_nsec) / 1e9);
            run(s3, 100);
            double sel = getp(s3, "shape_pic");
            CHECK(ok == U_SIZES && sel >= SEL_USER && sel < SEL_USER + U_SIZES, "your shape's pictures: %d of %d strips written at the skin's sizes, the screen shows picture %.0f", ok, U_SIZES, sel);
            E->destroy(s3);
            snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
            if (system(cmd) != 0) printf("(could not remove %s)\n", root);
        }
    }
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
