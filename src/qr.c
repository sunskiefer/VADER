/* A QR code (ISO 18004) for a short text: byte mode, error correction level M, versions 1 to 6 (up to 134 bytes), mask
 * chosen by the standard's penalty score. Enough for "http://192.168.100.100:6731/". Public-domain-style minimal
 * implementation; checked against the python 'qrcode' library module for module (test/engine_test.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qr.h"

/* ---- GF(256), the QR polynomial 0x11d */
static unsigned char g_exp[512], g_log[256];
static void gf_init(void) {
    if (g_exp[1]) return;
    unsigned x = 1;
    for (int i = 0; i < 255; i++) {
        g_exp[i] = (unsigned char)x;
        g_log[x] = (unsigned char)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11d;
    }
    for (int i = 255; i < 512; i++) g_exp[i] = g_exp[i - 255];
}
static unsigned char gf_mul(unsigned char a, unsigned char b) { return a && b ? g_exp[g_log[a] + g_log[b]] : 0; }

/* the error-correction codewords of data[n], ec of them (the generator polynomial of degree ec) */
static void rs_encode(const unsigned char *data, int n, int ec, unsigned char *out) {
    unsigned char gen[64];
    memset(gen, 0, sizeof gen);
    gen[0] = 1;
    for (int i = 0; i < ec; i++) {   /* gen *= (x - a^i) */
        for (int j = i + 1; j > 0; j--) gen[j] = gen[j - 1] ^ gf_mul(gen[j], g_exp[i]);
        gen[0] = gf_mul(gen[0], g_exp[i]);
    }
    memset(out, 0, (size_t)ec);
    for (int i = 0; i < n; i++) {
        unsigned char f = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)ec - 1);
        out[ec - 1] = 0;
        if (f) for (int j = 0; j < ec; j++) out[j] ^= gf_mul(gen[ec - 1 - j], f);
    }
}

/* versions 1..6, level H (the most error correction: a picture may cover the middle): total codewords, EC codewords
 * per block, the blocks of the first group and their data codewords, the second group's, the alignment pattern's
 * centre (0: none) */
typedef struct { int total, ec_per_block, blocks1, data1, blocks2, data2, align; } ver_t;
static const ver_t VER[7] = {
    {0},
    {26, 17, 1, 9, 0, 0, 0},
    {44, 28, 1, 16, 0, 0, 18},
    {70, 22, 2, 13, 0, 0, 22},
    {100, 16, 4, 9, 0, 0, 26},
    {134, 22, 2, 11, 2, 12, 30},
    {172, 28, 4, 15, 0, 0, 34},
};

static int size_of(int v) { return 17 + 4 * v; }

/* ---- the matrix: modules in m (1 dark), fn marks function modules */
static void set(unsigned char *m, unsigned char *fn, int n, int x, int y, int v) { m[y * n + x] = (unsigned char)v; fn[y * n + x] = 1; }

static void finder(unsigned char *m, unsigned char *fn, int n, int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= n || y >= n) continue;
            int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, r = d > e ? d : e;
            set(m, fn, n, x, y, r != 2 && r != 4);
        }
}

static void place_function(unsigned char *m, unsigned char *fn, int n, int v) {
    finder(m, fn, n, 3, 3);
    finder(m, fn, n, n - 4, 3);
    finder(m, fn, n, 3, n - 4);
    for (int i = 8; i < n - 8; i++) { set(m, fn, n, i, 6, !(i & 1)); set(m, fn, n, 6, i, !(i & 1)); }   /* timing */
    if (VER[v].align) {   /* one alignment pattern (versions 2..6) */
        int c = VER[v].align;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy, r = d > e ? d : e;
                set(m, fn, n, c + dx, c + dy, r != 1);
            }
    }
    /* the format areas (filled later) and the dark module */
    for (int i = 0; i < 9; i++) { if (i != 6) { set(m, fn, n, 8, i, 0); set(m, fn, n, i, 8, 0); } }
    for (int i = 0; i < 8; i++) { set(m, fn, n, n - 1 - i, 8, 0); set(m, fn, n, 8, n - 1 - i, 0); }
    set(m, fn, n, 8, n - 8, 1);
}

static int mask_bit(int mask, int x, int y) {
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (y / 2 + x / 3) % 2 == 0;
    case 5: return (x * y) % 2 + (x * y) % 3 == 0;
    case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
}

static void place_format(unsigned char *m, int n, int mask) {
    int data = (2 << 3) | mask;   /* level H is 10 */
    int rem = data << 10;
    for (int i = 14; i >= 10; i--) if (rem & (1 << i)) rem ^= 0x537 << (i - 10);
    int bits = ((data << 10) | rem) ^ 0x5412;
    for (int i = 0; i < 15; i++) {
        int b = (bits >> i) & 1;
        int x, y;   /* around the top-left finder: bits 0..5 down column 8, 6 and 7 past the timing row, 8 at (7,8), 9..14 along row 8 leftwards */
        if (i < 6) { x = 8; y = i; } else if (i == 6) { x = 8; y = 7; } else if (i == 7) { x = 8; y = 8; } else if (i == 8) { x = 7; y = 8; } else { x = 14 - i; y = 8; }
        m[y * n + x] = (unsigned char)b;
        /* the other copy: bits 0..7 along row 8 from the right edge, 8..14 down column 8 at the bottom */
        if (i < 8) m[8 * n + (n - 1 - i)] = (unsigned char)b;
        else m[(n - 15 + i) * n + 8] = (unsigned char)b;
    }
}

static long penalty(const unsigned char *m, int n) {
    long p = 0;
    for (int pass = 0; pass < 2; pass++)   /* runs in rows, then columns */
        for (int i = 0; i < n; i++) {
            int run = 1;
            for (int j = 1; j <= n; j++) {
                int cur = j < n ? (pass ? m[j * n + i] : m[i * n + j]) : -1;
                int prev = pass ? m[(j - 1) * n + i] : m[i * n + j - 1];
                if (cur == prev) run++;
                else { if (run >= 5) p += 3 + (run - 5); run = 1; }
            }
        }
    for (int y = 0; y + 1 < n; y++)
        for (int x = 0; x + 1 < n; x++) {
            int a = m[y * n + x];
            if (a == m[y * n + x + 1] && a == m[(y + 1) * n + x] && a == m[(y + 1) * n + x + 1]) p += 3;
        }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++)
            for (int j = 0; j + 11 <= n; j++) {
                static const int pat[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
                int ok1 = 1, ok2 = 1;
                for (int k = 0; k < 11; k++) {
                    int v = pass ? m[(j + k) * n + i] : m[i * n + j + k];
                    if (v != pat[k]) ok1 = 0;
                    if (v != pat[10 - k]) ok2 = 0;
                }
                if (ok1) p += 40;
                if (ok2) p += 40;
            }
    long dark = 0;
    for (int i = 0; i < n * n; i++) dark += m[i];
    long pct = dark * 100 / (n * n), k = pct > 50 ? pct - 50 : 50 - pct;
    p += (k / 5) * 10;
    return p;
}

int qr_capacity(int version) {
    if (version < 1 || version > 6) return 0;
    return VER[version].blocks1 * VER[version].data1 + VER[version].blocks2 * VER[version].data2 - 2;   /* mode + count + data */
}

int qr_encode(const char *text, int version, unsigned char *modules, int *size) {
    gf_init();
    int len = (int)strlen(text);
    int v = version;
    if (!v) for (int i = 1; i <= 6; i++) if (len <= qr_capacity(i)) { v = i; break; }
    if (v < 1 || v > 6 || len > qr_capacity(v)) return -1;
    const ver_t *V = &VER[v];
    int ndata = V->blocks1 * V->data1 + V->blocks2 * V->data2;
    /* the data codewords: mode 0100, an 8-bit count, the bytes, terminator, padding */
    unsigned char data[256];
    memset(data, 0, sizeof data);
    int bitpos = 0;
#define PUT(val, nb) do { for (int b_ = (nb) - 1; b_ >= 0; b_--) { if (((val) >> b_) & 1) data[bitpos >> 3] |= (unsigned char)(0x80 >> (bitpos & 7)); bitpos++; } } while (0)
    PUT(4, 4);
    PUT(len, 8);
    for (int i = 0; i < len; i++) PUT((unsigned char)text[i], 8);
    PUT(0, 4);
#undef PUT
    int nbytes = (bitpos + 7) >> 3;
    for (int i = nbytes, k = 0; i < ndata; i++, k++) data[i] = k & 1 ? 0x11 : 0xec;
    /* the blocks and their EC, interleaved */
    unsigned char ecw[8][32];
    int nb = V->blocks1 + V->blocks2, off = 0;
    const unsigned char *blk[8];
    int blen[8];
    for (int b = 0; b < nb; b++) {
        blen[b] = b < V->blocks1 ? V->data1 : V->data2;
        blk[b] = data + off;
        rs_encode(blk[b], blen[b], V->ec_per_block, ecw[b]);
        off += blen[b];
    }
    unsigned char all[256];
    int na = 0, maxlen = V->data2 > V->data1 ? V->data2 : V->data1;
    for (int i = 0; i < maxlen; i++) for (int b = 0; b < nb; b++) if (i < blen[b]) all[na++] = blk[b][i];
    for (int i = 0; i < V->ec_per_block; i++) for (int b = 0; b < nb; b++) all[na++] = ecw[b][i];
    /* the matrix */
    int n = size_of(v);
    static unsigned char m[41 * 41], fn[41 * 41], best[41 * 41];
    memset(m, 0, sizeof m);
    memset(fn, 0, sizeof fn);
    place_function(m, fn, n, v);
    /* the data bits, in the zigzag, with each mask tried */
    long best_p = -1;
    int best_mask = 0;
    for (int mask = 0; mask < 8; mask++) {
        unsigned char t[41 * 41];
        memcpy(t, m, (size_t)n * n);
        int bi = 0, up = 1;
        for (int x = n - 1; x > 0; x -= 2) {
            if (x == 6) x = 5;
            for (int k = 0; k < n; k++) {
                int y = up ? n - 1 - k : k;
                for (int dx = 0; dx < 2; dx++) {
                    int xx = x - dx;
                    if (fn[y * n + xx]) continue;
                    int bit = bi < na * 8 ? (all[bi >> 3] >> (7 - (bi & 7))) & 1 : 0;
                    bi++;
                    if (mask_bit(mask, xx, y)) bit ^= 1;
                    t[y * n + xx] = (unsigned char)bit;
                }
            }
            up = !up;
        }
        place_format(t, n, mask);
        long p = penalty(t, n);
        if (best_p < 0 || p < best_p) { best_p = p; best_mask = mask; memcpy(best, t, (size_t)n * n); }
    }
    if (getenv("QR_DBG")) fprintf(stderr, "mask %d\n", best_mask);
    memcpy(modules, best, (size_t)n * n);
    *size = n;
    return 0;
}
