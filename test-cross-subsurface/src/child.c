/*
 * test-cross-subsurface-child
 *
 * One-shot: connect, create remote subsurface, apply operations from
 * command-line arguments, commit, and exit.
 *
 * Usage:
 *   test-cross-subsurface-child --parent-token <TOKEN> [options...]
 *
 * Options:
 *   --pos <x> <y>           set_position (default: 50 50)
 *   --above [token]         place_above token (default: parent)
 *   --below [token]         place_below token
 *   --color <idx>            color index 0-6 (default: 0)
 *   --size <w> <h>           buffer size (default: 300x200)
 *   --no-attach              export surface only, no remote subsurface
 *   --help                  show this help
 */

#include "common.h"
#include <poll.h>
#include <time.h>

/* cross-process subsurface: large, to distinguish from parent's local subs */
#define CHILD_W 300
#define CHILD_H 200

/* ── parsed arguments ─────────────────────────────────── */

struct child_args {
    const char *parent_token;
    int         pos_x, pos_y;
    bool        set_pos;
    bool        do_above, do_below;
    const char *z_token;       /* NULL = use parent_token */
    int         color_idx;
    bool        set_color;
    int         buf_w, buf_h;
    bool        set_size;
    bool        no_attach;
};

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s --parent-token <TOKEN> [options]\n\n"
        "Options:\n"
        "  --pos <x> <y>       set_position (default: 50 50)\n"
        "  --above-parent      place_above parent_token\n"
        "  --above [token]     place_above token (default: parent)\n"
        "  --below-parent      place_below parent_token\n"
        "  --below [token]     place_below token\n"
        "  --color <0-6>        color index (default: 0)\n"
        "  --size <w> <h>       buffer size (default: 300x200)\n"
        "  --no-attach          export surface only, no remote subsurface\n"
        "  --help              show this help\n");
}

static int parse_args(int argc, char **argv, struct child_args *a)
{
    memset(a, 0, sizeof(*a));
    a->pos_x = 50;
    a->pos_y = 50;
    a->color_idx = -1;
    a->buf_w = CHILD_W;
    a->buf_h = CHILD_H;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--parent-token") == 0 && i + 1 < argc) {
            a->parent_token = argv[++i];
        } else if (strcmp(argv[i], "--pos") == 0 && i + 2 < argc) {
            a->pos_x = atoi(argv[++i]);
            a->pos_y = atoi(argv[++i]);
            a->set_pos = true;
        } else if (strcmp(argv[i], "--above-parent") == 0) {
            a->do_above = true;
            a->z_token = NULL;
        } else if (strcmp(argv[i], "--above") == 0) {
            a->do_above = true;
            a->z_token = (i + 1 < argc && argv[i + 1][0] != '-')
                ? argv[++i] : NULL;
        } else if (strcmp(argv[i], "--below-parent") == 0) {
            a->do_below = true;
            a->z_token = NULL;
        } else if (strcmp(argv[i], "--below") == 0) {
            a->do_below = true;
            a->z_token = (i + 1 < argc && argv[i + 1][0] != '-')
                ? argv[++i] : NULL;
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            a->color_idx = atoi(argv[++i]);
            a->set_color = true;
        } else if (strcmp(argv[i], "--size") == 0 && i + 2 < argc) {
            a->buf_w = atoi(argv[++i]);
            a->buf_h = atoi(argv[++i]);
            a->set_size = true;
        } else if (strcmp(argv[i], "--no-attach") == 0) {
            a->no_attach = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    return a->parent_token ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    struct child_args a;
    if (parse_args(argc, argv, &a) != 0) return 1;

    if (a.color_idx < 0) {
        srand(time(NULL) ^ getpid());
        a.color_idx = rand() % NUM_COLORS;
    }
    if (a.color_idx < 0 || a.color_idx >= NUM_COLORS) {
        fprintf(stderr, "[error] color index out of range 0-%d\n", NUM_COLORS - 1);
        return 1;
    }

    fprintf(stderr, "[child] parent_token: %s\n", a.parent_token);

    /* ── Wayland connection ── */
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "[error] wl_display_connect failed\n");
        return 1;
    }

    struct WlGlobals g;
    bind_wl_globals(display, &g);

    if (!g.compositor || !g.shm || !g.manager) {
        fprintf(stderr, "[error] missing Wayland globals\n");
        wl_display_disconnect(display); return 1;
    }

    /* ── create surface + buffer ── */
    struct wl_surface *surf = wl_compositor_create_surface(g.compositor);
    if (!surf) {
        fprintf(stderr, "[error] create_surface failed\n");
        wl_display_disconnect(display); return 1;
    }

    void *buf_data = NULL;
    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *buffer = create_solid_buffer(g.shm, a.buf_w, a.buf_h,
        color_cycle[a.color_idx], &buf_data, &pool);
    if (!buffer) {
        wl_surface_destroy(surf);
        wl_display_disconnect(display); return 1;
    }

    wl_surface_attach(surf, buffer, 0, 0);
    wl_surface_commit(surf);

    /* ── export ── */
    char *child_token = NULL;
    struct treeland_exported_surface_v1 *exported =
        treeland_subsurface_manager_v1_export_surface(g.manager, surf);
    treeland_exported_surface_v1_add_listener(
        exported, &exported_listener, &child_token);
    wl_display_roundtrip(display);

    fprintf(stderr, "[child] child_token: %s\n",
        child_token ? child_token : "(null)");
    fprintf(stderr, "[child] size: %dx%d  color: #%06X\n",
        a.buf_w, a.buf_h, color_cycle[a.color_idx] & 0xFFFFFF);

    if (a.no_attach) {
        fprintf(stderr, "[child] --no-attach: exported without remote subsurface\n");
        goto cleanup;
    }

    /* ── create remote subsurface ── */
    struct treeland_remote_subsurface_v1 *remote =
        treeland_exported_surface_v1_create_remote_subsurface(
            exported, a.parent_token);
    if (!remote) {
        fprintf(stderr, "[error] create_remote_subsurface failed\n");
        goto cleanup;
    }

    if (a.set_pos) {
        treeland_remote_subsurface_v1_set_position(remote, a.pos_x, a.pos_y);
        fprintf(stderr, "[child] pos -> (%d, %d)\n", a.pos_x, a.pos_y);
    }

    if (a.do_above) {
        const char *ref = a.z_token ? a.z_token : a.parent_token;
        treeland_remote_subsurface_v1_place_above(remote, ref);
        fprintf(stderr, "[child] place_above(\"%s\")\n", ref);
    }

    if (a.do_below) {
        const char *ref = a.z_token ? a.z_token : a.parent_token;
        treeland_remote_subsurface_v1_place_below(remote, ref);
        fprintf(stderr, "[child] place_below(\"%s\")\n", ref);
    }

    wl_surface_commit(surf);
    wl_display_roundtrip(display);
    fprintf(stderr, "[child] done\n");

    /* keep alive until stdin closes or signal */
    fprintf(stderr, "[child] running (close stdin or Ctrl+C to exit)\n");
    struct pollfd pfds[2] = {
        { .fd = wl_display_get_fd(display),   .events = POLLIN },
        { .fd = STDIN_FILENO,                 .events = POLLIN },
    };
    while (poll(pfds, 2, -1) > 0) {
        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            char buf[64];
            if (read(STDIN_FILENO, buf, sizeof(buf)) <= 0) break;
        }
        wl_display_dispatch_pending(display);
        wl_display_flush(display);
    }

cleanup:
    if (remote)       treeland_remote_subsurface_v1_destroy(remote);
    if (exported)      treeland_exported_surface_v1_destroy(exported);
    if (buffer)        wl_buffer_destroy(buffer);
    if (pool) {
        wl_shm_pool_destroy(pool);
        if (buf_data) munmap(buf_data, a.buf_w * a.buf_h * 4);
    }
    if (surf)          wl_surface_destroy(surf);
    if (g.manager)     treeland_subsurface_manager_v1_destroy(g.manager);
    free(child_token);
    wl_display_disconnect(display);
    return 0;
}
