# test-pointer-constraints

Tests the `wp_pointer_constraints` and `wp_relative_pointer` protocols for:
- **Lock pointer** (oneshot & persistent) — disables cursor movement, receives relative motion events
- **Confine pointer** (oneshot & persistent) — restricts cursor to a surface region
- **Relative pointer motion** — receives accelerated and unaccelerated motion deltas
- **Cursor position hint** — sets hint for pointer warp on unlock
- **Confine region** — defines a sub-region for pointer confinement

## Building

```bash
cmake -S test-pointer-constraints -B build-pc
cmake --build build-pc
```

or build all demos from the repo root:

```bash
cmake -S . -B build
cmake --build build
```

## Running

```bash
SDL_VIDEODRIVER=wayland ./build/test-pointer-constraints/test-pointer-constraints
```

## Controls

### Keyboard

| Key | Action |
|-----|--------|
| `1` | Lock pointer (oneshot) |
| `2` | Lock pointer (persistent) |
| `3` | Confine pointer (oneshot) |
| `4` | Confine pointer (persistent) |
| `U` | Unconstrain (release lock/confine) |
| `H` | Set cursor position hint (lock mode only) |
| `R` | Toggle confine region overlay |
| `C` | Clear motion data display |
| `Space` | Cycle: lock → confine → none |
| `Q` / `Esc` | Quit |

### Buttons

Clickable buttons in the control panel perform the same actions as keyboard shortcuts.

## Display

The canvas shows:
- HUD with constraint state, lifetime, cursor position
- Real-time relative motion values (dx/dy and unaccelerated deltas)
- Mini bar charts for motion magnitude
- Visual indicator when locked (crosshair) or confined (region highlight)

## Protocol Details

- `zwp_pointer_constraints_v1` — Global interface for locking/confining pointers
- `zwp_locked_pointer_v1` — Locked pointer state (receives locked/unlocked events)
- `zwp_confined_pointer_v1` — Confined pointer state (receives confined/unconfined events)
- `zwp_relative_pointer_manager_v1` — Global interface for relative pointer objects
- `zwp_relative_pointer_v1` — Emits relative_motion events with accelerated and unaccelerated deltas

When the pointer is **locked**, `wl_pointer.motion` events stop being sent, but
`zwp_relative_pointer_v1.relative_motion` events continue. This is the typical
pattern used by games and 3D applications for mouse capture.

When the pointer is **confined**, `wl_pointer.motion` events are still sent, but
the cursor is constrained to the specified region.