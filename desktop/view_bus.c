/* VADER desktop (0.7.0): the engine's screen data without the Force's display plane or the phone's web server.
 *
 * src/engine.c publishes what to draw (overlay_publish / overlay_publish_shape) every block, and asks overlay.c to
 * show it on the Force's hardware overlay and web.c to stream it to a phone. On the desktop the plugin's own window
 * draws it, so these replace overlay.c and web.c: the publish calls fill the same two seqlock snapshots (the audio
 * thread writes, the window's thread reads, nobody waits), and the web server and the display plane do nothing.
 * Same signatures as overlay.h / web.h, so engine.c is the same file in both builds. */
#include <stdint.h>
#include <string.h>
#include <sched.h>
#include "overlay.h"
#include "web.h"

static void write_locked(unsigned *seq, void *dst, const void *src, size_t n) {
    __atomic_store_n(seq, *seq + 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy(dst, src, n);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(seq, *seq + 1, __ATOMIC_RELEASE);
}
static void read_locked(unsigned *seq, const void *src, void *dst, size_t n) {
    unsigned s0, s1;
    do {
        s0 = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
        if (s0 & 1) { sched_yield(); continue; }
        memcpy(dst, src, n);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        s1 = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
    } while (s0 != s1 || (s0 & 1));
}

/* "started" is what makes engine.c publish; "live" stays 0 so the engine goes on as it does without a display plane */
void overlay_start(overlay_t *o, const char *plugin_dir) {
    (void)plugin_dir;
    memset(o, 0, sizeof *o);
    o->fd = -1;
    o->started = 1;
}
void overlay_stop(overlay_t *o) { o->started = 0; }
void overlay_publish(overlay_t *o, const ovl_view_t *v) { write_locked(&o->seq, &o->view, v, sizeof *v); }
void overlay_publish_shape(overlay_t *o, const ovl_shape_t *sh) { write_locked(&o->seq2, &o->shape, sh, sizeof *sh); }
void overlay_read_view(overlay_t *o, ovl_view_t *v) { read_locked(&o->seq, &o->view, v, sizeof *v); }
void overlay_read_shape(overlay_t *o, ovl_shape_t *sh) { read_locked(&o->seq2, &o->shape, sh, sizeof *sh); }
unsigned overlay_shape_version(overlay_t *o) { return __atomic_load_n(&o->seq2, __ATOMIC_ACQUIRE); }
int overlay_live(const overlay_t *o) { (void)o; return 0; }

/* no phone server on the desktop */
void web_start(web_t *w, overlay_t *ovl, const char *shapes_dir) { (void)ovl; (void)shapes_dir; memset(w, 0, sizeof *w); }
int web_take_load(web_t *w, char *path, int len) { (void)w; (void)path; (void)len; return 0; }
void web_stop(web_t *w) { (void)w; }
unsigned web_version(web_t *w) { (void)w; return 0; }
unsigned web_get(web_t *w, char *status, int len, unsigned char *qr) {
    (void)w;
    if (status && len > 0) status[0] = 0;
    if (qr) memset(qr, 0, 33 * 5);
    return 0;
}
int web_port(void) { return 0; }
/* PLUG: the engine's side of a mirror is only on the Force (the desktop is the other end, desktop/mirror.cpp) */
unsigned web_schema(void) { return 0; }
int web_mirror_active(web_t *w) { (void)w; return 0; }
int web_mirror_take(web_t *w, mir_msg_t *out, int max) { (void)w; (void)out; (void)max; return 0; }
int web_mirror_want_full(web_t *w) { (void)w; return 0; }
void web_mirror_push(web_t *w, const mir_ent_t *e, int n) { (void)w; (void)e; (void)n; }
void web_mirror_state(web_t *w, const char *txt, int len) { (void)w; (void)txt; (void)len; }
void web_mirror_text(web_t *w, const char *readout, float line) { (void)w; (void)readout; (void)line; }
