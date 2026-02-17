# Thread-Local HAL Migration Guide

## Overview

This document describes the planned migration from direct shared memory HAL pin access to a thread-local storage model with explicit synchronization.

### What's Changing

**Current Architecture:**
- Components directly access HAL pin values in shared memory
- All reads and writes happen immediately to shared memory

**New Architecture:**
- Pin values are stored in thread-local storage during component execution
- Explicit sync operations transfer data between thread-local storage and shared memory
- Better isolation and potential performance benefits

### Data Flow

```
Shared Memory ↔ Thread-Local Storage ↔ Component Code
    (HAL)            (sync calls)         (your code)

┌──────────────────────────────────────────────────────────────┐
│                     SHARED MEMORY (HAL)                       │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  pins, signals, parameters                              │ │
│  └─────────────────────────────────────────────────────────┘ │
│                            ↕                                  │
│            hal_thread_sync_read() / sync_write()              │
│                            ↕                                  │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  Thread-Local Copy                                      │ │
│  │  - Fast access during execution                         │ │
│  │  - No locks needed                                      │ │
│  └─────────────────────────────────────────────────────────┘ │
│                            ↕                                  │
│              Component reads/writes pin values                │
└──────────────────────────────────────────────────────────────┘
```

## Sync Responsibility Matrix

Different component types have different requirements for synchronization:

| Component Type | Loop Control | Sync Strategy | User Changes Required |
|----------------|--------------|---------------|----------------------|
| **RT (.comp, .c)** | RT executor | **Automatic ✅** | **None** - RT executor handles sync |
| **Userspace .comp** | User's `user_mainloop()` | **Manual ⚠️** | Add `hal_thread_sync_read()` / `hal_thread_sync_write()` |
| **Python halmodule** | User's `while` loop | **Manual ⚠️** | Add `h.sync_read()` / `h.sync_write()` |
| **Go components** | User's loop | **Manual ⚠️** | Add sync calls in main loop |

## Migration Guide

### For RT Components

**Good News: No changes required!**

The RT executor automatically injects sync calls before and after each function execution. Your existing RT components will continue to work without modification.

```c
// RT component update function
static void update(void *arg, long period) {
    // RT executor automatically calls hal_thread_sync_read() here
    
    // Your existing component logic - no changes needed
    *out = *in * scale;
    
    // RT executor automatically calls hal_thread_sync_write() here
}
```

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
        hal_thread_sync_read();   // NEW: sync inputs from shared memory
        
        FOR_ALL_INSTS() {
            out = in;  // existing component logic unchanged
        }
        
        hal_thread_sync_write();  // NEW: sync outputs to shared memory
        usleep(1000);
    }
}
```

**Key Points:**
- Add `hal_thread_sync_read()` at the start of each loop iteration
- Add `hal_thread_sync_write()` at the end of each loop iteration
- Place sync calls **outside** the `FOR_ALL_INSTS()` block
- All component logic between sync calls operates on thread-local data

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

#### `hal_thread_sync_read()`
```c
void hal_thread_sync_read(void);
```
Synchronizes input pins from shared memory to thread-local storage. Call this at the beginning of your processing cycle before reading any pin values.

**When to call:**
- Userspace components: Start of main loop iteration
- RT components: Handled automatically by RT executor

#### `hal_thread_sync_write()`
```c
void hal_thread_sync_write(void);
```
Synchronizes output pins from thread-local storage to shared memory. Call this at the end of your processing cycle after setting all output pin values.

**When to call:**
- Userspace components: End of main loop iteration
- RT components: Handled automatically by RT executor

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

## Implementation Phases

The migration will happen in phases to minimize disruption:

### Phase 1: Add Sync API (Current)
- Add `hal_thread_sync_read()` and `hal_thread_sync_write()` functions
- Initially these are **no-ops** (do nothing)
- No behavioral change - components work exactly as before
- Component developers can start adding sync calls
- **Status:** ✅ Complete

### Phase 2: RT Executor Auto-Sync
- RT thread executor automatically calls sync before/after functions
- RT components continue working without code changes
- Userspace components should add manual sync calls
- **Status:** Planned

### Phase 3: Thread-Local Storage Mandatory
- Implement actual thread-local storage for pin values
- Sync functions now actually copy data
- Components without sync calls will see stale data
- **Status:** Planned

### Phase 4: Remove Legacy Direct-Access Paths
- Remove ability to directly dereference pin pointers
- All access must go through accessor functions or sync
- Compile-time enforcement of thread-local model
- **Status:** Future

## Timeline and Migration Strategy

**Now (Phase 1):**
- No action required for RT components
- Userspace component developers should add sync calls when convenient
- Adding sync calls now prepares you for future phases

**Phase 2-3 Transition:**
- RT components: No changes needed
- Userspace components: Must have sync calls or data will be stale

**Best Practice:**
Add sync calls to userspace components now, even though they're currently no-ops. This ensures your code will work correctly when thread-local storage is enabled.

## Examples

### Minimal Userspace C Component

```c
#include "hal.h"

static hal_bit_t *input_pin;
static hal_bit_t *output_pin;

void user_mainloop(void) {
    while(1) {
        hal_thread_sync_read();
        
        *output_pin = *input_pin;  // Simple passthrough
        
        hal_thread_sync_write();
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

## Frequently Asked Questions

### Q: Do I need to change my RT component code?
**A:** No. RT components have sync handled automatically by the executor.

### Q: What happens if I forget sync calls in userspace components?
**A:** In Phase 1, nothing - sync is currently a no-op. In Phase 3, your component will see stale data from the last sync.

### Q: Can I call sync in the middle of my loop?
**A:** Yes, but typically you want sync at the boundaries (start/end). Multiple syncs per cycle add overhead.

### Q: Does sync affect performance?
**A:** In Phase 1, no overhead (no-ops). In Phase 3, minimal overhead - just memory copies. The thread-local model may actually improve performance by reducing cache contention.

### Q: What about HAL parameters?
**A:** Parameters follow the same thread-local model as pins and will be included in sync operations.

### Q: Do HAL streams need sync?
**A:** No. HAL streams have their own lock-free FIFO mechanism and are independent of the pin sync model.

## Additional Resources

For more detailed technical information about the thread-local HAL architecture, implementation details, and advanced topics, see the complete architecture document: `THREAD_LOCAL_HAL.md` in the repository root.

## Summary

- **RT Components:** No changes needed ✅
- **Userspace .comp:** Add `hal_thread_sync_read()` / `hal_thread_sync_write()` ⚠️
- **Python Components:** Add `h.sync_read()` / `h.sync_write()` or use `h.synced()` ⚠️
- **Current Status:** Sync calls are no-ops, but adding them now prepares for future phases
- **Key Insight:** Only you know your component's processing boundaries

The migration to thread-local HAL will improve thread safety and enable new features like string support. By adding sync calls to userspace components now, you're preparing your code for a safer and more capable HAL system.
