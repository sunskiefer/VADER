/* The orbit paths: the built-in shapes, resampling any traced line to PATHN points at equal spacing (so the orbiter
 * moves at an even speed whatever the drawing), and projecting a path (rotated in its plane, or a 3D shape turned and
 * tilted) into the -1..1 square the four corners sit on. */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "vader.h"

#define PI 3.14159265358979f

static const char *const NAMES[NSHAPES] = {"CIRCLE", "FIGURE 8", "STAR", "SQUARE", "TRIANGLE", "LISSA 3:2",
                                           "LISSA 5:4", "ROSE 3", "ROSE 5", "SPIRAL", "HEART", "ZIGZAG", "SPIRO",
                                           "CUBE", "ICOSA", "USER"};

/* resample a polyline to PATHN points spaced evenly along its length */
void path_resample(const float *x, const float *y, const float *z, int n, int closed, int is3d, path_t *out) {
    if (n < 2) {
        for (int i = 0; i < PATHN; i++) out->x[i] = out->y[i] = out->z[i] = 0;
        out->is3d = is3d;
        return;
    }
    int segs = closed ? n : n - 1;
    float total = 0;
    for (int i = 0; i < segs; i++) {
        int j = (i + 1) % n;
        float dz = z ? z[j] - z[i] : 0;
        total += sqrtf((x[j] - x[i]) * (x[j] - x[i]) + (y[j] - y[i]) * (y[j] - y[i]) + dz * dz);
    }
    if (total <= 0) total = 1;
    int seg = 0;
    float acc = 0, seglen = 0;
    {
        int j = 1 % n;
        float dz = z ? z[j] - z[0] : 0;
        seglen = sqrtf((x[j] - x[0]) * (x[j] - x[0]) + (y[j] - y[0]) * (y[j] - y[0]) + dz * dz);
    }
    for (int k = 0; k < PATHN; k++) {
        float target = total * (float)k / (closed ? PATHN : PATHN - 1);
        while (seg < segs - 1 && acc + seglen < target) {
            acc += seglen;
            seg++;
            int i = seg, j = (seg + 1) % n;
            float dz = z ? z[j] - z[i] : 0;
            seglen = sqrtf((x[j] - x[i]) * (x[j] - x[i]) + (y[j] - y[i]) * (y[j] - y[i]) + dz * dz);
        }
        int i = seg, j = (seg + 1) % n;
        float f = seglen > 0 ? (target - acc) / seglen : 0;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        out->x[k] = x[i] + (x[j] - x[i]) * f;
        out->y[k] = y[i] + (y[j] - y[i]) * f;
        out->z[k] = z ? z[i] + (z[j] - z[i]) * f : 0;
    }
    out->is3d = is3d;
}

/* walk every edge of a wireframe once at least (depth first, coming back along the way it went): a line through
 * the whole object, for the orbiter to follow */
static int edge_tour(const int (*edges)[2], int ne, int nv, int *seq, int maxseq) {
    unsigned char used[256];
    int n = 0;
    memset(used, 0, sizeof used);
    int stack[512], sp = 0;
    stack[sp++] = edges[0][0];
    seq[n++] = edges[0][0];
    while (sp > 0 && n < maxseq) {
        int v = stack[sp - 1], next = -1;
        for (int e = 0; e < ne; e++) {
            if (used[e]) continue;
            if (edges[e][0] == v) { next = edges[e][1]; used[e] = 1; break; }
            if (edges[e][1] == v) { next = edges[e][0]; used[e] = 1; break; }
        }
        if (next >= 0) {
            stack[sp++] = next;
            seq[n++] = next;
        } else {
            sp--;
            if (sp > 0) seq[n++] = stack[sp - 1];
        }
    }
    (void)nv;
    return n;
}

static void wire(path_t *p, const float (*v)[3], int nv, const int (*edges)[2], int ne) {
    int seq[600];
    int n = edge_tour(edges, ne, nv, seq, 600);
    float x[600], y[600], z[600];
    for (int i = 0; i < n; i++) {
        x[i] = v[seq[i]][0];
        y[i] = v[seq[i]][1];
        z[i] = v[seq[i]][2];
    }
    path_resample(x, y, z, n, 0, 1, p);
}

void shape_builtin(int idx, path_t *p) {
    float x[1024], y[1024];
    int n = 512;
    memset(p, 0, sizeof *p);
    if (idx < 0 || idx >= NSHAPES) idx = 0;
    snprintf(p->name, sizeof p->name, "%s", NAMES[idx]);
    switch (idx) {
    case 13: {   /* CUBE */
        static const float v[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        static const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        float vv[8][3];
        for (int i = 0; i < 8; i++)
            for (int k = 0; k < 3; k++) vv[i][k] = v[i][k] * 0.577f;
        wire(p, (const float(*)[3])vv, 8, e, 12);
        return;
    }
    case 14: {   /* ICOSA */
        const float t = 1.6180339887f, s = 1.0f / sqrtf(1 + t * t);
        const float v[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        float vv[12][3];
        for (int i = 0; i < 12; i++)
            for (int k = 0; k < 3; k++) vv[i][k] = v[i][k] * s;
        int e[30][2], ne = 0;
        for (int i = 0; i < 12; i++)
            for (int j = i + 1; j < 12; j++) {
                float d = 0;
                for (int k = 0; k < 3; k++) d += (v[i][k] - v[j][k]) * (v[i][k] - v[j][k]);
                if (fabsf(d - 4.0f) < 0.01f && ne < 30) { e[ne][0] = i; e[ne][1] = j; ne++; }
            }
        wire(p, (const float(*)[3])vv, 12, (const int(*)[2])e, ne);
        return;
    }
    case 2: {   /* STAR */
        n = 10;
        for (int i = 0; i < n; i++) {
            float a = -PI / 2 + i * PI / 5, r = (i & 1) ? 0.42f : 1.0f;
            x[i] = r * cosf(a);
            y[i] = r * sinf(a);
        }
        break;
    }
    case 3: {   /* SQUARE */
        static const float sx[4] = {-0.85f, 0.85f, 0.85f, -0.85f}, sy[4] = {-0.85f, -0.85f, 0.85f, 0.85f};
        n = 4;
        memcpy(x, sx, sizeof sx);
        memcpy(y, sy, sizeof sy);
        break;
    }
    case 4: {   /* TRIANGLE */
        n = 3;
        for (int i = 0; i < 3; i++) {
            float a = -PI / 2 + i * 2 * PI / 3;
            x[i] = cosf(a);
            y[i] = sinf(a) + 0.15f;
        }
        break;
    }
    case 11: {   /* ZIGZAG: across and back */
        n = 0;
        for (int i = 0; i <= 6; i++) { x[n] = -0.9f + i * 0.3f; y[n] = (i & 1) ? 0.8f : -0.8f; n++; }
        for (int i = 6; i >= 0; i--) { x[n] = -0.9f + i * 0.3f; y[n] = (i & 1) ? -0.8f : 0.8f; n++; }
        break;
    }
    default:
        for (int i = 0; i < n; i++) {
            float a = TWO_PI * i / n, r;
            switch (idx) {
            case 0: x[i] = cosf(a); y[i] = sinf(a); break;
            case 1: x[i] = sinf(a); y[i] = sinf(2 * a) * 0.8f; break;
            case 5: x[i] = sinf(3 * a); y[i] = sinf(2 * a + PI / 4); break;
            case 6: x[i] = sinf(5 * a); y[i] = sinf(4 * a + PI / 4); break;
            case 7: r = cosf(3 * a); x[i] = r * cosf(a); y[i] = r * sinf(a); break;
            case 8: r = cosf(5 * a); x[i] = r * cosf(a); y[i] = r * sinf(a); break;
            case 9: {   /* SPIRAL: out in three turns, back in on the same line */
                float u = (float)i / n, w = u < 0.5f ? u * 2 : (1 - u) * 2, b = 6 * PI * w;
                x[i] = (0.1f + 0.9f * w) * cosf(b);
                y[i] = (0.1f + 0.9f * w) * sinf(b);
                break;
            }
            case 10: {   /* HEART */
                float s = sinf(a);
                x[i] = 16 * s * s * s / 17.0f;
                y[i] = -(13 * cosf(a) - 5 * cosf(2 * a) - 2 * cosf(3 * a) - cosf(4 * a)) / 17.0f;
                break;
            }
            case 12: {   /* SPIRO: a hypotrochoid */
                const float R = 5, rr = 3, d = 5;
                x[i] = ((R - rr) * cosf(a * 3) + d * cosf((R - rr) / rr * a * 3)) / 7.0f;
                y[i] = ((R - rr) * sinf(a * 3) - d * sinf((R - rr) / rr * a * 3)) / 7.0f;
                break;
            }
            default: x[i] = cosf(a); y[i] = sinf(a);
            }
        }
    }
    path_resample(x, y, NULL, n, 1, 0, p);
}

void path_project(const path_t *p, float rot, float yaw, float pitch, float *x2, float *y2) {
    float cr = cosf(rot), sr = sinf(rot);
    if (p->is3d) {
        float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
        for (int i = 0; i < PATHN; i++) {
            float x = p->x[i], y = p->y[i], z = p->z[i];
            float x1 = x * cy + z * sy, z1 = -x * sy + z * cy;          /* turn (yaw) */
            float y1 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;         /* tilt (pitch) */
            float persp = 1.0f / (1.0f + 0.25f * z2);                    /* a little perspective */
            float a = x1 * persp, b = y1 * persp;
            x2[i] = a * cr - b * sr;
            y2[i] = a * sr + b * cr;
        }
    } else {
        for (int i = 0; i < PATHN; i++) {
            x2[i] = p->x[i] * cr - p->y[i] * sr;
            y2[i] = p->x[i] * sr + p->y[i] * cr;
        }
    }
    float m = 0;
    for (int i = 0; i < PATHN; i++) {
        if (fabsf(x2[i]) > m) m = fabsf(x2[i]);
        if (fabsf(y2[i]) > m) m = fabsf(y2[i]);
    }
    if (m > 1.0f) {
        float g = 1.0f / m;
        for (int i = 0; i < PATHN; i++) { x2[i] *= g; y2[i] *= g; }
    }
}
