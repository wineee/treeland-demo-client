# test-cross-subsurface

A standalone Wayland client that tests the `treeland_cross_subsurface_unstable_v1` protocol for creating remote subsurfaces across independently-exported surfaces.

Two separate binaries are built:

- **`test-cross-subsurface`** (parent): creates an SDL toplevel window, exports its surface, and creates 5 local remote subsurfaces at different z-levels with auto-animation support.
- **`test-cross-subsurface-child`** (`--parent-token <TOKEN>`): pure Wayland client (no window). Creates a raw wl_surface, exports it, and attaches it as a remote subsurface to the parent. Controlled via stdin commands.

## What it tests

### Protocol coverage

| Protocol request / event | Triggered by |
|--------------------------|-------------|
| `treeland_subsurface_manager_v1.export_surface` | startup (both parent and child) |
| `treeland_exported_surface_v1.surface_token` | listener — logs token on startup |
| `treeland_exported_surface_v1.create_remote_subsurface` | parent creates 2 local subsurfaces; child creates 1 cross-process subsurface |
| `treeland_remote_subsurface_v1.set_position` | Arrow keys |
| `treeland_remote_subsurface_v1.place_above` | `T` key (top), `A` key (parent: above previous sibling) |
| `treeland_remote_subsurface_v1.place_below` | `B` key (parent: below next sibling) |
| `treeland_remote_subsurface_v1.destroy` | `D` key |

## Architecture

### Dual-process model

```
Process 1 (parent)                     Process 2 (child)
+-----------------------+              +-----------------------+
| SDL toplevel window   |              | (no window — pure      |
| (semi-transparent)    |              |  Wayland client)       |
| exports wl_surface    |              | raw wl_surface + buffer|
| → parent_token        |              | exports → child_token  |
|        |              |              |        |              |
| 2 local subsurfaces:  |              | create_remote_         |
|   sub0 (red, below    |              |   subsurface(parent_  |
|     parent)           |              |   token)              |
|   sub1 (green)        |              | stdin commands:        |
+-----------------------+              |  pos, top, color,     |
        ^                             |  destroy, recreate     |
        |          user copies token   +-----------------------+
        +--------------------------------------+
```

The two processes are independent — the user starts the parent, copies the printed `parent_token`, and passes it to the child via `--parent-token`.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Requires: `SDL3`, `wayland-client`, `wayland-protocols`, `wayland-scanner`, `TreelandProtocols` cmake package, `cmake >= 3.16`.

## Run

### 1. Start parent

```bash
SDL_VIDEODRIVER=wayland ./build/test-cross-subsurface/test-cross-subsurface
```

The parent prints its `parent_token` to stdout and shows a status panel. It also creates 2 local remote subsurfaces (sub0 = red, placed below parent; sub1 = green, in front).

### 2. Start child (in another terminal)

```bash
SDL_VIDEODRIVER=wayland ./build/test-cross-subsurface/test-cross-subsurface-child \
    --parent-token <TOKEN>
```

Replace `<TOKEN>` with the token printed by the parent. The child creates a green remote subsurface attached to the parent's exported surface.

The binaries **must** run on a Treeland compositor that advertises `treeland_subsurface_manager_v1`. If the global is missing, the program will log an error and exit.

## Key bindings

### Parent (`test-cross-subsurface`)

| Key | Action |
|-----|--------|
| `1`–`5` | Select subsurface 1–5 |
| `Space` | Toggle auto-animation (drift + color cycling + z-order shuffle) |
| Arrow keys | Move selected subsurface ±20px |
| `T` | `place_above("")` — bring to top |
| `A` | `place_above(previous sibling)` |
| `B` | `place_below(next sibling)` |
| `C` | Cycle color of selected subsurface |
| `D` | Destroy selected remote subsurface |
| `R` | Recreate selected remote subsurface |
| `Q` / `Esc` | Quit |

**Initial z-order** (bottom → top): sub0 (red) < sub2 (blue) < parent < sub4 (cyan) < sub3 (yellow) < sub1 (green).

**Auto-animation** (press `Space`): subsurfaces drift along sinusoidal paths at different speeds, colors cycle every 1.2s, and a random sub is brought to top every 3s.

### Child (`test-cross-subsurface-child`)

No window — controlled via stdin commands:

| Command | Action |
|---------|--------|
| `pos <x> <y>` | `set_position(x, y)` |
| `top` | `place_above("")` — bring to top |
| `color` | Cycle color |
| `destroy` | Destroy remote subsurface |
| `recreate` | Recreate remote subsurface |
| `help` | Show available commands |
| `quit` | Exit |

Ctrl+C also exits cleanly.

## Visual feedback

- Parent window: semi-transparent (opacity 0.3), with a status panel showing subsurface positions, colors, and states
- Child process: no window. The remote subsurface appears as a colored rectangle attached to the parent's exported surface. All operations are logged to stderr.
- All protocol operations are logged to stderr
