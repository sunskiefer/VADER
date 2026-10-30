/* VADER's companion screen: the web server (src/web.c) */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include "overlay.h"

/* PLUG: a desktop VADER plugged into this one (path /mirror of the same port). The desktop shows this instance's screen and
 * controls, and what it changes goes back here. The engine (engine.c) and the server's thread meet in these small queues. */
#define MIR_PROTO 1
#define MIR_RING 2048              /* parameter changes waiting to be sent (a client that falls further behind is sent the whole state) */
#define MIR_INBOX 256              /* messages from the desktops waiting for the engine */
#define MIR_STATE_MAX (128 * 1024) /* the whole state as text */
typedef struct { unsigned char origin; uint16_t idx; float val; } mir_ent_t;       /* origin: the client that sent it, 0xff: this instance */
typedef struct { unsigned char type, client; uint16_t idx; float val; } mir_msg_t; /* type 1: set, 2: fire, 3: send the whole state again */

typedef struct {
    int started, stop;
    pthread_t thread;
    overlay_t *ovl;            /* where the snapshots to stream live */
    pthread_mutex_t lock;
    char url[96];              /* http://<address>:6731/ or empty (no network) */
    unsigned url_ver;
    char status[96];           /* for the readout: the URL, or why there is none */
    unsigned char qr[33 * 5];  /* its QR code: QR_N rows of QR_BYTES bytes of modules (all 0: none) */
    int watchers;
    char shapes_dir[600];      /* where a shape sent from the phone goes (empty: uploads refused) */
    char load_path[1100];      /* the last one saved, for the engine to load */
    unsigned load_ver, load_taken;
    int port;                  /* the port in use (6731, or the next free one) */
    /* PLUG */
    int mirror_n;              /* plugged desktops (atomic) */
    int mir_want_full;         /* atomic: a desktop needs the whole state */
    mir_ent_t ring[MIR_RING];
    unsigned ring_head;        /* entries ever pushed */
    char *mir_state;           /* the whole state, MIR_STATE_MAX bytes */
    int mir_state_len;
    unsigned mir_state_ver, mir_state_pos;   /* pos: the ring position it was taken at */
    mir_msg_t inbox[MIR_INBOX];
    unsigned in_head, in_tail;
    char mir_rd[96];           /* the readout line, for the desktop's top line */
    float mir_rd_line;
    unsigned mir_rd_ver;
} web_t;

void web_start(web_t *w, overlay_t *ovl, const char *shapes_dir);
int web_take_load(web_t *w, char *path, int len);   /* 1: a shape just arrived from the phone: its path, to load */
void web_stop(web_t *w);
unsigned web_version(web_t *w);                                        /* changes whenever the address or status does */
unsigned web_get(web_t *w, char *status, int len, unsigned char *qr);  /* the status text and the QR bytes (either NULL) */
int web_port(void);
unsigned web_schema(void);                                             /* a number that is the same only when two builds have the same parameters */
/* PLUG, for the engine (audio thread) */
int web_mirror_active(web_t *w);                                       /* plugged desktops */
int web_mirror_take(web_t *w, mir_msg_t *out, int max);                /* what the desktops sent */
int web_mirror_want_full(web_t *w);                                    /* 1 (once): a desktop needs the whole state */
void web_mirror_push(web_t *w, const mir_ent_t *e, int n);             /* parameter changes to send */
void web_mirror_state(web_t *w, const char *txt, int len);             /* the whole state, taken now (after the pushes of this block) */
void web_mirror_text(web_t *w, const char *readout, float line);       /* the readout line and its glow (0..1) */
