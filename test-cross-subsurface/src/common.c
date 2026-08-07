/*
 * common.c - Shared implementations for test-cross-subsurface.
 *
 * Contains: create_solid_buffer, fill_buffer_color,
 *           on_surface_token, exported_listener, bind_wl_globals.
 */

#include "common.h"

/* ─────────────────────────────── wl_shm helpers ────────── */

struct wl_buffer *create_solid_buffer(struct wl_shm *shm,
    int w, int h, uint32_t color_argb,
    void **out_data, struct wl_shm_pool **out_pool)
{
    int stride = w * 4;
    int size   = stride * h;

    int fd = memfd_create("wl_shm", MFD_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[error] memfd_create: %s\n", strerror(errno));
        return NULL;
    }
    if (ftruncate(fd, size) < 0) {
        fprintf(stderr, "[error] ftruncate: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "[error] mmap: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    if (!pool) {
        fprintf(stderr, "[error] wl_shm_create_pool failed\n");
        munmap(data, size);
        close(fd);
        return NULL;
    }

    uint32_t *px = (uint32_t *)data;
    for (int i = 0; i < w * h; ++i)
        px[i] = color_argb;

    struct wl_buffer *buf = wl_shm_pool_create_buffer(
        pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    if (!buf) {
        fprintf(stderr, "[error] create_buffer failed\n");
        wl_shm_pool_destroy(pool);
        munmap(data, size);
        close(fd);
        return NULL;
    }

    /* fd is duplicated by wl_shm_create_pool, safe to close */
    close(fd);

    if (out_data) *out_data = data;
    if (out_pool)  *out_pool  = pool;
    return buf;
}

void fill_buffer_color(void *data, int w, int h, uint32_t color)
{
    uint32_t *px = (uint32_t *)data;
    for (int i = 0; i < w * h; ++i)
        px[i] = color;
}

/* ──────────────────── exported_surface listener ─────────── */

static void on_surface_token(void *data,
    struct treeland_exported_surface_v1 *exported, const char *token)
{
    (void)exported;
    char **out = (char **)data;
    free(*out);
    *out = strdup(token);
    fprintf(stderr, "surface_token: %s\n", token);
}

/* ── protocol error loggers ─────────────────────────────── */

const struct treeland_exported_surface_v1_listener exported_listener = {
    .surface_token = on_surface_token,
};

/* ─────────────────── shared registry binding ───────────── */

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    struct WlGlobals *g = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
        g->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, MIN(version, 4u));
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        g->shm = wl_registry_bind(registry, name,
            &wl_shm_interface, MIN(version, 1u));
    else if (strcmp(interface,
               treeland_subsurface_manager_v1_interface.name) == 0)
        g->manager = wl_registry_bind(registry, name,
            &treeland_subsurface_manager_v1_interface,
            MIN(version, 1u));
}

static void registry_global_remove(void *data,
    struct wl_registry *reg, uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

void bind_wl_globals(struct wl_display *display, struct WlGlobals *g)
{
    memset(g, 0, sizeof(*g));
    g->wl_display = display;
    struct wl_registry *reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &registry_listener, g);
    wl_display_roundtrip(display);
    wl_registry_destroy(reg);
}
