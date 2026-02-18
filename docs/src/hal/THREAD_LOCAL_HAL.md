# Thread-Local HAL Migration Guide

## Overview

This document describes the planned migration from direct shared memory HAL pin access to a thread-local storage model with explicit synchronization.

### What's Changing

**Current Architecture:**
- Components directly access HAL pin values in shared memory via pointers
- All reads and writes happen immediately to shared memory
- RT function signature: `void update(void *arg, long period)`

**New Architecture:**
- Pin values are stored in thread-local context during component execution
- Explicit sync operations transfer data between thread-local context and shared memory
- Handle-based pin access via `hal_ctx_pin_*_get/set()` functions
- RT function signature changed to: `void update(void *arg, hal_ctx_t *ctx)`
- Period accessed via `hal_ctx_period(ctx)` instead of parameter
- Better isolation and improved real-time determinism

### Data Flow

```
Shared Memory ↔ Thread-Local Context ↔ Component Code
    (HAL)           (sync calls)        (your code)

┌──────────────────────────────────────────────────────────────┐
│                     SHARED MEMORY (HAL)                       │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  pins, signals, parameters                              │ │
│  └─────────────────────────────────────────────────────────┘ │
│                            ↕                                  │
│            hal_ctx_sync_read() / sync_write()                 │
│                            ↕                                  │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  Thread-Local Context (hal_ctx_t)                       │ │
│  │  - working_buf: 1MB copy of HAL memory                  │ │
│  │  - dirty_bitmap: tracks local modifications             │ │
│  │  - Fast access during execution                         │ │
│  │  - No locks needed                                      │ │
│  └─────────────────────────────────────────────────────────┘ │
│                            ↕                                  │
│      Component reads/writes via hal_ctx_pin_*_get/set()       │
└──────────────────────────────────────────────────────────────┘
```

## Sync Responsibility Matrix

Different component types have different requirements for synchronization:

| Component Type | Loop Control | Sync Strategy | User Changes Required |
|----------------|--------------|---------------|----------------------|
| **RT (.comp, .c)** | RT executor | **Automatic ✅** | **Yes** - Function signature changes to `(void *arg, hal_ctx_t *ctx)`, use handle-based pin access |
| **Userspace .comp** | User's `user_mainloop()` | **Manual ⚠️** | Add `hal_ctx_sync_read()` / `hal_ctx_sync_write()` calls |
| **Python halmodule** | User's `while` loop | **Manual ⚠️** | Add `h.sync_read()` / `h.sync_write()` calls |
| **Go components** | User's loop | **Manual ⚠️** | Add sync calls in main loop |

## Migration Guide

### For RT Components

**Breaking Changes Required!**

RT components require changes to adapt to the new context-based API. While the RT executor still handles sync automatically, the function signature and pin access patterns must be updated.

#### Changes Required:

1. **Function signature change** - from `(void *arg, long period)` to `(void *arg, hal_ctx_t *ctx)`
2. **Period access** - use `hal_ctx_period(ctx)` instead of the period parameter
3. **Pin creation** - use handle-based API (`hal_pin_*_new_handle`)
4. **Pin access** - use `hal_ctx_pin_*_get/set()` instead of pointer dereference

**Before (old API):**
```c
// Component with old API
static hal_float_t *in, *out;
static hal_float_t gain;

int rtapi_app_main(void) {
    // Pin creation with pointers
    hal_pin_float_new("example.in", HAL_IN, &in, comp_id);
    hal_pin_float_new("example.out", HAL_OUT, &out, comp_id);
    hal_param_float_new("example.gain", HAL_RW, &gain, comp_id);
}

// Old function signature
static void update(void *arg, long period) {
    // Direct pointer access
    *out = *in * gain;
    
    // Period from parameter
    if (period > 1000000) {
        // do something
    }
}
```

**After (new API):**
```c
// Component with new context-based API
static hal_pin_handle_t in_h, out_h;
static hal_param_handle_t gain_h;

int rtapi_app_main(void) {
    // Handle-based pin creation
    in_h = hal_pin_float_new_handle("example.in", HAL_IN, comp_id);
    out_h = hal_pin_float_new_handle("example.out", HAL_OUT, comp_id);
    gain_h = hal_param_float_new_handle("example.gain", HAL_RW, comp_id);
}

// New function signature with hal_ctx_t
static void update(void *arg, hal_ctx_t *ctx) {
    // Context-aware pin access
    hal_float_t in = hal_ctx_pin_float_get(ctx, in_h);
    hal_float_t gain = hal_ctx_param_float_get(ctx, gain_h);
    
    hal_ctx_pin_float_set(ctx, out_h, in * gain);
    
    // Period from context
    long period = hal_ctx_period(ctx);
    if (period > 1000000) {
        // do something
    }
}
```

**Key Points:**
- The RT executor still calls `hal_ctx_sync_read()` before your function
- The RT executor still calls `hal_ctx_sync_write()` after your function
- You don't call sync manually, but you must use the new signature and handle-based access
- All pin/param reads go through `hal_ctx_pin_*_get()` or `hal_ctx_param_*_get()`
- All pin/param writes go through `hal_ctx_pin_*_set()` or `hal_ctx_param_*_set()`
- These functions access the thread-local working buffer, not shared memory directly

### For Userspace .comp Files

Userspace components control their own main loop via `user_mainloop()`. You must add explicit sync calls.

**Before (current code):**
```c
void user_mainloop(void) {
    while(1) {
        FOR_ALL_INSTS() {
            out = in;  // component logic
        }
        usleep(1000);
    }
}
```

**After (with sync calls):**
```c
void user_mainloop(void) {
    while(1) {
        hal_ctx_sync_read();   // NEW: sync inputs from shared memory
        
        FOR_ALL_INSTS() {
            out = in;  // existing component logic unchanged
        }
        
        hal_ctx_sync_write();  // NEW: sync outputs to shared memory
        usleep(1000);
    }
}
```

**Key Points:**
- Add `hal_ctx_sync_read()` at the start of each loop iteration
- Add `hal_ctx_sync_write()` at the end of each loop iteration
- Place sync calls **outside** the `FOR_ALL_INSTS()` block
- All component logic between sync calls operates on thread-local context data

### For Python Components

Python components using the `hal` module need explicit sync in their main loop.

**Current pattern (will need update):**
```python
import hal
import time

h = hal.component("mycomp")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

while True:
    h['out'] = h['in']  # direct access
    time.sleep(0.001)
```

**New pattern with explicit sync:**
```python
import hal
import time

h = hal.component("mycomp")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

while True:
    h.sync_read()       # NEW: sync inputs from shared memory
    
    h['out'] = h['in']  # your component logic
    
    h.sync_write()      # NEW: sync outputs to shared memory
    time.sleep(0.001)
```

**Alternative: Context manager (convenience wrapper):**
```python
while True:
    with h.synced():    # sync_read on enter, sync_write on exit
        h['out'] = h['in']
    time.sleep(0.001)
```

**Key Points:**
- Add `h.sync_read()` at the start of each loop iteration
- Add `h.sync_write()` at the end of each loop iteration
- The context manager `h.synced()` is a convenience wrapper that handles both
- All pin access between sync calls operates on thread-local data

### For Go Components

Go components follow the same pattern as Python:

```go
// Pseudocode - actual Go HAL bindings TBD
for {
    hal.SyncRead()     // Sync inputs
    
    // Your component logic
    output.Set(input.Get() * 2.0)
    
    hal.SyncWrite()    // Sync outputs
    time.Sleep(1 * time.Millisecond)
}
```

## API Reference

### C Functions

#### `hal_ctx_sync_read()`
```c
int hal_ctx_sync_read(hal_ctx_t *ctx);
```
Copies allocated regions from master HAL shared memory to the context's working buffer. Call this at the beginning of your processing cycle before reading any pin values.

**When to call:**
- Userspace components: Start of main loop iteration
- RT components: Called automatically by RT executor (you don't call this)

**Returns:** 0 on success, negative error code on failure

#### `hal_ctx_sync_write()`
```c
int hal_ctx_sync_write(hal_ctx_t *ctx);
```
Copies dirty bytes from the context's working buffer back to master HAL shared memory. Call this at the end of your processing cycle after setting all output pin values.

**When to call:**
- Userspace components: End of main loop iteration
- RT components: Called automatically by RT executor (you don't call this)

**Returns:** 0 on success, negative error code on failure

### Context-Aware Pin Access

These functions are used in RT components to read and write pins via the thread-local context:

#### Pin Read Functions
```c
hal_float_t hal_ctx_pin_float_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_s32_t hal_ctx_pin_s32_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_u32_t hal_ctx_pin_u32_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
```

Reads a pin value from the context's working buffer (thread-local copy).

**Parameters:**
- `ctx`: Context passed to RT function
- `pin`: Handle returned by `hal_pin_*_new_handle()`

**Returns:** Pin value from working buffer

#### Pin Write Functions
```c
void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_float_t val);
void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_bit_t val);
void hal_ctx_pin_s32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_s32_t val);
void hal_ctx_pin_u32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_u32_t val);
```

Writes a pin value to the context's working buffer and marks it dirty for sync.

**Parameters:**
- `ctx`: Context passed to RT function
- `pin`: Handle returned by `hal_pin_*_new_handle()`
- `val`: Value to write

### Context Accessors

#### `hal_ctx_period()`
```c
long hal_ctx_period(hal_ctx_t *ctx);
```
Gets the thread period in nanoseconds. Use this instead of the old `period` parameter.

**Parameters:**
- `ctx`: Context passed to RT function

**Returns:** Thread period in nanoseconds

#### `hal_ctx_iteration()`
```c
unsigned long hal_ctx_iteration(hal_ctx_t *ctx);
```
Gets the iteration count for this thread.

#### `hal_ctx_overruns()`
```c
unsigned long hal_ctx_overruns(hal_ctx_t *ctx);
```
Gets the overrun count for this thread.

### Python Methods

#### `h.sync_read()`
```python
h.sync_read()
```
Synchronizes input pins from shared memory to thread-local storage. Call at the start of your main loop iteration.

**Example:**
```python
while True:
    h.sync_read()
    value = h['input-pin']
    # ... process value ...
```

#### `h.sync_write()`
```python
h.sync_write()
```
Synchronizes output pins from thread-local storage to shared memory. Call at the end of your main loop iteration.

**Example:**
```python
while True:
    # ... calculate output ...
    h['output-pin'] = result
    h.sync_write()
```

#### `h.synced()` (Context Manager)
```python
with h.synced():
    # pin operations here
```
Convenience context manager that calls `sync_read()` on entry and `sync_write()` on exit.

**Example:**
```python
while True:
    with h.synced():
        h['out'] = h['in'] * 2.0
    time.sleep(0.001)
```

## halcompile Integration

The halcompile tool will be updated to automatically generate code using the new context-based API for RT components.

### Generated Code Changes

halcompile must generate:

1. **New function signature**: `void update(void *arg, hal_ctx_t *ctx)` instead of `void update(void *arg, long period)`
2. **Handle-based pin/param creation**: Use `hal_pin_*_new_handle()` and `hal_param_*_new_handle()`
3. **Context-aware pin access**: Generate `hal_ctx_pin_*_get/set()` calls instead of pointer dereferences
4. **Period access**: Replace `period` parameter with `hal_ctx_period(ctx)` calls

### Example: .comp File

**.comp Source (unchanged):**
```
component example;
pin in float input;
pin out float output;
param rw float gain = 1.0;
function _;
;;
FUNCTION(_) {
    output = input * gain;
}
```

**Generated Code (Before - old API):**
```c
static hal_float_t *input, *output;
static hal_float_t gain;

static void _(void *arg, long period) {
    *output = *input * gain;
}

int rtapi_app_main(void) {
    hal_pin_float_new("example.input", HAL_IN, &input, comp_id);
    hal_pin_float_new("example.output", HAL_OUT, &output, comp_id);
    hal_param_float_new("example.gain", HAL_RW, &gain, comp_id);
    hal_export_funct("example", _, NULL, 1, 0, comp_id);
}
```

**Generated Code (After - new API):**
```c
static hal_pin_handle_t input_h, output_h;
static hal_param_handle_t gain_h;

static void _(void *arg, hal_ctx_t *ctx) {
    hal_float_t input = hal_ctx_pin_float_get(ctx, input_h);
    hal_float_t gain = hal_ctx_param_float_get(ctx, gain_h);
    
    hal_ctx_pin_float_set(ctx, output_h, input * gain);
}

int rtapi_app_main(void) {
    input_h = hal_pin_float_new_handle("example.input", HAL_IN, comp_id);
    output_h = hal_pin_float_new_handle("example.output", HAL_OUT, comp_id);
    gain_h = hal_param_float_new_handle("example.gain", HAL_RW, comp_id);
    hal_export_funct("example", _, NULL, 1, 0, comp_id);
}
```

### Period Access in .comp

If your .comp file uses `period`:

**.comp Source:**
```
FUNCTION(_) {
    if (period > 1000000) {
        // High-frequency operation
    }
}
```

**Generated Code:**
```c
static void _(void *arg, hal_ctx_t *ctx) {
    long period = hal_ctx_period(ctx);
    if (period > 1000000) {
        // High-frequency operation
    }
}
```

### Migration Timeline

- **Phase 1-2**: halcompile continues generating old API
- **Phase 3**: halcompile updated to generate new API
- **Phase 4+**: All .comp files automatically use context-based API

**Note:** Existing .comp source files do **not** need to change. Only the generated C code changes. halcompile handles the translation automatically.

## Why Userspace Needs Manual Sync

This is an important question that many developers ask: *Why can't the framework handle sync automatically for userspace components?*

**The Answer:**

> Userspace components control their own main loop. The HAL framework
> cannot inject sync calls because it doesn't know when your loop
> iterations begin and end. Only you know the logical boundaries of
> your processing cycle.

**Consider this example:**

```python
# Complex userspace component with multiple phases
while running:
    # Phase 1: Read sensors
    temp = h['temperature']
    pressure = h['pressure']
    
    # Phase 2: Complex calculation (may take variable time)
    result = complex_calculation(temp, pressure)
    
    # Phase 3: Update multiple outputs
    h['status'] = result.status
    h['value'] = result.value
    h['error'] = result.error
    
    time.sleep(0.001)
```

**Where should sync happen?** Only you know:
- Some components need sync at the start and end of each loop
- Others might need sync between phases
- Some might batch multiple cycles before syncing
- The timing depends on your component's logic

**In contrast, RT components:**
- Have their execution controlled by the RT thread scheduler
- Call a single `update()` function per cycle
- The framework knows exactly when execution starts and ends
- Therefore, sync can be automatic

## Why Full Thread-Local (Not Hybrid)?

During the design phase, we considered a hybrid approach where numeric pins could optionally use direct shared memory access (backward compatible) while string pins would require thread-local storage.

**We chose full thread-local for all pin types because of fail-fast behavior:**

| Approach | Forgotten Sync Behavior | Bug Detection |
|----------|------------------------|---------------|
| **Hybrid (direct fallback)** | Works, but with race conditions | Silent bugs, hard to find |
| **Full Thread-Local** | Pins read as initial values (0, false, "") | **Obvious failure**, easy to detect |

**Example of fail-fast detection:**
```python
# Component that forgot sync_read():
while True:
    h['out'] = h['in'] * 2  # h['in'] is always 0!
    h.sync_write()
    time.sleep(0.001)
# User immediately notices: "Why is my output always 0?"
```

**Benefits of full thread-local:**
- All pin types behave consistently (no special cases for strings vs. numerics)
- Bugs are obvious - forgotten sync = zero/empty values
- Simpler mental model: "call sync, period"
- Future-proof for new pin types
- No legacy "direct mode" code paths to maintain

## Implementation Phases

The migration will happen in phases to minimize disruption:

### Phase 1: Master HAL Changes
- Add `allocation_bitmap` to `hal_data_t` for tracking allocated regions
- Update signal/param allocation to set bitmap bits
- Add `dirty_offset`, `dirty_mask[2]` to signal/param structs
- Precompute dirty access info on allocation
- **Status:** Planned

### Phase 2: Context Implementation
- Implement `hal_ctx_t` with working_buf + dirty_bitmap
- Implement `hal_ctx_create/destroy` for userspace components
- Implement `hal_ctx_sync_read/write` with dirty tracking
- Implement `hal_ctx_pin_*_get/set` with dirty marking
- Implement handle-based pin/param creation APIs
- **Status:** Planned

### Phase 3: Thread Integration & Function Signature Change
- Change `hal_funct_t` signature from `(void *arg, long period)` to `(void *arg, hal_ctx_t *ctx)` (**breaking change**)
- Add `ctx` to `hal_thread_t`
- Update thread creation to create context
- Update thread runner to call `hal_ctx_sync_read/write` and pass context to functions
- **Status:** Planned

### Phase 4: halcompile Update
- Generate new function signature: `void update(void *arg, hal_ctx_t *ctx)`
- Generate handle-based pin/param creation
- Generate context-aware pin access (`hal_ctx_pin_*_get/set`)
- Replace period parameter with `hal_ctx_period(ctx)`
- **Status:** Planned

### Phase 5: Component Migration
- Update all RT components to new API (or regenerate with updated halcompile)
- Update all userspace .comp files to call `hal_ctx_sync_read/write` in `user_mainloop()`
- Update all Python HAL components to call sync in main loops
- See "Files Requiring Updates" section below
- **Status:** Planned

## Files Requiring Updates (Phase 5 Inventory)

The following files need to be updated during Phase 5 (Component Migration) to use the new context-based API.

### Userspace .comp Files

These files have `option userspace yes` and implement `user_mainloop()`. They need `hal_ctx_sync_read()` at the start of their loop and `hal_ctx_sync_write()` at the end.

| File | Description |
|------|-------------|
| `src/hal/user_comps/thermistor.comp` | Thermistor temperature estimator |
| `src/hal/user_comps/pi500_vfd/pi500_vfd.comp` | Powtran PI500 VFD modbus driver |
| `src/hal/user_comps/wj200_vfd/wj200_vfd.comp` | Hitachi WJ200 VFD modbus driver |
| `docs/src/hal/rand.comp` | Example/documentation random component |
| `tests/halcompile/userspace-count-names/userspace_count_names.comp` | Test component |
| `tests/halcompile/extralib/extralib_test.comp` | Test component |
| `tests/halcompile/relative-header-user/relative_header.comp` | Test component |

### Python HAL Module

The core Python HAL module needs sync methods added:

| File | Changes Needed |
|------|----------------|
| `lib/python/hal.py` | Add `sync_read()`, `sync_write()`, and `synced()` context manager to `component` class |
| `src/hal/halmodule.cc` | Add C implementation of sync methods |

### Python Components

These Python files create HAL components and have main loops that need `h.sync_read()` / `h.sync_write()` calls:

| File | Description |
|------|-------------|
| `src/hal/user_comps/sim-torch.py` | Simulated torch for plasma cutting |
| `src/hal/user_comps/mqtt-publisher.py` | MQTT publisher component |
| `src/hal/user_comps/pmx485.py` | Powermax RS485 plasma driver |
| `src/hal/user_comps/vismach/pumagui.py` | PUMA robot visualization |
| `configs/sim/woodpecker/numstr.py` | Line number file writer |
| `configs/sim/axis/orphans/pysubs/userfuncs.py` | User-defined task functions |

### Python HAL Wrapper Libraries

These libraries wrap HAL functionality and may need updates to support sync:

| File | Description |
|------|-------------|
| `lib/python/hal_glib.py` | GTK/GLib HAL component wrapper with signal emission |
| `lib/python/qtvcp/core.py` | Qt VCP HAL integration |
| `lib/python/vcpparse.py` | pyVCP XML parser and widget creator |

### Notes

- This list was generated by searching for `user_mainloop`, `option userspace yes`, and `hal.component` in the codebase
- There may be additional files not captured by these searches
- Test and example files should also be updated to serve as correct examples
- Search for more files: [user_mainloop](https://github.com/search?q=repo%3Asittner%2Flinuxcnc+user_mainloop&type=code), [hal.component Python](https://github.com/search?q=repo%3Asittner%2Flinuxcnc+hal.component+language%3APython&type=code)

## Timeline and Migration Strategy

**Phase 1: Master HAL Changes**
- Implement allocation bitmap and dirty tracking infrastructure
- No changes to components

**Phase 2: Context Implementation**
- Implement context API with sync operations
- No changes to components yet

**Phase 3: Thread Integration**
- RT thread executor updated to use context-based API
- Function signature changes from `(void *arg, long period)` to `(void *arg, hal_ctx_t *ctx)`
- **Breaking change for RT components**

**Phase 4: halcompile Update**
- halcompile generates new API code automatically
- Existing .comp files regenerated with new API

**Phase 5: Component Migration**
- Update all RT components (or regenerate with halcompile)
- Update all userspace components with sync calls
- All components migrated to new API

**Best Practice:**
When writing new components, use the new context-based API from the start to be future-proof.

## Examples

### Minimal Userspace C Component

```c
#include "hal.h"

static hal_bit_t *input_pin;
static hal_bit_t *output_pin;

void user_mainloop(void) {
    while(1) {
        hal_ctx_sync_read();
        
        *output_pin = *input_pin;  // Simple passthrough
        
        hal_ctx_sync_write();
        usleep(1000);
    }
}
```

### Python Component with Multiple Pins

```python
import hal
import time

h = hal.component("multi-pin")
h.newpin("in1", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("in2", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out1", hal.HAL_FLOAT, hal.HAL_OUT)
h.newpin("out2", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

while True:
    h.sync_read()
    
    # Read all inputs
    a = h['in1']
    b = h['in2']
    
    # Calculate outputs
    h['out1'] = a + b
    h['out2'] = a * b
    
    h.sync_write()
    time.sleep(0.001)
```

### Python Component with Error Handling

```python
import hal
import time

h = hal.component("safe-comp")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

try:
    while True:
        with h.synced():
            value = h['in']
            if value < 0:
                value = 0  # Clamp negative values
            h['out'] = value
        time.sleep(0.001)
except KeyboardInterrupt:
    pass  # Clean shutdown
```

## Advanced Sync Patterns

### Read-Only Components

A powerful feature of manual sync is the ability to create **intentionally read-only components** by only calling `sync_read()`:

```python
# Read-only monitoring component - never writes to HAL
import hal
import time

h = hal.component("monitor")
h.newpin("axis-x-pos", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("axis-y-pos", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("spindle-speed", hal.HAL_FLOAT, hal.HAL_IN)
h.ready()

while True:
    h.sync_read()  # Only sync_read - component is guaranteed read-only
    
    # Log, display, or transmit data - but never modify HAL state
    log_position(h['axis-x-pos'], h['axis-y-pos'])
    update_display(h['spindle-speed'])
    
    # No sync_write() - this component cannot affect machine state
    time.sleep(0.1)
```

**Benefits:**
- **Explicit constraint**: The component is architecturally prevented from writing
- **Safety**: Even if code accidentally sets an output pin, it won't propagate to HAL
- **Documentation**: The missing `sync_write()` clearly signals intent

### Write-Only Components (Rare)

Similarly, a component that only generates outputs could use only `sync_write()`:

```python
# Signal generator - produces output without reading inputs
while True:
    h['signal'] = math.sin(time.time() * frequency)
    h.sync_write()  # Only sync_write - doesn't need external inputs
    time.sleep(0.001)
```

### Multiple Sync Points

For components with distinct processing phases:

```c
void user_mainloop(void) {
    while(1) {
        // Phase 1: Read sensors
        hal_ctx_sync_read();
        sensor_data = process_inputs();
        
        // Phase 2: Complex calculation (may take time)
        result = complex_calculation(sensor_data);
        
        // Phase 3: Write outputs
        set_outputs(result);
        hal_ctx_sync_write();
        
        // Optional: Mid-cycle sync for time-critical feedback
        // hal_ctx_sync_read();
        // hal_ctx_sync_write();
        
        usleep(1000);
    }
}
```

## Frequently Asked Questions

### Q: Do I need to change my RT component code?
**A:** Yes. RT components require changes to use the new function signature `(void *arg, hal_ctx_t *ctx)` and handle-based pin access via `hal_ctx_pin_*_get/set()`. However, the RT executor still handles sync automatically - you don't call sync functions yourself.

### Q: What happens if I forget sync calls in userspace components?
**A:** Your component will read initial/zero values for all input pins, and output pins will never update in HAL.

This "fail-fast" behavior is intentional - it makes forgotten sync calls **obvious and easy to detect** rather than creating subtle race conditions. If your component reads all zeros or your outputs don't change, check your sync calls first!

### Q: Can I create a read-only component?
**A:** Yes! By only calling `sync_read()` and omitting `sync_write()`, you create a component that is architecturally prevented from modifying HAL state. This is useful for monitoring, logging, or display components. See "Advanced Sync Patterns" section.

### Q: Can I call sync in the middle of my loop?
**A:** Yes, but typically you want sync at the boundaries (start/end). Multiple syncs per cycle add overhead.

### Q: Does sync affect performance?
**A:** In Phase 1, no overhead (no-ops). In Phase 4, minimal overhead - just memory copies. The thread-local model may actually improve performance by reducing cache contention.

### Q: What about HAL parameters?
**A:** Parameters follow the same thread-local model as pins and will be included in sync operations.

### Q: Do HAL streams need sync?
**A:** No. HAL streams have their own lock-free FIFO mechanism and are independent of the pin sync model.

## Additional Resources

For detailed technical architecture and implementation specifics of the thread-local HAL system, including memory management, dirty bitmap mechanism, and performance analysis, see the technical architecture document: [THREAD_LOCAL_HAL.md](../../../THREAD_LOCAL_HAL.md) (in the repository root, separate from this migration guide).

## Summary

- **RT Components:** Require function signature change to `(void *arg, hal_ctx_t *ctx)` and handle-based pin access via `hal_ctx_pin_*_get/set()`. Sync handled automatically by executor. ⚠️
- **Userspace .comp:** Add `hal_ctx_sync_read()` / `hal_ctx_sync_write()` calls in `user_mainloop()` ⚠️
- **Python Components:** Add `h.sync_read()` / `h.sync_write()` or use `h.synced()` context manager ⚠️
- **halcompile:** Will be updated to automatically generate new API code for .comp files
- **Key Changes:** Context-based API (`hal_ctx_t`), handle-based pin access, period via `hal_ctx_period(ctx)`

The migration to thread-local HAL will improve real-time determinism by eliminating cache contention and providing consistent, predictable access patterns for all components.
