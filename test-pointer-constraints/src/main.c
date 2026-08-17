/*
 * test-pointer-constraints
 *
 * Interactive test for wp_pointer_constraints and wp_relative_pointer.
 *
 * Tests:
 *   - Lock pointer (oneshot & persistent)
 *   - Confine pointer (oneshot & persistent)
 *   - Relative pointer motion events (accelerated & unaccelerated deltas)
 *   - Cursor position hint (lock mode)
 *   - Confine region visualization
 *
 * Keys:
 *   Q / Esc    quit
 *   1          Lock oneshot
 *   2          Lock persistent
 *   3          Confine oneshot
 *   4          Confine persistent
 *   U          Unconstrain (release current lock/confine)
 *   H          Set cursor position hint (lock mode)
 *   R          Toggle confine region overlay
 *   C          Clear relative motion display
 *   Space      Cycle through modes
 */

#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <wayland-client.h>
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"

/* ─────────────────────────────────────────── tunables ─── */

#define WINDOW_W      800
#define CANVAS_H      420
#define PANEL_H       230
#define WINDOW_H      (CANVAS_H + PANEL_H)

#define SMOOTHING_BINS  16   /* rolling-average bins for motion display */

/* ── Button actions ────────────────────────────────────── */

typedef enum {
    BTN_LOCK_ONESHOT,
    BTN_LOCK_PERSISTENT,
    BTN_CONFINE_ONESHOT,
    BTN_CONFINE_PERSISTENT,
    BTN_UNCONSTRAIN,
    BTN_SET_HINT,
    BTN_TOGGLE_REGION,
    BTN_CLEAR_MOTION,
    BTN_SCENE_LOCK_MOTION,
    BTN_SCENE_CONFINE_ONLY,
    BTN_COUNT
} BtnAction;

typedef struct {
    SDL_FRect   rect;
    const char *label;
    BtnAction   action;
    bool        hovered;
} Button;

/* ── Constraint state ──────────────────────────────────── */

typedef enum {
    CONSTRAINT_NONE     = 0,
    CONSTRAINT_LOCK_PENDING,
    CONSTRAINT_LOCKED,
    CONSTRAINT_CONFINE_PENDING,
    CONSTRAINT_CONFINED
} ConstraintState;

typedef enum {
    LIFETIME_ONESHOT    = 1,
    LIFETIME_PERSISTENT = 2
} ConstraintLifetime;

/* ── Motion history for smoothing display ──────────────── */

typedef struct {
    float dx, dy, dxu, dyu;
} MotionSample;

/* ── AppState ──────────────────────────────────────────── */

typedef struct AppState {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    bool          running;

    /* Wayland core */
    struct wl_display   *wl_display;
    struct wl_registry  *wl_registry;
    struct wl_surface   *wl_surface;
    struct wl_seat      *wl_seat;
    struct wl_pointer   *wl_pointer;
    struct wl_keyboard  *wl_keyboard;

    /* Core Wayland globals */
    struct wl_compositor                   *compositor;

    /* Pointer constraints global */
    struct zwp_pointer_constraints_v1      *constraints;
    struct zwp_relative_pointer_manager_v1 *rel_mgr;

    /* Active constraint objects */
    struct zwp_locked_pointer_v1   *locked_ptr;
    struct zwp_confined_pointer_v1 *confined_ptr;

    /* Relative pointer object */
    struct zwp_relative_pointer_v1 *rel_ptr;

    /* State */
    ConstraintState    constraint_state;
    ConstraintLifetime requested_lifetime; /* for display */

    /* Cursor position (from wl_pointer.enter / .motion) */
    wl_fixed_t cursor_sx, cursor_sy;

    /* Relative motion — latest values */
    wl_fixed_t rel_dx, rel_dy;
    wl_fixed_t rel_dx_unaccel, rel_dy_unaccel;

    /* Rolling history for smoothed display */
    MotionSample  hist[SMOOTHING_BINS];
    int           hist_head;
    int           hist_count;

    /* Confine region (optional) */
    struct wl_region *confine_region;
    SDL_Rect         region_rect;
    bool             show_region;

    /* Cursor position hint */
    bool   hint_active;
    double hint_x, hint_y;

    /* Buttons */
    Button buttons[BTN_COUNT];

    /* Seat capabilities mask (for info display) */
    uint32_t seat_caps;
} AppState;

static AppState g_app;

/* ─────────────────────── helpers ──────────────────────── */

static const char *state_name(ConstraintState s)
{
    switch (s) {
    case CONSTRAINT_NONE:             return "none";
    case CONSTRAINT_LOCK_PENDING:     return "lock-pending";
    case CONSTRAINT_LOCKED:           return "LOCKED";
    case CONSTRAINT_CONFINE_PENDING:  return "confine-pending";
    case CONSTRAINT_CONFINED:         return "CONFINED";
    }
    return "?";
}

static const char *lifetime_name(ConstraintLifetime l)
{
    return l == LIFETIME_PERSISTENT ? "persistent" : "oneshot";
}

/* ─────────────────── constraint cleanup ────────────────── */

static void destroy_constraint(AppState *app)
{
    if (app->locked_ptr) {
        zwp_locked_pointer_v1_destroy(app->locked_ptr);
        app->locked_ptr = NULL;
    }
    if (app->confined_ptr) {
        zwp_confined_pointer_v1_destroy(app->confined_ptr);
        app->confined_ptr = NULL;
    }
    app->constraint_state = CONSTRAINT_NONE;
}

static void destroy_relative_ptr(AppState *app)
{
    if (app->rel_ptr) {
        zwp_relative_pointer_v1_destroy(app->rel_ptr);
        app->rel_ptr = NULL;
    }
}

/* ────────────────── wl_pointer listener ────────────────── */

static void pointer_enter(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    AppState *app = data;
    (void)wl_pointer; (void)serial;
    if (surface == app->wl_surface) {
        app->cursor_sx = surface_x;
        app->cursor_sy = surface_y;
    }
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)wl_pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    AppState *app = data;
    (void)wl_pointer; (void)time;
    app->cursor_sx = surface_x;
    app->cursor_sy = surface_y;
}

static void pointer_button(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    (void)data; (void)wl_pointer; (void)serial; (void)time;
    (void)button; (void)state;
}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)wl_pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
    (void)data; (void)wl_pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
    uint32_t axis_source)
{
    (void)data; (void)wl_pointer; (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
    uint32_t time, uint32_t axis)
{
    (void)data; (void)wl_pointer; (void)time; (void)axis;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter         = pointer_enter,
    .leave         = pointer_leave,
    .motion        = pointer_motion,
    .button        = pointer_button,
    .axis          = pointer_axis,
    .frame         = pointer_frame,
    .axis_source   = pointer_axis_source,
    .axis_stop     = pointer_axis_stop,
};

/* ──────────────────── wl_seat listener ────────────────── */

static void seat_capabilities(void *data, struct wl_seat *seat,
    uint32_t capabilities)
{
    AppState *app = data;
    app->seat_caps = capabilities;

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!app->wl_pointer) {
            app->wl_pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(app->wl_pointer, &pointer_listener, app);
            SDL_Log("Got wl_pointer from seat");
        }
    } else if (app->wl_pointer) {
        wl_pointer_destroy(app->wl_pointer);
        app->wl_pointer = NULL;
        SDL_Log("wl_pointer removed");
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!app->wl_keyboard) {
            app->wl_keyboard = wl_seat_get_keyboard(seat);
            SDL_Log("Got wl_keyboard from seat");
        }
    } else if (app->wl_keyboard) {
        wl_keyboard_destroy(app->wl_keyboard);
        app->wl_keyboard = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
};

/* ─────────── locked_pointer listener ──────────────────── */

static void locked_ptr_locked(void *data,
    struct zwp_locked_pointer_v1 *zwp_locked_pointer_v1)
{
    AppState *app = data;
    (void)zwp_locked_pointer_v1;
    app->constraint_state = CONSTRAINT_LOCKED;
    SDL_Log(">>> Pointer LOCKED");
}

static void locked_ptr_unlocked(void *data,
    struct zwp_locked_pointer_v1 *zwp_locked_pointer_v1)
{
    AppState *app = data;
    (void)zwp_locked_pointer_v1;
    SDL_Log(">>> Pointer unlocked");
    if (app->requested_lifetime == LIFETIME_ONESHOT) {
        /* oneshot: object is defunct, destroy it */
        zwp_locked_pointer_v1_destroy(app->locked_ptr);
        app->locked_ptr = NULL;
        app->constraint_state = CONSTRAINT_NONE;
    } else {
        /* persistent: object may reactivate later */
        app->constraint_state = CONSTRAINT_NONE;
    }
}

static const struct zwp_locked_pointer_v1_listener locked_ptr_listener = {
    .locked   = locked_ptr_locked,
    .unlocked = locked_ptr_unlocked,
};

/* ─────────── confined_pointer listener ────────────────── */

static void confined_ptr_confined(void *data,
    struct zwp_confined_pointer_v1 *zwp_confined_pointer_v1)
{
    AppState *app = data;
    (void)zwp_confined_pointer_v1;
    app->constraint_state = CONSTRAINT_CONFINED;
    SDL_Log(">>> Pointer CONFINED");
}

static void confined_ptr_unconfined(void *data,
    struct zwp_confined_pointer_v1 *zwp_confined_pointer_v1)
{
    AppState *app = data;
    (void)zwp_confined_pointer_v1;
    SDL_Log(">>> Pointer unconfined");
    if (app->requested_lifetime == LIFETIME_ONESHOT) {
        zwp_confined_pointer_v1_destroy(app->confined_ptr);
        app->confined_ptr = NULL;
        app->constraint_state = CONSTRAINT_NONE;
    } else {
        app->constraint_state = CONSTRAINT_NONE;
    }
}

static const struct zwp_confined_pointer_v1_listener confined_ptr_listener = {
    .confined   = confined_ptr_confined,
    .unconfined = confined_ptr_unconfined,
};

/* ─────────── relative_pointer listener ────────────────── */

static void rel_ptr_relative_motion(void *data,
    struct zwp_relative_pointer_v1 *zwp_relative_pointer_v1,
    uint32_t utime_hi, uint32_t utime_lo,
    wl_fixed_t dx, wl_fixed_t dy,
    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    AppState *app = data;
    (void)zwp_relative_pointer_v1; (void)utime_hi; (void)utime_lo;

    app->rel_dx = dx;
    app->rel_dy = dy;
    app->rel_dx_unaccel = dx_unaccel;
    app->rel_dy_unaccel = dy_unaccel;

    /* Push into rolling history */
    int head = app->hist_head;
    app->hist[head].dx  = wl_fixed_to_double(dx);
    app->hist[head].dy  = wl_fixed_to_double(dy);
    app->hist[head].dxu = wl_fixed_to_double(dx_unaccel);
    app->hist[head].dyu = wl_fixed_to_double(dy_unaccel);
    app->hist_head = (head + 1) % SMOOTHING_BINS;
    if (app->hist_count < SMOOTHING_BINS)
        app->hist_count++;
}

static const struct zwp_relative_pointer_v1_listener rel_ptr_listener = {
    .relative_motion = rel_ptr_relative_motion,
};

/* ───────────────────── registry ───────────────────────── */

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    AppState *app = data;

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t bind_ver = version < 1u ? version : 1u;
        app->wl_seat = wl_registry_bind(registry, name,
            &wl_seat_interface, bind_ver);
        wl_seat_add_listener(app->wl_seat, &seat_listener, app);
        SDL_Log("Bound wl_seat (name=%u, ver=%u)", name, bind_ver);
    }
    else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        uint32_t bind_ver = version < 1u ? version : 1u;
        app->constraints = wl_registry_bind(registry, name,
            &zwp_pointer_constraints_v1_interface, bind_ver);
        SDL_Log("Bound zwp_pointer_constraints_v1 (name=%u)", name);
    }
    else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t bind_ver = version < 4u ? version : 4u;
        app->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, bind_ver);
        SDL_Log("Bound wl_compositor (name=%u, ver=%u)", name, bind_ver);
    }
    else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        uint32_t bind_ver = version < 1u ? version : 1u;
        app->rel_mgr = wl_registry_bind(registry, name,
            &zwp_relative_pointer_manager_v1_interface, bind_ver);
        SDL_Log("Bound zwp_relative_pointer_manager_v1 (name=%u)", name);
    }
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

/* ─────────────────────── actions ──────────────────────── */

static void do_lock_pointer(AppState *app, enum zwp_pointer_constraints_v1_lifetime lifetime)
{
    if (!app->constraints || !app->wl_pointer || !app->wl_surface) {
        SDL_Log("Cannot lock: missing constraints global, pointer, or surface");
        return;
    }

    /* Release any existing constraint */
    destroy_constraint(app);

    app->requested_lifetime = (lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT)
        ? LIFETIME_PERSISTENT : LIFETIME_ONESHOT;

    app->locked_ptr = zwp_pointer_constraints_v1_lock_pointer(
        app->constraints,
        app->wl_surface,
        app->wl_pointer,
        NULL,  /* no region — entire surface */
        lifetime);
    zwp_locked_pointer_v1_add_listener(app->locked_ptr, &locked_ptr_listener, app);

    /* If we have a hint, set it */
    if (app->hint_active) {
        zwp_locked_pointer_v1_set_cursor_position_hint(app->locked_ptr,
            wl_fixed_from_double(app->hint_x),
            wl_fixed_from_double(app->hint_y));
    }

    app->constraint_state = CONSTRAINT_LOCK_PENDING;
    wl_display_flush(app->wl_display);
    SDL_Log("Lock requested (%s)", lifetime_name(
        app->requested_lifetime));
}

static void do_confine_pointer(AppState *app, enum zwp_pointer_constraints_v1_lifetime lifetime)
{
    if (!app->constraints || !app->wl_pointer || !app->wl_surface) {
        SDL_Log("Cannot confine: missing constraints global, pointer, or surface");
        return;
    }

    destroy_constraint(app);

    app->requested_lifetime = (lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT)
        ? LIFETIME_PERSISTENT : LIFETIME_ONESHOT;

    app->confined_ptr = zwp_pointer_constraints_v1_confine_pointer(
        app->constraints,
        app->wl_surface,
        app->wl_pointer,
        app->show_region ? app->confine_region : NULL,
        lifetime);
    zwp_confined_pointer_v1_add_listener(app->confined_ptr, &confined_ptr_listener, app);

    app->constraint_state = CONSTRAINT_CONFINE_PENDING;
    wl_display_flush(app->wl_display);
    SDL_Log("Confine requested (%s)%s",
        lifetime_name(app->requested_lifetime),
        app->show_region ? " with region" : "");
}

static void do_unconstrain(AppState *app)
{
    destroy_constraint(app);
    SDL_Log("Constraint released");
}

static void do_set_cursor_hint(AppState *app)
{
    if (!app->locked_ptr || app->constraint_state != CONSTRAINT_LOCKED) {
        SDL_Log("Cursor hint requires active lock");
        return;
    }
    /* Set hint to current cursor position */
    app->hint_x = wl_fixed_to_double(app->cursor_sx);
    app->hint_y = wl_fixed_to_double(app->cursor_sy);
    app->hint_active = true;
    zwp_locked_pointer_v1_set_cursor_position_hint(app->locked_ptr,
        app->cursor_sx, app->cursor_sy);
    wl_surface_commit(app->wl_surface);
    wl_display_flush(app->wl_display);
    SDL_Log("Cursor hint set to (%.0f, %.0f)", app->hint_x, app->hint_y);
}

static void do_toggle_region(AppState *app)
{
    app->show_region = !app->show_region;
    if (app->show_region) {
        if (!app->compositor) {
            SDL_Log("Cannot create region: no wl_compositor");
            app->show_region = false;
            return;
        }
        /* Create region covering the center third of the canvas */
        int rx = WINDOW_W / 4;
        int ry = CANVAS_H / 4;
        int rw = WINDOW_W / 2;
        int rh = CANVAS_H / 2;
        app->region_rect = (SDL_Rect){ rx, ry, rw, rh };

        app->confine_region = wl_compositor_create_region(app->compositor);
        if (app->confine_region) {
            wl_region_add(app->confine_region, rx, ry, rw, rh);
            /* Apply to locked/confined pointer if active */
            if (app->locked_ptr) {
                zwp_locked_pointer_v1_set_region(app->locked_ptr,
                    app->confine_region);
                wl_surface_commit(app->wl_surface);
            }
            if (app->confined_ptr) {
                zwp_confined_pointer_v1_set_region(app->confined_ptr,
                    app->confine_region);
                wl_surface_commit(app->wl_surface);
            }
            SDL_Log("Confine region set: (%d,%d) %dx%d", rx, ry, rw, rh);
        } else {
            SDL_Log("Failed to create wl_region");
            app->show_region = false;
        }
    } else {
        if (app->confine_region) {
            wl_region_destroy(app->confine_region);
            app->confine_region = NULL;
        }
    }
}

static void do_clear_motion(AppState *app)
{
    app->rel_dx = app->rel_dy = 0;
    app->rel_dx_unaccel = app->rel_dy_unaccel = 0;
    memset(app->hist, 0, sizeof(app->hist));
    app->hist_head = 0;
    app->hist_count = 0;
    SDL_Log("Motion data cleared");
}

static void do_scene_lock_motion(AppState *app)
{
    /* Scene 1: Lock pointer (oneshot) + relative pointer test */
    SDL_Log("── Scene: Lock + RelativePointer ──");
    /* Ensure relative pointer exists */
    if (!app->rel_ptr && app->rel_mgr && app->wl_pointer) {
        app->rel_ptr = zwp_relative_pointer_manager_v1_get_relative_pointer(
            app->rel_mgr, app->wl_pointer);
        zwp_relative_pointer_v1_add_listener(app->rel_ptr,
            &rel_ptr_listener, app);
        SDL_Log("Relative pointer created");
    }
    do_lock_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
}

static void do_scene_confine_only(AppState *app)
{
    /* Scene 2: Confine pointer (oneshot) — test region confinement */
    SDL_Log("── Scene: Confine only ──");
    app->show_region = true;
    app->region_rect = (SDL_Rect){ WINDOW_W/4, CANVAS_H/4, WINDOW_W/2, CANVAS_H/2 };
    do_confine_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
}

/* ─────────────────── button setup ─────────────────────── */

#define BW  140.f
#define BH   28.f
#define PAD   8.f

static void init_buttons(AppState *app)
{
    float py = (float)CANVAS_H + PAD;
    float col[3];
    for (int i = 0; i < 3; ++i)
        col[i] = PAD + (float)i * (BW + PAD);

    /* Row 0: constraint modes */
    app->buttons[BTN_LOCK_ONESHOT]       = (Button){{ col[0], py, BW, BH }, "Lock oneshot",  BTN_LOCK_ONESHOT };
    app->buttons[BTN_LOCK_PERSISTENT]    = (Button){{ col[1], py, BW, BH }, "Lock persist",  BTN_LOCK_PERSISTENT };
    app->buttons[BTN_CONFINE_ONESHOT]    = (Button){{ col[2], py, BW, BH }, "Conf oneshot",  BTN_CONFINE_ONESHOT };
    py += BH + PAD;

    /* Row 1: more modes + unconstrain */
    app->buttons[BTN_CONFINE_PERSISTENT] = (Button){{ col[0], py, BW, BH }, "Conf persist",  BTN_CONFINE_PERSISTENT };
    /* Span columns 1-2 for Unconstrain */
    app->buttons[BTN_UNCONSTRAIN]        = (Button){{ col[1], py, BW*2.f+PAD, BH }, "Unconstrain (U)", BTN_UNCONSTRAIN };
    py += BH + PAD;

    /* Row 2: tools */
    app->buttons[BTN_SET_HINT]           = (Button){{ col[0], py, BW, BH }, "Set Hint (H)",  BTN_SET_HINT };
    app->buttons[BTN_TOGGLE_REGION]      = (Button){{ col[1], py, BW, BH }, "Region (R)",    BTN_TOGGLE_REGION };
    app->buttons[BTN_CLEAR_MOTION]       = (Button){{ col[2], py, BW, BH }, "Clear (C)",     BTN_CLEAR_MOTION };
    py += BH + PAD;

    /* Row 3: scene presets */
    app->buttons[BTN_SCENE_LOCK_MOTION]  = (Button){{ col[0], py, BW, BH }, "Scene Lock",    BTN_SCENE_LOCK_MOTION };
    app->buttons[BTN_SCENE_CONFINE_ONLY] = (Button){{ col[1], py, BW, BH }, "Scene Confine", BTN_SCENE_CONFINE_ONLY };
}

/* ────────────────── button drawing ────────────────────── */

static void draw_button(SDL_Renderer *rend, const Button *b)
{
    if (b->hovered)
        SDL_SetRenderDrawColor(rend, 90, 160, 255, 255);
    else
        SDL_SetRenderDrawColor(rend, 55, 65, 85, 230);
    SDL_RenderFillRect(rend, &b->rect);
    SDL_SetRenderDrawColor(rend, 160, 200, 255, 180);
    SDL_RenderRect(rend, &b->rect);
    SDL_SetRenderDrawColor(rend, 230, 235, 255, 255);
    float tx = b->rect.x + 4.f;
    float ty = b->rect.y + (b->rect.h - 8.f) * 0.5f;
    SDL_RenderDebugText(rend, tx, ty, b->label);
}

/* ─────────────────── handle button action ─────────────── */

static void handle_button_action(AppState *app, BtnAction action)
{
    switch (action) {
    case BTN_LOCK_ONESHOT:
        do_lock_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        break;
    case BTN_LOCK_PERSISTENT:
        do_lock_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        break;
    case BTN_CONFINE_ONESHOT:
        do_confine_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        break;
    case BTN_CONFINE_PERSISTENT:
        do_confine_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        break;
    case BTN_UNCONSTRAIN:
        do_unconstrain(app);
        break;
    case BTN_SET_HINT:
        do_set_cursor_hint(app);
        break;
    case BTN_TOGGLE_REGION:
        do_toggle_region(app);
        break;
    case BTN_CLEAR_MOTION:
        do_clear_motion(app);
        break;
    case BTN_SCENE_LOCK_MOTION:
        do_scene_lock_motion(app);
        break;
    case BTN_SCENE_CONFINE_ONLY:
        do_scene_confine_only(app);
        break;
    default:
        break;
    }
    wl_display_flush(app->wl_display);
}

/* ────────────────────── rendering ─────────────────────── */

static void draw_mini_bar(SDL_Renderer *rend, float x, float y,
    float max_w, float value, Uint8 r, Uint8 g, Uint8 b)
{
    /* Clamp and scale */
    float w = (value > 0.f) ? SDL_min(max_w, value * 40.f)
                            : SDL_min(max_w, -value * 40.f);
    float sign = (value >= 0.f) ? 1.f : -1.f;
    (void)sign; /* we just show magnitude with color direction */

    if (w < 1.f) w = 1.f;
    SDL_FRect bar = { x, y, w, 6.f };
    SDL_SetRenderDrawColor(rend, r, g, b, 220);
    SDL_RenderFillRect(rend, &bar);
}

static void draw_canvas(AppState *app)
{
    SDL_Renderer *rend = app->renderer;
    uint64_t now = SDL_GetTicks();
    double t = now / 1000.0;

    /* ── background gradient ── */
    Uint8 bg_r = (Uint8)(30 + 20 * sin(t * 0.3));
    Uint8 bg_g = (Uint8)(40 + 20 * sin(t * 0.3 + 2.094));
    Uint8 bg_b = (Uint8)(60 + 20 * sin(t * 0.3 + 4.189));
    SDL_SetRenderDrawColor(rend, bg_r, bg_g, bg_b, 255);
    SDL_RenderClear(rend);

    /* ── constraint state visual ── */
    float cx = (float)WINDOW_W / 2.f;
    float cy = (float)CANVAS_H / 2.f;

    switch (app->constraint_state) {
    case CONSTRAINT_LOCKED: {
        /* Ring + crosshair */
        SDL_SetRenderDrawColor(rend, 255, 80, 60, 200);
        SDL_FRect ring = { cx - 60.f, cy - 60.f, 120.f, 120.f };
        SDL_RenderRect(rend, &ring);
        SDL_RenderLine(rend, cx - 40.f, cy, cx + 40.f, cy);
        SDL_RenderLine(rend, cx, cy - 40.f, cx, cy + 40.f);
        /* Lock icon text */
        SDL_SetRenderDrawColor(rend, 255, 100, 80, 255);
        SDL_RenderDebugText(rend, cx - 12.f, cy - 60.f - 16.f, "🔒 LOCKED");
        break;
    }
    case CONSTRAINT_CONFINED: {
        /* Highlight confine region */
        if (app->show_region) {
            SDL_FRect region = {
                (float)app->region_rect.x,
                (float)app->region_rect.y,
                (float)app->region_rect.w,
                (float)app->region_rect.h
            };
            SDL_SetRenderDrawColor(rend, 60, 200, 120, 80);
            SDL_RenderFillRect(rend, &region);
            SDL_SetRenderDrawColor(rend, 60, 255, 120, 220);
            SDL_RenderRect(rend, &region);
            SDL_RenderDebugText(rend, region.x + 4.f, region.y + 4.f, "CONFINE ZONE");
        }
        SDL_SetRenderDrawColor(rend, 60, 200, 120, 200);
        SDL_FRect rect = { cx - 50.f, cy - 50.f, 100.f, 100.f };
        SDL_RenderRect(rend, &rect);
        SDL_RenderDebugText(rend, cx - 32.f, cy - 50.f - 16.f, "CONFINED");
        break;
    }
    case CONSTRAINT_LOCK_PENDING:
        SDL_SetRenderDrawColor(rend, 255, 200, 60, 200);
        SDL_RenderDebugText(rend, cx - 40.f, cy - 8.f, "Lock pending...");
        break;
    case CONSTRAINT_CONFINE_PENDING:
        SDL_SetRenderDrawColor(rend, 255, 200, 60, 200);
        SDL_RenderDebugText(rend, cx - 50.f, cy - 8.f, "Confine pending...");
        break;
    case CONSTRAINT_NONE:
    default:
        /* Normal cursor indicator */
        SDL_SetRenderDrawColor(rend, 180, 210, 240, 160);
        SDL_RenderDebugText(rend, cx - 30.f, cy - 8.f, "No constraint");
        break;
    }

    /* ── crosshair at cursor position ── */
    float csx = (float)wl_fixed_to_double(app->cursor_sx);
    float csy = (float)wl_fixed_to_double(app->cursor_sy);
    if (csx >= 0 && csy >= 0 && csx < WINDOW_W && csy < CANVAS_H) {
        SDL_SetRenderDrawColor(rend, 220, 220, 255, 140);
        SDL_RenderLine(rend, csx - 8.f, csy, csx + 8.f, csy);
        SDL_RenderLine(rend, csx, csy - 8.f, csx, csy + 8.f);
        SDL_FRect dot = { csx - 2.f, csy - 2.f, 4.f, 4.f };
        SDL_SetRenderDrawColor(rend, 255, 255, 255, 200);
        SDL_RenderFillRect(rend, &dot);
    }

    /* ── HUD info box ── */
    SDL_FRect hud = { 8.f, 8.f, 340.f, 120.f };
    SDL_SetRenderDrawColor(rend, 0, 0, 0, 120);
    SDL_RenderFillRect(rend, &hud);
    SDL_SetRenderDrawColor(rend, 160, 200, 240, 160);
    SDL_RenderRect(rend, &hud);

    SDL_SetRenderDrawColor(rend, 230, 240, 255, 255);
    char line[96];
    SDL_RenderDebugText(rend, 14.f, 14.f,
        "pointer-constraints + relative-pointer test");

    snprintf(line, sizeof line, "State: %s  [%s]",
        state_name(app->constraint_state),
        lifetime_name(app->requested_lifetime));
    SDL_RenderDebugText(rend, 14.f, 30.f, line);

    snprintf(line, sizeof line, "Cursor: (%.0f, %.0f)",
        csx, csy);
    SDL_RenderDebugText(rend, 14.f, 44.f, line);

    snprintf(line, sizeof line, "Rel motion: dx=%.1f  dy=%.1f",
        wl_fixed_to_double(app->rel_dx),
        wl_fixed_to_double(app->rel_dy));
    SDL_RenderDebugText(rend, 14.f, 58.f, line);

    snprintf(line, sizeof line, "Raw unaccel: dx=%.1f  dy=%.1f",
        wl_fixed_to_double(app->rel_dx_unaccel),
        wl_fixed_to_double(app->rel_dy_unaccel));
    SDL_RenderDebugText(rend, 14.f, 72.f, line);

    /* ── motion mini-bars ── */
    if (app->hist_count > 0) {
        float bar_x = 14.f;
        float bar_y = 90.f;
        float bar_max = 100.f;

        /* Average history for smoothed bars */
        float avg_dx = 0.f, avg_dy = 0.f, avg_dxu = 0.f, avg_dyu = 0.f;
        int n = app->hist_count;
        for (int i = 0; i < n; i++) {
            avg_dx  += app->hist[i].dx;
            avg_dy  += app->hist[i].dy;
            avg_dxu += app->hist[i].dxu;
            avg_dyu += app->hist[i].dyu;
        }
        avg_dx  /= n; avg_dy  /= n;
        avg_dxu /= n; avg_dyu /= n;

        /* Accelerated bars */
        draw_mini_bar(rend, bar_x,       bar_y, bar_max, avg_dx,  255, 150, 100);
        draw_mini_bar(rend, bar_x + 110, bar_y, bar_max, avg_dy,  100, 180, 255);
        /* Unaccelerated bars (offset below) */
        draw_mini_bar(rend, bar_x,       bar_y + 10, bar_max, avg_dxu, 255, 80, 60);
        draw_mini_bar(rend, bar_x + 110, bar_y + 10, bar_max, avg_dyu, 60, 140, 255);

        SDL_SetRenderDrawColor(rend, 180, 200, 230, 180);
        SDL_RenderDebugText(rend, bar_x, bar_y - 8.f, "dx accel");
        SDL_RenderDebugText(rend, bar_x + 110, bar_y - 8.f, "dy accel");
    }
}

/* ─────────────────── main loop drawing ────────────────── */

static void draw_frame(AppState *app)
{
    SDL_Renderer *rend = app->renderer;

    /* ── canvas ── */
    draw_canvas(app);

    /* ── panel background ── */
    SDL_FRect panel = { 0.f, (float)CANVAS_H, (float)WINDOW_W, (float)PANEL_H };
    SDL_SetRenderDrawColor(rend, 22, 28, 40, 255);
    SDL_RenderFillRect(rend, &panel);
    SDL_SetRenderDrawColor(rend, 80, 100, 140, 180);
    SDL_FRect sep = { 0.f, (float)CANVAS_H, (float)WINDOW_W, 1.f };
    SDL_RenderFillRect(rend, &sep);

    /* ── button section label ── */
    SDL_SetRenderDrawColor(rend, 130, 160, 210, 200);
    SDL_RenderDebugText(rend, PAD, (float)CANVAS_H + PAD - 2.f,
        "CONTROLS");

    for (int i = 0; i < BTN_COUNT; ++i)
        draw_button(rend, &app->buttons[i]);

    /* ── key hints at bottom ── */
    SDL_SetRenderDrawColor(rend, 100, 120, 160, 200);
    float ky = (float)CANVAS_H + PANEL_H - 14.f;
    SDL_RenderDebugText(rend, PAD, ky,
        "1:Lock1 2:LockP 3:Conf1 4:ConfP  U:Unconstrain  H:Hint  R:Region  C:Clear  Q:Quit");

    SDL_RenderPresent(rend);
}

/* ─────────────────── event handling ───────────────────── */

static void handle_key(AppState *app, SDL_Keycode key, SDL_Keymod mod)
{
    (void)mod;
    switch (key) {
    case SDLK_ESCAPE: case SDLK_Q:
        app->running = false;
        break;
    case SDLK_1:
        do_lock_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        break;
    case SDLK_2:
        do_lock_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        break;
    case SDLK_3:
        do_confine_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        break;
    case SDLK_4:
        do_confine_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        break;
    case SDLK_U:
        do_unconstrain(app);
        break;
    case SDLK_H:
        do_set_cursor_hint(app);
        break;
    case SDLK_R:
        do_toggle_region(app);
        break;
    case SDLK_C:
        do_clear_motion(app);
        break;
    case SDLK_SPACE: {
        /* Cycle: lock oneshot -> confine oneshot -> none */
        if (app->constraint_state == CONSTRAINT_NONE)
            do_lock_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        else if (app->constraint_state == CONSTRAINT_LOCKED ||
                 app->constraint_state == CONSTRAINT_LOCK_PENDING)
            do_confine_pointer(app, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        else
            do_unconstrain(app);
        break;
    }
    default:
        break;
    }
    wl_display_flush(app->wl_display);
}

static void process_events(AppState *app)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            app->running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            handle_key(app, ev.key.key, ev.key.mod);
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            float mx = ev.motion.x, my = ev.motion.y;
            for (int i = 0; i < BTN_COUNT; ++i) {
                SDL_FRect *r = &app->buttons[i].rect;
                app->buttons[i].hovered =
                    mx >= r->x && mx < r->x + r->w &&
                    my >= r->y && my < r->y + r->h;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (ev.button.button != SDL_BUTTON_LEFT) break;
            float mx = ev.button.x, my = ev.button.y;
            for (int i = 0; i < BTN_COUNT; ++i) {
                SDL_FRect *r = &app->buttons[i].rect;
                if (mx >= r->x && mx < r->x + r->w &&
                    my >= r->y && my < r->y + r->h) {
                    handle_button_action(app, app->buttons[i].action);
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

/* ────────────────────── main ──────────────────────────── */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    AppState *app = &g_app;
    SDL_zero(*app);
    app->running = true;
    app->show_region = false;
    app->hint_active = false;
    app->constraint_state = CONSTRAINT_NONE;
    app->requested_lifetime = LIFETIME_ONESHOT;

    SDL_SetAppMetadata("test-pointer-constraints", "1.0",
        "org.treeland.demo.pointer-constraints");

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_Init: %s", SDL_GetError());
        return 1;
    }

    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Requires Wayland backend. Set SDL_VIDEODRIVER=wayland");
        SDL_Quit();
        return 1;
    }

    /* ── Wayland display from SDL ── */
    app->wl_display = SDL_GetPointerProperty(SDL_GetGlobalProperties(),
        SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, NULL);
    if (!app->wl_display) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No wl_display from SDL");
        SDL_Quit();
        return 1;
    }

    /* ── Registry ── */
    app->wl_registry = wl_display_get_registry(app->wl_display);
    wl_registry_add_listener(app->wl_registry, &registry_listener, app);
    wl_display_roundtrip(app->wl_display);

    if (!app->constraints) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Compositor does not advertise zwp_pointer_constraints_v1.");
        SDL_Log("Make sure you are running on a compositor that supports "
            "pointer-constraints-unstable-v1.");
        SDL_Quit();
        return 1;
    }

    if (!app->rel_mgr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Compositor does not advertise zwp_relative_pointer_manager_v1. "
            "Relative motion events will not be available.");
    }

    /* ── SDL window ── */
    app->window = SDL_CreateWindow(
        "pointer-constraints test", WINDOW_W, WINDOW_H, 0);
    if (!app->window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateRenderer: %s", SDL_GetError());
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        return 1;
    }

    SDL_ShowWindow(app->window);
    SDL_PumpEvents();
    wl_display_roundtrip(app->wl_display);

    /* ── wl_surface from SDL window ── */
    SDL_PropertiesID props = SDL_GetWindowProperties(app->window);
    app->wl_surface = SDL_GetPointerProperty(props,
        SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!app->wl_surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "No wl_surface from SDL window");
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        return 1;
    }

    /* ── Create relative pointer (always, if available) ── */
    if (app->rel_mgr && app->wl_pointer) {
        app->rel_ptr = zwp_relative_pointer_manager_v1_get_relative_pointer(
            app->rel_mgr, app->wl_pointer);
        zwp_relative_pointer_v1_add_listener(app->rel_ptr,
            &rel_ptr_listener, app);
        SDL_Log("Relative pointer created");
    }

    /* ── Default confine region ── */
    app->region_rect = (SDL_Rect){
        WINDOW_W / 4, CANVAS_H / 4,
        WINDOW_W / 2, CANVAS_H / 2
    };

    /* ── Buttons ── */
    init_buttons(app);

    /* ── Need another roundtrip to ensure wl_pointer is available ── */
    wl_display_roundtrip(app->wl_display);

    SDL_Log("═══════════════════════════════════════════════════");
    SDL_Log("  test-pointer-constraints");
    SDL_Log("  1-4: constraint modes    U: unconstrain");
    SDL_Log("  H: set cursor hint       R: toggle region");
    SDL_Log("  C: clear motion data     Q/Esc: quit");
    SDL_Log("═══════════════════════════════════════════════════");

    /* ── main loop ── */
    while (app->running) {
        if (wl_display_dispatch_pending(app->wl_display) < 0)
            break;

        process_events(app);
        draw_frame(app);

        wl_display_flush(app->wl_display);
        SDL_Delay(16);
    }

    /* ── cleanup ── */
    SDL_Log("Cleaning up...");

    destroy_constraint(app);
    destroy_relative_ptr(app);

    if (app->confine_region) {
        wl_region_destroy(app->confine_region);
        app->confine_region = NULL;
    }

    if (app->constraints) {
        zwp_pointer_constraints_v1_destroy(app->constraints);
        app->constraints = NULL;
    }
    if (app->rel_mgr) {
        zwp_relative_pointer_manager_v1_destroy(app->rel_mgr);
        app->rel_mgr = NULL;
    }
    if (app->wl_keyboard) {
        wl_keyboard_destroy(app->wl_keyboard);
        app->wl_keyboard = NULL;
    }
    if (app->wl_pointer) {
        wl_pointer_destroy(app->wl_pointer);
        app->wl_pointer = NULL;
    }
    if (app->wl_seat) {
        wl_seat_destroy(app->wl_seat);
        app->wl_seat = NULL;
    }
    if (app->wl_registry) {
        wl_registry_destroy(app->wl_registry);
        app->wl_registry = NULL;
    }
    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }
    SDL_Quit();

    SDL_Log("Done.");
    return 0;
}