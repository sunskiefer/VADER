/* For tools/preview.py: plays VADER for a moment and prints every parameter as MPC would read it (index, normalised
 * value, display text), so the preview shows the screen as it really looks.
 *   skin_dump <preset> [shape] [file to load]   */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"
#include "params.h"

int main(int argc, char **argv) {
    const mpc_engine_t *E = mpc_engine();
    void *s = E->create(getenv("VADER_DATA") ? getenv("VADER_DATA") : "shapes");   /* VADER_DATA: a plugin folder's Shapes, to write your shape's pictures beside it */
    char b[256];
    E->set_param(s, "preset", argc > 1 ? argv[1] : "0");
    if (argc > 2 && atoi(argv[2]) >= 0) E->set_param(s, "shape", argv[2]);
    if (argc > 3) {
        for (int i = 0; i < 500; i++) {
            snprintf(b, sizeof b, "%d", i);
            E->set_param(s, "file", b);
            E->get_param(s, "file_display", b, sizeof b);
            if (!strcmp(b, argv[3])) break;
        }
        E->set_param(s, "load", "1");
        for (int k = 0; k < 2000; k++) {   /* up to 10 s: writing the pictures takes a moment */
            usleep(5000);
            E->get_param(s, "file_display", b, sizeof b);
            if (strcmp(b, "LOADING...")) break;
        }
    }
    E->set_param(s, "f_cutoff", "0.64");
    uint8_t on[3] = {0x90, 48, 100}, on2[3] = {0x90, 55, 100};
    E->midi(s, on, 3);
    E->midi(s, on2, 3);
    int16_t out[256];
    int nb = getenv("VADER_BLOCKS") ? atoi(getenv("VADER_BLOCKS")) : 345 * 2 + 37;   /* how long it plays first */
    int realtime = getenv("VADER_REALTIME") != NULL;   /* the server test: play at the real pace, until killed */
    for (int i = 0; i < nb; i++) { E->render(s, out, 128); if (realtime) usleep(2900); }
    for (int i = 0; i < NPARAMS; i++) {
        const param_t *p = &PARAMS[i];
        char v[256] = "", d[256] = "";
        float n = 0;
        if (E->get_param(s, p->key, v, sizeof v) > 0) {
            if (p->nopts > 1) n = (float)atoi(v) / (p->nopts - 1);
            else if (p->max > p->min) n = (float)((atof(v) - p->min) / (p->max - p->min));
        }
        char k2[96];
        snprintf(k2, sizeof k2, "%s_display", p->key);
        if (p->string_display) snprintf(d, sizeof d, "%s", v);
        else if (p->dynamic_display && E->get_param(s, k2, d, sizeof d) > 0) {}
        else if (p->nopts > 1) snprintf(d, sizeof d, "%s", p->opts[atoi(v)]);
        else if (p->int_display) snprintf(d, sizeof d, "%d", atoi(v));
        else snprintf(d, sizeof d, "%.1f", atof(v));
        printf("%d\t%.5f\t%s\n", i, n, d);
    }
    E->destroy(s);
    return 0;
}
