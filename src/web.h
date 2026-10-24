/* VADER's companion screen: the web server (src/web.c) */
#pragma once
#include <pthread.h>
#include "overlay.h"

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
} web_t;

void web_start(web_t *w, overlay_t *ovl, const char *shapes_dir);
int web_take_load(web_t *w, char *path, int len);   /* 1: a shape just arrived from the phone: its path, to load */
void web_stop(web_t *w);
unsigned web_version(web_t *w);                                        /* changes whenever the address or status does */
unsigned web_get(web_t *w, char *status, int len, unsigned char *qr);  /* the status text and the QR bytes (either NULL) */
int web_port(void);
