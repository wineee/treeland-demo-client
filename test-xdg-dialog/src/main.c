#include <SDL3/SDL.h>
#include <wayland-client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-dialog-v1-client-protocol.h"

/* ── Wayland globals ───────────────────────────────────── */
static struct wl_display        *display      = NULL;
static struct wl_registry       *registry     = NULL;
static struct wl_compositor     *compositor   = NULL;
static struct xdg_wm_base       *wm_base      = NULL;
static struct xdg_wm_dialog_v1  *wm_dialog    = NULL;

/* ── Parent window (SDL manages xdg objects) ───────────── */
static SDL_Window   *parent_win    = NULL;
static SDL_Renderer *parent_rend   = NULL;
static struct wl_surface   *parent_wl_surface = NULL;
static struct xdg_toplevel *parent_toplevel   = NULL;

/* ── Dialog windows ────────────────────────────────────── */
typedef struct {
    SDL_Window        *win;
    SDL_Renderer      *rend;
    struct wl_surface  *wl_surface;
    struct xdg_toplevel *toplevel;
    struct xdg_dialog_v1 *dialog;
    bool                modal;
    bool                alive;
    bool                focused;
    const char         *title;
} DialogWindow;

#define MAX_DIALOGS 2
static DialogWindow dialogs[MAX_DIALOGS];
static int dialog_count = 0;

/* ── Registry listener ─────────────────────────────────── */
static void registry_global(void *data, struct wl_registry *reg,
                            uint32_t name, const char *iface, uint32_t ver) {
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
    else if (strcmp(iface, xdg_wm_base_interface.name) == 0)
        wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
    else if (strcmp(iface, xdg_wm_dialog_v1_interface.name) == 0) {
        wm_dialog = wl_registry_bind(reg, name, &xdg_wm_dialog_v1_interface, 1);
        printf("[init] xdg_wm_dialog_v1 bound\n");
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

/* ── xdg_wm_base ping ─────────────────────────────────── */
static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
    xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

/* ── Create one dialog ─────────────────────────────────── */
static bool create_dialog(int index, bool modal) {
    if (!wm_dialog || dialog_count >= MAX_DIALOGS) return false;

    DialogWindow *d = &dialogs[index];
    d->modal = modal;
    d->alive = true;
    d->title = modal ? "Modal Dialog" : "Non-Modal Dialog";

    /* SDL window */
    d->win = SDL_CreateWindow(d->title, 380, 250, SDL_WINDOW_RESIZABLE);
    if (!d->win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }

    d->rend = SDL_CreateRenderer(d->win, NULL);
    if (!d->rend) { SDL_DestroyWindow(d->win); d->win = NULL; return false; }

    /* pump so the wayland surface exists */
    SDL_ShowWindow(d->win);
    SDL_PumpEvents();
    wl_display_roundtrip(display);

    /* grab wayland objects that SDL created */
    SDL_PropertiesID props = SDL_GetWindowProperties(d->win);
    d->wl_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    d->toplevel   = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, NULL);
    if (!d->wl_surface || !d->toplevel) {
        fprintf(stderr, "Failed to get wayland objects for dialog %d\n", index);
        SDL_DestroyRenderer(d->rend); SDL_DestroyWindow(d->win);
        d->win = NULL; d->rend = NULL; d->alive = false;
        return false;
    }

    /* set parent */
    xdg_toplevel_set_parent(d->toplevel, parent_toplevel);

    /* create xdg_dialog_v1 */
    d->dialog = xdg_wm_dialog_v1_get_xdg_dialog(wm_dialog, d->toplevel);
    if (modal)
        xdg_dialog_v1_set_modal(d->dialog);
    else
        xdg_dialog_v1_unset_modal(d->dialog);

    wl_surface_commit(d->wl_surface);

    printf("[dialog %d] created  modal=%s\n", index, modal ? "YES" : "NO");
    dialog_count++;
    return true;
}

/* ── Destroy one dialog ────────────────────────────────── */
static void destroy_dialog(int index) {
    DialogWindow *d = &dialogs[index];
    if (!d->alive) return;

    if (d->dialog)   { xdg_dialog_v1_destroy(d->dialog);   d->dialog   = NULL; }
    d->toplevel = NULL;  /* SDL owns it */
    if (d->rend)     { SDL_DestroyRenderer(d->rend);       d->rend     = NULL; }
    if (d->win)      { SDL_DestroyWindow(d->win);          d->win      = NULL; }
    d->wl_surface = NULL;
    d->alive = false;
    dialog_count--;
    printf("[dialog %d] destroyed\n", index);
}

/* ── Check if any modal dialog is alive ───────────────── */
static bool has_modal_dialog(void) {
    for (int i = 0; i < MAX_DIALOGS; i++) {
        if (dialogs[i].alive && dialogs[i].modal)
            return true;
    }
    return false;
}

/* ── Button helper ─────────────────────────────────────── */
typedef struct { SDL_FRect rect; const char *label; SDL_Color color; int id; bool hovered; } Button;

enum { BTN_OPEN_ALL = 1, BTN_CLOSE_ALL = 2, BTN_QUIT = 3 };

static void draw_button(SDL_Renderer *r, Button *b) {
    Uint8 mul = b->hovered ? 255 : 200;
    SDL_SetRenderDrawColor(r,
        (Uint8)(b->color.r * mul / 255),
        (Uint8)(b->color.g * mul / 255),
        (Uint8)(b->color.b * mul / 255), 255);
    SDL_RenderFillRect(r, &b->rect);
    SDL_SetRenderDrawColor(r, 180, 200, 240, 200);
    SDL_RenderRect(r, &b->rect);
    /* text */
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    float tx = b->rect.x + (b->rect.w - strlen(b->label) * 8.f) * 0.5f;
    float ty = b->rect.y + (b->rect.h - 8.f) * 0.5f;
    SDL_RenderDebugText(r, tx, ty, b->label);
}

/* ── Draw a dialog window ──────────────────────────────── */
static void draw_dialog(DialogWindow *d) {
    if (!d->alive || !d->rend) return;
    SDL_Renderer *r = d->rend;

    /* Update focus state */
    if (d->win) {
        SDL_WindowFlags flags = SDL_GetWindowFlags(d->win);
        d->focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    }

    /* Background color changes based on focus */
    if (d->focused) {
        SDL_SetRenderDrawColor(r, 50, 50, 75, 255);
    } else {
        SDL_SetRenderDrawColor(r, 35, 35, 50, 255);
    }
    SDL_RenderClear(r);

    /* title */
    SDL_SetRenderDrawColor(r, 200, 220, 255, 255);
    SDL_RenderDebugText(r, 15, 12, d->title);

    /* Focus status indicator */
    if (d->focused) {
        SDL_SetRenderDrawColor(r, 80, 220, 80, 255);
        SDL_RenderDebugText(r, 250, 12, "[ FOCUSED ]");
    } else {
        SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
        SDL_RenderDebugText(r, 250, 12, "[ UNFOCUSED ]");
    }

    /* modal badge */
    if (d->modal) {
        SDL_SetRenderDrawColor(r, 255, 180, 60, 255);
        SDL_RenderDebugText(r, 15, 32, "[ MODAL ]");
        SDL_SetRenderDrawColor(r, 180, 190, 210, 255);
        SDL_RenderDebugText(r, 15, 48, "Parent window is blocked");
    } else {
        SDL_SetRenderDrawColor(r, 100, 200, 100, 255);
        SDL_RenderDebugText(r, 15, 32, "[ NON-MODAL ]");
        SDL_SetRenderDrawColor(r, 180, 190, 210, 255);
        SDL_RenderDebugText(r, 15, 48, "Parent window is interactive");
    }

    /* content box */
    SDL_FRect box = { 15, 70, 350, 140 };
    SDL_SetRenderDrawColor(r, 30, 30, 50, 255);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 100, 120, 160, 180);
    SDL_RenderRect(r, &box);

    SDL_SetRenderDrawColor(r, 180, 190, 210, 255);
    SDL_RenderDebugText(r, 25, 85,  "This dialog uses xdg-dialog-v1");
    SDL_RenderDebugText(r, 25, 101, "protocol to hint the compositor");
    SDL_RenderDebugText(r, 25, 117, "about its dialog behavior.");

    /* Focus details */
    SDL_SetRenderDrawColor(r, 140, 150, 175, 255);
    SDL_RenderDebugText(r, 25, 137, "Window state:");
    if (d->focused) {
        SDL_SetRenderDrawColor(r, 80, 220, 80, 255);
        SDL_RenderDebugText(r, 25, 153, "  - Has input focus");
        SDL_RenderDebugText(r, 25, 169, "  - Receiving keyboard events");
    } else {
        SDL_SetRenderDrawColor(r, 200, 150, 100, 255);
        SDL_RenderDebugText(r, 25, 153, "  - No input focus");
        SDL_RenderDebugText(r, 25, 169, "  - Click to focus");
    }

    /* Focus indicator bar at bottom */
    SDL_FRect focus_bar = { 0, 230, 380, 20 };
    if (d->focused) {
        SDL_SetRenderDrawColor(r, 60, 140, 60, 255);
    } else {
        SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
    }
    SDL_RenderFillRect(r, &focus_bar);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, 10, 235, d->focused ? "ACTIVE" : "INACTIVE");

    SDL_RenderPresent(r);
}

/* ── main ──────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0) {
        fprintf(stderr, "Need wayland backend (have '%s'). "
                "Set SDL_VIDEODRIVER=wayland\n", SDL_GetCurrentVideoDriver());
        SDL_Quit(); return 1;
    }

    display = SDL_GetPointerProperty(SDL_GetGlobalProperties(),
                 SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, NULL);
    if (!display) { fprintf(stderr, "No wl_display\n"); SDL_Quit(); return 1; }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !wm_base) {
        fprintf(stderr, "Missing required wayland interfaces\n");
        SDL_Quit(); return 1;
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    /* parent window */
    parent_win = SDL_CreateWindow("XDG Dialog v1 Test", 750, 480, SDL_WINDOW_RESIZABLE);
    if (!parent_win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    parent_rend = SDL_CreateRenderer(parent_win, NULL);

    SDL_ShowWindow(parent_win);
    SDL_PumpEvents();
    wl_display_roundtrip(display);

    SDL_PropertiesID props = SDL_GetWindowProperties(parent_win);
    parent_wl_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    parent_toplevel   = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_POINTER, NULL);
    if (!parent_wl_surface || !parent_toplevel) {
        fprintf(stderr, "Failed to get parent wayland objects\n");
        SDL_Quit(); return 1;
    }

    printf("=== XDG Dialog v1 Test ===\n");
    printf("Interfaces:\n");
    printf("  wl_compositor     : %s\n", compositor ? "YES" : "NO");
    printf("  xdg_wm_base       : %s\n", wm_base    ? "YES" : "NO");
    printf("  xdg_wm_dialog_v1  : %s\n", wm_dialog  ? "YES" : "NO");

    /* buttons */
    Button buttons[] = {
        { { 30,  60, 200, 44 }, "Open Both Dialogs",  {70, 140, 70, 255},  BTN_OPEN_ALL,  false },
        { { 30, 120, 200, 44 }, "Close All Dialogs",  {140, 70, 70, 255},  BTN_CLOSE_ALL, false },
        { { 30, 180, 200, 44 }, "Quit",               {100, 100, 100, 255}, BTN_QUIT,      false },
    };
    int nbtn = sizeof(buttons) / sizeof(buttons[0]);

    bool running = true;
    SDL_Event ev;

    while (running) {
        wl_display_flush(display);

        bool modal_active = has_modal_dialog();

        while (SDL_PollEvent(&ev)) {
            /* Events for dialog windows are handled by SDL automatically */
            
            /* Check if this event belongs to the parent window */
            bool is_parent_event = (ev.type == SDL_EVENT_QUIT) ||
                                   (ev.key.windowID == SDL_GetWindowID(parent_win)) ||
                                   (ev.button.windowID == SDL_GetWindowID(parent_win)) ||
                                   (ev.motion.windowID == SDL_GetWindowID(parent_win));

            /* If modal dialog is active, block parent input events (except ESC) */
            if (modal_active && is_parent_event) {
                /* Allow ESC to quit even when modal */
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                /* Block all other parent events */
                continue;
            }

            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.key == SDLK_ESCAPE) running = false;
                else if (ev.key.key == SDLK_O) {  /* 'O' = Open */
                    if (dialog_count == 0) {
                        create_dialog(0, true);   /* modal */
                        create_dialog(1, false);  /* non-modal */
                    }
                }
                else if (ev.key.key == SDLK_C) {  /* 'C' = Close */
                    destroy_dialog(0);
                    destroy_dialog(1);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                float mx = ev.button.x, my = ev.button.y;
                for (int i = 0; i < nbtn; i++) {
                    if (mx >= buttons[i].rect.x && mx <= buttons[i].rect.x + buttons[i].rect.w &&
                        my >= buttons[i].rect.y && my <= buttons[i].rect.y + buttons[i].rect.h) {
                        switch (buttons[i].id) {
                        case BTN_OPEN_ALL:
                            if (dialog_count == 0) {
                                create_dialog(0, true);
                                create_dialog(1, false);
                            }
                            break;
                        case BTN_CLOSE_ALL:
                            destroy_dialog(0);
                            destroy_dialog(1);
                            break;
                        case BTN_QUIT:
                            running = false;
                            break;
                        }
                    }
                }
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                float mx = ev.motion.x, my = ev.motion.y;
                for (int i = 0; i < nbtn; i++)
                    buttons[i].hovered = (mx >= buttons[i].rect.x && mx <= buttons[i].rect.x + buttons[i].rect.w &&
                                          my >= buttons[i].rect.y && my <= buttons[i].rect.y + buttons[i].rect.h);
                break;
            }
            }
        }

        /* ── draw parent ──────────────────────────────── */
        SDL_SetRenderDrawColor(parent_rend, 30, 30, 48, 255);
        SDL_RenderClear(parent_rend);

        /* title */
        SDL_SetRenderDrawColor(parent_rend, 200, 215, 255, 255);
        SDL_RenderDebugText(parent_rend, 20, 15, "xdg-dialog-v1 Test Client");

        /* buttons */
        for (int i = 0; i < nbtn; i++)
            draw_button(parent_rend, &buttons[i]);

        /* status panel */
        SDL_FRect panel = { 260, 60, 460, 380 };
        SDL_SetRenderDrawColor(parent_rend, 22, 22, 38, 255);
        SDL_RenderFillRect(parent_rend, &panel);
        SDL_SetRenderDrawColor(parent_rend, 80, 100, 140, 180);
        SDL_RenderRect(parent_rend, &panel);

        SDL_SetRenderDrawColor(parent_rend, 170, 185, 220, 255);
        SDL_RenderDebugText(parent_rend, 275, 72, "Interface Status");

        SDL_SetRenderDrawColor(parent_rend, compositor ? 80 : 220, compositor ? 200 : 80, 80, 255);
        SDL_RenderDebugText(parent_rend, 285, 92,  "wl_compositor");
        SDL_SetRenderDrawColor(parent_rend, wm_base ? 80 : 220, wm_base ? 200 : 80, 80, 255);
        SDL_RenderDebugText(parent_rend, 285, 106, "xdg_wm_base");
        SDL_SetRenderDrawColor(parent_rend, wm_dialog ? 80 : 220, wm_dialog ? 200 : 80, 80, 255);
        SDL_RenderDebugText(parent_rend, 285, 120, "xdg_wm_dialog_v1");

        /* dialog status */
        SDL_SetRenderDrawColor(parent_rend, 170, 185, 220, 255);
        SDL_RenderDebugText(parent_rend, 275, 150, "Dialog Status");

        for (int i = 0; i < MAX_DIALOGS; i++) {
            DialogWindow *d = &dialogs[i];
            if (d->alive) {
                /* Update focus state */
                if (d->win) {
                    SDL_WindowFlags flags = SDL_GetWindowFlags(d->win);
                    d->focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
                }
                
                SDL_SetRenderDrawColor(parent_rend, d->modal ? 255 : 100, d->modal ? 180 : 200, 60, 255);
                char buf[64];
                snprintf(buf, sizeof buf, "Dialog %d: %s (%s) %s", i,
                         d->modal ? "MODAL" : "NON-MODAL", "OPEN",
                         d->focused ? "[FOCUSED]" : "");
                SDL_RenderDebugText(parent_rend, 285, 170 + i * 16, buf);
            } else {
                SDL_SetRenderDrawColor(parent_rend, 120, 120, 120, 255);
                char buf[64];
                snprintf(buf, sizeof buf, "Dialog %d: closed", i);
                SDL_RenderDebugText(parent_rend, 285, 170 + i * 16, buf);
            }
        }

        /* instructions */
        SDL_SetRenderDrawColor(parent_rend, 130, 140, 165, 255);
        SDL_RenderDebugText(parent_rend, 275, 230, "Keyboard Shortcuts:");
        SDL_RenderDebugText(parent_rend, 285, 248, "O - Open both dialogs");
        SDL_RenderDebugText(parent_rend, 285, 264, "C - Close all dialogs");
        SDL_RenderDebugText(parent_rend, 285, 280, "ESC - Quit");

        /* explanation */
        SDL_SetRenderDrawColor(parent_rend, 170, 185, 220, 255);
        SDL_RenderDebugText(parent_rend, 275, 310, "How it works:");
        SDL_SetRenderDrawColor(parent_rend, 140, 150, 175, 255);
        SDL_RenderDebugText(parent_rend, 285, 330, "xdg-dialog-v1 hints the");
        SDL_RenderDebugText(parent_rend, 285, 346, "compositor that a window is");
        SDL_RenderDebugText(parent_rend, 285, 362, "a dialog relative to its");
        SDL_RenderDebugText(parent_rend, 285, 378, "parent. Modal dialogs may");
        SDL_RenderDebugText(parent_rend, 285, 394, "block parent interaction.");

        /* Draw overlay when modal dialog is active */
        if (modal_active) {
            /* Semi-transparent dark overlay */
            SDL_SetRenderDrawBlendMode(parent_rend, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(parent_rend, 0, 0, 0, 120);
            SDL_FRect overlay = { 0, 0, 750, 480 };
            SDL_RenderFillRect(parent_rend, &overlay);
            SDL_SetRenderDrawBlendMode(parent_rend, SDL_BLENDMODE_NONE);

            /* Blocked message */
            SDL_SetRenderDrawColor(parent_rend, 255, 180, 60, 255);
            SDL_RenderDebugText(parent_rend, 300, 230, "[ PARENT BLOCKED ]");
            SDL_SetRenderDrawColor(parent_rend, 220, 220, 220, 255);
            SDL_RenderDebugText(parent_rend, 280, 248, "Modal dialog is active");
            SDL_RenderDebugText(parent_rend, 260, 264, "Close the modal dialog first");
        }

        SDL_RenderPresent(parent_rend);

        /* ── draw dialogs ─────────────────────────────── */
        for (int i = 0; i < MAX_DIALOGS; i++)
            draw_dialog(&dialogs[i]);

        SDL_Delay(16);
    }

    /* cleanup */
    destroy_dialog(0);
    destroy_dialog(1);
    if (wm_dialog) xdg_wm_dialog_v1_destroy(wm_dialog);
    if (wm_base)   xdg_wm_base_destroy(wm_base);
    SDL_DestroyRenderer(parent_rend);
    SDL_DestroyWindow(parent_win);
    SDL_Quit();
    return 0;
}
