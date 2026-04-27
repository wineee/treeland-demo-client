# test-wine-window

A standalone Wayland client that tests the Treeland Wine window protocols:
- `treeland_wine_window_management_unstable_v1` (position & z-order control)
- `treeland_wine_window_state_unstable_v1` (minimize, activate, attention)

## What it tests

### Management Protocol

| Protocol request / event | Triggered by |
|--------------------------|--------------|
| `get_window_control`     | startup (one per SDL window) |
| `set_position`           | `Ctrl+Arrow` keys, Move buttons |
| `set_z_order HWND_TOP`   | `Up` arrow, TOP button |
| `set_z_order HWND_BOTTOM`| `B` key, BOTTOM button |
| `set_z_order HWND_TOPMOST` | `T` key, TOPMOST button |
| `set_z_order HWND_NOTOPMOST` | `N` key, NOTOPMOST button |
| `set_z_order HWND_INSERT_AFTER` | `Left`/`Right` arrows, INSERT buttons |
| `window_id` event        | logged on creation |
| `configure_position` event | logged; reflected as window state |
| `configure_stacking` event | logged; yellow strip shown when topmost |

### State Protocol

| Protocol request / event | Triggered by |
|--------------------------|--------------|
| `get_window_state`       | startup (one per SDL window) |
| `unminimize`             | `U` key, Unminimize button |
| `activate`               | `A` key, Activate button, Auto-Act button |
| `set_attention`          | `F` key, Attention button |
| `clear_attention`        | Clear Attn button |
| `state_changed` event    | logged; displays [MINIMIZED] and [ATTENTION] flags |
| `activate_denied` event  | logged when activation is denied |

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Requires: `SDL3`, `wayland-client`, `wayland-scanner`, `cmake ≥ 3.16`.

## Run

```bash
# Single window (default)
SDL_VIDEODRIVER=wayland ./build/test-wine-window

# Multiple windows
SDL_VIDEODRIVER=wayland ./build/test-wine-window --count 2 --pos 200,150 --gap 40
```

The binary **must** run on a Treeland compositor that advertises both protocols.
If either global is missing, the program will log a warning but continue with
reduced functionality.

## Key bindings

| Key | Action |
|-----|--------|
| `Q` / `Escape` | quit |
| **State Control** | |
| `M` | minimize window (via SDL) |
| `U` | unminimize window |
| `A` | activate window (request focus) |
| `F` | set attention (flash taskbar) |
| **Z-Order Control** | |
| `T` | `set_z_order HWND_TOPMOST` (enter topmost tier) |
| `N` | `set_z_order HWND_NOTOPMOST` (leave topmost tier) |
| `B` | `set_z_order HWND_BOTTOM` (absolute bottom) |
| `↑` (Up) | `set_z_order HWND_TOP` (raise in current tier) |
| `←` (Left) | `set_z_order HWND_INSERT_AFTER` below **prev** sibling |
| `→` (Right) | `set_z_order HWND_INSERT_AFTER` below **next** sibling |
| **Position Control** | |
| `Ctrl+↑↓←→` | `set_position` ±20 px |
| **Other** | |
| `R` | reset tier for all windows (HWND_NOTOPMOST) |
| `?` / `H` | print key-binding summary to console |

## UI Buttons

The window displays interactive buttons for all protocol operations:
- **Row 1**: Z-order (TOP, BOTTOM, TOPMOST, NOTOPMOST)
- **Row 2**: INSERT_AFTER (prev/next sibling)
- **Row 3**: Position movement (X±20, Y±20)
- **Row 4**: State control (Unminimize, Activate, Attention, Clear Attn)
- **Row 5**: Reset All, **Auto-Act 5s** (auto-activate 5 seconds after minimize)

## Auto-Activate Feature

The **Auto-Act 5s** button demonstrates the state protocol's restore capability:
1. Minimize the window (press `M` or use compositor controls)
2. Click **Auto-Act 5s** button
3. After 5 seconds, the window will automatically call `activate` with
   `ACTIVATE_REASON_RESTORE`
4. The compositor should restore and focus the window

This simulates Wine's behavior when restoring minimized applications.

## Visual feedback

- Each window has a distinct colour
- **Yellow top strip**: compositor reports `topmost=1`
- **[MINIMIZED]** tag: window is minimized
- **[ATTENTION]** tag: attention hint is active
- Focused window: full brightness; unfocused: dimmed
- All state changes are logged to stdout with timestamps

## State Monitoring

All `state_changed` events are logged to stdout:
```
[win0] state_changed: minimized=1 attention=0
[win0] Window minimized at t=12345 ms
[win0] state_changed: minimized=0 attention=0
[win0] Window restored
```

This allows verification that the compositor correctly reports window state.
