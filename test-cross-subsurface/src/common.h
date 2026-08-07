/*
 * common.h - Shared definitions for test-cross-subsurface.
 *
 * Split from the original single-file client into:
 *   - common.h / common.c   (shared code)
 *   - parent.c              (parent binary)
 *   - child.c               (child binary)
 */

#ifndef COMMON_H
#define COMMON_H

/* Feature-test macros — must precede all includes */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <unistd.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "treeland-cross-subsurface-unstable-v1-client-protocol.h"

/* ── constants ──────────────────────────────────────────── */

#define WINDOW_W    700
#define WINDOW_H    400
#define MOVE_STEP   20
#define NUM_SUBSURFACES 5

static const uint32_t color_cycle[] = {
    0xFF44CC66,  /* green  */
    0xFFCC4444,  /* red    */
    0xFF4466CC,  /* blue   */
    0xFFCCCC44,  /* yellow */
    0xFFCC44CC,  /* magenta*/
    0xFF44CCCC,  /* cyan   */
    0xFFFFFFFF,  /* white  */
};
#define NUM_COLORS (int)(sizeof(color_cycle) / sizeof(color_cycle[0]))

/* ── structs ────────────────────────────────────────────── */

struct WlGlobals {
    struct wl_display *wl_display;
    struct wl_compositor  *compositor;
    struct wl_shm         *shm;
    struct treeland_subsurface_manager_v1 *manager;
};

struct SubSurface {
    struct wl_surface  *surface;
    struct wl_buffer   *buffer;
    struct wl_shm_pool *pool;
    void               *buf_data;
    char               *token;
    struct treeland_exported_surface_v1  *exported;
    struct treeland_remote_subsurface_v1 *remote;
    int x, y;
    int color_idx;
    bool alive;
};

/* ── function declarations ──────────────────────────────── */

struct wl_buffer *create_solid_buffer(struct wl_shm *shm,
    int w, int h, uint32_t color_argb,
    void **out_data, struct wl_shm_pool **out_pool);

void fill_buffer_color(void *data, int w, int h, uint32_t color);

void bind_wl_globals(struct wl_display *display, struct WlGlobals *g);

void sub_cleanup(struct SubSurface *s);

/* listener defined in common.c */
extern const struct treeland_exported_surface_v1_listener exported_listener;

#endif /* COMMON_H */
