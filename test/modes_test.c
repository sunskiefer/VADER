/* Offline check of the 0.8 modes and maths shapes: every mode stepped for a while, every shape generated, nothing
 * non-finite and every ball inside the square. Usage: test/modes_test.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "vader.h"

int main(void) {
    int bad = 0;
    for (int m = 0; m < NMODES; m++) {
        for (int kv = 0; kv < 3; kv++) {
            float k[8];
            for (int i = 0; i < 8; i++) k[i] = kv == 0 ? 0.5f : kv == 1 ? 0.0f : 1.0f;
            sim_t s;
            memset(&s, 0, sizeof s);
            s.rng = 777;
            sim_reset(&s, m, k);
            for (int step = 0; step < 4000; step++) {
                sim_step(&s, k, 0.003f);
                for (int i = 0; i < s.n && i < SIM_N; i++) {
                    if (!isfinite(s.sx[i]) || !isfinite(s.sy[i]) || fabsf(s.sx[i]) > 1.0001f || fabsf(s.sy[i]) > 1.0001f) {
                        if (bad < 20) printf("BAD mode %d (%s) knobs %d step %d ball %d: %f %f\n", m, MODE_NAMES[m], kv, step, i, s.sx[i], s.sy[i]);
                        bad++;
                        step = 99999;
                        break;
                    }
                }
            }
        }
    }
    for (int m = 0; m < NMATH; m++) {
        for (int kv = 0; kv < 5; kv++) {
            float k[4];
            for (int i = 0; i < 4; i++) k[i] = kv == 0 ? 0.5f : kv == 1 ? 0.0f : kv == 2 ? 1.0f : kv == 3 ? 0.25f : 0.8f;
            path_t p;
            paths_gen(m, k, &p);
            for (int i = 0; i < PATHN; i++)
                if (!isfinite(p.x[i]) || !isfinite(p.y[i]) || !isfinite(p.z[i]) || fabsf(p.x[i]) > 1.001f || fabsf(p.y[i]) > 1.001f) {
                    if (bad < 20) printf("BAD shape %d (%s) knobs %d at %d: %f %f\n", m, MATH_NAMES[m], kv, i, p.x[i], p.y[i]);
                    bad++;
                    break;
                }
        }
    }
    printf("%s (%d problems)\n", bad ? "FAILED" : "PASSED", bad);
    return bad != 0;
}
