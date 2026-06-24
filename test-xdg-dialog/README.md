# XDG Dialog v1 Test Client

Tests the `xdg-dialog-v1` Wayland protocol extension with two dialogs: one modal, one non-modal.

## Features

- **Parent Window**: Main window with control buttons and status display
- **Modal Dialog**: Blocks parent window interaction (client-side)
- **Non-Modal Dialog**: Allows parent window interaction
- **Focus Detection**: Each dialog shows its focus/active state
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
   - **Dialog 0**: Modal (orange badge) - client blocks parent input
   - **Dialog 1**: Non-Modal (green badge) - parent remains interactive
3. When modal dialog is active:
   - Parent window shows a dark overlay with "[ PARENT BLOCKED ]" message
   - Parent input events are ignored (except ESC to quit)
   - Non-modal dialog remains fully interactive
4. Click "Close All Dialogs" or press 'C' to close both

## Protocol Overview

The `xdg-dialog-v1` protocol provides:

- `xdg_wm_dialog_v1`: Global interface to create dialog objects
- `xdg_dialog_v1`: Per-toplevel interface with `set_modal`/`unset_modal` requests

Per the protocol specification:
- `set_modal` is a **hint** to the compositor
- **Clients must implement the logic to filter events in the parent toplevel on their own**
- Compositors may choose any policy for event delivery

This demo implements client-side modal blocking:
- Parent input events are filtered when modal dialog is active
- Semi-transparent overlay indicates blocked state
- Non-modal dialogs do not block the parent

## Dependencies

- SDL3
- wayland-client
- wayland-protocols
- wayland-scanner

## Text Rendering

Uses SDL3's built-in 8x8 debug font (`SDL_RenderDebugText`) - no external fonts needed.
