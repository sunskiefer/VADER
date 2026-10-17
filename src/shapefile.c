/* Your own orbit shapes: a drawing (SVG), a 3D wireframe (OBJ) or a picture (PNG, JPG) becomes a path for the
 * orbiter to follow.
 *   SVG  path (M L H V C S Q T A Z, absolute and relative), polygon, polyline, line, rect, circle and ellipse;
 *        curves flattened; every part joined in drawing order; transforms are ignored
 *   OBJ  v / f / l lines: every edge, walked as one line (and turned in 3D like CUBE and ICOSA)
 *   PNG  the outline of the biggest shape in the picture (light or dark, whichever is the smaller part), traced
 * Runs on a worker thread (engine.c), never on the audio thread. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "vader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"

#define MAXPTS 16384
#define MAXFILE (4 << 20)

typedef struct {
    float *x, *y, *z;
    int n;
} pts_t;

static void put(pts_t *p, float x, float y, float z) {
    if (p->n >= MAXPTS) return;
    p->x[p->n] = x;
    p->y[p->n] = y;
    p->z[p->n] = z;
    p->n++;
}

static char *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > MAXFILE) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    *len = n;
    return b;
}

/* ------------------------------------------------------------------------------------------------ SVG */
static const char *attr(const char *tag, const char *end, const char *name, int *len) {
    size_t nl = strlen(name);
    for (const char *s = tag; s + nl + 2 < end; s++) {
        if ((s == tag || isspace((unsigned char)s[-1])) && !strncmp(s, name, nl)) {
            const char *q = s + nl;
            while (q < end && isspace((unsigned char)*q)) q++;
            if (*q != '=') continue;
            q++;
            while (q < end && isspace((unsigned char)*q)) q++;
            char quote = *q;
            if (quote != '"' && quote != '\'') continue;
            const char *v = q + 1, *e = memchr(v, quote, (size_t)(end - v));
            if (!e) return NULL;
            *len = (int)(e - v);
            return v;
        }
    }
    return NULL;
}

static float num_attr(const char *tag, const char *end, const char *name, float def) {
    int l;
    const char *v = attr(tag, end, name, &l);
    return v ? strtof(v, NULL) : def;
}

static const char *skip_sep(const char *s, const char *e) {
    while (s < e && (isspace((unsigned char)*s) || *s == ',')) s++;
    return s;
}

static int read_num(const char **s, const char *e, float *out) {
    const char *p = skip_sep(*s, e);
    if (p >= e) return 0;
    if (!(isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')) return 0;
    char *end;
    *out = strtof(p, &end);
    if (end == p) return 0;
    *s = end;
    return 1;
}

static void cubic(pts_t *P, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
    for (int i = 1; i <= 12; i++) {
        float t = i / 12.0f, u = 1 - t;
        put(P, u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3,
            u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3, 0);
    }
}

static void quad(pts_t *P, float x0, float y0, float x1, float y1, float x2, float y2) {
    for (int i = 1; i <= 10; i++) {
        float t = i / 10.0f, u = 1 - t;
        put(P, u * u * x0 + 2 * u * t * x1 + t * t * x2, u * u * y0 + 2 * u * t * y1 + t * t * y2, 0);
    }
}

static void svg_path(pts_t *P, const char *s, const char *e) {
    float cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0;   /* current, subpath start, last control point */
    char cmd = 0, prev = 0;
    while (s < e) {
        s = skip_sep(s, e);
        if (s >= e) break;
        if (isalpha((unsigned char)*s)) cmd = *s++;
        else if (!cmd) break;
        int rel = islower((unsigned char)cmd);
        char C = (char)toupper((unsigned char)cmd);
        float a[7];
        int ok = 1;
        switch (C) {
        case 'M':
            if (!(ok = read_num(&s, e, &a[0]) && read_num(&s, e, &a[1]))) break;
            cx = rel ? cx + a[0] : a[0];
            cy = rel ? cy + a[1] : a[1];
            sx = cx; sy = cy;
            put(P, cx, cy, 0);
            cmd = rel ? 'l' : 'L';   /* further pairs are lines */
            break;
        case 'L':
            if (!(ok = read_num(&s, e, &a[0]) && read_num(&s, e, &a[1]))) break;
            cx = rel ? cx + a[0] : a[0];
            cy = rel ? cy + a[1] : a[1];
            put(P, cx, cy, 0);
            break;
        case 'H':
            if (!(ok = read_num(&s, e, &a[0]))) break;
            cx = rel ? cx + a[0] : a[0];
            put(P, cx, cy, 0);
            break;
        case 'V':
            if (!(ok = read_num(&s, e, &a[0]))) break;
            cy = rel ? cy + a[0] : a[0];
            put(P, cx, cy, 0);
            break;
        case 'C':
            for (int i = 0; i < 6 && ok; i++) ok = read_num(&s, e, &a[i]);
            if (!ok) break;
            if (rel) for (int i = 0; i < 6; i += 2) { a[i] += cx; a[i + 1] += cy; }
            cubic(P, cx, cy, a[0], a[1], a[2], a[3], a[4], a[5]);
            lcx = a[2]; lcy = a[3]; cx = a[4]; cy = a[5];
            break;
        case 'S': {
            for (int i = 0; i < 4 && ok; i++) ok = read_num(&s, e, &a[i]);
            if (!ok) break;
            if (rel) for (int i = 0; i < 4; i += 2) { a[i] += cx; a[i + 1] += cy; }
            int pc = toupper((unsigned char)prev) == 'C' || toupper((unsigned char)prev) == 'S';
            float x1 = pc ? 2 * cx - lcx : cx, y1 = pc ? 2 * cy - lcy : cy;
            cubic(P, cx, cy, x1, y1, a[0], a[1], a[2], a[3]);
            lcx = a[0]; lcy = a[1]; cx = a[2]; cy = a[3];
            break;
        }
        case 'Q':
            for (int i = 0; i < 4 && ok; i++) ok = read_num(&s, e, &a[i]);
            if (!ok) break;
            if (rel) for (int i = 0; i < 4; i += 2) { a[i] += cx; a[i + 1] += cy; }
            quad(P, cx, cy, a[0], a[1], a[2], a[3]);
            lcx = a[0]; lcy = a[1]; cx = a[2]; cy = a[3];
            break;
        case 'T': {
            for (int i = 0; i < 2 && ok; i++) ok = read_num(&s, e, &a[i]);
            if (!ok) break;
            if (rel) { a[0] += cx; a[1] += cy; }
            int pq = toupper((unsigned char)prev) == 'Q' || toupper((unsigned char)prev) == 'T';
            float x1 = pq ? 2 * cx - lcx : cx, y1 = pq ? 2 * cy - lcy : cy;
            quad(P, cx, cy, x1, y1, a[0], a[1]);
            lcx = x1; lcy = y1; cx = a[0]; cy = a[1];
            break;
        }
        case 'A':   /* an arc: its end point, joined with a line (good enough for an orbit) */
            for (int i = 0; i < 7 && ok; i++) ok = read_num(&s, e, &a[i]);
            if (!ok) break;
            cx = rel ? cx + a[5] : a[5];
            cy = rel ? cy + a[6] : a[6];
            put(P, cx, cy, 0);
            break;
        case 'Z':
            cx = sx; cy = sy;
            put(P, cx, cy, 0);
            break;
        default:
            ok = 0;
        }
        prev = cmd;
        if (!ok) {
            /* skip to the next command letter */
            while (s < e && !isalpha((unsigned char)*s)) s++;
        }
    }
}

static void svg_points(pts_t *P, const char *s, const char *e, int close) {
    float x, y, x0 = 0, y0 = 0;
    int first = 1;
    while (read_num(&s, e, &x) && read_num(&s, e, &y)) {
        if (first) { x0 = x; y0 = y; first = 0; }
        put(P, x, y, 0);
    }
    if (close && !first) put(P, x0, y0, 0);
}

static void ellipse(pts_t *P, float cx, float cy, float rx, float ry) {
    for (int i = 0; i <= 48; i++) {
        float a = TWO_PI * i / 48;
        put(P, cx + rx * cosf(a), cy + ry * sinf(a), 0);
    }
}

static int load_svg(const char *buf, long len, pts_t *P) {
    const char *end = buf + len;
    for (const char *s = buf; s < end; s++) {
        if (*s != '<') continue;
        const char *t = s + 1, *te = memchr(t, '>', (size_t)(end - t));
        if (!te) break;
        int l;
        const char *v;
        if (!strncmp(t, "path", 4) && isspace((unsigned char)t[4])) {
            if ((v = attr(t, te, "d", &l))) svg_path(P, v, v + l);
        } else if (!strncmp(t, "polygon", 7) || !strncmp(t, "polyline", 8)) {
            if ((v = attr(t, te, "points", &l))) svg_points(P, v, v + l, t[4] == 'g');
        } else if (!strncmp(t, "line", 4) && isspace((unsigned char)t[4])) {
            put(P, num_attr(t, te, "x1", 0), num_attr(t, te, "y1", 0), 0);
            put(P, num_attr(t, te, "x2", 0), num_attr(t, te, "y2", 0), 0);
        } else if (!strncmp(t, "rect", 4) && isspace((unsigned char)t[4])) {
            float x = num_attr(t, te, "x", 0), y = num_attr(t, te, "y", 0);
            float w = num_attr(t, te, "width", 0), h = num_attr(t, te, "height", 0);
            if (w > 0 && h > 0) {
                put(P, x, y, 0); put(P, x + w, y, 0); put(P, x + w, y + h, 0); put(P, x, y + h, 0); put(P, x, y, 0);
            }
        } else if (!strncmp(t, "circle", 6)) {
            float r = num_attr(t, te, "r", 0);
            if (r > 0) ellipse(P, num_attr(t, te, "cx", 0), num_attr(t, te, "cy", 0), r, r);
        } else if (!strncmp(t, "ellipse", 7)) {
            float rx = num_attr(t, te, "rx", 0), ry = num_attr(t, te, "ry", 0);
            if (rx > 0 && ry > 0) ellipse(P, num_attr(t, te, "cx", 0), num_attr(t, te, "cy", 0), rx, ry);
        }
        s = te;
    }
    return P->n >= 2 ? 0 : -1;
}

/* ------------------------------------------------------------------------------------------------ OBJ */
static int load_obj(char *buf, long len, pts_t *P) {
    (void)len;
    int maxv = 4096, maxe = 8192, nv = 0, ne = 0;
    float (*v)[3] = malloc(sizeof(float) * 3 * (size_t)maxv);
    int (*ed)[2] = malloc(sizeof(int) * 2 * (size_t)maxe);
    int rc = -1;
    if (!v || !ed) goto out;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t') line++;
        if (line[0] == 'v' && line[1] == ' ' && nv < maxv) {
            if (sscanf(line + 2, "%f %f %f", &v[nv][0], &v[nv][1], &v[nv][2]) == 3) nv++;
        } else if ((line[0] == 'f' || line[0] == 'l') && line[1] == ' ') {
            int idx[64], k = 0;
            char *s = line + 2;
            while (*s && k < 64) {
                while (*s == ' ' || *s == '\t' || *s == '\r') s++;
                if (!*s) break;
                char *e;
                long i = strtol(s, &e, 10);
                if (e == s) break;
                idx[k++] = (int)(i < 0 ? nv + i : i - 1);
                s = e;
                while (*s && *s != ' ' && *s != '\t') s++;   /* /vt/vn */
            }
            int edges = line[0] == 'f' ? k : k - 1;
            for (int j = 0; j < edges && ne < maxe; j++) {
                int a = idx[j], b = idx[(j + 1) % k];
                if (a < 0 || b < 0 || a >= nv || b >= nv || a == b) continue;
                int dup = 0;
                for (int q = ne - 1; q >= 0 && q > ne - 64; q--)   /* neighbouring faces share edges */
                    if ((ed[q][0] == a && ed[q][1] == b) || (ed[q][0] == b && ed[q][1] == a)) { dup = 1; break; }
                if (!dup) { ed[ne][0] = a; ed[ne][1] = b; ne++; }
            }
        }
    }
    if (nv < 2 || ne < 1) goto out;
    /* centre on the bounding box, fit in a unit sphere */
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < nv; i++)
        for (int k = 0; k < 3; k++) { if (v[i][k] < lo[k]) lo[k] = v[i][k]; if (v[i][k] > hi[k]) hi[k] = v[i][k]; }
    float r = 0;
    for (int i = 0; i < nv; i++) {
        float d = 0;
        for (int k = 0; k < 3; k++) { v[i][k] -= (lo[k] + hi[k]) / 2; d += v[i][k] * v[i][k]; }
        if (d > r) r = d;
    }
    r = r > 0 ? 1.0f / sqrtf(r) : 1;
    /* walk every edge (depth first, back along the way it went) */
    {
        unsigned char *used = calloc((size_t)ne, 1);
        int *stack = malloc(sizeof(int) * (size_t)(ne + 2));
        if (!used || !stack) { free(used); free(stack); goto out; }
        int sp = 0, start = ed[0][0];
        stack[sp++] = start;
        put(P, v[start][0] * r, -v[start][1] * r, v[start][2] * r);
        int scan = 0;
        while (sp > 0 && P->n < MAXPTS) {
            int at = stack[sp - 1], next = -1;
            for (int q = 0; q < ne; q++) {
                if (used[q]) continue;
                if (ed[q][0] == at) { next = ed[q][1]; used[q] = 1; break; }
                if (ed[q][1] == at) { next = ed[q][0]; used[q] = 1; break; }
            }
            if (next >= 0) {
                stack[sp++] = next;
            } else {
                sp--;
                if (sp == 0) {   /* a separate part: jump to its first unused edge */
                    while (scan < ne && used[scan]) scan++;
                    if (scan < ne) stack[sp++] = ed[scan][0];
                }
                if (sp == 0) break;
            }
            int w = stack[sp - 1];
            put(P, v[w][0] * r, -v[w][1] * r, v[w][2] * r);
        }
        free(used);
        free(stack);
    }
    rc = P->n >= 2 ? 0 : -1;
out:
    free(v);
    free(ed);
    return rc;
}

/* ------------------------------------------------------------------------------------------------ PNG / JPG */
/* the picture kept for the IMAGE page, until the outline is fitted (shape_load_file) */
typedef struct { unsigned char *rgba; int w, h, sc; } picture_t;

/* a colour's place (0..1) on a spectrogram's colour scale: black, deep blue, purple, red, orange, yellow, white.
 * A grey picture reads as its brightness; a colourful one by where its colours sit on the scale. */
static float spectro(float r, float g, float b) {
    static const float C[7][3] = {{0, 0, 0}, {10, 20, 150}, {120, 10, 160}, {220, 30, 60}, {255, 140, 0}, {255, 230, 40}, {255, 255, 255}};
    static const float T[7] = {0, 0.15f, 0.3f, 0.5f, 0.7f, 0.85f, 1};
    float best = 1e30f, bt = 0;
    for (int i = 0; i < 6; i++) {
        float d[3], e[3], dd = 0, de = 0;
        for (int k = 0; k < 3; k++) { d[k] = C[i + 1][k] - C[i][k]; e[k] = (k == 0 ? r : k == 1 ? g : b) - C[i][k]; dd += d[k] * d[k]; de += d[k] * e[k]; }
        float u = dd > 0 ? de / dd : 0;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float q = 0;
        for (int k = 0; k < 3; k++) { float x = e[k] - u * d[k]; q += x * x; }
        if (q < best) { best = q; bt = T[i] + (T[i + 1] - T[i]) * u; }
    }
    return bt;
}

static int load_image(const unsigned char *buf, long len, pts_t *P, char *err, int errlen, picture_t *pic) {
    int w, h, ch;
    unsigned char *img = stbi_load_from_memory(buf, (int)len, &w, &h, &ch, 4);
    if (!img) { snprintf(err, (size_t)errlen, "can't read the picture"); return -1; }
    /* to at most 160 px across, as luminance (transparent = background) */
    int sc = (w > h ? w : h) > 160 ? ((w > h ? w : h) + 159) / 160 : 1;
    int W = w / sc, H = h / sc;
    if (W < 4 || H < 4) { stbi_image_free(img); snprintf(err, (size_t)errlen, "picture too small"); return -1; }
    float *lum = malloc(sizeof(float) * (size_t)(W * H));
    unsigned char *fg = calloc((size_t)((W + 2) * (H + 2)), 1);
    int *lab = calloc((size_t)((W + 2) * (H + 2)), sizeof(int));
    int *queue = malloc(sizeof(int) * (size_t)((W + 2) * (H + 2)));
    int rc = -1;
    if (!lum || !fg || !lab || !queue) goto out;
    int hist[256] = {0};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float s = 0;
            for (int j = 0; j < sc; j++)
                for (int i = 0; i < sc; i++) {
                    const unsigned char *p = img + 4 * ((y * sc + j) * w + x * sc + i);
                    float al = p[3] / 255.0f;
                    s += (0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]) * al + 255.0f * (1 - al);
                }
            float L = s / (float)(sc * sc);
            lum[y * W + x] = L;
            hist[(int)L > 255 ? 255 : (int)L]++;
        }
    /* Otsu's threshold */
    int total = W * H;
    double sum = 0, sumB = 0, best = -1;
    int wB = 0, thr = 128;
    for (int i = 0; i < 256; i++) sum += (double)i * hist[i];
    for (int i = 0; i < 256; i++) {
        wB += hist[i];
        if (!wB) continue;
        int wF = total - wB;
        if (!wF) break;
        sumB += (double)i * hist[i];
        double mB = sumB / wB, mF = (sum - sumB) / wF, between = (double)wB * wF * (mB - mF) * (mB - mF);
        if (between > best) { best = between; thr = i; }
    }
    int dark = 0;
    for (int i = 0; i < total; i++) dark += lum[i] <= thr;
    int fg_dark = dark <= total - dark;   /* the shape is the smaller part */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            fg[(y + 1) * (W + 2) + x + 1] = fg_dark ? lum[y * W + x] <= thr : lum[y * W + x] > thr;
    /* the biggest connected part */
    int W2 = W + 2, H2 = H + 2, nlab = 0, bestlab = 0, bestsz = 0;
    for (int i = 0; i < W2 * H2; i++) {
        if (!fg[i] || lab[i]) continue;
        nlab++;
        int qh = 0, qt = 0, sz = 0;
        queue[qt++] = i;
        lab[i] = nlab;
        while (qh < qt) {
            int c = queue[qh++];
            sz++;
            int nb[4] = {c - 1, c + 1, c - W2, c + W2};
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && nb[k] < W2 * H2 && fg[nb[k]] && !lab[nb[k]]) { lab[nb[k]] = nlab; queue[qt++] = nb[k]; }
        }
        if (sz > bestsz) { bestsz = sz; bestlab = nlab; }
    }
    if (bestsz < 6) { snprintf(err, (size_t)errlen, "no shape found in the picture"); goto out; }
    /* its outline, traced (Moore neighbourhood) from its top-left pixel */
    int start = -1;
    for (int i = 0; i < W2 * H2 && start < 0; i++)
        if (lab[i] == bestlab) start = i;
    /* Moore-neighbour tracing, clockwise (on screen, y down), with Jacob's stop: done when the start is entered
     * again from the same side. bd: the direction from the current pixel to the one checked just before it. */
    static const int dx[8] = {-1, -1, 0, 1, 1, 1, 0, -1}, dy[8] = {0, -1, -1, -1, 0, 1, 1, 1};
    int cur = start, bd = 0, steps = 0;   /* the start is the region's first pixel in reading order: west of it is empty */
    do {
        put(P, (float)(cur % W2), (float)(cur / W2), 0);
        int found = 0;
        for (int k = 1; k <= 8; k++) {
            int d = (bd + k) % 8;
            int nx = cur % W2 + dx[d], ny = cur / W2 + dy[d];
            if (nx < 0 || ny < 0 || nx >= W2 || ny >= H2) continue;
            int ni = ny * W2 + nx;
            if (lab[ni] == bestlab) {
                int pd = (d + 7) % 8;   /* the pixel checked before it, seen from the new one */
                int bx = cur % W2 + dx[pd] - nx, by = cur / W2 + dy[pd] - ny;
                for (int q = 0; q < 8; q++)
                    if (dx[q] == bx && dy[q] == by) { bd = q; break; }
                cur = ni;
                found = 1;
                break;
            }
        }
        if (!found) break;   /* a single pixel */
    } while (!(cur == start && bd == 0) && ++steps < MAXPTS);
    /* the traced pixels make a staircase: smooth it (three passes of a 1-2-1 average round the closed outline) */
    if (P->n >= 8) {
        float *tx = malloc(sizeof(float) * (size_t)P->n), *ty = malloc(sizeof(float) * (size_t)P->n);
        if (tx && ty)
            for (int pass = 0; pass < 3; pass++) {
                for (int i = 0; i < P->n; i++) {
                    int a = (i + P->n - 1) % P->n, b = (i + 1) % P->n;
                    tx[i] = 0.25f * P->x[a] + 0.5f * P->x[i] + 0.25f * P->x[b];
                    ty[i] = 0.25f * P->y[a] + 0.5f * P->y[i] + 0.25f * P->y[b];
                }
                memcpy(P->x, tx, sizeof(float) * (size_t)P->n);
                memcpy(P->y, ty, sizeof(float) * (size_t)P->n);
            }
        free(tx);
        free(ty);
    }
    rc = P->n >= 4 ? 0 : -1;
    if (rc) snprintf(err, (size_t)errlen, "outline too short");
out:
    free(lum);
    free(fg);
    free(lab);
    free(queue);
    if (!rc && pic) { pic->rgba = img; pic->w = w; pic->h = h; pic->sc = sc; }   /* kept for the IMAGE page */
    else stbi_image_free(img);
    return rc;
}

static int ends_with(const char *s, const char *ext) {
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !strcasecmp(s + a - b, ext);
}

int shape_load_file(const char *path, path_t *out, image_t *imgout, char *err, int errlen) {
    long len = 0;
    char *buf = slurp(path, &len);
    err[0] = 0;
    if (!buf) { snprintf(err, (size_t)errlen, "can't open the file"); return -1; }
    pts_t P;
    P.x = malloc(sizeof(float) * MAXPTS);
    P.y = malloc(sizeof(float) * MAXPTS);
    P.z = malloc(sizeof(float) * MAXPTS);
    P.n = 0;
    int rc = -1, is3d = 0, closed = 1;
    picture_t pic = {0};
    if (!P.x || !P.y || !P.z) goto out;
    if (ends_with(path, ".svg")) rc = load_svg(buf, len, &P);
    else if (ends_with(path, ".obj")) { rc = load_obj(buf, len, &P); is3d = 1; closed = 0; }
    else if (ends_with(path, ".png") || ends_with(path, ".jpg") || ends_with(path, ".jpeg"))
        rc = load_image((const unsigned char *)buf, len, &P, err, errlen, &pic);
    else snprintf(err, (size_t)errlen, "not an SVG, OBJ, PNG or JPG");
    if (rc) {
        if (!err[0]) snprintf(err, (size_t)errlen, "no lines found in the file");
        goto out;
    }
    if (!is3d) {   /* centre and fit in -1..1, keeping its proportions */
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (int i = 0; i < P.n; i++) {
            if (P.x[i] < x0) x0 = P.x[i];
            if (P.x[i] > x1) x1 = P.x[i];
            if (P.y[i] < y0) y0 = P.y[i];
            if (P.y[i] > y1) y1 = P.y[i];
        }
        float s = (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) / 2;
        if (s <= 0) s = 1;
        for (int i = 0; i < P.n; i++) {
            P.x[i] = (P.x[i] - (x0 + x1) / 2) / s;
            P.y[i] = (P.y[i] - (y0 + y1) / 2) / s;
        }
        if (pic.rgba && imgout) {   /* the picture, laid under the outline: IMG_N x IMG_N over -1..1 */
            for (int gy = 0; gy < IMG_N; gy++)
                for (int gx = 0; gx < IMG_N; gx++) {
                    float u = -1 + (gx + 0.5f) * 2.0f / IMG_N, v = -1 + (gy + 0.5f) * 2.0f / IMG_N;
                    float px = ((x0 + x1) / 2 + u * s - 1 + 0.5f) * pic.sc, py = ((y0 + y1) / 2 + v * s - 1 + 0.5f) * pic.sc;
                    int ix = (int)px, iy = (int)py;
                    float val = 0, hue = 0, sat = 0;
                    if (ix >= 0 && iy >= 0 && ix < pic.w && iy < pic.h) {
                        const unsigned char *q = pic.rgba + 4 * (iy * pic.w + ix);
                        float al = q[3] / 255.0f, r = q[0] / 255.0f, g = q[1] / 255.0f, b = q[2] / 255.0f;
                        val = spectro(q[0], q[1], q[2]) * al;
                        float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), c = mx - mn;
                        sat = mx > 0 ? c / mx * al : 0;   /* how colourful */
                        if (c > 1e-4f) {                  /* where round the colour wheel: 0 red .. 1 back to red */
                            float hh = mx == r ? fmodf((g - b) / c + 6, 6) : mx == g ? (b - r) / c + 2 : (r - g) / c + 4;
                            hue = hh / 6;
                        }
                    }
                    imgout->v[gy * IMG_N + gx] = val;
                    imgout->hue[gy * IMG_N + gx] = hue;
                    imgout->sat[gy * IMG_N + gx] = sat;
                }
            imgout->ok = 1;
        }
    }
    path_resample(P.x, P.y, P.z, P.n, closed, is3d, out);
    {
        const char *b = strrchr(path, '/');
        snprintf(out->name, sizeof out->name, "%s", b ? b + 1 : path);
    }
out:
    if (pic.rgba) stbi_image_free(pic.rgba);
    free(P.x);
    free(P.y);
    free(P.z);
    free(buf);
    return rc;
}
