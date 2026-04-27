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
| `A` | activate window after 5s delay |
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
- **Row 4**: State control (Act 5s, Attention, Clear Attn, Min+Act5s)
- **Row 5**: Reset All

### Button Descriptions

- **Act 5s**: Schedule activate request after 5 seconds (useful for testing delayed activation)
- **Attention**: Request taskbar flash (indefinite, 500ms interval)
- **Clear Attn**: Cancel attention hint
- **Min+Act5s**: Minimize window immediately, then activate after 5 seconds (demonstrates restore flow)
- **Reset All**: Reset all windows to NOTOPMOST tier

## Delayed Activation Features

### Act 5s Button
Schedules an `activate` request 5 seconds after clicking. This demonstrates:
- Delayed activation without minimize
- Compositor's handling of programmatic activation requests
- Useful for testing focus-stealing prevention policies

### Min+Act5s Button
This button demonstrates the complete minimize-restore cycle:
1. Immediately minimizes the window (via SDL)
2. Schedules an `activate` request for 5 seconds later
3. After 5 seconds:
   - First calls `unminimize` to restore the window
   - Then calls `activate` with `ACTIVATE_REASON_RESTORE`
4. Compositor should restore and focus the window

This simulates Wine's behavior when restoring minimized applications and tests the compositor's restore activation policy.

## Visual feedback

- Each window has a distinct colour
- **Yellow top strip**: compositor reports `topmost=1`
- **[MINIMIZED]** tag: window is minimized
- **[ATTENTION]** tag: attention hint is active
- Focused window: full brightness; unfocused: dimmed
- All state changes and focus changes are logged to stdout with timestamps

## State Monitoring

All `state_changed` events are logged to stdout:
```
[win0] state_changed: minimized=1 attention=0
[win0] state_changed: minimized=0 attention=0
[win0] Activate scheduled in 5 seconds
[win0] Activate triggered after 5001 ms
[win0] activate serial=1 reason=2
```

### Testing Scenarios

**Delayed Activation (Act 5s button or A key):**
1. Click "Act 5s" button or press `A`
2. Observe log: `Activate scheduled in 5 seconds`
3. Wait 5 seconds
4. Observe log: `Activate triggered after 5001 ms`
5. Window should receive focus (if compositor policy allows)

**Minimize and Restore (Min+Act5s button):**
1. Click "Min+Act5s" button
2. Window minimizes immediately
3. Observe log: `Minimized, will unminimize then activate in 5 seconds`
4. Observe `[MINIMIZED]` tag appears
5. Wait 5 seconds
6. Observe logs:
   - `Activate triggered after 5001 ms`
   - `Unminimizing before activate`
   - `unminimize`
   - `activate serial=N reason=2`
7. Window should restore and receive focus
8. Observe log: `Focus GAINED (activated)`

**Attention (Flash):**
1. Press `F` or click "Attention" button
2. Observe `[ATTENTION]` tag appears
3. Taskbar should flash (compositor-dependent)
4. Click "Clear Attn" to stop

This allows verification that the compositor correctly reports window state and handles activation requests.
