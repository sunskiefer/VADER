/* Prints the built-in shapes' paths (src/shape.c) for tools/vader_skin.py, which draws them as the screen's pictures,
 * turned exactly as the engine turns them (path_project, with its fitting). Built and run by build.sh on the host.
 *   shape_dump        one line per 2D shape: its PATHN "x,y" points, unturned
 *   shape_dump turns  "2d <shape> <frame> x,y ..." for every turn frame of every 2D shape (src/engine.c shape_picture)
 *                     "3d <j> <tilt> <frame> x,y ..." for CUBE (j 0) and ICOSA (j 1): spin frames per tilt step */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "vader.h"

static void pts(const float *x, const float *y) {
    for (int k = 0; k < PATHN; k++) printf(" %.5f,%.5f", x[k], y[k]);
    printf("\n");
}

int main(int argc, char **argv) {
    static path_t p;
    static float x[PATHN], y[PATHN];
    if (argc < 2) {
        for (int i = 0; i < PIC_SHAPES; i++) {
            shape_builtin(i, &p);
            for (int k = 0; k < PATHN; k++) printf("%s%.5f,%.5f", k ? " " : "", p.x[k], p.y[k]);
            printf("\n");
        }
        return 0;
    }
    if (strcmp(argv[1], "turns")) return 1;
    for (int i = 0; i < PIC_SHAPES; i++) {
        shape_builtin(i, &p);
        int sym = PIC_SYM[i], rots = sym ? PIC_ROT[i] : 1;
        for (int f = 0; f < rots; f++) {
            float ang = sym ? (float)f / sym / rots : 0;
            path_project(&p, ang * TWO_PI, 0, 0, x, y);
            printf("2d %d %d", i, f);
            pts(x, y);
        }
    }
    for (int j = 0; j < P3_SHAPES; j++) {
        shape_builtin(13 + j, &p);
        for (int ti = 0; ti < P3_TILT; ti++)
            for (int f = 0; f < P3_YAW; f++) {
                path_project(&p, 0, (float)f / P3_SYM[j] / P3_YAW * TWO_PI, (float)ti / (P3_TILT - 1) * 1.5707963f, x, y);
                printf("3d %d %d %d", j, ti, f);
                pts(x, y);
            }
    }
    return 0;
}
