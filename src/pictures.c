/* Your own shape's screen pictures (an experiment, 0.4.0). MPC draws a plugin's screen only from pictures in its skin
 * folder, and can't draw new ones. So when a 2D shape file is loaded, VADER draws its outline itself, as the skin's
 * built-in shapes are drawn (tools/vader_skin.py), and writes them over the skin's empty vd_user_<k>.png strips: one
 * strip per size step k (1..U_SIZES), U_ROT turns stacked as frames. Whether MPC shows them depends on whether it reads
 * the files again when the plugin window opens (it may keep what it loaded). Runs on the loader thread. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_image_write.h"
#include "vader.h"

/* the outline (screen pixels, frame origin x0, y0) into an alpha frame: a 2.4 px line with a soft glow */
static void draw_path(float *a, int w, int h, const float *x, const float *y, int n, float x0, float y0) {
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ax = x[i] - x0, ay = y[i] - y0, bx = x[j] - x0, by = y[j] - y0;
        float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        int xa = (int)floorf(fminf(ax, bx) - 10), xb = (int)ceilf(fmaxf(ax, bx) + 10);
        int ya = (int)floorf(fminf(ay, by) - 10), yb = (int)ceilf(fmaxf(ay, by) + 10);
        if (xa < 0) xa = 0;
        if (ya < 0) ya = 0;
        if (xb > w - 1) xb = w - 1;
        if (yb > h - 1) yb = h - 1;
        for (int py = ya; py <= yb; py++)
            for (int px = xa; px <= xb; px++) {
                float qx = px + 0.5f - ax, qy = py + 0.5f - ay;
                float t = l2 > 1e-9f ? (qx * dx + qy * dy) / l2 : 0;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                float ex = qx - t * dx, ey = qy - t * dy, d = sqrtf(ex * ex + ey * ey);
                float core = 1.2f + 0.6f - d;       /* half the line, plus a pixel of smoothing */
                core = core < 0 ? 0 : core > 1 ? 1 : core;
                float glow = 0.55f * expf(-d * d / (2 * 4.0f * 4.0f));
                float v = core > glow ? core : glow;
                float *o = &a[py * w + px];
                if (v > *o) *o = v;
            }
    }
}

int write_user_pictures(const char *skin_dir, const path_t *p) {
    stbi_write_png_compression_level = 2;   /* quick: mostly empty pictures compress well anyway */
    static float x2[PATHN], y2[PATHN], sx[PATHN], sy[PATHN];
    for (int k = 1; k <= U_SIZES; k++) {
        int w, h;
        upic_frame(k, &w, &h);
        float fx0 = SQUARE_X + SQUARE * 0.5f - w * 0.5f, fy0 = SQUARE * 0.5f - h * 0.5f;
        float *alpha = calloc((size_t)(w * h), sizeof(float));
        unsigned char *rgba = calloc((size_t)(w * h * U_ROT), 4);
        if (!alpha || !rgba) { free(alpha); free(rgba); return 0; }
        for (int f = 0; f < U_ROT; f++) {
            memset(alpha, 0, sizeof(float) * (size_t)(w * h));
            path_project(p, TWO_PI * f / U_ROT, 0, 0, x2, y2);
            float sc = (float)k / U_SIZES;
            for (int i = 0; i < PATHN; i++) { sx[i] = scr_x(x2[i] * sc); sy[i] = scr_y(y2[i] * sc); }
            draw_path(alpha, w, h, sx, sy, PATHN, fx0, fy0);
            unsigned char *o = rgba + (size_t)f * w * h * 4;
            for (int i = 0; i < w * h; i++) {
                o[4 * i] = 58; o[4 * i + 1] = 160; o[4 * i + 2] = 255;
                o[4 * i + 3] = (unsigned char)(alpha[i] * 255.0f + 0.5f);
            }
        }
        char path[800], tmp[820];
        snprintf(path, sizeof path, "%s/vd_user_%d.png", skin_dir, k);
        snprintf(tmp, sizeof tmp, "%s.tmp", path);
        int ok = stbi_write_png(tmp, w, h * U_ROT, 4, rgba, w * 4) && rename(tmp, path) == 0;   /* whole or not at all */
        free(alpha);
        free(rgba);
        if (!ok) { remove(tmp); return 0; }
    }
    return 1;
}
