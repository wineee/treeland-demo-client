#include <SDL3/SDL.h>
#include <wayland-client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <stdbool.h>

static struct wl_compositor *compositor = NULL;
static struct wl_subcompositor *subcompositor = NULL;
static struct wl_shm *shm = NULL;

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                          uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

static int create_anonymous_file(off_t size) {
    static const char template[] = "/wayland-shm-XXXXXX";
    const char *path = getenv("XDG_RUNTIME_DIR");
    if (!path) return -1;
    char *name = malloc(strlen(path) + sizeof(template));
    strcpy(name, path);
    strcat(name, template);
    int fd = mkstemp(name);
    if (fd >= 0) {
        unlink(name);
        if (ftruncate(fd, size) < 0) {
            close(fd);
            fd = -1;
        }
    }
    free(name);
    return fd;
}

static struct wl_buffer *create_shm_buffer(int width, int height, uint32_t color) {
    int stride = width * 4;
    int size = stride * height;
    int fd = create_anonymous_file(size);
    if (fd < 0) return NULL;
    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    for (int i = 0; i < width * height; ++i) {
        data[i] = color;
    }
    munmap(data, size);
    return buffer;
}

struct subsurface_test {
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    struct wl_buffer *buffer;
    int x, y;
    bool is_desync;
    bool is_z_toggle;
    bool is_static;
};

#define MAX_SUBS 50
static struct subsurface_test subsurfaces[MAX_SUBS];
static int sub_count = 0;
static struct wl_surface *main_surface = NULL;

static void add_subsurface(int type) {
    if (sub_count >= MAX_SUBS) return;
    
    struct subsurface_test *sub = &subsurfaces[sub_count];
    sub->surface = wl_compositor_create_surface(compositor);
    sub->subsurface = wl_subcompositor_get_subsurface(subcompositor, sub->surface, main_surface);
    sub->is_desync = false;
    sub->is_z_toggle = false;
    sub->is_static = false;

    // Generate random color
    uint32_t r = 50 + rand() % 200;
    uint32_t g = 50 + rand() % 200;
    uint32_t b = 50 + rand() % 200;
    uint32_t color = (255 << 24) | (r << 16) | (g << 8) | b;
    
    sub->buffer = create_shm_buffer(80, 80, color);
    
    if (type == 6) { // Static Out-of-Bounds
        sub->is_static = true;
        int edge = rand() % 4;
        if (edge == 0) { sub->x = -40; sub->y = rand() % 600; } // Left
        else if (edge == 1) { sub->x = 760; sub->y = rand() % 600; } // Right
        else if (edge == 2) { sub->x = rand() % 800; sub->y = -40; } // Top
        else { sub->x = rand() % 800; sub->y = 560; } // Bottom
    } else {
        sub->x = 50 + (rand() % 600);
        sub->y = 100 + (rand() % 400);
    }

    // Apply type
    if (type == 1) { // Desync
        wl_subsurface_set_desync(sub->subsurface);
        sub->is_desync = true;
    } else if (type == 2) { // Above
        wl_subsurface_place_above(sub->subsurface, main_surface);
    } else if (type == 3) { // Below
        wl_subsurface_place_below(sub->subsurface, main_surface);
    } else if (type == 5) { // Z-Toggle
        sub->is_z_toggle = true;
    }

    wl_subsurface_set_position(sub->subsurface, sub->x, sub->y);
    wl_surface_attach(sub->surface, sub->buffer, 0, 0);
    wl_surface_damage(sub->surface, 0, 0, 80, 80);
    wl_surface_commit(sub->surface);
    
    sub_count++;
}

static void remove_subsurface() {
    if (sub_count <= 0) return;
    sub_count--;
    struct subsurface_test *sub = &subsurfaces[sub_count];
    wl_buffer_destroy(sub->buffer);
    wl_subsurface_destroy(sub->subsurface);
    wl_surface_destroy(sub->surface);
}

typedef struct {
    SDL_FRect rect;
    const char *label;
    SDL_Color color;
    int action;
} Button;

int main(int argc, char **argv) {
    srand(time(NULL));

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Dynamic Subsurface Demo", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT);
    if (!window) return 1;
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    struct wl_display *display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    main_surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);

    if (!display || !main_surface) {
        fprintf(stderr, "Wayland is required!\n");
        SDL_Quit();
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!subcompositor || !compositor || !shm) return 1;

    Button buttons[] = {
        { { 10, 10, 100, 40 }, "Sync", {200, 100, 100, 255}, 0 },
        { { 120, 10, 100, 40 }, "Desync", {100, 200, 100, 255}, 1 },
        { { 230, 10, 100, 40 }, "Above", {100, 100, 200, 255}, 2 },
        { { 340, 10, 100, 40 }, "Below", {200, 200, 100, 255}, 3 },
        { { 450, 10, 100, 40 }, "Z-Toggle", {100, 200, 200, 255}, 5 },
        { { 560, 10, 100, 40 }, "Remove", {200, 100, 200, 255}, 4 },
        { { 10, 60, 100, 40 }, "Static Out", {150, 150, 150, 255}, 6 }
    };
    int num_buttons = sizeof(buttons) / sizeof(buttons[0]);

    bool running = true;
    SDL_Event event;
    int tick = 0;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float x = event.button.x;
                float y = event.button.y;
                for (int i = 0; i < num_buttons; i++) {
                    if (x >= buttons[i].rect.x && x <= (buttons[i].rect.x + buttons[i].rect.w) &&
                        y >= buttons[i].rect.y && y <= (buttons[i].rect.y + buttons[i].rect.h)) {
                        if (buttons[i].action == 4) remove_subsurface();
                        else add_subsurface(buttons[i].action);
                    }
                }
            }
        }

        tick++;

        // Animate all subsurfaces to show they are alive
        for (int i = 0; i < sub_count; i++) {
            struct subsurface_test *sub = &subsurfaces[i];
            
            int y_anim = sub->y;
            if (!sub->is_static) {
                y_anim += (SDL_sin((tick + i * 10) * 0.05) * 20);
            }
            
            wl_subsurface_set_position(sub->subsurface, sub->x, y_anim);
            
            if (sub->is_z_toggle && (tick % 60 == 0)) { // Toggle Z-order roughly every 1 second (at 60fps)
                if ((tick / 60) % 2 == 0) {
                    wl_subsurface_place_above(sub->subsurface, main_surface);
                } else {
                    wl_subsurface_place_below(sub->subsurface, main_surface);
                }
            }

            // If it's desync, we must commit its surface to apply position immediately
            // If it's sync, committing it makes it pending until main surface commits
            wl_surface_commit(sub->surface);
        }

        // Draw main UI
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 180); // semi-transparent background to see 'below'
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        for (int i = 0; i < num_buttons; i++) {
            SDL_SetRenderDrawColor(renderer, buttons[i].color.r, buttons[i].color.g, buttons[i].color.b, buttons[i].color.a);
            SDL_RenderFillRect(renderer, &buttons[i].rect);
            // Draw a border
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderRect(renderer, &buttons[i].rect);
        }

        // Commit main surface (applies sync subsurfaces)
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    // Cleanup
    while (sub_count > 0) remove_subsurface();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
