/* VADER's live screen (0.5.0): drawn by the plugin onto a hardware overlay plane of the display. See overlay.c. */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

/* the screen box (tools/vader_skin.py SCREEN / TILES_XY): 656 x 296, the tiles 640 x 288 at (8, 4) inside it, the
 * corners' square 288 wide at x 176 of the tiles */
#define OVL_W 656
#define OVL_H 296
#define OVL_TILES_X 8
#define OVL_TILES_Y 4
#define OVL_TILES_W 640
#define OVL_TILES_H 288
#define OVL_SQUARE_X 176
#define OVL_SQUARE 288
#define OVL_PATHN 256
#define OVL_IMG_N 64
#define OVL_BALL_OD 32
#define OVL_BALL_PD 22

/* what to draw: the moving part, published by the audio thread every block (overlay_publish) */
typedef struct {
    int motion;
    float rot, yaw, tilt, size, sub_size;               /* ROTATE (turns), the 3D spin and tilt (radians), the sizes */
    float ot, oo, hz, sub;                              /* the orbit and suborbit phases and speeds (turns, turns/s) */
} ovl_view_t;
/* and the shape, published when it changes (overlay_publish_shape) */
typedef struct {
    float px[OVL_PATHN], py[OVL_PATHN], pz[OVL_PATHN];   /* the shape's path (unturned) */
    int is3d, shape_user, has_img;
    float img[OVL_IMG_N * OVL_IMG_N];                   /* the loaded picture's brightness (shape_user && has_img) */
} ovl_shape_t;

typedef struct { uint32_t handle, pitch, fb_id; uint64_t size; uint8_t *map; } ovl_buf_t;

typedef struct {
    char log_path[700];
    int started, stop, live, waited_logged;
    pthread_t thread;
    /* DRM */
    int fd, crtc_index;
    uint32_t crtc_id, plane_id, format, disp_w, disp_h;
    uint64_t zpos_max;
    int has_zpos;
    ovl_buf_t buf[2];
    int back, fb_slot;
    /* the box on the display */
    int box_found, box_logged, box_x, box_y, on_screen, fmt_unknown, fb_logged, fb_err_logged;
    double ms_avg;                                     /* a frame's drawing time, lately */
    /* the snapshots (seqlocks) */
    unsigned seq, seq2;
    ovl_view_t view;
    ovl_shape_t shape;
} overlay_t;

void overlay_start(overlay_t *o, const char *plugin_dir);   /* the thread; nothing when plugin_dir/overlay.off exists */
void overlay_stop(overlay_t *o);
void overlay_publish(overlay_t *o, const ovl_view_t *v);   /* audio thread, every block */
void overlay_publish_shape(overlay_t *o, const ovl_shape_t *sh);   /* audio thread, when the shape or picture changes */
int overlay_live(const overlay_t *o);                      /* 1: the plane is up: the tile screen can rest */
int overlay_render_test(const ovl_shape_t *sh, const ovl_view_t *v, uint8_t *out, uint32_t pitch);   /* one frame, offline */
int overlay_find_test(uint8_t *xrgb, int w, int h, int pitch, int *bx, int *by);   /* the box finder on a picture of the caller's */
