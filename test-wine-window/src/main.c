/*
 * test-wine-window-management
 *
 * A standalone Wayland client that exercises the
 * treeland_wine_window_management_unstable_v1 protocol.
 *
 * Usage:
 *   test-wine-window-management [options]
 *
 * Options:
 *   --count N        open N windows (default: 1)
 *   --pos  X,Y       initial position for the first window (default: 100,100)
 *   --gap  PX        pixel gap between windows (default: 60)
 *   --help           show this help
 *
 * Key bindings (focus any window, then press):
 *   Q / Escape       quit
 *   T                set_z_order HWND_TOPMOST   for the focused window
 *   N                set_z_order HWND_NOTOPMOST for the focused window
 *   B                set_z_order HWND_BOTTOM    for the focused window
 *   Up               set_z_order HWND_TOP       for the focused window
 *   Left / Right     set_z_order HWND_INSERT_AFTER (below prev/next sibling)
 *   Arrow keys+Ctrl  move focused window by 20 px
 *   R                reset all windows to initial layout
 */

#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "treeland-wine-window-management-unstable-v1-client-protocol.h"

/* ─────────────────────────────────────────── tunables ─── */
#define MAX_WINDOWS   8
#define WINDOW_W      580
#define PANEL_H       190
#define CANVAS_H      240
#define WINDOW_H      (CANVAS_H + PANEL_H)
#define DEFAULT_GAP   40
#define MOVE_STEP     20

/* ── Button ─────────────────────────────────────────────── */
typedef enum {
    BTN_Z_TOP, BTN_Z_BOTTOM, BTN_Z_TOPMOST, BTN_Z_NOTOPMOST,
    BTN_Z_INSERT_PREV, BTN_Z_INSERT_NEXT,
    BTN_MOVE_LEFT, BTN_MOVE_RIGHT, BTN_MOVE_UP, BTN_MOVE_DOWN,
    BTN_RESET_ALL,
    BTN_COUNT
} BtnAction;

typedef struct {
    SDL_FRect   rect;
    const char *label;
    BtnAction   action;
    bool        hovered;
} Button;

/* ─────────────────────────────────────────── structs ──── */

typedef struct AppState AppState;

typedef struct {
    AppState   *app;
    int         index;           /* 0-based */

    SDL_Window *sdl_window;
    SDL_Renderer *renderer;

    struct wl_surface                   *wl_surface;
    struct treeland_wine_window_control_v1 *wine_control;

    /* state reported by compositor */
    uint32_t    window_id;       /* 0 = not yet received */
    int         actual_x;
    int         actual_y;
    uint32_t    topmost;         /* 0 = normal tier, 1 = topmost tier */

    /* state the client tracks */
    int         req_x;
    int         req_y;
    bool        focused;
    bool        running;

    /* UI buttons */
    Button      buttons[BTN_COUNT];
} WineWindow;

struct AppState {
    struct wl_display   *wl_display;
    struct wl_registry  *wl_registry;

    struct treeland_wine_window_manager_v1 *wine_manager;

    WineWindow  windows[MAX_WINDOWS];
    int         nwindows;

    bool        any_running;
};

/* ─────────────────────────────────────────── helpers ──── */

static const char *z_op_name(uint32_t op)
{
    switch (op) {
    case TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_TOP:         return "HWND_TOP";
    case TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_BOTTOM:      return "HWND_BOTTOM";
    case TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_TOPMOST:     return "HWND_TOPMOST";
    case TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_NOTOPMOST:   return "HWND_NOTOPMOST";
    case TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_INSERT_AFTER: return "HWND_INSERT_AFTER";
    default: return "UNKNOWN";
    }
}

/* ─────────────────── wine_control event handlers ───────── */

static void wine_control_window_id(void *data,
    struct treeland_wine_window_control_v1 *ctrl, uint32_t id)
{
    (void)ctrl;
    WineWindow *w = data;
    w->window_id = id;
    SDL_Log("[win%d] window_id = %u", w->index, id);
}

static void wine_control_configure_position(void *data,
    struct treeland_wine_window_control_v1 *ctrl, int32_t x, int32_t y)
{
    (void)ctrl;
    WineWindow *w = data;
    w->actual_x = x;
    w->actual_y = y;
    SDL_Log("[win%d] configure_position x=%d y=%d", w->index, x, y);
}

static void wine_control_configure_stacking(void *data,
    struct treeland_wine_window_control_v1 *ctrl, uint32_t topmost)
{
    (void)ctrl;
    WineWindow *w = data;
    w->topmost = topmost;
    SDL_Log("[win%d] configure_stacking topmost=%u", w->index, topmost);
}

static const struct treeland_wine_window_control_v1_listener wine_control_listener = {
    .window_id          = wine_control_window_id,
    .configure_position = wine_control_configure_position,
    .configure_stacking = wine_control_configure_stacking,
};

/* ─────────────────────── registry ─────────────────────── */

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    AppState *app = data;

    if (strcmp(interface, treeland_wine_window_manager_v1_interface.name) == 0) {
        uint32_t bind_ver = version < 1u ? version : 1u;
        app->wine_manager = wl_registry_bind(registry, name,
            &treeland_wine_window_manager_v1_interface, bind_ver);
        SDL_Log("Bound treeland_wine_window_manager_v1 (name=%u, ver=%u)", name, bind_ver);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

/* ─────────────────────── rendering ────────────────────── */

/* Colour palette: different hue for each window */
static void window_hue(int index, Uint8 *r, Uint8 *g, Uint8 *b)
{
    /* evenly spaced hues in HSV, S=70%, V=75% */
    static const struct { Uint8 r, g, b; } palette[] = {
        { 80, 140, 220 },   /* steel-blue   */
        { 90, 195, 120 },   /* jade-green   */
        { 220, 130,  60 },  /* amber        */
        { 200,  70, 130 },  /* rose         */
        { 130,  90, 210 },  /* violet       */
        { 60,  190, 200 },  /* teal         */
        { 220, 200,  55 },  /* gold         */
        { 200, 100,  80 },  /* terracotta   */
    };
    int i = index % (int)(sizeof palette / sizeof palette[0]);
    *r = palette[i].r; *g = palette[i].g; *b = palette[i].b;
}

/* ───────────────────── button layout & drawing ─────── */

/* BW=button width, BH=button height, PAD=padding */
#define BW  116.f
#define BH   28.f
#define PAD   8.f

static void init_buttons(WineWindow *w)
{
    /* Row origins (relative to panel top = CANVAS_H) */
    float py = (float)CANVAS_H + PAD;
    float col[4];
    for (int i = 0; i < 4; ++i) col[i] = PAD + i * (BW + PAD);

    /* Row 0: Z-order */
    w->buttons[BTN_Z_TOP]          = (Button){{ col[0], py,       BW, BH }, "TOP",          BTN_Z_TOP };
    w->buttons[BTN_Z_BOTTOM]       = (Button){{ col[1], py,       BW, BH }, "BOTTOM",       BTN_Z_BOTTOM };
    w->buttons[BTN_Z_TOPMOST]      = (Button){{ col[2], py,       BW, BH }, "TOPMOST",      BTN_Z_TOPMOST };
    w->buttons[BTN_Z_NOTOPMOST]    = (Button){{ col[3], py,       BW, BH }, "NOTOPMOST",    BTN_Z_NOTOPMOST };
    py += BH + PAD;
    /* Row 1: INSERT_AFTER */
    w->buttons[BTN_Z_INSERT_PREV]  = (Button){{ col[0], py, BW*2.f+PAD, BH }, "INSERT_AFTER prev", BTN_Z_INSERT_PREV };
    w->buttons[BTN_Z_INSERT_NEXT]  = (Button){{ col[2], py, BW*2.f+PAD, BH }, "INSERT_AFTER next", BTN_Z_INSERT_NEXT };
    py += BH + PAD;
    /* Row 2: move */
    w->buttons[BTN_MOVE_LEFT]      = (Button){{ col[0], py,       BW, BH }, "Move X-20",    BTN_MOVE_LEFT };
    w->buttons[BTN_MOVE_RIGHT]     = (Button){{ col[1], py,       BW, BH }, "Move X+20",    BTN_MOVE_RIGHT };
    w->buttons[BTN_MOVE_UP]        = (Button){{ col[2], py,       BW, BH }, "Move Y-20",    BTN_MOVE_UP };
    w->buttons[BTN_MOVE_DOWN]      = (Button){{ col[3], py,       BW, BH }, "Move Y+20",    BTN_MOVE_DOWN };
    py += BH + PAD;
    /* Row 3: reset */
    w->buttons[BTN_RESET_ALL]      = (Button){{ PAD,    py, (float)WINDOW_W - PAD*2.f, BH }, "Reset All (NOTOPMOST)", BTN_RESET_ALL };
}

static void draw_button(SDL_Renderer *rend, const Button *b)
{
    /* background */
    if (b->hovered)
        SDL_SetRenderDrawColor(rend, 90, 160, 255, 255);
    else
        SDL_SetRenderDrawColor(rend, 55, 65, 85, 230);
    SDL_RenderFillRect(rend, &b->rect);
    /* border */
    SDL_SetRenderDrawColor(rend, 160, 200, 255, 180);
    SDL_RenderRect(rend, &b->rect);
    /* label (SDL3 built-in 8x8 debug font) */
    SDL_SetRenderDrawColor(rend, 230, 235, 255, 255);
    float tx = b->rect.x + 4.f;
    float ty = b->rect.y + (b->rect.h - 8.f) * 0.5f;
    SDL_RenderDebugText(rend, tx, ty, b->label);
}

static void draw_window(WineWindow *w)
{
    SDL_Renderer *rend = w->renderer;

    Uint8 br, bg, bb;
    window_hue(w->index, &br, &bg, &bb);
    if (!w->focused) { br=(Uint8)(br*0.5f); bg=(Uint8)(bg*0.5f); bb=(Uint8)(bb*0.5f); }

    /* ---- canvas area ---- */
    SDL_SetRenderDrawColor(rend, br, bg, bb, 255);
    SDL_RenderClear(rend);

    /* topmost strip */
    if (w->topmost) {
        SDL_FRect s = { 0.f, 0.f, (float)WINDOW_W, 5.f };
        SDL_SetRenderDrawColor(rend, 255, 220, 50, 255);
        SDL_RenderFillRect(rend, &s);
    }

    /* info box */
    SDL_FRect info = { 12.f, 12.f, (float)WINDOW_W - 24.f, 110.f };
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 100);
    SDL_RenderFillRect(rend, &info);
    SDL_SetRenderDrawColor(rend, 200, 220, 255, 160);
    SDL_RenderRect(rend, &info);

    /* text inside info box */
    SDL_SetRenderDrawColor(rend, 240, 245, 255, 255);
    char line[80];
    SDL_RenderDebugText(rend, 20.f, 22.f,  "treeland-wine-window-management test");
    snprintf(line, sizeof line, "win%-2d  window_id: %-6u  %s",
        w->index, w->window_id, w->topmost ? "[TOPMOST]" : "[normal]");
    SDL_RenderDebugText(rend, 20.f, 38.f, line);
    snprintf(line, sizeof line, "actual  x=%-5d y=%d", w->actual_x, w->actual_y);
    SDL_RenderDebugText(rend, 20.f, 54.f, line);
    snprintf(line, sizeof line, "req     x=%-5d y=%d", w->req_x, w->req_y);
    SDL_RenderDebugText(rend, 20.f, 70.f, line);
    SDL_RenderDebugText(rend, 20.f, 90.f, "Keys: T N B Up Left Right  Ctrl+Arrows  R=reset  Q=quit");
    SDL_RenderDebugText(rend, 20.f, 106.f, "Mouse: click buttons below");

    /* ---- panel area ---- */
    SDL_FRect panel = { 0.f, (float)CANVAS_H, (float)WINDOW_W, (float)PANEL_H };
    SDL_SetRenderDrawColor(rend, 22, 28, 40, 255);
    SDL_RenderFillRect(rend, &panel);
    SDL_SetRenderDrawColor(rend, 80, 100, 140, 180);
    SDL_FRect sep = { 0.f, (float)CANVAS_H, (float)WINDOW_W, 1.f };
    SDL_RenderFillRect(rend, &sep);

    /* section label */
    SDL_SetRenderDrawColor(rend, 130, 160, 210, 200);
    SDL_RenderDebugText(rend, PAD, (float)CANVAS_H + PAD - 2.f, "Z-ORDER");

    for (int i = 0; i < BTN_COUNT; ++i)
        draw_button(rend, &w->buttons[i]);

    SDL_RenderPresent(rend);
}

/* ─────────────────────── actions ──────────────────────── */

static void do_set_position(WineWindow *w, int x, int y)
{
    if (!w->wine_control) return;
    w->req_x = x;
    w->req_y = y;
    treeland_wine_window_control_v1_set_position(w->wine_control, x, y);
    SDL_Log("[win%d] set_position x=%d y=%d", w->index, x, y);
}

static void do_set_z_order(WineWindow *w, uint32_t op, uint32_t sibling_id)
{
    if (!w->wine_control) return;
    treeland_wine_window_control_v1_set_z_order(w->wine_control, op, sibling_id);
    SDL_Log("[win%d] set_z_order op=%s sibling_id=%u",
        w->index, z_op_name(op), sibling_id);
}

static void handle_button_action(AppState *app, WineWindow *w, BtnAction action)
{
    switch (action) {
    case BTN_Z_TOP:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_TOP, 0);
        break;
    case BTN_Z_BOTTOM:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_BOTTOM, 0);
        break;
    case BTN_Z_TOPMOST:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_TOPMOST, 0);
        break;
    case BTN_Z_NOTOPMOST:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_NOTOPMOST, 0);
        break;
    case BTN_Z_INSERT_PREV: {
        int prev = (w->index - 1 + app->nwindows) % app->nwindows;
        WineWindow *sib = &app->windows[prev];
        if (sib->window_id && sib != w)
            do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_INSERT_AFTER, sib->window_id);
        break;
    }
    case BTN_Z_INSERT_NEXT: {
        int next = (w->index + 1) % app->nwindows;
        WineWindow *sib = &app->windows[next];
        if (sib->window_id && sib != w)
            do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_INSERT_AFTER, sib->window_id);
        break;
    }
    case BTN_MOVE_LEFT:  do_set_position(w, w->req_x - MOVE_STEP, w->req_y); break;
    case BTN_MOVE_RIGHT: do_set_position(w, w->req_x + MOVE_STEP, w->req_y); break;
    case BTN_MOVE_UP:    do_set_position(w, w->req_x, w->req_y - MOVE_STEP); break;
    case BTN_MOVE_DOWN:  do_set_position(w, w->req_x, w->req_y + MOVE_STEP); break;
    case BTN_RESET_ALL:
        for (int i = 0; i < app->nwindows; ++i)
            do_set_z_order(&app->windows[i],
                TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_NOTOPMOST, 0);
        break;
    default: break;
    }
    wl_display_flush(app->wl_display);
}

/* ─────────────────────── init / cleanup ───────────────── */

static bool init_wine_control(WineWindow *w)
{
    AppState *app = w->app;
    if (!app->wine_manager) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "treeland_wine_window_manager_v1 not available from compositor");
        return false;
    }

    /* We need xdg_toplevel; SDL has already created it.
     * The protocol requires an xdg_toplevel — SDL internally wraps the
     * wl_surface in an xdg_surface+toplevel before the first present.
     * We trigger that by calling SDL_ShowWindow / pump once. */
    SDL_ShowWindow(w->sdl_window);
    SDL_PumpEvents();
    wl_display_roundtrip(app->wl_display);

    /* Retrieve the underlying wl_surface */
    SDL_PropertiesID props = SDL_GetWindowProperties(w->sdl_window);
    w->wl_surface = SDL_GetPointerProperty(props,
        SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!w->wl_surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[win%d] Could not get wl_surface from SDL window", w->index);
        return false;
    }

    /* get_window_control wants an xdg_toplevel; the SDL wayland backend
     * exposes it via SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER.       */
    struct xdg_toplevel *toplevel = SDL_GetPointerProperty(props,
        SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, NULL);
    if (!toplevel) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[win%d] Could not get xdg_toplevel from SDL window", w->index);
        return false;
    }

    w->wine_control = treeland_wine_window_manager_v1_get_window_control(
        app->wine_manager, toplevel);
    if (!w->wine_control) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[win%d] get_window_control failed", w->index);
        return false;
    }

    treeland_wine_window_control_v1_add_listener(
        w->wine_control, &wine_control_listener, w);

    /* Flush so compositor processes get_window_control and sends window_id */
    wl_display_roundtrip(app->wl_display);

    SDL_Log("[win%d] wine_control created (window_id=%u)", w->index, w->window_id);
    return true;
}

static bool create_wine_window(AppState *app, int index, int x, int y,
    const char *title)
{
    WineWindow *w = &app->windows[index];
    w->app     = app;
    w->index   = index;
    w->req_x   = x;
    w->req_y   = y;
    w->running = true;

    char buf[128];
    snprintf(buf, sizeof buf, "%s [win%d]", title, index);

    w->sdl_window = SDL_CreateWindow(buf, WINDOW_W, WINDOW_H, 0);
    if (!w->sdl_window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    w->renderer = SDL_CreateRenderer(w->sdl_window, NULL);
    if (!w->renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    if (!init_wine_control(w))
        return false;
    init_buttons(w);
    return true;
}

static void destroy_wine_window(WineWindow *w)
{
    if (w->wine_control) {
        treeland_wine_window_control_v1_destroy(w->wine_control);
        w->wine_control = NULL;
    }
    if (w->renderer) {
        SDL_DestroyRenderer(w->renderer);
        w->renderer = NULL;
    }
    if (w->sdl_window) {
        SDL_DestroyWindow(w->sdl_window);
        w->sdl_window = NULL;
    }
}

static void cleanup(AppState *app)
{
    /* destroy controls before manager */
    for (int i = 0; i < app->nwindows; ++i)
        destroy_wine_window(&app->windows[i]);

    if (app->wine_manager) {
        treeland_wine_window_manager_v1_destroy(app->wine_manager);
        app->wine_manager = NULL;
    }
    if (app->wl_registry) {
        wl_registry_destroy(app->wl_registry);
        app->wl_registry = NULL;
    }
    SDL_Quit();
}

/* ─────────────────────── find window by SDL_WindowID ──── */

static WineWindow *find_window(AppState *app, SDL_WindowID id)
{
    for (int i = 0; i < app->nwindows; ++i) {
        if (app->windows[i].sdl_window &&
            SDL_GetWindowID(app->windows[i].sdl_window) == id)
            return &app->windows[i];
    }
    return NULL;
}

static WineWindow *focused_window(AppState *app)
{
    for (int i = 0; i < app->nwindows; ++i)
        if (app->windows[i].focused) return &app->windows[i];
    return &app->windows[0]; /* fallback */
}

/* ─────────────────────── event handling ────────────────── */

static void handle_key(AppState *app, WineWindow *w, SDL_Keycode key, SDL_Keymod mod)
{
    bool ctrl = (mod & SDL_KMOD_CTRL) != 0;

    switch (key) {
    case SDLK_ESCAPE:
    case SDLK_Q:
        app->any_running = false;
        break;

    case SDLK_T:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_TOPMOST, 0);
        break;

    case SDLK_N:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_NOTOPMOST, 0);
        break;

    case SDLK_B:
        do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_BOTTOM, 0);
        break;

    case SDLK_UP:
        if (!ctrl) {
            do_set_z_order(w, TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_TOP, 0);
        } else {
            do_set_position(w, w->req_x, w->req_y - MOVE_STEP);
        }
        break;

    case SDLK_DOWN:
        if (ctrl)
            do_set_position(w, w->req_x, w->req_y + MOVE_STEP);
        break;

    case SDLK_LEFT:
        if (!ctrl) {
            /* HWND_INSERT_AFTER: put below the previous window */
            int prev = (w->index - 1 + app->nwindows) % app->nwindows;
            WineWindow *sib = &app->windows[prev];
            if (sib->window_id && sib != w) {
                do_set_z_order(w,
                    TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_INSERT_AFTER,
                    sib->window_id);
            }
        } else {
            do_set_position(w, w->req_x - MOVE_STEP, w->req_y);
        }
        break;

    case SDLK_RIGHT:
        if (!ctrl) {
            /* HWND_INSERT_AFTER: put below the next window */
            int next = (w->index + 1) % app->nwindows;
            WineWindow *sib = &app->windows[next];
            if (sib->window_id && sib != w) {
                do_set_z_order(w,
                    TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_INSERT_AFTER,
                    sib->window_id);
            }
        } else {
            do_set_position(w, w->req_x + MOVE_STEP, w->req_y);
        }
        break;

    case SDLK_R:
        /* Reset all windows to initial layout */
        SDL_Log("Resetting all windows to initial layout...");
        for (int i = 0; i < app->nwindows; ++i) {
            WineWindow *wi = &app->windows[i];
            do_set_z_order(wi,
                TREELAND_WINE_WINDOW_CONTROL_V1_Z_ORDER_OP_HWND_NOTOPMOST, 0);
        }
        wl_display_flush(app->wl_display);
        break;

    case SDLK_H:
    case SDLK_SLASH:
        SDL_Log("─────────── Key bindings ───────────");
        SDL_Log("  Q / Escape  : quit");
        SDL_Log("  T           : HWND_TOPMOST");
        SDL_Log("  N           : HWND_NOTOPMOST");
        SDL_Log("  B           : HWND_BOTTOM");
        SDL_Log("  Up          : HWND_TOP");
        SDL_Log("  Left        : HWND_INSERT_AFTER (below prev sibling)");
        SDL_Log("  Right       : HWND_INSERT_AFTER (below next sibling)");
        SDL_Log("  Ctrl+Arrows : move window by %d px", MOVE_STEP);
        SDL_Log("  R           : reset tier for all windows");
        SDL_Log("────────────────────────────────────");
        break;

    default:
        break;
    }
}

static void process_events(AppState *app)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            app->any_running = false;
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            WineWindow *w = find_window(app, ev.window.windowID);
            if (w) {
                /* clear focus from all others */
                for (int i = 0; i < app->nwindows; ++i)
                    app->windows[i].focused = false;
                w->focused = true;
            }
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST: {
            WineWindow *w = find_window(app, ev.window.windowID);
            if (w) w->focused = false;
            break;
        }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            WineWindow *w = find_window(app, ev.window.windowID);
            if (w) w->running = false;
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            WineWindow *w = find_window(app, ev.key.windowID);
            if (!w) w = focused_window(app);
            if (w) handle_key(app, w, ev.key.key, ev.key.mod);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            WineWindow *w = find_window(app, ev.motion.windowID);
            if (w) {
                float mx = ev.motion.x, my = ev.motion.y;
                for (int i = 0; i < BTN_COUNT; ++i) {
                    SDL_FRect *r = &w->buttons[i].rect;
                    w->buttons[i].hovered =
                        mx >= r->x && mx < r->x + r->w &&
                        my >= r->y && my < r->y + r->h;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (ev.button.button != SDL_BUTTON_LEFT) break;
            WineWindow *w = find_window(app, ev.button.windowID);
            if (!w) break;
            float mx = ev.button.x, my = ev.button.y;
            for (int i = 0; i < BTN_COUNT; ++i) {
                SDL_FRect *r = &w->buttons[i].rect;
                if (mx >= r->x && mx < r->x + r->w &&
                    my >= r->y && my < r->y + r->h) {
                    handle_button_action(app, w, w->buttons[i].action);
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

/* ─────────────────────── argument parsing ─────────────── */

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --count N      number of windows to open (1..%d, default 1)\n"
        "  --pos  X,Y     initial position for window 0 (default 100,100)\n"
        "  --gap  PX      horizontal gap between windows (default %d)\n"
        "  --help         show this help\n"
        "\n"
        "Key bindings (press ? inside any window for summary):\n"
        "  T = TOPMOST  N = NOTOPMOST  B = BOTTOM\n"
        "  Up = TOP  Left/Right = INSERT_AFTER prev/next sibling\n"
        "  Ctrl+Arrows = move  R = reset  Q/Esc = quit\n",
        argv0, MAX_WINDOWS, DEFAULT_GAP);
}

/* ─────────────────────── main ──────────────────────────── */

int main(int argc, char **argv)
{
    int   nwindows = 1;
    int   base_x   = 100, base_y = 100;
    int   gap       = DEFAULT_GAP;

    /* ---- parse args ---- */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            nwindows = SDL_clamp((int)strtol(argv[++i], NULL, 10), 1, MAX_WINDOWS);
        } else if (strcmp(argv[i], "--pos") == 0 && i + 1 < argc) {
            sscanf(argv[++i], "%d,%d", &base_x, &base_y);
        } else if (strcmp(argv[i], "--gap") == 0 && i + 1 < argc) {
            gap = (int)strtol(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    SDL_SetAppMetadata("test-wine-window-management", "1.0",
        "org.treeland.demo.wine-window-management");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "This demo requires the Wayland SDL backend "
            "(current: '%s'). Set SDL_VIDEODRIVER=wayland.",
            SDL_GetCurrentVideoDriver());
        SDL_Quit();
        return 1;
    }

    /* ---- Wayland globals ---- */
    AppState app;
    SDL_zero(app);
    app.nwindows    = nwindows;
    app.any_running = true;

    app.wl_display = SDL_GetPointerProperty(SDL_GetGlobalProperties(),
        SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, NULL);
    if (!app.wl_display) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No wl_display from SDL.");
        SDL_Quit();
        return 1;
    }

    app.wl_registry = wl_display_get_registry(app.wl_display);
    wl_registry_add_listener(app.wl_registry, &registry_listener, &app);
    wl_display_roundtrip(app.wl_display);

    if (!app.wine_manager) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Compositor does not advertise treeland_wine_window_manager_v1.\n"
            "Make sure you are running on a Treeland compositor that supports this protocol.");
        SDL_Quit();
        return 1;
    }

    /* ---- create windows ---- */
    for (int i = 0; i < nwindows; ++i) {
        int wx = base_x + i * (WINDOW_W + gap);
        int wy = base_y;
        if (!create_wine_window(&app, i, wx, wy, "Wine Window Management Test")) {
            cleanup(&app);
            return 1;
        }
        /* ask compositor to position each window */
        do_set_position(&app.windows[i], wx, wy);
    }
    wl_display_roundtrip(app.wl_display);

    SDL_Log("═══════════════════════════════════════════════════");
    SDL_Log("  test-wine-window-management  —  %d window(s) open", nwindows);
    SDL_Log("  Press ? inside any window to list key bindings.");
    SDL_Log("═══════════════════════════════════════════════════");

    /* ---- main loop ---- */
    while (app.any_running) {
        /* dispatch pending Wayland events */
        if (wl_display_dispatch_pending(app.wl_display) < 0)
            break;

        process_events(&app);

        /* check if all windows closed */
        bool all_closed = true;
        for (int i = 0; i < app.nwindows; ++i)
            if (app.windows[i].running) { all_closed = false; break; }
        if (all_closed) break;

        /* flush any protocol requests we queued */
        wl_display_flush(app.wl_display);

        /* render */
        for (int i = 0; i < app.nwindows; ++i) {
            WineWindow *w = &app.windows[i];
            if (w->running && w->renderer)
                draw_window(w);
        }

        SDL_Delay(16);
    }

    cleanup(&app);
    return 0;
}
