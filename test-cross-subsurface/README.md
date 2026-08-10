# test-cross-subsurface

A standalone Wayland client that tests the `treeland_cross_subsurface_unstable_v1` protocol for creating remote subsurfaces across independently-exported surfaces.

Two separate binaries are built:

- **`test-cross-subsurface`** (parent): creates an SDL toplevel window, exports its surface, and creates 5 local remote subsurfaces at different z-levels with auto-animation support.
- **`test-cross-subsurface-child`** (`--parent-token <TOKEN>`): pure Wayland client (no window). Creates a raw wl_surface, exports it, and attaches it as a remote subsurface to the parent. One-shot: all options set via command-line arguments.

## What it tests

### Protocol coverage

| Protocol request / event | Triggered by |
|--------------------------|-------------|
| `treeland_subsurface_manager_v1.export_surface` | startup (both parent and child) |
| `treeland_exported_surface_v1.surface_token` | listener - logs token on startup |
| `treeland_exported_surface_v1.create_remote_subsurface` | parent creates 5 local subsurfaces; child creates 1 cross-process subsurface |
| `treeland_remote_subsurface_v1.set_position` | Arrow keys (parent), `--pos` (child) |
| `treeland_remote_subsurface_v1.place_above` | `T`/`A` keys (parent), `--above` (child) |
| `treeland_remote_subsurface_v1.place_below` | `B` key (parent), `--below` (child) |
| `treeland_remote_subsurface_v1.destroy` | `D` key (parent), `--no-attach` (child) |

## Architecture

### Dual-process model

```
Process 1 (parent)                     Process 2 (child)
+-----------------------+              +-----------------------+
| SDL toplevel window   |              | (no window - pure      |
| (semi-transparent)    |              |  Wayland client)       |
| exports wl_surface    |              | raw wl_surface + buffer|
| -> parent_token       |              | exports -> child_token |
|        |              |              |        |              |
| 5 local subsurfaces:  |              | create_remote_         |
|   sub0..sub4 with     |              |   subsurface(parent_  |
|   auto-animation      |              |   token)              |
+-----------------------+              | --pos --above --below |
        ^                             | --color --size       |
        |          user copies token   +-----------------------+
        +--------------------------------------+
```

The two processes are independent - the user starts the parent, copies the printed `parent_token`, and passes it to the child via `--parent-token`.

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

The parent prints its `parent_token` to stderr and creates 5 local remote subsurfaces with auto-animation.

### 2. Start child (in another terminal)

```bash
# Attach a 300x200 green subsurface at (100, 200), above parent
./build/test-cross-subsurface/test-cross-subsurface-child \
    --parent-token <TOKEN> --pos 100 200 --above

# Place below parent, custom size 150x100, blue
./build/test-cross-subsurface/test-cross-subsurface-child \
    --parent-token <TOKEN> --below --size 150 100 --color 2

# Export surface only, no remote subsurface
./build/test-cross-subsurface/test-cross-subsurface-child \
    --parent-token <TOKEN> --no-attach
```

The binaries **must** run on a Treeland compositor that advertises `treeland_subsurface_manager_v1`. If the global is missing, the program will log an error and exit.

## Key bindings

### Parent (`test-cross-subsurface`)

| Key | Action |
|-----|--------|
| `1`-`5` | Select subsurface 1-5 |
| `Space` | Toggle auto-animation (drift + color cycling + z-order ops) |
| Arrow keys | Move selected subsurface +/-20px |
| `T` | `place_above(parent_token)` - above parent |
| `A` | `place_above(previous sibling)` |
| `B` | `place_below(next sibling)` |
| `C` | Cycle color of selected subsurface |
| `S` | Resize + recolor (cycles 6 sizes) |
| `D` | Destroy selected remote subsurface |
| `R` | Recreate selected remote subsurface |
| `Q` / `Esc` | Quit |

**Initial z-order** (bottom to top): sub0 (red) < sub2 (blue) < parent < sub4 (cyan) < sub3 (yellow) < sub1 (green).

**Auto-animation** (press `Space`): subsurfaces drift along sinusoidal paths, colors cycle every 1.2s, z-order operations every 2s (7-op cycle: above-parent, below-parent, above-sibling, below-sibling, destroy+recreate, rotate-all).

### Child (`test-cross-subsurface-child`)

One-shot command-line tool - creates a remote subsurface, applies all requested operations, commits, and exits. No window, no interactive loop.

| Option | Description | Default |
|--------|-------------|----------|
| `--pos <x> <y>` | set_position | 50 50 |
| `--above [token]` | place_above token | parent |
| `--below [token]` | place_below token | (none) |
| `--color <0-6>` | color index | 0 (green) |
| `--size <w> <h>` | buffer size | 300x200 |
| `--no-attach` | export only, no remote subsurface | (off) |
| `--help` | show help | |

## Visual feedback

- Parent window: semi-transparent (opacity 0.3), with a status panel showing subsurface positions, sizes, colors, and states.
- Child process: no window. The remote subsurface appears as a colored rectangle attached to the parent's exported surface. All operations are logged to stderr.
- All protocol operations are logged to stderr.
