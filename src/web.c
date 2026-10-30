/* VADER's companion screen (0.6.0): a small web server inside the plugin. A phone, tablet or laptop on the same
 * network opens http://<the Force's address>:6731/ and gets a page that draws the screen (the shape, the orbit line,
 * the orbiter with its trail, the balls) in its own browser, full screen if wanted; the Force draws nothing. The page
 * is sent once; after that the plugin streams what moves as small binary WebSocket messages, 30 a second, and the
 * shape when it changes. Up to 8 watchers at once. The plugin also answers for the name "vader.local" (mDNS), so Apple
 * devices can type that instead of an address, and tells the engine the address to show (and to put in the QR code).
 * No library: HTTP, the WebSocket handshake (SHA-1 + base64) and framing, and the mDNS answer are all here.
 *
 * PLUG: a desktop VADER (desktop/mirror.cpp) can connect to the path /mirror of the same port and become a mirror of this
 * instance: it is sent the whole state as text, then every parameter change as it happens, the shape, and the screen's data
 * at 60 a second; what it changes comes back as messages the engine applies between blocks (engine.c, mirror_tick).
 * Messages, all binary WebSocket frames, numbers little-endian:
 *   to the desktop   0x30 hello   proto u32, parameter count u32, schema u32, id u8, version length u8, version text
 *                    0x31 state   the state text (engine "state"), the deltas after it apply on top
 *                    0x32 deltas  count u16, then count x (idx u16, value f32); idx is the engine's parameter index
 *                    0x02 shape   as for the phone page
 *                    0x21 view    as the phone page's 0x01, then the IMAGE meters (4 f32) and the page counter (f32)
 *                    0x33 readout the top line's glow (f32) and its text (the control last touched on the Force)
 *   to the Force     0x40 hello   proto u32, parameter count u32, schema u32
 *                    0x41 set     idx u16, value f32
 *                    0x42 fire    idx u16 (a button)
 *                    0x43 again   send the whole state again */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "qr.h"
#include "vader_params.h"
#include "web.h"
#include "mirror_schema.h"
#include "web_assets.h"   /* WEB_INDEX_HTML, WEB_ICON_PNG (tools/gen_web.py) */

#define WEB_PORT 6731
#define WEB_PORTS 9           /* 6731..6739: the first free one */
#define MAXC 8
#define FPS 60                /* the loop's frame rate: the phone page gets every second one (30), a plugged desktop every one */
#include "version.h"

/* ------------------------------------------------------------------------------------------------ SHA-1, base64 */
static void sha1(const unsigned char *msg, size_t len, unsigned char out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    size_t total = ((len + 8) / 64 + 1) * 64;
    unsigned char *buf = calloc(total, 1);
    if (!buf) return;
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) buf[total - 1 - i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)buf[off + i * 4] << 24 | (uint32_t)buf[off + i * 4 + 1] << 16 | (uint32_t)buf[off + i * 4 + 2] << 8 | buf[off + i * 4 + 3];
        for (int i = 16; i < 80; i++) { uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = (x << 1) | (x >> 31); }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    free(buf);
    for (int i = 0; i < 5; i++) { out[i * 4] = (unsigned char)(h[i] >> 24); out[i * 4 + 1] = (unsigned char)(h[i] >> 16); out[i * 4 + 2] = (unsigned char)(h[i] >> 8); out[i * 4 + 3] = (unsigned char)h[i]; }
}

static void base64(const unsigned char *in, size_t n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    out[o] = 0;
}

/* ------------------------------------------------------------------------------------------------ the address */
/* the first address of a network interface that is up and not the loopback, wired or wireless */
static int my_address(char *out, int len) {
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs)) return 0;
    int found = 0;
    for (int pass = 0; pass < 2 && !found; pass++)
        for (struct ifaddrs *i = ifs; i && !found; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            if (pass == 0 && strncmp(i->ifa_name, "wl", 2) && strncmp(i->ifa_name, "eth", 3) && strncmp(i->ifa_name, "en", 2) && strncmp(i->ifa_name, "usb", 3)) continue;
            struct sockaddr_in *sa = (struct sockaddr_in *)i->ifa_addr;
            if (inet_ntop(AF_INET, &sa->sin_addr, out, (socklen_t)len)) found = 1;
        }
    freeifaddrs(ifs);
    return found;
}

/* ------------------------------------------------------------------------------------------------ mDNS */
/* answers "vader.local" (type A) on the multicast group; one answer per query */
static int mdns_open(void) {
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(5353), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
    struct ip_mreq mr = {.imr_multiaddr.s_addr = inet_addr("224.0.0.251"), .imr_interface.s_addr = htonl(INADDR_ANY)};
    setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr);
    unsigned char ttl = 255;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    return s;
}

static void mdns_serve(int s, const char *ip) {
    unsigned char q[1500];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(s, q, sizeof q, 0, (struct sockaddr *)&from, &fl);
    if (n < 12 || (q[2] & 0x80)) return;   /* not a query */
    int qd = q[4] << 8 | q[5];
    size_t p = 12;
    for (int k = 0; k < qd && p < (size_t)n; k++) {
        char name[256];
        size_t nl = 0, pp = p;
        while (pp < (size_t)n && q[pp]) {
            int l = q[pp];
            if (l & 0xc0) { pp += 2; break; }
            if (nl + l + 1 >= sizeof name) return;
            if (nl) name[nl++] = '.';
            memcpy(name + nl, q + pp + 1, (size_t)l);
            nl += (size_t)l;
            pp += (size_t)l + 1;
        }
        name[nl] = 0;
        if (pp < (size_t)n && !q[pp]) pp++;
        if (pp + 4 > (size_t)n) return;
        int type = q[pp] << 8 | q[pp + 1];
        p = pp + 4;
        if (strcasecmp(name, "vader.local") || (type != 1 && type != 255)) continue;
        /* the answer: a response, authoritative, one answer: vader.local A ip, ttl 120, cache-flush */
        unsigned char r[64];
        int o = 0;
        r[o++] = 0; r[o++] = 0;         /* id 0 (multicast) */
        r[o++] = 0x84; r[o++] = 0;      /* response, authoritative */
        r[o++] = 0; r[o++] = 0;         /* questions */
        r[o++] = 0; r[o++] = 1;         /* answers */
        r[o++] = 0; r[o++] = 0; r[o++] = 0; r[o++] = 0;
        r[o++] = 5; memcpy(r + o, "vader", 5); o += 5;
        r[o++] = 5; memcpy(r + o, "local", 5); o += 5;
        r[o++] = 0;
        r[o++] = 0; r[o++] = 1;         /* A */
        r[o++] = 0x80; r[o++] = 1;      /* IN, cache flush */
        r[o++] = 0; r[o++] = 0; r[o++] = 0; r[o++] = 120;
        r[o++] = 0; r[o++] = 4;
        struct in_addr ia;
        inet_pton(AF_INET, ip, &ia);
        memcpy(r + o, &ia, 4); o += 4;
        struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(5353), .sin_addr.s_addr = inet_addr("224.0.0.251")};
        sendto(s, r, (size_t)o, 0, (struct sockaddr *)&to, sizeof to);
        sendto(s, r, (size_t)o, 0, (struct sockaddr *)&from, fl);   /* and straight back to the asker (a desktop VADER looking for us) */
        return;
    }
}

/* ------------------------------------------------------------------------------------------------ PLUG: finding the Force */
/* a UDP socket on the same number as the web port: a desktop VADER broadcasts "VADER PLUG?" and is answered "VADER <port> <version>" */
static int disco_open(int port) {
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(s, (struct sockaddr *)&a, sizeof a)) { close(s); return -1; }
    return s;
}
static void disco_serve(int s, int port) {
    char q[64];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    ssize_t n = recvfrom(s, q, sizeof q - 1, 0, (struct sockaddr *)&from, &fl);
    if (n < 11) return;
    q[n] = 0;
    if (strncmp(q, "VADER PLUG?", 11)) return;
    char r[64];
    int rl = snprintf(r, sizeof r, "VADER %d %s", port, VADER_VERSION_STR);
    sendto(s, r, (size_t)rl, 0, (struct sockaddr *)&from, fl);
}

/* ------------------------------------------------------------------------------------------------ clients */
typedef struct {
    int fd;
    int ws;             /* 1: a WebSocket, streaming */
    char in[2048];
    int inlen;
    unsigned shape_seen;
    int mirror, id, hello_ok;     /* PLUG: a desktop VADER; its slot number; it said hello with the same parameters */
    unsigned cursor, state_seen, rd_seen;  /* PLUG: the next ring entry to send; the state and readout versions it has */
    time_t since;
    /* an upload in progress (POST /upload): the file being written, what is still to come */
    FILE *up;
    long up_left;
    char up_tmp[1100], up_final[1100];
} client_t;

/* a safe file name from the upload's: letters, digits, dot, dash, underscore; a known shape type */
static int safe_name(const char *in, char *out, int len) {
    int n = 0;
    const char *base = strrchr(in, '/');
    base = base ? base + 1 : in;
    for (const char *c = base; *c && n < len - 1; c++) {
        unsigned char ch = (unsigned char)*c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_') out[n++] = (char)ch;
        else if (ch == ' ') out[n++] = '_';
    }
    out[n] = 0;
    if (!n || out[0] == '.') return 0;
    const char *dot = strrchr(out, '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".svg") || !strcasecmp(dot, ".obj");
}

static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && s[1] && s[2]) { char h[3] = {s[1], s[2], 0}; *o++ = (char)strtol(h, NULL, 16); s += 2; }
        else if (*s == '+') *o++ = ' ';
        else *o++ = *s;
    }
    *o = 0;
}

static int send_all(int fd, const void *p, size_t n) {
    const unsigned char *b = p;
    while (n) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EINTR) continue; if (errno == EAGAIN) { struct pollfd pf = {fd, POLLOUT, 0}; poll(&pf, 1, 200); continue; } return -1; }
        b += w; n -= (size_t)w;
    }
    return 0;
}

static int ws_send(int fd, const void *payload, size_t n, int text) {
    unsigned char h[10];
    int hl = 0;
    h[hl++] = (unsigned char)(0x80 | (text ? 1 : 2));
    if (n < 126) h[hl++] = (unsigned char)n;
    else if (n < 65536) { h[hl++] = 126; h[hl++] = (unsigned char)(n >> 8); h[hl++] = (unsigned char)n; }
    else { h[hl++] = 127; for (int i = 7; i >= 0; i--) h[hl++] = (unsigned char)((uint64_t)n >> (8 * i)); }
    if (send_all(fd, h, (size_t)hl)) return -1;
    return send_all(fd, payload, n);
}

/* the parameters' keys hashed: a desktop plugin of another build (other parameters) must not be mirrored */
unsigned web_schema(void) { return mirror_schema(); }

static void client_drop(web_t *w, client_t *c) {
    if (c->fd < 0) return;
    if (c->ws) __atomic_sub_fetch(&w->watchers, 1, __ATOMIC_RELAXED);
    if (c->mirror) __atomic_sub_fetch(&w->mirror_n, 1, __ATOMIC_RELAXED);
    if (c->up) { fclose(c->up); c->up = NULL; unlink(c->up_tmp); }
    close(c->fd);
    c->fd = -1;
}

static void http_reply(int fd, const char *status, const char *type, const void *body, size_t n, int cache) {
    char h[256];
    int hl = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n%s\r\n",
                      status, type, n, cache ? "Cache-Control: max-age=86400\r\n" : "Cache-Control: no-store\r\n");
    send_all(fd, h, (size_t)hl);
    send_all(fd, body, n);
}

/* one HTTP request, complete in c->in: a page, the icon, or the WebSocket upgrade. 1: now a WebSocket; 0: done (close) */
static int handle_http(web_t *w, client_t *c) {
    char *line_end = strstr(c->in, "\r\n");
    if (!line_end) return 0;
    *line_end = 0;
    char method[8], path[256];
    if (sscanf(c->in, "%7s %255s", method, path) != 2) return 0;
    *line_end = '\r';
    char *key = strcasestr(c->in, "Sec-WebSocket-Key:");
    if ((!strcmp(path, "/ws") || !strcmp(path, "/mirror")) && key) {
        key += 18;
        while (*key == ' ') key++;
        char k[128];
        int kl = 0;
        while (*key && *key != '\r' && kl < 100) k[kl++] = *key++;
        k[kl] = 0;
        strcat(k, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        unsigned char dig[20];
        sha1((unsigned char *)k, strlen(k), dig);
        char acc[64];
        base64(dig, 20, acc);
        char h[256];
        int hl = snprintf(h, sizeof h, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
        if (send_all(c->fd, h, (size_t)hl)) return 0;
        int one = 1;
        setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        c->ws = 1;
        c->shape_seen = ~0u;
        c->mirror = !strcmp(path, "/mirror");
        __atomic_add_fetch(&w->watchers, 1, __ATOMIC_RELAXED);
        if (c->mirror) __atomic_add_fetch(&w->mirror_n, 1, __ATOMIC_RELAXED);
        return 1;
    }
    if (!strncmp(path, "/upload?", 8) && !strcmp(method, "POST")) {
        /* the body is the file; it goes into the Shapes folder, then the engine loads it */
        char name[256] = "", safe[200];
        const char *q = strstr(path, "name=");
        if (q) { snprintf(name, sizeof name, "%s", q + 5); char *amp = strchr(name, '&'); if (amp) *amp = 0; url_decode(name); }
        const char *cl = strcasestr(c->in, "Content-Length:");
        long len = cl ? atol(cl + 15) : -1;
        if (!w->shapes_dir[0]) { http_reply(c->fd, "503 Service Unavailable", "text/plain", "no shapes folder", 16, 0); return 0; }
        if (!safe_name(name, safe, sizeof safe)) { http_reply(c->fd, "415 Unsupported Media Type", "text/plain", "PNG, JPG, SVG or OBJ", 20, 0); return 0; }
        if (len <= 0 || len > 16 * 1024 * 1024) { http_reply(c->fd, "413 Payload Too Large", "text/plain", "up to 16 MB", 11, 0); return 0; }
        snprintf(c->up_final, sizeof c->up_final, "%s/%s", w->shapes_dir, safe);
        snprintf(c->up_tmp, sizeof c->up_tmp, "%s/.upload-%d.tmp", w->shapes_dir, c->fd);
        c->up = fopen(c->up_tmp, "wb");
        if (!c->up) { http_reply(c->fd, "500 Internal Server Error", "text/plain", "can't write", 11, 0); return 0; }
        c->up_left = len;
        /* what came with the headers already */
        char *body = strstr(c->in, "\r\n\r\n") + 4;
        int have = c->inlen - (int)(body - c->in);
        if (have > 0) { fwrite(body, 1, (size_t)have, c->up); c->up_left -= have; }
        c->inlen = 0;
        return 2;
    }
    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) http_reply(c->fd, "200 OK", "text/html; charset=utf-8", WEB_INDEX_HTML, sizeof WEB_INDEX_HTML - 1, 0);
    else if (!strcmp(path, "/icon.png")) http_reply(c->fd, "200 OK", "image/png", WEB_ICON_PNG, sizeof WEB_ICON_PNG, 1);
    else if (!strcmp(path, "/manifest.json")) {
        static const char man[] = "{\"name\":\"VADER\",\"short_name\":\"VADER\",\"start_url\":\"/\",\"display\":\"fullscreen\",\"orientation\":\"landscape\",\"background_color\":\"#06080f\",\"theme_color\":\"#06080f\",\"icons\":[{\"src\":\"/icon.png\",\"sizes\":\"192x192\",\"type\":\"image/png\"}]}";
        http_reply(c->fd, "200 OK", "application/manifest+json", man, sizeof man - 1, 1);
    } else http_reply(c->fd, "404 Not Found", "text/plain", "no", 2, 0);
    return 0;
}

/* the upload's bytes as they come; done: the file is renamed into place and handed to the engine */
static int upload_more(web_t *w, client_t *c, const char *data, int n) {
    if (n > 0) { fwrite(data, 1, (size_t)n, c->up); c->up_left -= n; }
    if (c->up_left > 0) return 1;
    fclose(c->up);
    c->up = NULL;
    if (rename(c->up_tmp, c->up_final)) { unlink(c->up_tmp); http_reply(c->fd, "500 Internal Server Error", "text/plain", "can't save", 10, 0); return 0; }
    pthread_mutex_lock(&w->lock);
    snprintf(w->load_path, sizeof w->load_path, "%s", c->up_final);
    w->load_ver++;
    pthread_mutex_unlock(&w->lock);
    http_reply(c->fd, "200 OK", "text/plain", "loaded", 6, 0);
    return 0;
}

/* a message from a plugged desktop: into the engine's inbox (it applies them between blocks). 0: fine; -1: drop the client */
static int mirror_msg(web_t *w, client_t *c, const unsigned char *m, int n) {
    if (n < 1) return 0;
    if (m[0] == 0x40) {
        uint32_t pr, np, sc;
        if (n < 13) return -1;
        memcpy(&pr, m + 1, 4); memcpy(&np, m + 5, 4); memcpy(&sc, m + 9, 4);
        if (pr != MIR_PROTO || np != (uint32_t)NP || sc != web_schema()) return -1;   /* another build: not mirrored */
        c->hello_ok = 1;
        pthread_mutex_lock(&w->lock);
        c->state_seen = w->mir_state_ver;
        pthread_mutex_unlock(&w->lock);
        __atomic_store_n(&w->mir_want_full, 1, __ATOMIC_RELEASE);
        return 0;
    }
    if (!c->hello_ok) return 0;
    mir_msg_t q;
    memset(&q, 0, sizeof q);
    q.client = (unsigned char)c->id;
    if (m[0] == 0x41 && n >= 7) { q.type = 1; memcpy(&q.idx, m + 1, 2); memcpy(&q.val, m + 3, 4); }
    else if (m[0] == 0x42 && n >= 3) { q.type = 2; memcpy(&q.idx, m + 1, 2); }
    else if (m[0] == 0x43) { q.type = 3; }
    else return 0;
    pthread_mutex_lock(&w->lock);
    if (w->in_head - w->in_tail < MIR_INBOX) w->inbox[w->in_head++ % MIR_INBOX] = q;
    pthread_mutex_unlock(&w->lock);
    return 0;
}

/* a WebSocket message from the browser: close and ping are answered, a plugged desktop's messages go to the engine, the rest ignored */
static int ws_input(web_t *w, client_t *c) {
    while (c->inlen >= 2) {
        unsigned char op = c->in[0] & 0x0f;
        int masked = (c->in[1] & 0x80) != 0;
        size_t len = (unsigned char)c->in[1] & 0x7f, hl = 2;
        if (len == 126) { if (c->inlen < 4) return 0; len = (size_t)(unsigned char)c->in[2] << 8 | (unsigned char)c->in[3]; hl = 4; }
        else if (len == 127) return -1;   /* too long for us */
        if (masked) hl += 4;
        if ((size_t)c->inlen < hl + len) { if (hl + len > sizeof c->in - 1) return -1; return 0; }
        if (op == 8) return -1;   /* close */
        if (op == 9) {            /* ping -> pong, unmasked payload */
            unsigned char p[128];
            size_t n = len > sizeof p ? sizeof p : len;
            for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c->in[hl + i] ^ (masked ? (unsigned char)c->in[hl - 4 + (i & 3)] : 0);
            unsigned char h[2] = {0x8a, (unsigned char)n};
            send_all(c->fd, h, 2);
            send_all(c->fd, p, n);
        }
        if (op == 2 && c->mirror && len <= 64) {
            unsigned char p[64];
            for (size_t i = 0; i < len; i++) p[i] = (unsigned char)c->in[hl + i] ^ (masked ? (unsigned char)c->in[hl - 4 + (i & 3)] : 0);
            if (mirror_msg(w, c, p, (int)len)) return -1;
        }
        memmove(c->in, c->in + hl + len, (size_t)c->inlen - hl - len);
        c->inlen -= (int)(hl + len);
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the thread */
static void put_f(unsigned char **p, float f) { memcpy(*p, &f, 4); *p += 4; }
static void put_u32(unsigned char **p, uint32_t v) { memcpy(*p, &v, 4); *p += 4; }

static void *web_thread(void *arg) {
    web_t *w = arg;
    int ls = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (ls < 0) { pthread_mutex_lock(&w->lock); snprintf(w->status, sizeof w->status, "NO SOCKET"); w->url_ver++; pthread_mutex_unlock(&w->lock); return NULL; }
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    int bound = 0;
    for (int k = 0; k < WEB_PORTS && !bound; k++) {   /* 6731, or the next free port when another program has it */
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)(WEB_PORT + k)), .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (!bind(ls, (struct sockaddr *)&a, sizeof a) && !listen(ls, 8)) { bound = 1; w->port = WEB_PORT + k; }
        else { close(ls); ls = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); if (ls < 0) break; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); }
    }
    if (!bound) {
        pthread_mutex_lock(&w->lock);
        snprintf(w->status, sizeof w->status, "PORT %d BUSY", WEB_PORT);
        w->url_ver++;
        pthread_mutex_unlock(&w->lock);
        if (ls >= 0) close(ls);
        return NULL;
    }
    int ms = mdns_open();
    int ds = disco_open(w->port);
    client_t cl[MAXC];
    memset(cl, 0, sizeof cl);
    for (int i = 0; i < MAXC; i++) cl[i].fd = -1;
    char ip[64] = "";
    time_t ip_at = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    unsigned char msg[8192];
    unsigned char *stbuf = malloc(MIR_STATE_MAX + 8);   /* a copy of the state to send without holding the lock */
    unsigned frame_no = 0;
    if (!stbuf) { close(ls); return NULL; }
    while (!w->stop) {
        /* the address, looked up every 5 s (a network may come or go) */
        time_t now = time(NULL);
        if (now - ip_at >= 5) {
            ip_at = now;
            char nip[64] = "";
            if (my_address(nip, sizeof nip)) {
                if (strcmp(nip, ip)) {
                    snprintf(ip, sizeof ip, "%s", nip);
                    char url[96];
                    snprintf(url, sizeof url, "http://%s:%d/", ip, w->port);
                    /* the QR code of it, as bytes of 8 modules per row (the skin's grid) */
                    unsigned char mods[41 * 41], qb[QR_N * QR_BYTES];
                    int n = 0;
                    memset(qb, 0, sizeof qb);
                    if (qr_encode(url, QR_VER, mods, &n) == 0 && n == QR_N)
                        for (int y = 0; y < n; y++)
                            for (int x = 0; x < n; x++)
                                if (mods[y * n + x]) qb[y * QR_BYTES + x / 8] |= (unsigned char)(0x80 >> (x & 7));
                    pthread_mutex_lock(&w->lock);
                    snprintf(w->url, sizeof w->url, "%s", url);
                    memcpy(w->qr, qb, sizeof qb);
                    snprintf(w->status, sizeof w->status, "%s", url);
                    w->url_ver++;
                    pthread_mutex_unlock(&w->lock);
                }
            } else if (ip[0] || !w->url_ver) {
                ip[0] = 0;
                pthread_mutex_lock(&w->lock);
                w->url[0] = 0;
                memset(w->qr, 0, sizeof w->qr);
                snprintf(w->status, sizeof w->status, "NO NETWORK");
                w->url_ver++;
                pthread_mutex_unlock(&w->lock);
            }
        }
        /* new connections, requests, mDNS */
        struct pollfd pf[MAXC + 3];
        int np = 0, msi = -1, dsi = -1;
        pf[np++] = (struct pollfd){ls, POLLIN, 0};
        if (ms >= 0) { msi = np; pf[np++] = (struct pollfd){ms, POLLIN, 0}; }
        if (ds >= 0) { dsi = np; pf[np++] = (struct pollfd){ds, POLLIN, 0}; }
        for (int i = 0; i < MAXC; i++) if (cl[i].fd >= 0) pf[np++] = (struct pollfd){cl[i].fd, POLLIN, 0};
        struct timespec nowts;
        clock_gettime(CLOCK_MONOTONIC, &nowts);
        long wait_ms = (next.tv_sec - nowts.tv_sec) * 1000 + (next.tv_nsec - nowts.tv_nsec) / 1000000;
        /* with a desktop plugged in the loop wakes every 2 ms (and at once when it sends something): changes go out at once */
        long cap = __atomic_load_n(&w->mirror_n, __ATOMIC_RELAXED) > 0 ? 2 : 100;
        poll(pf, (nfds_t)np, wait_ms < 0 ? 0 : wait_ms > cap ? (int)cap : (int)wait_ms);
        if (pf[0].revents & POLLIN) {
            int fd = accept4(ls, NULL, NULL, SOCK_NONBLOCK);
            if (fd >= 0) {
                int k = -1;
                for (int i = 0; i < MAXC; i++) if (cl[i].fd < 0) { k = i; break; }
                if (k < 0) close(fd);
                else { memset(&cl[k], 0, sizeof cl[k]); cl[k].fd = fd; cl[k].since = now; }
            }
        }
        if (msi >= 0 && pf[msi].revents & POLLIN && ip[0]) mdns_serve(ms, ip);
        if (dsi >= 0 && pf[dsi].revents & POLLIN) disco_serve(ds, w->port);
        for (int i = 0; i < MAXC; i++) {
            client_t *c = &cl[i];
            if (c->fd < 0) continue;
            ssize_t n = recv(c->fd, c->in + c->inlen, sizeof c->in - 1 - (size_t)c->inlen, MSG_DONTWAIT);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { client_drop(w, c); continue; }
            if (n > 0) {
                if (c->up) { if (!upload_more(w, c, c->in + c->inlen, (int)n)) { client_drop(w, c); } continue; }
                c->inlen += (int)n;
                c->in[c->inlen] = 0;
                if (!c->ws) {
                    if (strstr(c->in, "\r\n\r\n")) {
                        int r = handle_http(w, c);
                        if (!r) { client_drop(w, c); continue; }
                        if (r == 2 && c->up_left <= 0 && !upload_more(w, c, NULL, 0)) { client_drop(w, c); continue; }
                        c->inlen = 0;
                        if (r == 1 && c->mirror) {   /* PLUG: who we are, and which build */
                            c->id = i;
                            unsigned char h[64], *p = h;
                            *p++ = 0x30;
                            put_u32(&p, MIR_PROTO); put_u32(&p, (uint32_t)NP); put_u32(&p, web_schema());
                            *p++ = (unsigned char)i;
                            *p++ = (unsigned char)strlen(VADER_VERSION_STR);
                            memcpy(p, VADER_VERSION_STR, strlen(VADER_VERSION_STR)); p += strlen(VADER_VERSION_STR);
                            if (ws_send(c->fd, h, (size_t)(p - h), 0)) { client_drop(w, c); continue; }
                        }
                    } else if (c->inlen >= (int)sizeof c->in - 1) { client_drop(w, c); continue; }
                } else if (ws_input(w, c)) { client_drop(w, c); continue; }
            }
            if (!c->ws && now - c->since > 10) client_drop(w, c);   /* a request that never came */
        }
        /* PLUG: the whole state when it changed (and for a desktop that just came), then the parameter changes since, at once */
        for (int i = 0; i < MAXC; i++) {
            client_t *c = &cl[i];
            if (c->fd < 0 || !c->mirror || !c->hello_ok) continue;
            pthread_mutex_lock(&w->lock);
            if (w->mir_state_ver != c->state_seen && w->mir_state_len > 0) {
                int n = w->mir_state_len;
                unsigned ver = w->mir_state_ver, pos = w->mir_state_pos;
                memcpy(stbuf + 1, w->mir_state, (size_t)n);
                pthread_mutex_unlock(&w->lock);
                stbuf[0] = 0x31;
                if (ws_send(c->fd, stbuf, (size_t)n + 1, 0)) { client_drop(w, c); continue; }
                c->state_seen = ver;
                c->cursor = pos;
                continue;
            }
            if (w->mir_state_ver == c->state_seen && w->ring_head != c->cursor) {
                if (w->ring_head - c->cursor > MIR_RING) {   /* too far behind: the whole state again */
                    c->cursor = w->ring_head;
                    __atomic_store_n(&w->mir_want_full, 1, __ATOMIC_RELEASE);
                    pthread_mutex_unlock(&w->lock);
                    continue;
                }
                unsigned char *p = msg + 3;
                int cnt = 0;
                while (c->cursor != w->ring_head && cnt < 400) {
                    const mir_ent_t *e = &w->ring[c->cursor % MIR_RING];
                    c->cursor++;
                    if (e->origin == c->id) continue;   /* it made this change itself */
                    memcpy(p, &e->idx, 2); memcpy(p + 2, &e->val, 4); p += 6; cnt++;
                }
                pthread_mutex_unlock(&w->lock);
                if (cnt) {
                    msg[0] = 0x32; msg[1] = (unsigned char)cnt; msg[2] = (unsigned char)(cnt >> 8);
                    if (ws_send(c->fd, msg, (size_t)(p - msg), 0)) client_drop(w, c);
                }
                continue;
            }
            pthread_mutex_unlock(&w->lock);
        }
        /* PLUG: the Force's readout line when it changed */
        for (int i = 0; i < MAXC; i++) {
            client_t *c = &cl[i];
            if (c->fd < 0 || !c->mirror || !c->hello_ok) continue;
            pthread_mutex_lock(&w->lock);
            if (c->rd_seen == w->mir_rd_ver) { pthread_mutex_unlock(&w->lock); continue; }
            c->rd_seen = w->mir_rd_ver;
            unsigned char *p = msg;
            *p++ = 0x33;
            put_f(&p, w->mir_rd_line);
            size_t tl = strlen(w->mir_rd);
            memcpy(p, w->mir_rd, tl); p += tl;
            pthread_mutex_unlock(&w->lock);
            if (ws_send(c->fd, msg, (size_t)(p - msg), 0)) client_drop(w, c);
        }
        /* a frame to every watcher, FPS times a second; the shape first when it changed. A plugged desktop gets every frame
         * (60 a second) with the extras the desktop's screen draws; the phone page every second one (30), as before. */
        clock_gettime(CLOCK_MONOTONIC, &nowts);
        if ((nowts.tv_sec - next.tv_sec) * 1000000000L + (nowts.tv_nsec - next.tv_nsec) >= 0) {
            next.tv_nsec += 1000000000L / FPS;
            while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
            if ((nowts.tv_sec - next.tv_sec) * 1000000000L + (nowts.tv_nsec - next.tv_nsec) > 200000000L) next = nowts;
            frame_no++;
            int any = 0;
            for (int i = 0; i < MAXC; i++) if (cl[i].fd >= 0 && cl[i].ws && (cl[i].mirror ? cl[i].hello_ok : (frame_no & 1) == 0)) any = 1;
            if (any) {
                ovl_view_t v;
                overlay_read_view(w->ovl, &v);
                unsigned sv = overlay_shape_version(w->ovl);
                for (int i = 0; i < MAXC; i++) {
                    client_t *c = &cl[i];
                    if (c->fd < 0 || !c->ws) continue;
                    if (c->mirror ? !c->hello_ok : (frame_no & 1)) continue;
                    if (c->shape_seen != sv) {
                        static ovl_shape_t sh;   /* the thread's own */
                        overlay_read_shape(w->ovl, &sh);
                        unsigned char *p = msg;
                        *p++ = 2;
                        *p++ = (unsigned char)sh.is3d;
                        *p++ = (unsigned char)sh.shape_user;
                        *p++ = (unsigned char)sh.has_img;
                        put_u32(&p, OVL_PATHN);
                        for (int k = 0; k < OVL_PATHN; k++) { put_f(&p, sh.px[k]); put_f(&p, sh.py[k]); put_f(&p, sh.pz[k]); }
                        if (sh.has_img) for (int k = 0; k < OVL_IMG_N * OVL_IMG_N; k++) *p++ = (unsigned char)(sh.img[k] * 255);
                        if (ws_send(c->fd, msg, (size_t)(p - msg), 0)) { client_drop(w, c); continue; }
                        c->shape_seen = sv;
                    }
                    unsigned char *p = msg;
                    *p++ = c->mirror ? 0x21 : 1;
                    *p++ = (unsigned char)v.motion;
                    *p++ = 0; *p++ = 0;
                    put_f(&p, v.rot); put_f(&p, v.yaw); put_f(&p, v.tilt); put_f(&p, v.size); put_f(&p, v.sub_size);
                    put_f(&p, v.ot); put_f(&p, v.oo); put_f(&p, v.hz); put_f(&p, v.sub);
                    put_f(&p, v.env); put_f(&p, v.w[0]); put_f(&p, v.w[1]); put_f(&p, v.w[2]); put_f(&p, v.w[3]);
                    put_f(&p, (float)v.use_pos); put_f(&p, v.fx); put_f(&p, v.fy); put_f(&p, v.ox); put_f(&p, v.oy);
                    put_f(&p, (float)v.nballs); for (int k = 0; k < 16; k++) { put_f(&p, v.bx[k]); put_f(&p, v.by[k]); }
                    put_f(&p, v.ps); put_f(&p, v.pe); put_f(&p, v.style);
                    if (c->mirror) { for (int k = 0; k < 4; k++) put_f(&p, v.im[k]); put_f(&p, (float)v.pgctr); }
                    if (ws_send(c->fd, msg, (size_t)(p - msg), 0)) client_drop(w, c);
                }
            }
        }
    }
    for (int i = 0; i < MAXC; i++) client_drop(w, &cl[i]);
    free(stbuf);
    if (ds >= 0) close(ds);
    if (ms >= 0) close(ms);
    close(ls);
    return NULL;
}

void web_start(web_t *w, overlay_t *ovl, const char *shapes_dir) {
    memset(w, 0, sizeof *w);
    w->ovl = ovl;
    if (shapes_dir) snprintf(w->shapes_dir, sizeof w->shapes_dir, "%s", shapes_dir);
    pthread_mutex_init(&w->lock, NULL);
    w->mir_state = malloc(MIR_STATE_MAX);
    w->port = WEB_PORT;
    snprintf(w->status, sizeof w->status, "STARTING");
    w->started = 1;
    if (pthread_create(&w->thread, NULL, web_thread, w)) { w->started = 0; snprintf(w->status, sizeof w->status, "NO THREAD"); }
}

void web_stop(web_t *w) {
    if (!w->started) return;
    w->stop = 1;
    pthread_join(w->thread, NULL);
    w->started = 0;
    pthread_mutex_destroy(&w->lock);
    free(w->mir_state);
    w->mir_state = NULL;
}

unsigned web_version(web_t *w) {
    if (!w->started) return 0;
    pthread_mutex_lock(&w->lock);
    unsigned v = w->url_ver;
    pthread_mutex_unlock(&w->lock);
    return v;
}

unsigned web_get(web_t *w, char *status, int len, unsigned char *qr) {
    pthread_mutex_lock(&w->lock);
    if (status) snprintf(status, (size_t)len, "%s", w->status);
    if (qr) memcpy(qr, w->qr, sizeof w->qr);
    unsigned v = w->url_ver;
    pthread_mutex_unlock(&w->lock);
    return v;
}

int web_port(void) { return WEB_PORT; }   /* the first port tried; the one in use is in web_t.port */

int web_take_load(web_t *w, char *path, int len) {
    pthread_mutex_lock(&w->lock);
    int got = w->load_ver != w->load_taken;
    if (got) { w->load_taken = w->load_ver; snprintf(path, (size_t)len, "%s", w->load_path); }
    pthread_mutex_unlock(&w->lock);
    return got;
}

/* ------------------------------------------------------------------------------------------------ PLUG, for the engine */
int web_mirror_active(web_t *w) { return w->started ? __atomic_load_n(&w->mirror_n, __ATOMIC_RELAXED) : 0; }

int web_mirror_take(web_t *w, mir_msg_t *out, int max) {
    int n = 0;
    pthread_mutex_lock(&w->lock);
    while (w->in_tail != w->in_head && n < max) out[n++] = w->inbox[w->in_tail++ % MIR_INBOX];
    pthread_mutex_unlock(&w->lock);
    return n;
}

int web_mirror_want_full(web_t *w) { return __atomic_exchange_n(&w->mir_want_full, 0, __ATOMIC_ACQ_REL); }

void web_mirror_push(web_t *w, const mir_ent_t *e, int n) {
    if (n <= 0) return;
    pthread_mutex_lock(&w->lock);
    for (int i = 0; i < n; i++) w->ring[w->ring_head++ % MIR_RING] = e[i];
    pthread_mutex_unlock(&w->lock);
}

void web_mirror_state(web_t *w, const char *txt, int len) {
    if (!w->mir_state || len <= 0 || len > MIR_STATE_MAX) return;
    pthread_mutex_lock(&w->lock);
    memcpy(w->mir_state, txt, (size_t)len);
    w->mir_state_len = len;
    w->mir_state_pos = w->ring_head;
    w->mir_state_ver++;
    pthread_mutex_unlock(&w->lock);
}

void web_mirror_text(web_t *w, const char *readout, float line) {
    pthread_mutex_lock(&w->lock);
    snprintf(w->mir_rd, sizeof w->mir_rd, "%s", readout);
    w->mir_rd_line = line;
    w->mir_rd_ver++;
    pthread_mutex_unlock(&w->lock);
}
