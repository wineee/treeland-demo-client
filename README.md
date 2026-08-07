# treeland-demo-client

A collection of Wayland client demos for testing Treeland compositor protocols.

## Available Demos

### 1. test-subsurface
Tests the `wl_subcompositor` protocol for creating subsurfaces with various behaviors:
- Sync/Desync modes
- Z-ordering (Above/Below)
- Static positioning
- Animation

### 2. test-wine-window
Tests the `treeland_wine_window_management_unstable_v1` protocol for Wine window management:
- Window z-ordering (TOPMOST, NOTOPMOST, BOTTOM, TOP)
- Window positioning
- Window state management

### 3. test-xdg-foreign
Tests the `xdg_foreign_unstable_v2` protocol for exporting/importing window handles.

### 4. test-xdg-dialog
Tests the `xdg_dialog_v1` protocol for creating dialog windows:
- Modal/Non-modal dialog behavior
- Parent-child window relationships
- Protocol interface testing

### 5. test-cross-subsurface
Tests the `treeland_cross_subsurface_unstable_v1` protocol for creating remote subsurfaces across independently-exported surfaces:
- Surface export via `treeland_subsurface_manager_v1`
- Remote subsurface creation and positioning
- Z-ordering between exported surfaces (place above/below)

## Building

### Build All Demos
```bash
cmake -S . -B build
cmake --build build
```

### Build Individual Demos
```bash
# Build specific demo
cmake -S test-xdg-dialog -B build-xdg-dialog
cmake --build build-xdg-dialog
```

## Running

### test-subsurface
```bash
./build/test-subsurface/test-subsurface
```

### test-wine-window
```bash
./build/test-wine-window/test-wine-window
```

### test-xdg-foreign
```bash
./build/test-xdg-foreign/test-xdg-foreign
```

### test-xdg-dialog
```bash
# Using run script (recommended)
cd build-xdg-dialog
./run.sh

# Or manually
SDL_VIDEODRIVER=wayland ./build-xdg-dialog/test-xdg-dialog
```

### test-cross-subsurface
```bash
# Parent: creates an SDL window with 2 local remote subsurfaces
SDL_VIDEODRIVER=wayland ./build/test-cross-subsurface/test-cross-subsurface

# Child (in another terminal): attaches a remote subsurface to the parent
SDL_VIDEODRIVER=wayland ./build/test-cross-subsurface/test-cross-subsurface-child \
    --parent-token <TOKEN>
```

## Requirements

- Wayland compositor (e.g., Treeland)
- SDL3
- wayland-client
- wayland-protocols
- wayland-scanner

## Protocol Support

Each demo tests specific Wayland protocols:

| Demo | Protocol | Description |
|------|----------|-------------|
| test-subsurface | wl_subcompositor | Subsurface management |
| test-wine-window | treeland_wine_window_management_v1 | Wine window management |
| test-xdg-foreign | xdg_foreign_unstable_v2 | Window handle export/import |
| test-xdg-dialog | xdg_dialog_v1 | Dialog window management |
| test-cross-subsurface | treeland_cross_subsurface_unstable_v1 | Cross-surface remote subsurface |

## Documentation

Each demo has its own README.md with detailed instructions:
- `test-subsurface/README.md`
- `test-wine-window/README.md`
- `test-xdg-foreign/README.md`
- `test-xdg-dialog/README.md`
- `test-cross-subsurface/README.md`

## License

These demos are provided for testing and development purposes.