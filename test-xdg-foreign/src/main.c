#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client.h>
#include <xdg-foreign-unstable-v2-client-protocol.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 500

struct AppState {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_PropertiesID window_props;

    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_surface *wl_surface;

    struct zxdg_exporter_v2 *exporter;
    struct zxdg_importer_v2 *importer;
    struct zxdg_exported_v2 *exported;
    struct zxdg_imported_v2 *imported;

    const char *import_handle_arg;
    char *protocol_export_handle;
    bool import_parent_set;
    bool imported_destroyed;
    bool running;
    const char *argv0;
};

static void print_usage(const char *argv0)
{
    SDL_Log("Usage: %s [--import HANDLE] [--title TITLE]", argv0);
    SDL_Log("  No arguments: create an SDL Wayland toplevel and export it.");
    SDL_Log("  --import HANDLE: import HANDLE and set it as this window's parent.");
}

static void exported_handle(void *data, struct zxdg_exported_v2 *exported, const char *handle)
{
    struct AppState *app = (struct AppState *)data;
    (void)exported;

    SDL_free(app->protocol_export_handle);
    app->protocol_export_handle = SDL_strdup(handle);
    SDL_Log("xdg-foreign protocol export handle: %s", handle);
    SDL_Log("Launch another instance with:");
    SDL_Log("  %s --import %s", app->argv0 ? app->argv0 : "./build/treeland-demo-client", handle);
}

static const struct zxdg_exported_v2_listener exported_listener = {
    .handle = exported_handle,
};

static void imported_destroyed_handler(void *data, struct zxdg_imported_v2 *imported)
{
    struct AppState *app = (struct AppState *)data;
    (void)imported;

    app->imported_destroyed = true;
    app->import_parent_set = false;
    SDL_Log("Imported parent was destroyed or became invalid.");
}

static const struct zxdg_imported_v2_listener imported_listener = {
    .destroyed = imported_destroyed_handler,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    struct AppState *app = (struct AppState *)data;

    if (strcmp(interface, zxdg_exporter_v2_interface.name) == 0) {
        app->exporter = wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, SDL_min(version, 1u));
    } else if (strcmp(interface, zxdg_importer_v2_interface.name) == 0) {
        app->importer = wl_registry_bind(registry, name, &zxdg_importer_v2_interface, SDL_min(version, 1u));
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static bool parse_arguments(struct AppState *app, int argc, char **argv, const char **title)
{
    *title = "treeland demo client";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--import") == 0) {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--import requires a handle");
                return false;
            }
            app->import_handle_arg = argv[++i];
        } else if (strcmp(argv[i], "--title") == 0) {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--title requires a value");
                return false;
            }
            *title = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return false;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown argument: %s", argv[i]);
            print_usage(argv[0]);
            return false;
        }
    }

    return true;
}

static bool init_wayland(struct AppState *app)
{
    app->wl_display = SDL_GetPointerProperty(SDL_GetGlobalProperties(), SDL_PROP_GLOBAL_VIDEO_WAYLAND_WL_DISPLAY_POINTER, NULL);
    if (!app->wl_display) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL did not expose a wl_display.");
        return false;
    }

    app->wl_registry = wl_display_get_registry(app->wl_display);
    if (!app->wl_registry) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get wl_registry.");
        return false;
    }

    wl_registry_add_listener(app->wl_registry, &registry_listener, app);
    wl_display_roundtrip(app->wl_display);
    return true;
}

static bool init_window(struct AppState *app, const char *title)
{
    app->window = SDL_CreateWindow(title, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (!app->window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    app->window_props = SDL_GetWindowProperties(app->window);
    app->wl_surface = SDL_GetPointerProperty(app->window_props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (!app->wl_surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL did not expose a wl_surface for the window.");
        return false;
    }

    return true;
}

static void dump_sdl_export_handle(struct AppState *app)
{
    const char *sdl_handle = NULL;

    for (int i = 0; i < 50 && !sdl_handle; ++i) {
        SDL_PumpEvents();
        wl_display_roundtrip(app->wl_display);
        sdl_handle = SDL_GetStringProperty(app->window_props, SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_EXPORT_HANDLE_STRING, NULL);
    }

    if (sdl_handle) {
        SDL_Log("SDL window export handle property: %s", sdl_handle);
    } else {
        SDL_Log("SDL window export handle property is not available.");
    }
}

static void start_protocol_export(struct AppState *app)
{
    if (!app->exporter) {
        SDL_Log("Compositor does not advertise zxdg_exporter_v2.");
        return;
    }

    app->exported = zxdg_exporter_v2_export_toplevel(app->exporter, app->wl_surface);
    zxdg_exported_v2_add_listener(app->exported, &exported_listener, app);
    wl_display_roundtrip(app->wl_display);
}

static void import_parent(struct AppState *app)
{
    if (!app->import_handle_arg) {
        return;
    }

    if (!app->importer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compositor does not advertise zxdg_importer_v2.");
        return;
    }

    app->imported = zxdg_importer_v2_import_toplevel(app->importer, app->import_handle_arg);
    zxdg_imported_v2_add_listener(app->imported, &imported_listener, app);
    zxdg_imported_v2_set_parent_of(app->imported, app->wl_surface);
    app->import_parent_set = true;
    wl_surface_commit(app->wl_surface);
    wl_display_roundtrip(app->wl_display);
    SDL_Log("Imported parent handle applied to this SDL window.");
}

static void draw_frame(struct AppState *app, Uint64 ticks)
{
    const float t = (float)(ticks % 5000) / 5000.0f;
    const Uint8 r = (Uint8)(30 + 120 * t);
    const Uint8 g = (Uint8)(80 + 100 * (1.0f - t));
    const Uint8 b = (Uint8)(160 + 70 * t);
    const SDL_FRect top = { 40.0f, 40.0f, 720.0f, 110.0f };
    const SDL_FRect bottom = { 40.0f, 190.0f, 720.0f, 230.0f };

    SDL_SetRenderDrawColor(app->renderer, r, g, b, 255);
    SDL_RenderClear(app->renderer);

    SDL_SetRenderDrawColor(app->renderer, 245, 245, 245, 255);
    SDL_RenderFillRect(app->renderer, &top);

    SDL_SetRenderDrawColor(app->renderer, 25, 25, 25, 255);
    SDL_RenderRect(app->renderer, &top);
    SDL_RenderRect(app->renderer, &bottom);

    if (app->import_handle_arg && app->import_parent_set && !app->imported_destroyed) {
        SDL_SetRenderDrawColor(app->renderer, 60, 170, 90, 255);
    } else if (app->import_handle_arg) {
        SDL_SetRenderDrawColor(app->renderer, 190, 80, 60, 255);
    } else {
        SDL_SetRenderDrawColor(app->renderer, 70, 120, 220, 255);
    }
    SDL_RenderFillRect(app->renderer, &bottom);
    SDL_RenderPresent(app->renderer);
}

static void cleanup(struct AppState *app)
{
    if (app->imported) {
        zxdg_imported_v2_destroy(app->imported);
    }
    if (app->exported) {
        zxdg_exported_v2_destroy(app->exported);
    }
    if (app->importer) {
        zxdg_importer_v2_destroy(app->importer);
    }
    if (app->exporter) {
        zxdg_exporter_v2_destroy(app->exporter);
    }
    if (app->wl_registry) {
        wl_registry_destroy(app->wl_registry);
    }
    SDL_free(app->protocol_export_handle);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    SDL_Quit();
}

int main(int argc, char **argv)
{
    struct AppState app;
    const char *title = NULL;

    SDL_zero(app);
    app.running = true;
    app.argv0 = (argc > 0) ? argv[0] : "./build/treeland-demo-client";

    if (!parse_arguments(&app, argc, argv, &title)) {
        return 1;
    }

    SDL_SetAppMetadata("treeland-demo-client", "1.0", "org.treeland.demo.client");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "This demo must run on SDL's wayland backend, current backend is '%s'.", SDL_GetCurrentVideoDriver());
        SDL_Quit();
        return 1;
    }

    if (!init_wayland(&app) || !init_window(&app, title)) {
        cleanup(&app);
        return 1;
    }

    dump_sdl_export_handle(&app);
    start_protocol_export(&app);
    import_parent(&app);

    while (app.running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                app.running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                app.running = false;
            }
        }

        draw_frame(&app, SDL_GetTicks());
        SDL_Delay(16);
    }

    cleanup(&app);
    return 0;
}
