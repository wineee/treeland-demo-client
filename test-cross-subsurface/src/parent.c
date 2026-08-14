/*
 * test-cross-subsurface (parent binary)
 *
 * SDL toplevel window, exports its surface, prints token.
 * Creates 5 local remote subsurfaces at different z-levels and positions.
 * Space toggles auto-animation (position drift + color cycling + z-order ops).
 *
 * Keys:
 *   1-5         select subsurface
 *   Space       toggle auto-animation
 *   Arrows      move selected +/-20px
 *   T           place_above(parent) -- top
 *   A           place_above(previous sibling)
 *   B           place_below(next sibling)
 *   C           cycle color
 *   D           destroy selected
 *   R           recreate selected
 *   Q / Esc     quit
 */

#include "common.h"
#include <SDL3/SDL.h>
#include <math.h>

/* local subsurfaces: small, to distinguish from cross-process child */
#define LOCAL_SUB_W  80
#define LOCAL_SUB_H  60

/* ── SubSurface helpers ─────────────────────────────────── */

static bool sub_create(struct SubSurface *s, struct WlGlobals *g,
    const char *parent_token, int idx, int ix, int iy, int ic)
{
    memset(s, 0, sizeof(*s));
    s->color_idx = ic;
    s->w = LOCAL_SUB_W;
    s->h = LOCAL_SUB_H;

    s->surface = wl_compositor_create_surface(g->compositor);
    if (!s->surface) return false;

    for (int b = 0; b < SUB_NUM_BUFS; b++) {
        if (!create_buf_slot(g->shm, s->w, s->h,
                color_cycle[s->color_idx], &s->bufs[b])) {
            for (int j = 0; j < b; j++)
                destroy_buf_slot(&s->bufs[j], s->w, s->h);
            wl_surface_destroy(s->surface);
            return false;
        }
    }

    sub_commit_color(s, color_cycle[s->color_idx]);

    s->exported = treeland_subsurface_manager_v1_export_surface(
        g->manager, s->surface);
    treeland_exported_surface_v1_add_listener(
        s->exported, &exported_listener, &s->token);
    wl_display_roundtrip(g->wl_display);

    SDL_Log("[sub%d] token: %s", idx,
        s->token ? s->token : "(null)");

    s->remote = treeland_exported_surface_v1_create_remote_subsurface(
        s->exported, parent_token);
    if (!s->remote) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[sub%d] create_remote_subsurface failed", idx);
        return false;
    }

    s->x = ix;  s->y = iy;
    treeland_remote_subsurface_v1_set_position(s->remote, s->x, s->y);
    wl_surface_commit(s->surface);
    wl_display_flush(g->wl_display);
    s->alive = true;
    SDL_Log("[sub%d] created at (%d, %d)", idx, s->x, s->y);
    return true;
}

/* ── animation parameters per subsurface ───────────────── */

struct SubAnim {
    float base_x, base_y;
    float amp_x,  amp_y;
    float speed;
    float phase;
};

static const struct SubAnim anim[NUM_SUBSURFACES] = {
    {  20,  50,  40, 30, 0.8, 0.0 },
    { 220,  20,  60, 20, 1.4, 1.2 },
    { 440,  70,  30, 40, 0.6, 2.4 },
    { 120, 220,  50, 25, 1.1, 3.6 },
    { 360, 210,  45, 35, 1.3, 4.8 },
};

static const int init_clr[NUM_SUBSURFACES] = { 1, 0, 2, 3, 5 };

/* ── position drift ─────────────────────────────────────── */

static void auto_update_positions(struct SubSurface *subs, double t)
{
    for (int i = 0; i < NUM_SUBSURFACES; i++) {
        if (!subs[i].alive || !subs[i].remote) continue;
        int nx = (int)(anim[i].base_x
            + anim[i].amp_x * sin(t * anim[i].speed + anim[i].phase));
        int ny = (int)(anim[i].base_y
            + anim[i].amp_y * cos(t * anim[i].speed * 0.7 + anim[i].phase));
        if (nx != subs[i].x || ny != subs[i].y) {
            subs[i].x = nx;
            subs[i].y = ny;
            treeland_remote_subsurface_v1_set_position(subs[i].remote, nx, ny);
        }
    }
}

/* ── z-order animation sequence ────────────────────────────
 *
 * Cycles through different z-order protocol operations every 2 s:
 *   0  sub[i] place_above(parent_token)  — above parent
 *   1  sub[i] place_below(parent_token)  — below parent
 *   2  sub[i] place_above(parent_token)  — above parent
 *   3  sub[i] place_above(sub[j])        — above a sibling
 *   4  sub[i] place_below(sub[j])        — below a sibling
 *   5  sub[i] destroy, recreate next tick — destroy/recreate
 *   6  rotate-all: each sub above next   — full reorder
 */
#define ZORDER_INTERVAL_MS  2000
#define NUM_ZORDER_OPS      7

static const char *zop_name[NUM_ZORDER_OPS] = {
    "above-parent", "below-parent", "above-parent",
    "above-sibling", "below-sibling",
    "destroy+recreate", "rotate-all",
};

static void auto_zorder_op(struct SubSurface *subs, int step,
    const char *parent_token, bool *recreate_pending, int *recreate_idx)
{
    int a = step % NUM_SUBSURFACES;
    int b = (step + 2) % NUM_SUBSURFACES;
    if (!subs[a].alive) return;

    switch (step % NUM_ZORDER_OPS) {
    case 0:
        if (subs[a].remote) {
            treeland_remote_subsurface_v1_place_above(subs[a].remote, parent_token);
            SDL_Log("[auto] sub%d -> top", a);
        }
        break;
    case 1:
        if (subs[a].remote) {
            treeland_remote_subsurface_v1_place_below(
                subs[a].remote, parent_token);
            SDL_Log("[auto] sub%d -> below parent", a);
        }
        break;
    case 2:
        if (subs[a].remote) {
            treeland_remote_subsurface_v1_place_above(
                subs[a].remote, parent_token);
            SDL_Log("[auto] sub%d -> above parent", a);
        }
        break;
    case 3:
        if (subs[a].remote && subs[b].token && subs[b].alive) {
            treeland_remote_subsurface_v1_place_above(
                subs[a].remote, subs[b].token);
            SDL_Log("[auto] sub%d -> above sub%d", a, b);
        }
        break;
    case 4:
        if (subs[a].remote && subs[b].token && subs[b].alive) {
            treeland_remote_subsurface_v1_place_below(
                subs[a].remote, subs[b].token);
            SDL_Log("[auto] sub%d -> below sub%d", a, b);
        }
        break;
    case 5:
        if (subs[a].remote) {
            treeland_remote_subsurface_v1_destroy(subs[a].remote);
            subs[a].remote = NULL;
            subs[a].alive = false;
            wl_surface_attach(subs[a].surface, NULL, 0, 0);
            wl_surface_commit(subs[a].surface);
            *recreate_pending = true;
            *recreate_idx = a;
            SDL_Log("[auto] sub%d destroyed", a);
        }
        break;
    case 6:
        for (int i = 0; i < NUM_SUBSURFACES; i++) {
            int next = (i + 1) % NUM_SUBSURFACES;
            if (subs[i].remote && subs[next].token && subs[next].alive) {
                treeland_remote_subsurface_v1_place_above(
                    subs[i].remote, subs[next].token);
            }
        }
        SDL_Log("[auto] rotate-all");
        break;
    }
}

static void auto_try_recreate(struct SubSurface *subs,
    int idx, const char *parent_token)
{
    if (subs[idx].alive || !subs[idx].exported) return;
    subs[idx].remote =
        treeland_exported_surface_v1_create_remote_subsurface(
            subs[idx].exported, parent_token);
    if (subs[idx].remote) {
        treeland_remote_subsurface_v1_set_position(
            subs[idx].remote, subs[idx].x, subs[idx].y);
        sub_commit_color(&subs[idx], color_cycle[subs[idx].color_idx]);
        subs[idx].alive = true;
        SDL_Log("[auto] sub%d recreated", idx);
    }
}

/* ═══════════════════════════════════════════════════════════
 *   PARENT MODE
 * ═══════════════════════════════════════════════════════════ */

static int parent_main(int argc, char *argv[])
{
    bool show_remote = true, show_std = true, export_only = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--std-only") == 0)
            show_remote = false;
        else if (strcmp(argv[i], "--remote-only") == 0)
            show_std = false;
        else if (strcmp(argv[i], "--export-only") == 0)
            export_only = true;
    }
    if (export_only) { show_remote = false; show_std = false; }
    SDL_Log("[parent] mode: remote=%s std=%s export-only=%s",
        show_remote ? "yes" : "no", show_std ? "yes" : "no",
        export_only ? "yes" : "no");

    /* Force Wayland video driver */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Requires Wayland backend (current: '%s'). Set SDL_VIDEODRIVER=wayland",
            SDL_GetCurrentVideoDriver());
        SDL_Quit(); return 1;
    }

    struct wl_display *wl = SDL_GetPointerProperty(SDL_GetGlobalProperties(),
        SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, NULL);
    if (!wl) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No wl_display from SDL");
        SDL_Quit(); return 1;
    }

    struct WlGlobals g;
    bind_wl_globals(wl, &g);

    if (!g.compositor || !g.shm || !g.manager ||
        (show_std && !g.subcompositor)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing globals");
        SDL_Quit(); return 1;
    }

    /* ── SDL window ── */
    SDL_Window *win = SDL_CreateWindow(
        "Cross-Subsurface [parent]", WINDOW_W, WINDOW_H, 0);
    SDL_Renderer *rend = SDL_CreateRenderer(win, NULL);
    if (!win || !rend) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL window failed");
        SDL_Quit(); return 1;
    }
    SDL_ShowWindow(win);
    SDL_SetWindowOpacity(win, 0.3f);
    SDL_PumpEvents();
    wl_display_roundtrip(wl);

    struct wl_surface *parent_surf = SDL_GetPointerProperty(
        SDL_GetWindowProperties(win),
        SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!parent_surf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No wl_surface from SDL");
        SDL_Quit(); return 1;
    }

    /* ── export parent surface ── */
    char *parent_token = NULL;
    struct treeland_exported_surface_v1 *parent_exported = NULL;
    parent_exported = treeland_subsurface_manager_v1_export_surface(
        g.manager, parent_surf);
    treeland_exported_surface_v1_add_listener(
        parent_exported, &exported_listener, &parent_token);
    wl_display_roundtrip(wl);
    if (!parent_token) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get parent token");
        SDL_Quit(); return 1;
    }
    SDL_Log("");
    SDL_Log("==============================================");
    SDL_Log("  parent_token: %s", parent_token);
    SDL_Log("==============================================");
    SDL_Log("");

    /* ── remote subsurfaces ── */
    struct SubSurface subs[NUM_SUBSURFACES];
    memset(subs, 0, sizeof(subs));
    struct SubSurface sync_sub;
    memset(&sync_sub, 0, sizeof(sync_sub));

    if (show_remote) {
        for (int i = 0; i < NUM_SUBSURFACES; i++) {
            if (!sub_create(&subs[i], &g, parent_token, i,
                    (int)anim[i].base_x, (int)anim[i].base_y, init_clr[i]))
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "[sub%d] create failed", i);
        }

        /* initial z-order: sub0,sub2 < parent < sub4 < sub3 < sub1 */
        if (subs[0].remote)
            treeland_remote_subsurface_v1_place_below(subs[0].remote, parent_token);
        if (subs[2].remote)
            treeland_remote_subsurface_v1_place_below(subs[2].remote, parent_token);
        if (subs[4].remote)
            treeland_remote_subsurface_v1_place_above(subs[4].remote, parent_token);
        if (subs[3].remote && subs[4].token)
            treeland_remote_subsurface_v1_place_above(subs[3].remote, subs[4].token);
        if (subs[1].remote)
            treeland_remote_subsurface_v1_place_above(subs[1].remote, parent_token);
        wl_display_flush(wl);
        SDL_Log("z-order: sub0,sub2 < parent < sub4 < sub3 < sub1");

        /* ── sync subsurface (parent color inverse) ── */
        sync_sub.w = 120; sync_sub.h = 90;
        sync_sub.surface = wl_compositor_create_surface(g.compositor);
        if (sync_sub.surface) {
            for (int b = 0; b < SUB_NUM_BUFS; b++)
                create_buf_slot(g.shm, sync_sub.w, sync_sub.h,
                    0xFF808080, &sync_sub.bufs[b]);
            sub_commit_color(&sync_sub, 0xFF808080);

            sync_sub.exported = treeland_subsurface_manager_v1_export_surface(
                g.manager, sync_sub.surface);
            treeland_exported_surface_v1_add_listener(
                sync_sub.exported, &exported_listener, &sync_sub.token);
            wl_display_roundtrip(wl);

            sync_sub.remote = treeland_exported_surface_v1_create_remote_subsurface(
                sync_sub.exported, parent_token);
            if (sync_sub.remote) {
                sync_sub.x = 540; sync_sub.y = 290;
                treeland_remote_subsurface_v1_set_position(
                    sync_sub.remote, sync_sub.x, sync_sub.y);
                wl_surface_commit(sync_sub.surface);
                wl_display_flush(wl);
                sync_sub.alive = true;
                SDL_Log("[sync] token: %s pos: (%d,%d) size: %dx%d",
                    sync_sub.token ? sync_sub.token : "(null)",
                    sync_sub.x, sync_sub.y, sync_sub.w, sync_sub.h);
            }
        }
    }

    /* ── standard wl_subsurface (upstream protocol comparison) ── */
    struct SubSurface std_sub = {0};
    struct wl_subsurface *std_wl_sub = NULL;

    if (show_std) {
        std_sub.w = 100; std_sub.h = 80;
        std_sub.surface = wl_compositor_create_surface(g.compositor);
        if (std_sub.surface) {
            for (int b = 0; b < SUB_NUM_BUFS; b++)
                create_buf_slot(g.shm, std_sub.w, std_sub.h,
                    color_cycle[0], &std_sub.bufs[b]);

            std_wl_sub = wl_subcompositor_get_subsurface(
                g.subcompositor, std_sub.surface, parent_surf);
            wl_subsurface_set_position(std_wl_sub, 280, 310);
            sub_commit_color(&std_sub, color_cycle[0]);
            wl_display_flush(wl);
            SDL_Log("[std_sub] created at (280,310) size: %dx%d",
                std_sub.w, std_sub.h);
        }
    }

    /* ── state ── */
    int selected = 0;
    bool auto_mode = true;
    bool running = true;
    uint64_t last_color = 0;
    uint64_t last_zorder = 0;
    int color_idx = 0;
    int zorder_step = 0;
    bool recreate_pending = false;
    int recreate_idx = 0;

    /* ── main loop ── */
    while (running) {
        if (wl_display_dispatch_pending(wl) < 0) break;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                struct SubSurface *s = &subs[selected];
                switch (ev.key.key) {
                case SDLK_ESCAPE: case SDLK_Q: running = false; break;
                case SDLK_1: selected = 0; break;
                case SDLK_2: selected = 1; break;
                case SDLK_3: selected = 2; break;
                case SDLK_4: selected = 3; break;
                case SDLK_5: selected = 4; break;
                case SDLK_SPACE:
                    auto_mode = !auto_mode;
                    SDL_Log("[auto] %s", auto_mode ? "ON" : "OFF");
                    break;
                case SDLK_UP:
                    s->y -= MOVE_STEP;
                    if (s->remote) treeland_remote_subsurface_v1_set_position(s->remote, s->x, s->y);
                    break;
                case SDLK_DOWN:
                    s->y += MOVE_STEP;
                    if (s->remote) treeland_remote_subsurface_v1_set_position(s->remote, s->x, s->y);
                    break;
                case SDLK_LEFT:
                    s->x -= MOVE_STEP;
                    if (s->remote) treeland_remote_subsurface_v1_set_position(s->remote, s->x, s->y);
                    break;
                case SDLK_RIGHT:
                    s->x += MOVE_STEP;
                    if (s->remote) treeland_remote_subsurface_v1_set_position(s->remote, s->x, s->y);
                    break;
                case SDLK_T:
                    if (s->remote) {
                        treeland_remote_subsurface_v1_place_above(s->remote, parent_token);
                        SDL_Log("[sub%d] above parent", selected);
                    } break;
                case SDLK_A: {
                    int prev = (selected - 1 + NUM_SUBSURFACES) % NUM_SUBSURFACES;
                    if (s->remote && subs[prev].token) {
                        treeland_remote_subsurface_v1_place_above(s->remote, subs[prev].token);
                        SDL_Log("[sub%d] place_above(sub%d)", selected, prev);
                    } break;
                }
                case SDLK_B: {
                    int next = (selected + 1) % NUM_SUBSURFACES;
                    if (s->remote && subs[next].token) {
                        treeland_remote_subsurface_v1_place_below(s->remote, subs[next].token);
                        SDL_Log("[sub%d] place_below(sub%d)", selected, next);
                    } break;
                }
                case SDLK_C:
                    s->color_idx = (s->color_idx + 1) % NUM_COLORS;
                    if (!sub_commit_color(s, color_cycle[s->color_idx]))
                        SDL_Log("[sub%d] color skipped (buffers busy)", selected);
                    else
                        SDL_Log("[sub%d] color -> #%06X", selected,
                            color_cycle[s->color_idx] & 0xFFFFFF);
                    break;
                case SDLK_S: {
                    static const int sizes[][2] = {
                        { 80,  60 }, { 120, 90 }, { 160, 120 },
                        { 200, 150 }, { 60, 120 }, { 100, 40 },
                    };
                    int nw = sizes[(selected + 1) % 6][0];
                    int nh = sizes[(selected + 1) % 6][1];
                    for (int b = 0; b < SUB_NUM_BUFS; b++)
                        destroy_buf_slot(&s->bufs[b], s->w, s->h);
                    s->next_buf = 0;
                    s->color_idx = (s->color_idx + 1) % NUM_COLORS;
                    s->w = nw; s->h = nh;
                    for (int b = 0; b < SUB_NUM_BUFS; b++)
                        create_buf_slot(g.shm, nw, nh,
                            color_cycle[s->color_idx], &s->bufs[b]);
                    sub_commit_color(s, color_cycle[s->color_idx]);
                    wl_display_flush(wl);
                    SDL_Log("[sub%d] resize -> %dx%d color -> #%06X",
                        selected, nw, nh,
                        color_cycle[s->color_idx] & 0xFFFFFF);
                    break;
                }
                case SDLK_D:
                    if (s->remote) {
                        treeland_remote_subsurface_v1_destroy(s->remote);
                        s->remote = NULL;
                        s->alive = false;
                        wl_surface_attach(s->surface, NULL, 0, 0);
                        wl_surface_commit(s->surface);
                        SDL_Log("[sub%d] destroyed", selected);
                    } break;
                case SDLK_R:
                    if (!s->alive && s->exported) {
                        s->remote = treeland_exported_surface_v1_create_remote_subsurface(
                            s->exported, parent_token);
                        if (s->remote) {
                            treeland_remote_subsurface_v1_set_position(s->remote, s->x, s->y);
                            sub_commit_color(s, color_cycle[s->color_idx]);
                            s->alive = true;
                            SDL_Log("[sub%d] recreated", selected);
                        }
                    } break;
                default: break;
                }
                wl_display_flush(wl);
            }
        }

        /* ── auto-animation ── */
        if (auto_mode) {
            uint64_t now = SDL_GetTicks();
            double t = now / 1000.0;

            if (show_remote)
                auto_update_positions(subs, t);

            if (now - last_color >= 1200) {
                last_color = now;
                if (show_remote) {
                    struct SubSurface *s = &subs[color_idx];
                    s->color_idx = (s->color_idx + 1) % NUM_COLORS;
                    sub_commit_color(s, color_cycle[s->color_idx]);
                    color_idx = (color_idx + 1) % NUM_SUBSURFACES;
                }
                if (show_std) {
                    std_sub.color_idx = (std_sub.color_idx + 1) % NUM_COLORS;
                    sub_commit_color(&std_sub, color_cycle[std_sub.color_idx]);
                }
            }

            if (show_remote && now - last_zorder >= ZORDER_INTERVAL_MS) {
                last_zorder = now;
                if (!recreate_pending) {
                    auto_zorder_op(subs, zorder_step,
                        parent_token, &recreate_pending, &recreate_idx);
                    zorder_step++;
                } else {
                    auto_try_recreate(subs, recreate_idx, parent_token);
                    recreate_pending = false;
                }
            }

            wl_display_flush(wl);
        }

        /* ── parent color cycling + sync subsurface ── */
        uint8_t bg_r, bg_g, bg_b;
        {
            uint64_t now = SDL_GetTicks();
            double ct = now / 1000.0;
            bg_r = (uint8_t)(127.5 + 127.5 * sin(ct * 0.7));
            bg_g = (uint8_t)(127.5 + 127.5 * sin(ct * 0.7 + 2.094));
            bg_b = (uint8_t)(127.5 + 127.5 * sin(ct * 0.7 + 4.189));

            if (show_remote && sync_sub.alive) {
                uint32_t icolor = 0xFF000000u
                    | ((uint32_t)(255 - bg_b) << 16)
                    | ((uint32_t)(255 - bg_g) << 8)
                    | (uint32_t)(255 - bg_r);
                sub_commit_color(&sync_sub, icolor);
            }
        }
        wl_display_flush(wl);

        /* ── render HUD ── */
        SDL_SetRenderDrawColor(rend, bg_r, bg_g, bg_b, 255);
        SDL_RenderClear(rend);

        float panel_h = 24.f;
        if (show_remote) panel_h += NUM_SUBSURFACES * 12.f + 4.f + 14.f + 14.f + 12.f;
        if (show_std)    panel_h += 14.f;
        panel_h += 12.f + 12.f + 12.f;
        SDL_FRect box = { 10.f, 10.f, (float)(WINDOW_W - 20), panel_h };
        SDL_SetRenderDrawColor(rend, 0, 0, 0, 100);
        SDL_RenderFillRect(rend, &box);
        SDL_SetRenderDrawColor(rend, 100, 140, 200, 160);
        SDL_RenderRect(rend, &box);

        SDL_SetRenderDrawColor(rend, 220, 235, 255, 255);
        float ty = 16.f;
        SDL_RenderDebugText(rend, 18.f, ty,
            "treeland-cross-subsurface [parent]");
        ty += 16.f;

        char line[80];
        if (show_remote) {
            for (int i = 0; i < NUM_SUBSURFACES; i++) {
                uint32_t c = color_cycle[subs[i].color_idx];
                snprintf(line, sizeof(line),
                    "%s %d: (%3d,%3d) %2dx%-2d #%06X %s",
                    (i == selected) ? ">>>" : "   ",
                    i + 1, subs[i].x, subs[i].y,
                    subs[i].w, subs[i].h,
                    c & 0xFFFFFF,
                    subs[i].alive ? "alive" : "dead");
                SDL_RenderDebugText(rend, 18.f, ty, line);
                ty += 12.f;
            }
            ty += 4.f;
            snprintf(line, sizeof(line), "token: %.30s%s",
                parent_token, (strlen(parent_token) > 30) ? "..." : "");
            SDL_RenderDebugText(rend, 18.f, ty, line);
            ty += 14.f;
            snprintf(line, sizeof(line),
                "sync: (%3d,%3d) %2dx%-2d  p:#%02X%02X%02X inv:#%02X%02X%02X",
                sync_sub.x, sync_sub.y, sync_sub.w, sync_sub.h,
                bg_r, bg_g, bg_b, 255 - bg_r, 255 - bg_g, 255 - bg_b);
            SDL_RenderDebugText(rend, 18.f, ty, line);
            ty += 14.f;
            if (auto_mode) {
                int op = zorder_step % NUM_ZORDER_OPS;
                snprintf(line, sizeof(line), "z-op: %s (step %d)",
                    zop_name[op], zorder_step);
                SDL_RenderDebugText(rend, 18.f, ty, line);
                ty += 12.f;
            }
        }
        if (show_std) {
            snprintf(line, sizeof(line),
                "std:  (280,310) %2dx%-2d  #%06X [wl_subsurface]",
                std_sub.w, std_sub.h,
                color_cycle[std_sub.color_idx] & 0xFFFFFF);
            SDL_RenderDebugText(rend, 18.f, ty, line);
            ty += 14.f;
        }

        SDL_SetRenderDrawColor(rend, 160, 180, 210, 255);
        if (auto_mode)
            SDL_RenderDebugText(rend, 18.f, ty, "[AUTO]  Space: pause");
        else
            SDL_RenderDebugText(rend, 18.f, ty, "[MAN]  Space: auto");
        ty += 12.f;
        if (show_remote)
            SDL_RenderDebugText(rend, 18.f, ty,
                "1-5: sel  Arrows: move  T: top  A/B: z-order");
        else
            SDL_RenderDebugText(rend, 18.f, ty, "(std-only mode)");
        ty += 12.f;
        if (show_remote)
            SDL_RenderDebugText(rend, 18.f, ty,
                "C: color  S: resize  D: destroy  R: recreate  Q/Esc: quit");
        else
            SDL_RenderDebugText(rend, 18.f, ty, "Q/Esc: quit");

        SDL_RenderPresent(rend);
        SDL_Delay(16);
    }

    /* ── cleanup ── */
    if (show_remote) {
        for (int i = 0; i < NUM_SUBSURFACES; i++) sub_cleanup(&subs[i]);
        sub_cleanup(&sync_sub);
    }
    if (show_std) {
        if (std_wl_sub) wl_subsurface_destroy(std_wl_sub);
        sub_cleanup(&std_sub);
    }
    treeland_exported_surface_v1_destroy(parent_exported);
    treeland_subsurface_manager_v1_destroy(g.manager);
    free(parent_token);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

int main(int argc, char *argv[])
{
    return parent_main(argc, argv);
}
