# XDG Dialog v1 Test Client

Tests the `xdg-dialog-v1` Wayland protocol extension with two dialogs: one modal, one non-modal.

## Features

- **Parent Window**: Main window with control buttons and status display
- **Modal Dialog**: Blocks parent window interaction (compositor-dependent)
- **Non-Modal Dialog**: Allows parent window interaction
- **Protocol Testing**: Tests `xdg_wm_dialog_v1` and `xdg_dialog_v1` interfaces

## Building

```bash
cd /home/deepin/ui/treeland-demo-client
cmake -S test-xdg-dialog -B build-xdg-dialog
cmake --build build-xdg-dialog
```

## Running

```bash
# Using the run script (recommended)
cd build-xdg-dialog
./run.sh

# Or manually
SDL_VIDEODRIVER=wayland ./build-xdg-dialog/test-xdg-dialog

# Or using cmake custom target
cmake --build build-xdg-dialog --target run-xdg-dialog
```

## Controls

### Mouse
- Click buttons to perform actions

### Keyboard
- **O**: Open both dialogs (modal + non-modal)
- **C**: Close all dialogs
- **ESC**: Quit

## How It Works

1. Click "Open Both Dialogs" or press 'O'
2. Two dialog windows appear:
   - **Dialog 0**: Modal (orange badge) - hints compositor to block parent
   - **Dialog 1**: Non-Modal (green badge) - parent remains interactive
3. The compositor decides how to handle modal behavior
4. Click "Close All Dialogs" or press 'C' to close both

## Protocol Overview

The `xdg-dialog-v1` protocol provides:

- `xdg_wm_dialog_v1`: Global interface to create dialog objects
- `xdg_dialog_v1`: Per-toplevel interface with `set_modal`/`unset_modal` requests

The compositor may use these hints to:
- Place dialogs near their parent
- Block parent interaction for modal dialogs
- Apply special visual treatment

## Dependencies

- SDL3
- wayland-client
- wayland-protocols
- wayland-scanner

## Text Rendering

Uses SDL3's built-in 8x8 debug font (`SDL_RenderDebugText`) - no external fonts needed.
