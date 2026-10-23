/* VADER's live screen (0.5.0): the plugin draws the screen itself, straight onto a hardware overlay plane of the
 * display, so MPC redraws nothing while it moves.
 *
 * How: the plugin runs inside MPC's process, and MPC owns the display through DRM/KMS (the Force: an RK3288, whose
 * display controller has four hardware windows with alpha blending; MPC uses one). This file finds MPC's own DRM
 * handle among the process's open files, picks a spare overlay plane, gives it two ARGB buffers of the screen box's
 * size and swaps them at 30 frames a second from a thread of its own. The audio thread only publishes a small
 * snapshot of what to draw (overlay_publish). Where the box is on the display, and whether it is on screen at all
 * (VADER's MAIN page shown, nothing over it), is read from the picture MPC is showing: the skin's glass layer has two
 * marker pixels at the box's corners (tools/vader_skin.py glass()); while both are visible the plane is shown there,
 * else it is hidden. Everything it finds and does goes to "<plugin folder>/overlay.log". When any step fails, the
 * plane stays off and the tile screen (screen.c) carries on as before. A file "<plugin folder>/overlay.off" keeps it
 * off altogether.
 *
 * The drawing (render_frame): the shape as an anti-aliased glowing line (3D shapes shaded by depth), the orbit line
 * faint, the orbiter's light trail as a smooth fading curve, the two balls as soft-edged discs with a hot core, a loaded
 * picture dimly under its shape, a bloom over the lights, scanlines and a vignette. All in software, about 2 to 4 ms a
 * frame on one Cortex-A17 core. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include "overlay.h"

#define OVL_FPS 30
#define TRAIL_PTS_OVL 28

/* ------------------------------------------------------------------------------------------------ the log */
static void logf_(overlay_t *o, const char *fmt, ...) {
    if (!o->log_path[0]) return;
    FILE *f = fopen(o->log_path, "a");
    if (!f) return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(f, "[%6ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ------------------------------------------------------------------------------------------------ DRM */
static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do r = ioctl(fd, req, arg); while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r;
}

/* the DRM cards this process has open (MPC's own handles): up to 8 candidates; the one that lets us set a plane is
 * MPC's master handle */
static int find_card_fds(overlay_t *o, int *fds, int max) {
    DIR *d = opendir("/proc/self/fd");
    if (!d) { logf_(o, "no /proc/self/fd: %s", strerror(errno)); return 0; }
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) && n < max) {
        char link[64], target[256];
        int fd = atoi(e->d_name);
        if (fd < 3) continue;
        snprintf(link, sizeof link, "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(link, target, sizeof target - 1);
        if (n <= 0) continue;
        target[n] = 0;
        if (strncmp(target, "/dev/dri/card", 13)) continue;
        struct drm_mode_card_res r;
        memset(&r, 0, sizeof r);
        if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &r)) { logf_(o, "fd %d %s: GETRESOURCES: %s", fd, target, strerror(errno)); continue; }
        logf_(o, "fd %d is %s: %u crtcs, %u connectors, %u fbs", fd, target, r.count_crtcs, r.count_connectors, r.count_fbs);
        fds[n++] = fd;
    }
    closedir(d);
    return n;
}

static int get_prop_u64(int fd, uint32_t obj, uint32_t type, const char *name, uint64_t *val, uint32_t *prop_id,
                        uint64_t *vmin, uint64_t *vmax) {
    struct drm_mode_obj_get_properties gp;
    memset(&gp, 0, sizeof gp);
    gp.obj_id = obj;
    gp.obj_type = type;
    if (xioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gp)) return -1;
    uint32_t n = gp.count_props;
    if (!n || n > 64) return -1;
    uint32_t ids[64];
    uint64_t vals[64];
    gp.props_ptr = (uintptr_t)ids;
    gp.prop_values_ptr = (uintptr_t)vals;
    gp.count_props = n;
    if (xioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gp)) return -1;
    for (uint32_t i = 0; i < n && i < gp.count_props; i++) {
        struct drm_mode_get_property p;
        memset(&p, 0, sizeof p);
        p.prop_id = ids[i];
        if (xioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) continue;
        if (strcmp(p.name, name)) continue;
        if (val) *val = vals[i];
        if (prop_id) *prop_id = ids[i];
        if ((vmin || vmax) && p.count_values >= 2 && (p.flags & DRM_MODE_PROP_RANGE)) {
            uint64_t range[2];
            p.values_ptr = (uintptr_t)range;
            p.count_values = 2;
            p.count_enum_blobs = 0;
            if (!xioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) { if (vmin) *vmin = range[0]; if (vmax) *vmax = range[1]; }
        }
        return (int)(p.flags & DRM_MODE_PROP_IMMUTABLE ? 2 : 1);
    }
    return 0;
}

static void log_props(overlay_t *o, int fd, uint32_t obj, uint32_t type, const char *what) {
    struct drm_mode_obj_get_properties gp;
    memset(&gp, 0, sizeof gp);
    gp.obj_id = obj;
    gp.obj_type = type;
    if (xioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gp)) return;
    uint32_t n = gp.count_props;
    if (!n || n > 64) return;
    uint32_t ids[64];
    uint64_t vals[64];
    gp.props_ptr = (uintptr_t)ids;
    gp.prop_values_ptr = (uintptr_t)vals;
    gp.count_props = n;
    if (xioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &gp)) return;
    char line[512];
    int ln = snprintf(line, sizeof line, "  %s %u props:", what, obj);
    for (uint32_t i = 0; i < n && i < gp.count_props && ln < 480; i++) {
        struct drm_mode_get_property p;
        memset(&p, 0, sizeof p);
        p.prop_id = ids[i];
        if (xioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) continue;
        ln += snprintf(line + ln, sizeof line - (size_t)ln, " %s=%llu%s", p.name, (unsigned long long)vals[i],
                       (p.flags & DRM_MODE_PROP_IMMUTABLE) ? "(ro)" : "");
    }
    logf_(o, "%s", line);
}

static int create_buffer(overlay_t *o, ovl_buf_t *b) {
    struct drm_mode_create_dumb cd;
    memset(&cd, 0, sizeof cd);
    cd.width = OVL_W;
    cd.height = OVL_H;
    cd.bpp = 32;
    if (xioctl(o->fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { logf_(o, "CREATE_DUMB: %s", strerror(errno)); return -1; }
    b->handle = cd.handle;
    b->pitch = cd.pitch;
    b->size = cd.size;
    struct drm_mode_fb_cmd2 fb;
    memset(&fb, 0, sizeof fb);
    fb.width = OVL_W;
    fb.height = OVL_H;
    fb.pixel_format = o->format;
    fb.handles[0] = cd.handle;
    fb.pitches[0] = cd.pitch;
    if (xioctl(o->fd, DRM_IOCTL_MODE_ADDFB2, &fb)) { logf_(o, "ADDFB2 (%.4s): %s", (char *)&o->format, strerror(errno)); return -1; }
    b->fb_id = fb.fb_id;
    struct drm_mode_map_dumb md;
    memset(&md, 0, sizeof md);
    md.handle = cd.handle;
    if (xioctl(o->fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { logf_(o, "MAP_DUMB: %s", strerror(errno)); return -1; }
    b->map = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, o->fd, (off_t)md.offset);
    if (b->map == MAP_FAILED) { b->map = NULL; logf_(o, "mmap dumb: %s", strerror(errno)); return -1; }
    memset(b->map, 0, cd.size);
    logf_(o, "buffer: handle %u fb %u pitch %u size %llu", b->handle, b->fb_id, b->pitch, (unsigned long long)b->size);
    return 0;
}

static void destroy_buffer(overlay_t *o, ovl_buf_t *b) {
    if (b->map) munmap(b->map, b->size);
    if (b->fb_id) { uint32_t id = b->fb_id; xioctl(o->fd, DRM_IOCTL_MODE_RMFB, &id); }
    if (b->handle) { struct drm_mode_destroy_dumb dd = {.handle = b->handle}; xioctl(o->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd); }
    memset(b, 0, sizeof *b);
}

static int set_plane(overlay_t *o, uint32_t fb_id, int x, int y) {
    struct drm_mode_set_plane sp;
    memset(&sp, 0, sizeof sp);
    sp.plane_id = o->plane_id;
    sp.crtc_id = o->crtc_id;
    sp.fb_id = fb_id;
    if (fb_id) {
        sp.crtc_x = x;
        sp.crtc_y = y;
        sp.crtc_w = OVL_W;
        sp.crtc_h = OVL_H;
        sp.src_x = 0;
        sp.src_y = 0;
        sp.src_w = OVL_W << 16;
        sp.src_h = OVL_H << 16;
    }
    return xioctl(o->fd, DRM_IOCTL_MODE_SETPLANE, &sp);
}

/* the display: MPC's CRTC and a spare plane on it that takes ARGB. 0: ready. */
static int drm_setup_fd(overlay_t *o);
static int drm_setup(overlay_t *o) {
    int fds[8], n = find_card_fds(o, fds, 8);
    if (!n) { logf_(o, "no DRM card among this process's files"); return -1; }
    for (int i = 0; i < n; i++) {
        o->fd = fds[i];
        logf_(o, "trying fd %d", o->fd);
        if (drm_setup_fd(o) == 0) {
            /* can we set a plane on it? (only the display's master can: a disable of our unused plane is harmless) */
            if (set_plane(o, 0, 0, 0) == 0) return 0;
            logf_(o, "fd %d: SETPLANE: %s (not the master handle?)", o->fd, strerror(errno));
        }
        for (int k = 0; k < 2; k++) destroy_buffer(o, &o->buf[k]);
    }
    o->fd = -1;
    return -1;
}
static int drm_setup_fd(overlay_t *o) {
    uint64_t cap = 1;
    struct drm_set_client_cap cc = {.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES, .value = cap};
    if (xioctl(o->fd, DRM_IOCTL_SET_CLIENT_CAP, &cc)) logf_(o, "universal planes: %s", strerror(errno));
    /* the active CRTC */
    struct drm_mode_card_res r;
    memset(&r, 0, sizeof r);
    if (xioctl(o->fd, DRM_IOCTL_MODE_GETRESOURCES, &r)) { logf_(o, "GETRESOURCES: %s", strerror(errno)); return -1; }
    uint32_t crtcs[16], ncrtc = r.count_crtcs > 16 ? 16 : r.count_crtcs;
    memset(&r, 0, sizeof r);
    r.crtc_id_ptr = (uintptr_t)crtcs;
    r.count_crtcs = ncrtc;
    if (xioctl(o->fd, DRM_IOCTL_MODE_GETRESOURCES, &r)) { logf_(o, "GETRESOURCES 2: %s", strerror(errno)); return -1; }
    o->crtc_id = 0;
    for (uint32_t i = 0; i < ncrtc; i++) {
        struct drm_mode_crtc c;
        memset(&c, 0, sizeof c);
        c.crtc_id = crtcs[i];
        if (xioctl(o->fd, DRM_IOCTL_MODE_GETCRTC, &c)) continue;
        logf_(o, "crtc %u: fb %u, %ux%u at %u,%u, mode %s %ux%u", c.crtc_id, c.fb_id, c.mode.hdisplay, c.mode.vdisplay, c.x, c.y,
              c.mode.name, c.mode.hdisplay, c.mode.vdisplay);
        if (c.fb_id && !o->crtc_id) { o->crtc_id = c.crtc_id; o->crtc_index = (int)i; o->disp_w = c.mode.hdisplay; o->disp_h = c.mode.vdisplay; }
    }
    if (!o->crtc_id) { logf_(o, "no active crtc"); return -1; }
    /* the planes */
    struct drm_mode_get_plane_res pr;
    memset(&pr, 0, sizeof pr);
    if (xioctl(o->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr)) { logf_(o, "GETPLANERESOURCES: %s", strerror(errno)); return -1; }
    uint32_t planes[32], np = pr.count_planes > 32 ? 32 : pr.count_planes;
    pr.plane_id_ptr = (uintptr_t)planes;
    pr.count_planes = np;
    if (xioctl(o->fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pr)) { logf_(o, "GETPLANERESOURCES 2: %s", strerror(errno)); return -1; }
    logf_(o, "%u planes", np);
    o->plane_id = 0;
    int best_rank = -1;
    for (uint32_t i = 0; i < np; i++) {
        struct drm_mode_get_plane gp;
        memset(&gp, 0, sizeof gp);
        gp.plane_id = planes[i];
        uint32_t fmts[64];
        gp.format_type_ptr = (uintptr_t)fmts;
        gp.count_format_types = 64;
        if (xioctl(o->fd, DRM_IOCTL_MODE_GETPLANE, &gp)) { logf_(o, "plane %u: GETPLANE: %s", planes[i], strerror(errno)); continue; }
        uint64_t type = 99, zpos = 0, zmin = 0, zmax = 0;
        get_prop_u64(o->fd, planes[i], DRM_MODE_OBJECT_PLANE, "type", &type, NULL, NULL, NULL);
        int hasz = get_prop_u64(o->fd, planes[i], DRM_MODE_OBJECT_PLANE, "zpos", &zpos, NULL, &zmin, &zmax);
        int ar24 = 0, xr24 = 0;
        char fl[256];
        int fn = 0;
        for (uint32_t k = 0; k < gp.count_format_types && k < 64; k++) {
            if (fmts[k] == DRM_FORMAT_ARGB8888) ar24 = 1;
            if (fmts[k] == DRM_FORMAT_XRGB8888) xr24 = 1;
            if (fn < 240) fn += snprintf(fl + fn, sizeof fl - (size_t)fn, " %.4s", (char *)&fmts[k]);
        }
        logf_(o, "plane %u: type %s, crtcs 0x%x, in use: crtc %u fb %u, zpos %llu [%llu..%llu]%s, formats:%s",
              planes[i], type == 1 ? "primary" : type == 2 ? "cursor" : type == 0 ? "overlay" : "?", gp.possible_crtcs,
              gp.crtc_id, gp.fb_id, (unsigned long long)zpos, (unsigned long long)zmin, (unsigned long long)zmax,
              hasz == 2 ? "(ro)" : hasz == 1 ? "" : "(none)", fl);
        log_props(o, o->fd, planes[i], DRM_MODE_OBJECT_PLANE, "plane");
        if (!(gp.possible_crtcs & (1u << o->crtc_index))) continue;
        if (gp.fb_id) continue;              /* MPC's own */
        if (type == 1) continue;             /* a primary plane */
        if (!ar24 && !xr24) continue;
        int rank = (type == 0 ? 2 : 1) + (ar24 ? 4 : 0);
        if (rank > best_rank) { best_rank = rank; o->plane_id = planes[i]; o->format = ar24 ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888; o->zpos_max = zmax; o->has_zpos = hasz == 1; }
    }
    if (!o->plane_id) { logf_(o, "no spare plane on crtc %u takes ARGB/XRGB", o->crtc_id); return -1; }
    logf_(o, "using plane %u (%.4s) on crtc %u", o->plane_id, (char *)&o->format, o->crtc_id);
    if (o->has_zpos && o->zpos_max) {
        uint32_t pid = 0;
        get_prop_u64(o->fd, o->plane_id, DRM_MODE_OBJECT_PLANE, "zpos", NULL, &pid, NULL, NULL);
        struct drm_mode_obj_set_property sp = {.value = o->zpos_max, .prop_id = pid, .obj_id = o->plane_id, .obj_type = DRM_MODE_OBJECT_PLANE};
        if (xioctl(o->fd, DRM_IOCTL_MODE_OBJ_SETPROPERTY, &sp)) logf_(o, "zpos %llu: %s", (unsigned long long)o->zpos_max, strerror(errno));
        else logf_(o, "zpos set to %llu", (unsigned long long)o->zpos_max);
    }
    for (int i = 0; i < 2; i++) if (create_buffer(o, &o->buf[i])) return -1;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ where the box is */
/* MPC's picture, read back from the display (read-only), to find the skin's marker pixels. Cached per framebuffer. */
typedef struct { uint32_t fb_id; uint8_t *map; size_t len; uint32_t pitch, w, h, fmt; } fbmap_t;
static fbmap_t g_fbmaps[4];

static fbmap_t *scanout(overlay_t *o) {
    struct drm_mode_crtc c;
    memset(&c, 0, sizeof c);
    c.crtc_id = o->crtc_id;
    if (xioctl(o->fd, DRM_IOCTL_MODE_GETCRTC, &c) || !c.fb_id) return NULL;
    for (int i = 0; i < 4; i++) if (g_fbmaps[i].fb_id == c.fb_id && g_fbmaps[i].map) return &g_fbmaps[i];
    struct drm_mode_fb_cmd2 f;
    memset(&f, 0, sizeof f);
    f.fb_id = c.fb_id;
    if (xioctl(o->fd, DRM_IOCTL_MODE_GETFB2, &f)) {
        /* an older kernel: the legacy GETFB (a handle, the pitch, bpp and depth; the format follows) */
        struct drm_mode_fb_cmd f1;
        memset(&f1, 0, sizeof f1);
        f1.fb_id = c.fb_id;
        if (xioctl(o->fd, DRM_IOCTL_MODE_GETFB, &f1)) { logf_(o, "GETFB2 and GETFB %u: %s", c.fb_id, strerror(errno)); return NULL; }
        memset(&f, 0, sizeof f);
        f.fb_id = f1.fb_id;
        f.width = f1.width;
        f.height = f1.height;
        f.pitches[0] = f1.pitch;
        f.handles[0] = f1.handle;
        f.pixel_format = f1.bpp == 32 ? DRM_FORMAT_XRGB8888 : f1.bpp == 24 ? DRM_FORMAT_RGB888 : f1.bpp == 16 ? DRM_FORMAT_RGB565 : 0;
        logf_(o, "GETFB %u: %ux%u bpp %u depth %u", c.fb_id, f1.width, f1.height, f1.bpp, f1.depth);
    }
    if (!f.handles[0]) { logf_(o, "GETFB2 %u: no handle (not the master handle, and not root?)", c.fb_id); return NULL; }
    struct drm_prime_handle p = {.handle = f.handles[0], .flags = DRM_CLOEXEC | DRM_RDWR};
    int rc = xioctl(o->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p);
    if (rc) { p.flags = DRM_CLOEXEC; rc = xioctl(o->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p); }
    struct drm_gem_close gc = {.handle = f.handles[0]};
    xioctl(o->fd, DRM_IOCTL_GEM_CLOSE, &gc);   /* GETFB2 gave us a handle of our own: drop it */
    if (rc) { logf_(o, "PRIME export of fb %u: %s", c.fb_id, strerror(errno)); return NULL; }
    size_t len = (size_t)f.pitches[0] * f.height + f.offsets[0];
    uint8_t *m = mmap(NULL, len, PROT_READ, MAP_SHARED, p.fd, 0);
    close(p.fd);
    if (m == MAP_FAILED) { logf_(o, "mmap of fb %u: %s", c.fb_id, strerror(errno)); return NULL; }
    /* a slot: an empty one, else the oldest */
    int k = 0;
    for (int i = 0; i < 4; i++) if (!g_fbmaps[i].map) { k = i; break; } else if (i == 3) k = o->fb_slot++ & 3;
    if (g_fbmaps[k].map) munmap(g_fbmaps[k].map, g_fbmaps[k].len);
    g_fbmaps[k] = (fbmap_t){c.fb_id, m + f.offsets[0], len, f.pitches[0], f.width, f.height, f.pixel_format};
    logf_(o, "scanout fb %u: %ux%u %.4s pitch %u", c.fb_id, f.width, f.height, (char *)&f.pixel_format, f.pitches[0]);
    return &g_fbmaps[k];
}

static void unmap_scanouts(void) {
    for (int i = 0; i < 4; i++) if (g_fbmaps[i].map) { munmap(g_fbmaps[i].map - 0, g_fbmaps[i].len); memset(&g_fbmaps[i], 0, sizeof g_fbmaps[i]); }
}

/* the markers (tools/vader_skin.py glass()): 2 x 2 pixels at the tiles' top-left and bottom-right, in these colours */
static const uint8_t MARK_TL[4][3] = {{23, 7, 91}, {91, 7, 23}, {7, 91, 23}, {23, 91, 7}};
static const uint8_t MARK_BR[4][3] = {{91, 23, 7}, {7, 23, 91}, {61, 7, 61}, {7, 61, 61}};

static inline int px_rgb(const fbmap_t *m, int x, int y, uint8_t *rgb) {
    if (x < 0 || y < 0 || x >= (int)m->w || y >= (int)m->h) return 0;
    const uint8_t *p = m->map + (size_t)y * m->pitch;
    switch (m->fmt) {
    case DRM_FORMAT_XRGB8888: case DRM_FORMAT_ARGB8888: p += x * 4; rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0]; return 1;
    case DRM_FORMAT_XBGR8888: case DRM_FORMAT_ABGR8888: p += x * 4; rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2]; return 1;
    case DRM_FORMAT_RGB888: p += x * 3; rgb[0] = p[2]; rgb[1] = p[1]; rgb[2] = p[0]; return 1;
    case DRM_FORMAT_BGR888: p += x * 3; rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2]; return 1;
    case DRM_FORMAT_RGB565: { p += x * 2; uint16_t v = (uint16_t)(p[0] | p[1] << 8); rgb[0] = (uint8_t)((v >> 11) << 3); rgb[1] = (uint8_t)(((v >> 5) & 63) << 2); rgb[2] = (uint8_t)((v & 31) << 3); return 1; }
    default: return 0;
    }
}
static inline int near_(const uint8_t *a, const uint8_t *b) {
    return abs(a[0] - b[0]) <= 8 && abs(a[1] - b[1]) <= 8 && abs(a[2] - b[2]) <= 8;
}
static int marker_at(const fbmap_t *m, int x, int y, const uint8_t mk[4][3]) {
    uint8_t c[3];
    return px_rgb(m, x, y, c) && near_(c, mk[0]) && px_rgb(m, x + 1, y, c) && near_(c, mk[1]) &&
           px_rgb(m, x, y + 1, c) && near_(c, mk[2]) && px_rgb(m, x + 1, y + 1, c) && near_(c, mk[3]);
}

static int find_box(const fbmap_t *m, int *bx, int *by);

/* the box finder on a picture of the caller's (the offline test): 1 found, with the box's top-left */
int overlay_find_test(uint8_t *xrgb, int w, int h, int pitch, int *bx, int *by) {
    fbmap_t m = {0, xrgb, 0, (uint32_t)pitch, (uint32_t)w, (uint32_t)h, DRM_FORMAT_XRGB8888};
    return find_box(&m, bx, by);
}

static int find_box(const fbmap_t *m, int *bx, int *by) {
    for (int y = 0; y + OVL_TILES_H <= (int)m->h; y++) {
        const uint8_t *row = m->map + (size_t)y * m->pitch;
        for (int x = 0; x + OVL_TILES_W <= (int)m->w; x++) {
            uint8_t c[3];
            if (m->fmt == DRM_FORMAT_XRGB8888 || m->fmt == DRM_FORMAT_ARGB8888) {   /* the fast path */
                const uint8_t *p = row + x * 4;
                if (abs(p[2] - MARK_TL[0][0]) > 8 || abs(p[1] - MARK_TL[0][1]) > 8 || abs(p[0] - MARK_TL[0][2]) > 8) continue;
            } else if (!px_rgb(m, x, y, c) || !near_(c, MARK_TL[0])) continue;
            if (marker_at(m, x, y, MARK_TL) && marker_at(m, x + OVL_TILES_W - 2, y + OVL_TILES_H - 2, MARK_BR)) {
                *bx = x - OVL_TILES_X;
                *by = y - OVL_TILES_Y;
                return 1;
            }
        }
    }
    return 0;
}

/* 1: the box is on screen at (o->box_x, o->box_y) (display pixels); 0: not on screen (another page, a list over it) */
static int locate(overlay_t *o) {
    fbmap_t *m = scanout(o);
    if (!m) { o->fmt_unknown = 1; return 0; }
    if (o->box_found) {   /* still there? */
        int tx = o->box_x + OVL_TILES_X, ty = o->box_y + OVL_TILES_Y;
        if (marker_at(m, tx, ty, MARK_TL) && marker_at(m, tx + OVL_TILES_W - 2, ty + OVL_TILES_H - 2, MARK_BR)) return 1;
        o->box_found = 0;
    }
    /* look for it: the top-left marker's first pixel, every row */
    if (find_box(m, &o->box_x, &o->box_y)) {
        o->box_found = 1;
        if (!o->box_logged) { o->box_logged = 1; logf_(o, "the screen box is at %d,%d on the display (%ux%u %.4s)", o->box_x, o->box_y, m->w, m->h, (char *)&m->fmt); }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ drawing */
typedef struct { float r, g, b; } rgbf;
static float *g_acc;        /* OVL_W * OVL_H * 3: the lights, linear */
static float *g_lo;         /* OVL_W/4 * OVL_H/4 * 3: the bloom */
static float *g_lo2;

static inline void acc_add(int x, int y, float r, float g, float b, float a) {
    if (x < 0 || y < 0 || x >= OVL_W || y >= OVL_H) return;
    float *p = g_acc + ((size_t)y * OVL_W + x) * 3;
    p[0] += r * a; p[1] += g * a; p[2] += b * a;
}

/* an anti-aliased line of width w (px) in colour c at strength k: coverage from the distance to the segment */
static void line_aa(float x0, float y0, float x1, float y1, float w, rgbf c, float k) {
    float hw = w * 0.5f;
    int bx0 = (int)floorf(fminf(x0, x1) - hw - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
    int by0 = (int)floorf(fminf(y0, y1) - hw - 1), by1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= OVL_W) bx1 = OVL_W - 1;
    if (by1 >= OVL_H) by1 = OVL_H - 1;
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float t = l2 > 1e-6f ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float ex = x0 + dx * t - px, ey = y0 + dy * t - py;
            float d = sqrtf(ex * ex + ey * ey);
            float cov = hw + 0.5f - d;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            acc_add(x, y, c.r, c.g, c.b, cov * k);
        }
}

/* a polyline as one stroke: each pixel takes the most coverage any of its segments gives (no beads at the joints),
 * then the colour is added once. Per-segment strength and width (ks, ws: n-1 of each, or NULL). */
static float *g_cov;
static void polyline_aa(const float *xs, const float *ys, int n, int closed, const float *ws, const float *ks, float w1, float k1, rgbf c) {
    int bx0 = OVL_W, bx1 = -1, by0 = OVL_H, by1 = -1;
    float wmax = w1;
    if (ws) for (int i = 0; i < n; i++) if (ws[i] > wmax) wmax = ws[i];
    for (int i = 0; i < n; i++) {
        int x0 = (int)floorf(xs[i] - wmax - 1), x1 = (int)ceilf(xs[i] + wmax + 1), y0 = (int)floorf(ys[i] - wmax - 1), y1 = (int)ceilf(ys[i] + wmax + 1);
        if (x0 < bx0) bx0 = x0; if (x1 > bx1) bx1 = x1; if (y0 < by0) by0 = y0; if (y1 > by1) by1 = y1;
    }
    if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 >= OVL_W) bx1 = OVL_W - 1; if (by1 >= OVL_H) by1 = OVL_H - 1;
    if (bx1 < bx0 || by1 < by0) return;
    for (int y = by0; y <= by1; y++) memset(g_cov + (size_t)y * OVL_W + bx0, 0, (size_t)(bx1 - bx0 + 1) * sizeof(float));
    int segs = closed ? n : n - 1;
    for (int s = 0; s < segs; s++) {
        int j = (s + 1) % n;
        float x0 = xs[s], y0 = ys[s], x1 = xs[j], y1 = ys[j];
        float w = ws ? ws[s] : w1, k = ks ? ks[s] : k1, hw = w * 0.5f;
        int sx0 = (int)floorf(fminf(x0, x1) - hw - 1), sx1 = (int)ceilf(fmaxf(x0, x1) + hw + 1);
        int sy0 = (int)floorf(fminf(y0, y1) - hw - 1), sy1 = (int)ceilf(fmaxf(y0, y1) + hw + 1);
        if (sx0 < bx0) sx0 = bx0; if (sy0 < by0) sy0 = by0; if (sx1 > bx1) sx1 = bx1; if (sy1 > by1) sy1 = by1;
        float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
        for (int y = sy0; y <= sy1; y++)
            for (int x = sx0; x <= sx1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float tt = l2 > 1e-6f ? ((px - x0) * dx + (py - y0) * dy) / l2 : 0;
                if (tt < 0) tt = 0;
                if (tt > 1) tt = 1;
                float ex = x0 + dx * tt - px, ey = y0 + dy * tt - py;
                float cov = hw + 0.5f - sqrtf(ex * ex + ey * ey);
                if (cov <= 0) continue;
                if (cov > 1) cov = 1;
                cov *= k;
                float *cp = g_cov + (size_t)y * OVL_W + x;
                if (cov > *cp) *cp = cov;
            }
    }
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float cv = g_cov[(size_t)y * OVL_W + x];
            if (cv > 0) acc_add(x, y, c.r, c.g, c.b, cv);
        }
}

/* a soft disc: a hot core, a soft edge, in colour c, strength k, at radius r */
static void disc(float cx, float cy, float r, rgbf c, float k) {
    int bx0 = (int)floorf(cx - r - 2), bx1 = (int)ceilf(cx + r + 2), by0 = (int)floorf(cy - r - 2), by1 = (int)ceilf(cy + r + 2);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= OVL_W) bx1 = OVL_W - 1;
    if (by1 >= OVL_H) by1 = OVL_H - 1;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float ex = x + 0.5f - cx, ey = y + 0.5f - cy, d = sqrtf(ex * ex + ey * ey);
            float cov = r + 0.5f - d;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            float u = d / r;                        /* 0 centre .. 1 edge */
            float core = 1 + 0.9f * (1 - u) * (1 - u);   /* brighter, whiter in the middle */
            float w = (1 - u) * (1 - u) * 0.8f;
            acc_add(x, y, c.r * core + w, c.g * core + w, c.b * core + w, cov * k);
        }
}

static const rgbf PATH_COLS[6] = {{1.0f, 0.88f, 0.29f}, {1.0f, 0.69f, 0.13f}, {1.0f, 0.50f, 0.10f}, {1.0f, 0.33f, 0.16f}, {1.0f, 0.20f, 0.35f}, {1.0f, 0.24f, 0.67f}};
static const rgbf ORB_COLS[6] = {{0.27f, 0.47f, 1.0f}, {0.24f, 0.69f, 1.0f}, {0.49f, 0.99f, 1.0f}, {0.35f, 1.0f, 0.78f}, {0.43f, 1.0f, 0.55f}, {0.75f, 1.0f, 0.35f}};
static int col_step(float size) { int k = (int)floorf(size * 6); return k < 0 ? 0 : k > 5 ? 5 : k; }

/* -1..1 on the corners' square -> box pixels */
static inline float bx_of(float x) { return OVL_TILES_X + OVL_SQUARE_X + (x + 1) * 0.5f * OVL_SQUARE; }
static inline float by_of(float y) { return OVL_TILES_Y + (y + 1) * 0.5f * OVL_SQUARE; }

static void project_view(const ovl_shape_t *sh, const ovl_view_t *v, float *x2, float *y2, float *z2) {
    float cr = cosf(v->rot * 6.2831853f), sr = sinf(v->rot * 6.2831853f);
    if (sh->is3d) {
        float cy = cosf(v->yaw), sy = sinf(v->yaw), cp = cosf(v->tilt), sp = sinf(v->tilt);
        for (int i = 0; i < OVL_PATHN; i++) {
            float x = sh->px[i], y = sh->py[i], z = sh->pz[i];
            float x1 = x * cy + z * sy, z1 = -x * sy + z * cy;
            float y1 = y * cp - z1 * sp, zz = y * sp + z1 * cp;
            float persp = 1.0f / (1.0f + 0.25f * zz);
            float a = x1 * persp, b = y1 * persp;
            x2[i] = a * cr - b * sr;
            y2[i] = a * sr + b * cr;
            z2[i] = zz;
        }
    } else
        for (int i = 0; i < OVL_PATHN; i++) {
            x2[i] = sh->px[i] * cr - sh->py[i] * sr;
            y2[i] = sh->px[i] * sr + sh->py[i] * cr;
            z2[i] = 0;
        }
    float m = 0;
    for (int i = 0; i < OVL_PATHN; i++) { if (fabsf(x2[i]) > m) m = fabsf(x2[i]); if (fabsf(y2[i]) > m) m = fabsf(y2[i]); }
    if (m > 1.0f) { float g = 1.0f / m; for (int i = 0; i < OVL_PATHN; i++) { x2[i] *= g; y2[i] *= g; } }
}

static inline void path_at2(const float *x2, const float *y2, float t, float *ox, float *oy) {
    float u = (t - floorf(t)) * OVL_PATHN;
    int i = (int)u;
    float f = u - (float)i;
    i %= OVL_PATHN;
    int j = (i + 1) % OVL_PATHN;
    *ox = x2[i] + (x2[j] - x2[i]) * f;
    *oy = y2[i] + (y2[j] - y2[i]) * f;
}

static inline float clampf_(float x, float a, float b) { return x < a ? a : x > b ? b : x; }

static void render_frame(overlay_t *o, const ovl_shape_t *sh, const ovl_view_t *v, uint8_t *out, uint32_t pitch) {
    (void)o;
    static float x2[OVL_PATHN], y2[OVL_PATHN], z2[OVL_PATHN];
    memset(g_acc, 0, sizeof(float) * OVL_W * OVL_H * 3);
    project_view(sh, v, x2, y2, z2);
    float size = v->size, sr_ = v->sub_size * 0.6f;
    /* the picture under the shape (a PNG/JPG loaded as the shape): dim, in the shape's blue */
    if (sh->has_img && sh->shape_user) {
        for (int y = 0; y < OVL_SQUARE; y += 2)
            for (int x = 0; x < OVL_SQUARE; x += 2) {
                float u = (float)x / OVL_SQUARE, w = (float)y / OVL_SQUARE;
                /* back onto the picture: undo the turn and the size */
                float qx = (u * 2 - 1), qy = (w * 2 - 1);
                if (size > 0.02f) { qx /= size; qy /= size; }
                float cr = cosf(-v->rot * 6.2831853f), sn = sinf(-v->rot * 6.2831853f);
                float ix = qx * cr - qy * sn, iy = qx * sn + qy * cr;
                if (ix < -1 || ix > 1 || iy < -1 || iy > 1) continue;
                int ii = (int)((ix + 1) * 0.5f * (OVL_IMG_N - 1)), jj = (int)((iy + 1) * 0.5f * (OVL_IMG_N - 1));
                float b = sh->img[jj * OVL_IMG_N + ii] * 0.22f;
                if (b < 0.01f) continue;
                for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++)
                    acc_add(OVL_TILES_X + OVL_SQUARE_X + x + dx, OVL_TILES_Y + y + dy, b * 0.35f, b * 0.6f, b, 1);
            }
    }
    /* the corner marks */
    {
        rgbf m = {0.12f, 0.24f, 0.4f};
        float sx0 = OVL_TILES_X + OVL_SQUARE_X, sy0 = OVL_TILES_Y, sx1 = sx0 + OVL_SQUARE, sy1 = sy0 + OVL_SQUARE;
        line_aa(sx0, sy0 + 1, sx0 + 14, sy0 + 1, 2, m, 1); line_aa(sx0 + 1, sy0, sx0 + 1, sy0 + 14, 2, m, 1);
        line_aa(sx1 - 14, sy0 + 1, sx1, sy0 + 1, 2, m, 1); line_aa(sx1 - 1, sy0, sx1 - 1, sy0 + 14, 2, m, 1);
        line_aa(sx0, sy1 - 1, sx0 + 14, sy1 - 1, 2, m, 1); line_aa(sx0 + 1, sy1 - 14, sx0 + 1, sy1, 2, m, 1);
        line_aa(sx1 - 14, sy1 - 1, sx1, sy1 - 1, 2, m, 1); line_aa(sx1 - 1, sy1 - 14, sx1 - 1, sy1, 2, m, 1);
    }
    /* the orbit line: the orbiter's whole path over one orbit, with its suborbit loops, faint */
    if (v->motion && fabsf(v->hz) > 0.05f && sr_ > 0.004f) {
        float ratio = roundf(v->sub / v->hz);
        if (ratio > 12) ratio = 12;
        if (ratio < -12) ratio = -12;
        float b0 = v->oo - ratio * v->ot;
        int ln = (int)(96 + fabsf(ratio) * 40);
        if (ln > 640) ln = 640;
        rgbf c = {0.35f, 0.63f, 0.96f};
        static float LX[640], LY[640];
        for (int k = 0; k < ln; k++) {
            float a = (float)k / (ln - 1), b = b0 + ratio * a, px, py;
            path_at2(x2, y2, a, &px, &py);
            px *= size; py *= size;
            b -= floorf(b);
            LX[k] = bx_of(clampf_(px + sr_ * cosf(6.2831853f * b), -1, 1));
            LY[k] = by_of(clampf_(py + sr_ * sinf(6.2831853f * b), -1, 1));
        }
        polyline_aa(LX, LY, ln, 0, NULL, NULL, 1.1f, 0.3f, c);
    }
    /* the shape: a glowing line; a 3D shape shaded by depth (nearer: brighter and wider) */
    {
        rgbf c = {0.3f, 0.66f, 1.0f};
        static float SX[OVL_PATHN], SY[OVL_PATHN], SW[OVL_PATHN], SK[OVL_PATHN];
        for (int i = 0; i < OVL_PATHN; i++) {
            int j = (i + 1) % OVL_PATHN;
            SX[i] = bx_of(x2[i] * size);
            SY[i] = by_of(y2[i] * size);
            float depth = sh->is3d ? clampf_(0.5f - 0.5f * (z2[i] + z2[j]) * 0.5f, 0, 1) : 0.5f;   /* 0 far .. 1 near */
            SK[i] = sh->is3d ? 0.5f + 0.9f * depth : 1.3f;
            SW[i] = sh->is3d ? 1.6f + 1.4f * depth : 2.5f;
        }
        polyline_aa(SX, SY, OVL_PATHN, 1, SW, SK, 2.5f, 1.3f, c);
    }
    /* the orbiter's light trail: where it has just been, from the orbit and suborbit speeds, a smooth fading curve */
    float fx, fy, ox, oy;
    path_at2(x2, y2, v->ot, &fx, &fy);
    fx *= size; fy *= size;
    ox = clampf_(fx + sr_ * cosf(6.2831853f * v->oo), -1, 1);
    oy = clampf_(fy + sr_ * sinf(6.2831853f * v->oo), -1, 1);
    rgbf oc = ORB_COLS[col_step(v->sub_size)], pc = PATH_COLS[col_step(v->size)];
    if (v->motion) {
        float tl = fabsf(v->sub) > 0.01f ? 0.6f / fabsf(v->sub) : 0.35f;
        if (tl > 0.6f) tl = 0.6f;
        if (tl < 0.06f) tl = 0.06f;
        static float TX[TRAIL_PTS_OVL], TY[TRAIL_PTS_OVL], TW[TRAIL_PTS_OVL], TK[TRAIL_PTS_OVL];
        TX[0] = bx_of(ox); TY[0] = by_of(oy);
        for (int k = 1; k < TRAIL_PTS_OVL; k++) {
            float tk = tl * k / (TRAIL_PTS_OVL - 1), px, py;
            path_at2(x2, y2, v->ot - v->hz * tk, &px, &py);
            px *= size; py *= size;
            float b = v->oo - v->sub * tk;
            TX[k] = bx_of(clampf_(px + sr_ * cosf(6.2831853f * b), -1, 1));
            TY[k] = by_of(clampf_(py + sr_ * sinf(6.2831853f * b), -1, 1));
            float f = 1 - (float)k / TRAIL_PTS_OVL;          /* 1 at the orbiter .. 0 at the end */
            TW[k - 1] = 1.2f + 3.4f * f * f;
            TK[k - 1] = 0.15f + 1.2f * f * f * f;
        }
        polyline_aa(TX, TY, TRAIL_PTS_OVL, 0, TW, TK, 2, 1, oc);
    }
    /* a glow under each ball (it blooms; the sharp ball is painted over it at the end) */
    if (v->motion) {
        float R0 = OVL_BALL_OD * 0.5f, R1 = OVL_BALL_PD * 0.5f;
        disc(clampf_(bx_of(ox), OVL_TILES_X + R0, OVL_TILES_X + OVL_TILES_W - R0), clampf_(by_of(oy), OVL_TILES_Y + R0, OVL_TILES_Y + OVL_TILES_H - R0), R0 * 1.15f, oc, 0.55f);
        disc(clampf_(bx_of(fx), OVL_TILES_X + R1, OVL_TILES_X + OVL_TILES_W - R1), clampf_(by_of(fy), OVL_TILES_Y + R1, OVL_TILES_Y + OVL_TILES_H - R1), R1 * 1.15f, pc, 0.5f);
    }
    /* the bloom: the lights so far, at quarter size, blurred, added back */
    const int LW = OVL_W / 2, LH = OVL_H / 2;
    for (int y = 0; y < LH; y++)
        for (int x = 0; x < LW; x++) {
            float r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; dy++) {
                const float *p = g_acc + ((size_t)(y * 2 + dy) * OVL_W + x * 2) * 3;
                for (int dx = 0; dx < 2; dx++) { r += p[dx * 3]; g += p[dx * 3 + 1]; b += p[dx * 3 + 2]; }
            }
            float *q = g_lo + ((size_t)y * LW + x) * 3;
            q[0] = r * 0.25f; q[1] = g * 0.25f; q[2] = b * 0.25f;
        }
    for (int pass = 0; pass < 3; pass++) {   /* a box blur, radius 3, three times (near enough a gaussian), by running sums */
        const int R = 3;
        const float inv = 1.0f / (2 * R + 1);
        for (int y = 0; y < LH; y++) {   /* rows: g_lo -> g_lo2 */
            const float *p = g_lo + (size_t)y * LW * 3;
            float *q = g_lo2 + (size_t)y * LW * 3;
            float sr = 0, sg = 0, sb = 0;
            for (int d = -R; d <= R; d++) { int xx = d < 0 ? 0 : d; sr += p[xx * 3]; sg += p[xx * 3 + 1]; sb += p[xx * 3 + 2]; }
            for (int x = 0; x < LW; x++) {
                q[x * 3] = sr * inv; q[x * 3 + 1] = sg * inv; q[x * 3 + 2] = sb * inv;
                int xo = x - R < 0 ? 0 : x - R, xi = x + R + 1 >= LW ? LW - 1 : x + R + 1;
                sr += p[xi * 3] - p[xo * 3]; sg += p[xi * 3 + 1] - p[xo * 3 + 1]; sb += p[xi * 3 + 2] - p[xo * 3 + 2];
            }
        }
        for (int x = 0; x < LW; x++) {   /* columns: g_lo2 -> g_lo */
            float sr = 0, sg = 0, sb = 0;
            for (int d = -R; d <= R; d++) { int yy = d < 0 ? 0 : d; const float *p = g_lo2 + ((size_t)yy * LW + x) * 3; sr += p[0]; sg += p[1]; sb += p[2]; }
            for (int y = 0; y < LH; y++) {
                float *q = g_lo + ((size_t)y * LW + x) * 3;
                q[0] = sr * inv; q[1] = sg * inv; q[2] = sb * inv;
                int yo = y - R < 0 ? 0 : y - R, yi = y + R + 1 >= LH ? LH - 1 : y + R + 1;
                const float *pi = g_lo2 + ((size_t)yi * LW + x) * 3, *po = g_lo2 + ((size_t)yo * LW + x) * 3;
                sr += pi[0] - po[0]; sg += pi[1] - po[1]; sb += pi[2] - po[2];
            }
        }
    }
    /* the balls, sharp, over the glow (drawn into the picture itself, after the bloom) */
    int behind = sinf(6.2831853f * v->oo) < 0;
    /* compose: lights + bloom, a vignette, scanlines, into the ARGB picture; then the balls. The empty background
     * (glass, vignette, scanlines) is worked out once; a pixel with no light copies it. */
    static uint8_t shoulder[2049];   /* 1 - exp(-1.4 x) for x in 0..4, as a byte */
    static float vigx[OVL_W], vigy[OVL_H];
    static uint8_t *bg;
    if (!bg) {
        for (int i = 0; i <= 2048; i++) shoulder[i] = (uint8_t)((1 - expf(-1.4f * i * (4.0f / 2048))) * 255 + 0.5f);
        for (int x = 0; x < OVL_W; x++) { float vx = (x - OVL_W * 0.5f) / (OVL_W * 0.5f); vigx[x] = vx * vx * 0.9f; }
        for (int y = 0; y < OVL_H; y++) { float vy = (y - OVL_H * 0.5f) / (OVL_H * 0.5f); vigy[y] = vy * vy * 1.1f - 0.3f; }
        bg = malloc((size_t)OVL_W * OVL_H * 4);
        for (int y = 0; y < OVL_H; y++) {
            float scan = (y % 3 == 0) ? 0.93f : 1.0f;
            for (int x = 0; x < OVL_W; x++) {
                float vg = vigx[x] + vigy[y];
                float vig = (1 - 0.35f * (vg < 0 ? 0 : vg > 1 ? 1 : vg)) * scan;
                uint8_t *q = bg + ((size_t)y * OVL_W + x) * 4;
                q[0] = shoulder[(int)(0.035f * vig * 512)]; q[1] = shoulder[(int)(0.018f * vig * 512)]; q[2] = shoulder[(int)(0.012f * vig * 512)]; q[3] = 255;
            }
        }
    }
    static float brow[(OVL_W / 2 + 1) * 3];   /* this row's bloom, the two half-res rows blended */
    for (int y = 0; y < OVL_H; y++) {
        uint8_t *row = out + (size_t)y * pitch;
        const uint8_t *bgrow = bg + (size_t)y * OVL_W * 4;
        float ly = (y + 0.5f) * 0.5f - 0.5f;
        if (ly < 0) ly = 0;
        int ly0 = (int)ly; float lf = ly - ly0; if (ly0 >= LH - 1) { ly0 = LH - 1; lf = 0; }
        const float *r0 = g_lo + (size_t)ly0 * LW * 3, *r1 = g_lo + (size_t)(ly0 + (lf > 0 ? 1 : 0)) * LW * 3;
        float any = 0;
        for (int i = 0; i < LW * 3; i++) { brow[i] = (r0[i] * (1 - lf) + r1[i] * lf) * 2.4f; any += brow[i]; }
        brow[LW * 3] = brow[LW * 3 - 3]; brow[LW * 3 + 1] = brow[LW * 3 - 2]; brow[LW * 3 + 2] = brow[LW * 3 - 1];
        const float *p = g_acc + (size_t)y * OVL_W * 3;
        if (any < 1e-5f) {   /* no glow on this row: only the lights themselves */
            for (int x = 0; x < OVL_W; x++, p += 3) {
                if (p[0] == 0 && p[1] == 0 && p[2] == 0) { memcpy(row + x * 4, bgrow + x * 4, 4); continue; }
                float vg = vigx[x] + vigy[y];
                float vig = (1 - 0.35f * (vg < 0 ? 0 : vg > 1 ? 1 : vg)) * ((y % 3 == 0) ? 0.93f : 1.0f);
                int ir = (int)((0.012f + p[0]) * vig * 512), ig = (int)((0.018f + p[1]) * vig * 512), ib = (int)((0.035f + p[2]) * vig * 512);
                row[x * 4 + 0] = shoulder[ib > 2048 ? 2048 : ib];
                row[x * 4 + 1] = shoulder[ig > 2048 ? 2048 : ig];
                row[x * 4 + 2] = shoulder[ir > 2048 ? 2048 : ir];
                row[x * 4 + 3] = 255;
            }
            continue;
        }
        float scan = (y % 3 == 0) ? 0.93f : 1.0f;
        for (int x = 0; x < OVL_W; x++, p += 3) {
            float lx = (x + 0.5f) * 0.5f - 0.5f;
            if (lx < 0) lx = 0;
            int lx0 = (int)lx; float ff = lx - lx0;
            const float *b0 = brow + lx0 * 3, *b1 = b0 + 3;
            float r = p[0] + b0[0] + (b1[0] - b0[0]) * ff;
            float g = p[1] + b0[1] + (b1[1] - b0[1]) * ff;
            float b = p[2] + b0[2] + (b1[2] - b0[2]) * ff;
            if (r + g + b < 0.002f) { memcpy(row + x * 4, bgrow + x * 4, 4); continue; }
            float vg = vigx[x] + vigy[y];
            float vig = (1 - 0.35f * (vg < 0 ? 0 : vg > 1 ? 1 : vg)) * scan;
            int ir = (int)((0.012f + r) * vig * 512), ig = (int)((0.018f + g) * vig * 512), ib = (int)((0.035f + b) * vig * 512);
            row[x * 4 + 0] = shoulder[ib > 2048 ? 2048 : ib];
            row[x * 4 + 1] = shoulder[ig > 2048 ? 2048 : ig];
            row[x * 4 + 2] = shoulder[ir > 2048 ? 2048 : ir];
            row[x * 4 + 3] = 255;
        }
    }
    if (v->motion) {
        /* the balls: drawn straight into the picture, a soft edge and a hot core, the orbiter behind the path point
         * on the upper half of its circle; whole inside the screen */
        float X[2] = {bx_of(ox), bx_of(fx)}, Y[2] = {by_of(oy), by_of(fy)}, R[2] = {OVL_BALL_OD * 0.5f, OVL_BALL_PD * 0.5f};
        rgbf C[2] = {oc, pc};
        int order[2] = {behind ? 0 : 1, behind ? 1 : 0};
        for (int q = 0; q < 2; q++) {
            int k = order[q];
            float cx = clampf_(X[k], OVL_TILES_X + R[k], OVL_TILES_X + OVL_TILES_W - R[k]), cy = clampf_(Y[k], OVL_TILES_Y + R[k], OVL_TILES_Y + OVL_TILES_H - R[k]);
            int bx0 = (int)(cx - R[k] - 2), bx1 = (int)(cx + R[k] + 2), by0 = (int)(cy - R[k] - 2), by1 = (int)(cy + R[k] + 2);
            for (int y = by0 < 0 ? 0 : by0; y <= by1 && y < OVL_H; y++) {
                uint8_t *row = out + (size_t)y * pitch;
                for (int x = bx0 < 0 ? 0 : bx0; x <= bx1 && x < OVL_W; x++) {
                    float ex = x + 0.5f - cx, ey = y + 0.5f - cy, d = sqrtf(ex * ex + ey * ey);
                    float cov = R[k] + 0.5f - d;
                    if (cov <= 0) continue;
                    if (cov > 1) cov = 1;
                    float u = d / R[k];
                    float hi = (1 - u) * (1 - u) * 0.75f;                     /* a highlight toward the centre */
                    float sh = 0.75f + 0.25f * (1 - u);                        /* a little shading at the rim */
                    float r = clampf_(C[k].r * sh + hi, 0, 1), g = clampf_(C[k].g * sh + hi, 0, 1), b = clampf_(C[k].b * sh + hi, 0, 1);
                    row[x * 4 + 0] = (uint8_t)(row[x * 4 + 0] * (1 - cov) + b * 255 * cov + 0.5f);
                    row[x * 4 + 1] = (uint8_t)(row[x * 4 + 1] * (1 - cov) + g * 255 * cov + 0.5f);
                    row[x * 4 + 2] = (uint8_t)(row[x * 4 + 2] * (1 - cov) + r * 255 * cov + 0.5f);
                }
            }
        }
    }
}

/* one frame into a caller's buffer (the offline test and preview); 0: ok */
int overlay_render_test(const ovl_shape_t *sh, const ovl_view_t *v, uint8_t *out, uint32_t pitch) {
    if (!g_acc) {
        g_acc = calloc((size_t)OVL_W * OVL_H * 3, sizeof(float));
        g_lo = calloc((size_t)(OVL_W / 2) * (OVL_H / 2) * 3, sizeof(float));
        g_lo2 = calloc((size_t)(OVL_W / 2) * (OVL_H / 2) * 3, sizeof(float));
        g_cov = calloc((size_t)OVL_W * OVL_H, sizeof(float));
        if (!g_acc || !g_lo || !g_lo2 || !g_cov) return -1;
    }
    render_frame(NULL, sh, v, out, pitch);
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the thread */
/* a seqlock read: copy, and again if the writer was in the middle of it */
static void read_locked(unsigned *seq, const void *src, void *dst, size_t n) {
    unsigned s0, s1;
    do {
        s0 = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
        if (s0 & 1) { usleep(50); continue; }
        memcpy(dst, src, n);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        s1 = __atomic_load_n(seq, __ATOMIC_ACQUIRE);
    } while (s0 != s1 || (s0 & 1));
}

static pthread_mutex_t g_owner_lock = PTHREAD_MUTEX_INITIALIZER;
static overlay_t *g_owner;   /* one VADER instance in this process draws the live screen; the others keep the tiles */

static void *ovl_thread(void *p) {
    overlay_t *o = p;
    logf_(o, "VADER overlay starting (box %dx%d)", OVL_W, OVL_H);
    /* the owner: the first instance; another waits for it to go (its page can't be told apart on the display) */
    for (;;) {
        pthread_mutex_lock(&g_owner_lock);
        if (!g_owner) g_owner = o;
        int mine = g_owner == o;
        pthread_mutex_unlock(&g_owner_lock);
        if (mine) break;
        if (o->stop) return NULL;
        if (!o->waited_logged) { o->waited_logged = 1; logf_(o, "another VADER instance has the live screen: waiting (the tile screen stays meanwhile)"); }
        for (int i = 0; i < 20 && !o->stop; i++) usleep(100000);
    }
    int tries = 0;
    while (!o->stop && drm_setup(o)) {
        for (int i = 0; i < 2; i++) destroy_buffer(o, &o->buf[i]);
        if (++tries >= 3) { logf_(o, "giving up: the tile screen stays"); goto give_up; }
        logf_(o, "retry %d in 3 s", tries);
        for (int i = 0; i < 30 && !o->stop; i++) usleep(100000);
    }
    if (o->stop) goto give_up;
    g_acc = calloc((size_t)OVL_W * OVL_H * 3, sizeof(float));
    g_lo = calloc((size_t)(OVL_W / 2) * (OVL_H / 2) * 3, sizeof(float));
    g_lo2 = calloc((size_t)(OVL_W / 2) * (OVL_H / 2) * 3, sizeof(float));
    g_cov = calloc((size_t)OVL_W * OVL_H, sizeof(float));
    if (!g_acc || !g_lo || !g_lo2 || !g_cov) { logf_(o, "out of memory"); return NULL; }
    __atomic_store_n(&o->live, 1, __ATOMIC_RELEASE);
    logf_(o, "live: drawing %d frames a second", OVL_FPS);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    int frame = 0, shown = 0, fails = 0;
    double busy = 0;
    static ovl_view_t v;
    static ovl_shape_t sh;
    unsigned shape_seen = ~0u;
    while (!o->stop) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int on = 0;
        if (frame % 3 == 0 || !o->box_found) on = locate(o), o->on_screen = on;   /* 10 times a second, or until found */
        else on = o->on_screen;
        if (on && !o->fmt_unknown) {
            read_locked(&o->seq, &o->view, &v, sizeof v);
            unsigned s2 = __atomic_load_n(&o->seq2, __ATOMIC_ACQUIRE);
            if (s2 != shape_seen) { read_locked(&o->seq2, &o->shape, &sh, sizeof sh); shape_seen = __atomic_load_n(&o->seq2, __ATOMIC_ACQUIRE); }
            ovl_buf_t *b = &o->buf[o->back];
            render_frame(o, &sh, &v, b->map, b->pitch);
            clock_gettime(CLOCK_MONOTONIC, &t1);   /* the drawing's time; SETPLANE may wait for the display */
            if (set_plane(o, b->fb_id, o->box_x, o->box_y)) {
                if (++fails == 1 || fails % 300 == 0) logf_(o, "SETPLANE: %s (%d so far)", strerror(errno), fails);
                if (errno == EACCES || errno == EPERM) { logf_(o, "not the display's master: the tile screen stays"); __atomic_store_n(&o->live, 0, __ATOMIC_RELEASE); break; }
            } else {
                o->back ^= 1;
                if (!shown) { shown = 1; logf_(o, "shown at %d,%d", o->box_x, o->box_y); }
            }
        } else if (shown) {
            set_plane(o, 0, 0, 0);
            shown = 0;
            logf_(o, "hidden (%s)", o->fmt_unknown ? "the display's picture can't be read" : "the screen box is not on the display");
        }
        if (!(on && !o->fmt_unknown)) clock_gettime(CLOCK_MONOTONIC, &t1);
        busy += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        frame++;
        if (frame % (OVL_FPS * 30) == 0) { logf_(o, "%d frames, %.2f ms a frame (lately %.2f), %s", frame, busy / frame, o->ms_avg, shown ? "shown" : "hidden"); }
        /* the pace: 30 frames a second when a frame takes under half of that; slower when the device is slower, so
         * this thread never takes a whole core from MPC */
        double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        o->ms_avg += (ms - o->ms_avg) * 0.05;
        long period = 1000000000L / OVL_FPS;
        if (o->ms_avg * 2.2 > 1000.0 / OVL_FPS) period = (long)(o->ms_avg * 2.2e6);
        if (period > 100000000L) period = 100000000L;   /* never under 10 a second */
        next.tv_nsec += period;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - next.tv_sec) * 1e9 + (now.tv_nsec - next.tv_nsec) > 2e8) next = now;   /* far behind: don't catch up */
    }
    set_plane(o, 0, 0, 0);
    for (int i = 0; i < 2; i++) destroy_buffer(o, &o->buf[i]);
    unmap_scanouts();
    free(g_acc); free(g_lo); free(g_lo2); free(g_cov);
    g_acc = g_lo = g_lo2 = g_cov = NULL;
    __atomic_store_n(&o->live, 0, __ATOMIC_RELEASE);
    logf_(o, "stopped after %d frames", frame);
give_up:
    pthread_mutex_lock(&g_owner_lock);
    if (g_owner == o) g_owner = NULL;
    pthread_mutex_unlock(&g_owner_lock);
    return NULL;
}

/* ------------------------------------------------------------------------------------------------ the engine's side */
static void write_locked(unsigned *seq, void *dst, const void *src, size_t n) {
    __atomic_store_n(seq, *seq + 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    memcpy(dst, src, n);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(seq, *seq + 1, __ATOMIC_RELEASE);
}
void overlay_publish(overlay_t *o, const ovl_view_t *v) {
    if (!o->started) return;
    write_locked(&o->seq, &o->view, v, sizeof *v);
}
void overlay_publish_shape(overlay_t *o, const ovl_shape_t *sh) {
    if (!o->started) return;
    write_locked(&o->seq2, &o->shape, sh, sizeof *sh);
}

int overlay_live(const overlay_t *o) { return __atomic_load_n(&o->live, __ATOMIC_ACQUIRE); }

void overlay_start(overlay_t *o, const char *plugin_dir) {
    memset(o, 0, sizeof *o);
    o->fd = -1;
    if (!plugin_dir || !plugin_dir[0]) return;
    char off[700];
    snprintf(off, sizeof off, "%s/overlay.off", plugin_dir);
    if (access(off, F_OK) == 0) return;
    snprintf(o->log_path, sizeof o->log_path, "%s/overlay.log", plugin_dir);
    FILE *f = fopen(o->log_path, "w");   /* a fresh log each start */
    if (f) fclose(f); else { o->log_path[0] = 0; return; }   /* can't write there (tests): no overlay */
    if (access("/proc/self/fd", R_OK)) return;
    o->started = 1;
    if (pthread_create(&o->thread, NULL, ovl_thread, o)) { o->started = 0; logf_(o, "no thread"); }
}

void overlay_stop(overlay_t *o) {
    if (!o->started) return;
    o->stop = 1;
    pthread_join(o->thread, NULL);
    o->started = 0;
}
