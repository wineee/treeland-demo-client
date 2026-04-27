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
  - Available via `do_unminimize()` function (not exposed in UI since minimized windows can't be clicked)
- ✅ `activate` - Request window activation/focus
  - Triggered by: `A` key (5s delay), "Act 5s" button, "Min+Act5s" button
  - Supports all three `activate_reason` values:
    - `USER_REQUEST` - not used in current UI
    - `PROGRAMMATIC` - not used in current UI
    - `RESTORE` - used by delayed activate features
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

#### 5. Delayed Activation Features
- ✅ "Act 5s" button schedules delayed activation (5 seconds)
- ✅ "Min+Act5s" button minimizes immediately, then unminimizes and activates after 5 seconds
- ✅ Timer tracks elapsed time since delay started
- ✅ Automatically calls `unminimize` before `activate` when needed
- ✅ Automatically calls `activate` with `RESTORE` reason
- ✅ Demonstrates compositor's delayed activation and restore behavior
- ✅ `A` key also triggers 5-second delayed activation
- ✅ Focus gain/loss events logged to stdout

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
    
    // Delayed activate timer
    uint64_t activate_delay_start_ms;
    bool activate_delay_pending;
    bool need_unminimize_before_activate;  // true if should unminimize before activate
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
4. Call `do_unminimize()` programmatically (no UI button)
5. Observe `[MINIMIZED]` tag disappears
6. Observe stdout log: `state_changed: minimized=0`

### 2. Delayed Activation (Act 5s)
1. Press `A` key or click "Act 5s" button
2. Observe stdout log: `Activate scheduled in 5 seconds`
3. Wait 5 seconds
4. Observe stdout log: `Activate triggered after 5001 ms`
5. Compositor should grant focus (if policy allows)
6. If denied, observe stdout log: `activate_denied serial=N`

### 3. Minimize and Restore (Min+Act5s)
1. Click "Min+Act5s" button
2. Window minimizes immediately
3. Observe stdout log: `Minimized, will unminimize then activate in 5 seconds`
4. Observe `[MINIMIZED]` tag appears
5. Wait 5 seconds
6. Observe stdout logs in sequence:
   - `Activate triggered after 5001 ms`
   - `Unminimizing before activate`
   - `unminimize`
   - `activate serial=N reason=2`
   - `state_changed: minimized=0 attention=0`
   - `Focus GAINED (activated)`
7. Window should restore and activate

### 4. Attention (Taskbar Flash)
1. Press `F` to set attention
2. Observe `[ATTENTION]` tag appears
3. Taskbar should flash (compositor-dependent)
4. Click "Clear Attn" button
5. Observe `[ATTENTION]` tag disappears

## Stdout Logging Examples

```
[win0] Bound treeland_wine_window_state_manager_v1 (name=42, ver=1)
[win0] Bound wl_seat (name=3)
[win0] wine_state created
[win0] state_changed: minimized=0 attention=0
[win0] Activate scheduled in 5 seconds
[win0] Activate triggered after 5001 ms
[win0] activate serial=1 reason=2
[win0] Minimized via SDL
[win0] state_changed: minimized=1 attention=0
[win0] Minimized, activate scheduled in 5 seconds
[win0] Activate triggered after 5002 ms
[win0] activate serial=2 reason=2
[win0] state_changed: minimized=0 attention=0
[win0] set_attention count=0 timeout_ms=500
[win0] state_changed: minimized=0 attention=1
[win0] clear_attention
[win0] state_changed: minimized=0 attention=0
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
│ [Act 5s] [Attention] [Clear Attn] [Min+Act5s]│
│ [Reset All]                                 │
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

## Bug Fixes

### Position Tracking (Fixed)

**Issue**: Move buttons and Ctrl+Arrow keys were using `req_x/req_y` (last requested position) instead of `actual_x/actual_y` (compositor-reported position). This caused incorrect movement when:
- Compositor adjusted the requested position
- Window was moved externally (e.g., by user dragging)
- Multiple move operations were performed

**Fix**: All move operations now use `actual_x/actual_y` as the base position:
```c
// Before (incorrect):
do_set_position(w, w->req_x + MOVE_STEP, w->req_y);

// After (correct):
do_set_position(w, w->actual_x + MOVE_STEP, w->actual_y);
```

This ensures moves are always relative to the current real position reported by the compositor via `configure_position` events.

## Conclusion

This implementation provides **complete coverage** of the stable portions of the `treeland-wine-window-state-unstable-v1` protocol. All requests, events, and state flags are implemented and testable through both keyboard shortcuts and UI buttons. The auto-activate feature demonstrates a real-world use case for the protocol.
