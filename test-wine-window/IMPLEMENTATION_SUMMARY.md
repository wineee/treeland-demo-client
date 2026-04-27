# treeland-wine-window-state Protocol Implementation Summary

## Overview

This document summarizes the implementation of `treeland-wine-window-state-unstable-v1` protocol support in the test-wine-window client.

## Implementation Status

### ✅ Fully Implemented Features

#### 1. Protocol Binding
- ✅ Registry listener binds `treeland_wine_window_state_manager_v1`
- ✅ Registry listener binds `wl_seat` (required for activate requests)
- ✅ Per-window `treeland_wine_window_state_v1` objects created via `get_window_state`
- ✅ Event listeners attached to state objects

#### 2. State Tracking
- ✅ `minimized` flag tracked and displayed
- ✅ `attention` flag tracked and displayed
- ✅ State changes logged to stdout with timestamps
- ✅ Visual indicators: `[MINIMIZED]` and `[ATTENTION]` tags in window

#### 3. Protocol Requests
- ✅ `unminimize` - Restore minimized window
  - Triggered by: `U` key, "Unminimize" button
- ✅ `activate` - Request window activation/focus
  - Triggered by: `A` key, "Activate" button, "Auto-Act 5s" button
  - Supports all three `activate_reason` values:
    - `USER_REQUEST` - manual activation
    - `PROGRAMMATIC` - not used in this demo
    - `RESTORE` - used by auto-activate feature
- ✅ `set_attention` - Request taskbar flash
  - Triggered by: `F` key, "Attention" button
  - Parameters: count=0 (indefinite), timeout_ms=500
- ✅ `clear_attention` - Cancel attention hint
  - Triggered by: "Clear Attn" button

#### 4. Protocol Events
- ✅ `state_changed` - Window state flags updated
  - Logs minimized and attention flags
  - Updates visual display
  - Tracks minimize timestamp for auto-activate
- ✅ `activate_denied` - Activation request denied
  - Logs denial with serial number

#### 5. Auto-Activate Feature
- ✅ "Auto-Act 5s" button schedules delayed activation
- ✅ Timer tracks 5 seconds after minimize
- ✅ Automatically calls `activate` with `RESTORE` reason
- ✅ Demonstrates compositor's restore behavior

### ❌ Not Implemented (Intentionally Omitted)

The following features are commented out in the protocol spec pending security review:

- ❌ `set_pointer_grab` - Pointer event capture
- ❌ `release_pointer_grab` - Release pointer capture
- ❌ `pointer_grab_ended` event - Grab termination notification
- ❌ `grab_end_reason` enum

These are not implemented because they are not yet finalized in the protocol.

## Code Structure

### Data Structures

```c
struct WineWindow {
    // State protocol objects
    struct treeland_wine_window_state_v1 *wine_state;
    
    // State flags from compositor
    bool minimized;
    bool attention;
    
    // Auto-activate timer
    uint64_t minimize_time_ms;
    bool auto_activate_pending;
};

struct AppState {
    struct treeland_wine_window_state_manager_v1 *wine_state_manager;
    struct wl_seat *wl_seat;
    uint32_t activate_serial;  // for tracking activate requests
};
```

### Event Handlers

```c
static void wine_state_state_changed(void *data,
    struct treeland_wine_window_state_v1 *state, uint32_t state_flags);

static void wine_state_activate_denied(void *data,
    struct treeland_wine_window_state_v1 *state, uint32_t serial);
```

### Request Functions

```c
static void do_unminimize(WineWindow *w);
static void do_activate(WineWindow *w, uint32_t reason);
static void do_set_attention(WineWindow *w, uint32_t count, uint32_t timeout_ms);
static void do_clear_attention(WineWindow *w);
```

## Testing Scenarios

### 1. Minimize/Restore Cycle
1. Press `M` to minimize window (via SDL)
2. Observe `[MINIMIZED]` tag appears
3. Observe stdout log: `state_changed: minimized=1`
4. Press `U` to unminimize
5. Observe `[MINIMIZED]` tag disappears
6. Observe stdout log: `state_changed: minimized=0`

### 2. Activation
1. Focus another window
2. Press `A` on unfocused window
3. Compositor should grant focus (if policy allows)
4. If denied, observe stdout log: `activate_denied serial=N`

### 3. Attention (Taskbar Flash)
1. Press `F` to set attention
2. Observe `[ATTENTION]` tag appears
3. Taskbar should flash (compositor-dependent)
4. Click "Clear Attn" button
5. Observe `[ATTENTION]` tag disappears

### 4. Auto-Activate After Minimize
1. Press `M` to minimize window
2. Click "Auto-Act 5s" button
3. Observe stdout log: `Auto-activate scheduled in 5 seconds`
4. Wait 5 seconds
5. Observe stdout log: `Auto-activate triggered after 5000 ms`
6. Window should restore and activate

## Stdout Logging Examples

```
[win0] Bound treeland_wine_window_state_manager_v1 (name=42, ver=1)
[win0] Bound wl_seat (name=3)
[win0] wine_state created
[win0] state_changed: minimized=0 attention=0
[win0] state_changed: minimized=1 attention=0
[win0] Window minimized at t=12345 ms
[win0] unminimize
[win0] state_changed: minimized=0 attention=0
[win0] Window restored
[win0] activate serial=1 reason=0
[win0] set_attention count=0 timeout_ms=500
[win0] state_changed: minimized=0 attention=1
[win0] clear_attention
[win0] state_changed: minimized=0 attention=0
[win0] Auto-activate scheduled in 5 seconds
[win0] Auto-activate triggered after 5001 ms
[win0] activate serial=2 reason=2
```

## UI Layout

```
┌─────────────────────────────────────────────┐
│ Info Box:                                   │
│   win0  window_id: 12345  [normal]          │
│   actual  x=100   y=100                     │
│   req     x=100   y=100                     │
├─────────────────────────────────────────────┤
│ Z-ORDER                                     │
│ [TOP] [BOTTOM] [TOPMOST] [NOTOPMOST]        │
│ [INSERT_AFTER prev] [INSERT_AFTER next]     │
│ [Move X-20] [Move X+20] [Move Y-20] [Y+20]  │
│ [Unminimize] [Activate] [Attention] [Clear] │
│ [Reset All]           [Auto-Act 5s]         │
└─────────────────────────────────────────────┘
```

## Compliance with Protocol Specification

### Request Semantics
- ✅ `unminimize` has no effect if window not minimized
- ✅ `activate` includes serial for correlation with `activate_denied`
- ✅ `activate` includes reason enum for compositor policy
- ✅ `activate` includes seat parameter
- ✅ `set_attention` with count=0 means indefinite flash
- ✅ `set_attention` replaces previous attention hint
- ✅ `clear_attention` is idempotent (no effect if no hint active)

### Event Handling
- ✅ `state_changed` received immediately after object creation
- ✅ `state_changed` bitfield correctly parsed
- ✅ `activate_denied` serial matches request serial

### Lifecycle
- ✅ State objects destroyed before state_manager
- ✅ State objects become inert if toplevel destroyed (handled by SDL)

## Known Limitations

1. **SDL Minimize**: Uses SDL's `SDL_MinimizeWindow()` instead of compositor-native minimize, because xdg-shell provides `set_minimized` but the compositor may not expose a way to trigger it programmatically.

2. **Seat Assumption**: Uses first `wl_seat` found in registry. Multi-seat systems may need seat selection logic.

3. **Serial Tracking**: Simple counter for activate serials. Production code might need more sophisticated tracking.

4. **Attention Parameters**: Hardcoded to count=0, timeout_ms=500. Could be made configurable.

## Conclusion

This implementation provides **complete coverage** of the stable portions of the `treeland-wine-window-state-unstable-v1` protocol. All requests, events, and state flags are implemented and testable through both keyboard shortcuts and UI buttons. The auto-activate feature demonstrates a real-world use case for the protocol.
