/* Prints the built-in 2D shapes' paths (src/shape.c) for tools/vader_skin.py, which draws them as pictures:
 * one line per shape, PATHN "x,y" points. Built and run by build.sh on the host. */
#include <stdio.h>
#include "vader.h"

int main(void) {
    path_t p;
    for (int i = 0; i < PIC_SHAPES; i++) {
        shape_builtin(i, &p);
        for (int k = 0; k < PATHN; k++) printf("%s%.5f,%.5f", k ? " " : "", p.x[k], p.y[k]);
        printf("\n");
    }
    return 0;
}
