# test-wine-window-management

A standalone Wayland client that tests the
`treeland_wine_window_management_unstable_v1` compositor protocol.

## What it tests

| Protocol request / event | Triggered by |
|--------------------------|--------------|
| `get_window_control`     | startup (one per SDL window) |
| `set_position`           | `Ctrl+Arrow` keys, `--pos` arg |
| `set_z_order HWND_TOP`   | `Up` arrow |
| `set_z_order HWND_BOTTOM`| `B` |
| `set_z_order HWND_TOPMOST` | `T` |
| `set_z_order HWND_NOTOPMOST` | `N` |
| `set_z_order HWND_INSERT_AFTER` | `Left` / `Right` arrows |
| `window_id` event        | logged on creation |
| `configure_position` event | logged; reflected as window state |
| `configure_stacking` event | logged; yellow strip shown when topmost |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Requires: `SDL3`, `wayland-client`, `wayland-scanner`, `cmake ≥ 3.16`.

## Run

```bash
# Two overlapping windows (default)
SDL_VIDEODRIVER=wayland ./build/test-wine-window-management

# Four windows, custom start position and gap
SDL_VIDEODRIVER=wayland ./build/test-wine-window-management \
    --count 4 --pos 200,150 --gap 40
```

The binary **must** run on a Treeland compositor that advertises
`treeland_wine_window_manager_v1`. If the global is missing the program
exits with an informative error.

## Key bindings

| Key | Action |
|-----|--------|
| `Q` / `Escape` | quit |
| `T` | `set_z_order HWND_TOPMOST` (enter topmost tier) |
| `N` | `set_z_order HWND_NOTOPMOST` (leave topmost tier) |
| `B` | `set_z_order HWND_BOTTOM` (absolute bottom) |
| `↑` (Up) | `set_z_order HWND_TOP` (raise in current tier) |
| `←` (Left) | `set_z_order HWND_INSERT_AFTER` below **prev** sibling |
| `→` (Right) | `set_z_order HWND_INSERT_AFTER` below **next** sibling |
| `Ctrl+↑↓←→` | `set_position` ±20 px |
| `R` | reset tier for all windows (HWND_NOTOPMOST) |
| `?` / `H` | print key-binding summary to console |

## Visual feedback

- Each window has a distinct colour.
- A **yellow top strip** appears when the compositor reports `configure_stacking topmost=1`.
- The focused window is rendered at full brightness; unfocused windows are dimmed.
- All `configure_position` and `configure_stacking` events are logged to stdout.
