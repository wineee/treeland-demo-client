/*
 * test-cross-subsurface-child
 *
 * Creates a raw wl_surface as a remote subsurface of the parent's
 * exported surface.  No visible window — controlled via stdin commands.
 *
 * Usage:
 *   test-cross-subsurface-child --parent-token <TOKEN>
 *
 * Commands (one per line, stdin):
 *   pos <x> <y>   set_position
 *   top            place_above(parent_token) — above parent
 *   color          cycle color
 *   destroy        destroy remote subsurface
 *   recreate       recreate remote subsurface
 *   help           show available commands
 *   quit           exit
 */

#include "common.h"

#include <poll.h>
#include <signal.h>

/* cross-process subsurface: large, to distinguish from parent's local subs */
#define CHILD_W 300
#define CHILD_H 200

/* ─────────────────────────────── app state ────────────── */

static volatile sig_atomic_t g_running = 1;

static void sigint_handler(int sig) { (void)sig; g_running = 0; }

struct child_app {
    struct wl_display *display;
    struct wl_surface *surface;
    struct wl_buffer *buffer;
    struct wl_shm_pool *pool;
    void               *buf_data;
    struct treeland_exported_surface_v1  *exported;
    struct treeland_remote_subsurface_v1 *remote;
    const char         *parent_token;
    int pos_x, pos_y;
    int color_idx;
    bool alive;
};

/* ───────────────────────── command processing ─────────── */

static void app_destroy_sub(struct child_app *a)
{
    if (!a->remote) return;
    treeland_remote_subsurface_v1_destroy(a->remote);
    a->remote = NULL;
    a->alive  = false;
    wl_surface_attach(a->surface, NULL, 0, 0);
    wl_surface_commit(a->surface);
    wl_display_flush(a->display);
    fprintf(stderr, "[child] destroyed\n");
}

static void app_recreate_sub(struct child_app *a)
{
    if (a->alive || !a->exported) return;
    a->remote = treeland_exported_surface_v1_create_remote_subsurface(
        a->exported, a->parent_token);
    if (!a->remote) return;
    treeland_remote_subsurface_v1_set_position(a->remote, a->pos_x, a->pos_y);
    wl_surface_attach(a->surface, a->buffer, 0, 0);
    wl_surface_commit(a->surface);
    wl_display_flush(a->display);
    a->alive = true;
    fprintf(stderr, "[child] recreated at (%d, %d)\n", a->pos_x, a->pos_y);
}

static void process_line(struct child_app *a, const char *line)
{
    char cmd[32] = {0};
    char tok[256] = {0};
    int x = 0, y = 0;

    if (sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#' || cmd[0] == '\0')
        return;

    if (strcmp(cmd, "pos") == 0) {
        if (sscanf(line, "%*s %d %d", &x, &y) == 2 && a->remote) {
            a->pos_x = x; a->pos_y = y;
            treeland_remote_subsurface_v1_set_position(a->remote, x, y);
            wl_display_flush(a->display);
            fprintf(stderr, "[child] pos -> (%d, %d)\n", x, y);
        }
    } else if (strcmp(cmd, "above") == 0) {
        if (!a->remote) return;
        const char *ref = a->parent_token;  /* default: parent */
        if (sscanf(line, "%*s %255s", tok) == 1)
            ref = tok;
        treeland_remote_subsurface_v1_place_above(a->remote, ref);
        wl_display_flush(a->display);
        fprintf(stderr, "[child] place_above(\"%s\")\n", ref);
    } else if (strcmp(cmd, "below") == 0) {
        if (!a->remote) return;
        const char *ref = a->parent_token;  /* default: parent */
        if (sscanf(line, "%*s %255s", tok) == 1)
            ref = tok;
        treeland_remote_subsurface_v1_place_below(a->remote, ref);
        wl_display_flush(a->display);
        fprintf(stderr, "[child] place_below(\"%s\")\n", ref);
    } else if (strcmp(cmd, "color") == 0) {
        a->color_idx = (a->color_idx + 1) % NUM_COLORS;
        if (a->buf_data) {
            fill_buffer_color(a->buf_data, CHILD_W, CHILD_H,
                color_cycle[a->color_idx]);
            wl_surface_damage_buffer(a->surface, 0, 0, CHILD_W, CHILD_H);
            wl_surface_commit(a->surface);
            wl_display_flush(a->display);
        }
        fprintf(stderr, "[child] color -> #%06X\n",
            color_cycle[a->color_idx] & 0xFFFFFF);
    } else if (strcmp(cmd, "destroy") == 0 || strcmp(cmd, "d") == 0) {
        app_destroy_sub(a);
    } else if (strcmp(cmd, "recreate") == 0 || strcmp(cmd, "r") == 0) {
        app_recreate_sub(a);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
        fprintf(stderr,
            "Commands:\n"
            "  pos <x> <y>       set_position\n"
            "  above [token]      above parent (or token)\n"
            "  below [token]      below parent (or token)\n"
            "  color              cycle color\n"
            "  destroy            destroy remote subsurface\n"
            "  recreate           recreate remote subsurface\n"
            "  quit               exit\n");
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        g_running = 0;
    } else if (line[0] != '\0') {
        fprintf(stderr, "[child] unknown command: %s (type 'help')\n", cmd);
    }
}

/* ─────────────────────────────── main ─────────────────── */

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s --parent-token <TOKEN>\n"
        "  -h, --help  Show this help\n",
        argv0);
}

int main(int argc, char **argv)
{
    const char *parent_token_arg = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--parent-token") == 0 && i + 1 < argc) {
            parent_token_arg = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]); return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]); return 1;
        }
    }

    if (!parent_token_arg) {
        fprintf(stderr, "Error: --parent-token TOKEN is required\n");
        print_usage(argv[0]);
        return 1;
    }

    fprintf(stderr, "[child] started, parent_token: %s\n", parent_token_arg);

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

    /* ── raw wl_surface + buffer ── */
    struct wl_surface *sub_surf = wl_compositor_create_surface(g.compositor);
    if (!sub_surf) {
        fprintf(stderr, "[error] create_surface failed\n");
        wl_display_disconnect(display); return 1;
    }

    struct child_app app = {
        .display      = display,
        .surface      = sub_surf,
        .parent_token = parent_token_arg,
        .pos_x        = 50,
        .pos_y        = 50,
        .color_idx    = 0,
        .alive        = false,
    };

    app.buffer = create_solid_buffer(g.shm, CHILD_W, CHILD_H,
        color_cycle[app.color_idx], &app.buf_data, &app.pool);
    if (!app.buffer) {
        wl_surface_destroy(sub_surf);
        wl_display_disconnect(display); return 1;
    }

    wl_surface_attach(sub_surf, app.buffer, 0, 0);
    wl_surface_commit(sub_surf);

    /* ── export + remote subsurface ── */
    char *child_token = NULL;
    app.exported = treeland_subsurface_manager_v1_export_surface(
        g.manager, sub_surf);
    treeland_exported_surface_v1_add_listener(
        app.exported, &exported_listener, &child_token);
    wl_display_roundtrip(display);

    fprintf(stderr, "  child_token: %s\n",
        child_token ? child_token : "(null)");

    app_recreate_sub(&app);

    if (!app.alive) {
        fprintf(stderr, "[error] create_remote_subsurface failed\n");
        /* continue anyway so user can try 'recreate' */
    }

    /* ── SIGINT for clean exit ── */
    struct sigaction sa = { .sa_handler = sigint_handler };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /* ── event loop: poll on wl_display fd + stdin ── */
    fprintf(stderr, "[child] ready — type 'help' for commands\n");

    while (g_running) {
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);

        struct pollfd pfds[2] = {
            { .fd = wl_display_get_fd(display), .events = POLLIN },
            { .fd = STDIN_FILENO,               .events = POLLIN },
        };

        int ret = poll(pfds, 2, 200);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (ret == 0) {
            wl_display_cancel_read(display);
            continue;
        }

        /* must complete prepare_read cycle BEFORE any other wl calls */
        if (pfds[0].revents) {
            wl_display_read_events(display);
            wl_display_dispatch_pending(display);
        } else {
            wl_display_cancel_read(display);
        }

        /* now safe to process stdin commands */
        if (pfds[1].revents) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n <= 0) break;  /* EOF */
            buf[n] = '\0';

            char *saveptr = NULL;
            char *line = strtok_r(buf, "\n", &saveptr);
            while (line) {
                process_line(&app, line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
        }
    }

    /* ── cleanup ── */
    if (app.remote)  treeland_remote_subsurface_v1_destroy(app.remote);
    if (app.exported) treeland_exported_surface_v1_destroy(app.exported);
    if (app.buffer)   wl_buffer_destroy(app.buffer);
    if (app.pool) {
        wl_shm_pool_destroy(app.pool);
        if (app.buf_data) munmap(app.buf_data, CHILD_W * CHILD_H * 4);
    }
    if (sub_surf)     wl_surface_destroy(sub_surf);
    if (g.manager)    treeland_subsurface_manager_v1_destroy(g.manager);
    free(child_token);
    wl_display_disconnect(display);
    return 0;
}
