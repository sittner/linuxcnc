# Thread-Local HAL Data Architecture

## Overview

This document describes the thread-local HAL data copy mechanism for improved
real-time determinism. The architecture provides explicit context management
for all component types, following a "fail early" pattern with no silent
fallbacks.

## Problem Statement

Current HAL accesses shared memory directly, which can cause:
- Cache contention between RT threads
- Non-deterministic latency from memory barriers
- Potential for mid-cycle data changes

## Solution: Explicit Context with Double-Buffer Diff

Each thread that accesses HAL pins must:
1. Create a context with `hal_ctx_create()`
2. Call `hal_ctx_sync_read()` at cycle start
3. Access pins via context-aware functions
4. Call `hal_ctx_sync_write()` at cycle end
5. Destroy context with `hal_ctx_destroy()` on exit

At cycle end, `sync_write` compares before/after buffers and writes only
changed values back to shared memory.

## Key Design Decisions

### Fail Early Pattern

- **No backward compatibility mode**: Invalid usage results in immediate error
- **No silent fallbacks**: All errors are hard errors
- **Predictable behavior**: Undefined states are not tolerated

### Thread-Local Contexts

- Each thread manages its own `hal_ctx_t`, independent of component
- Components do not know the internal thread structure of userspace processes
- Library does not manage thread lifecycle - that's the component's responsibility
- Multiple contexts per component are supported (for multi-threaded components)

### Explicit API

- All languages mirror the C API pattern
- No implicit sync or automatic context management
- User code must explicitly call sync functions

### Hard Errors

| Condition | Behavior |
|-----------|----------|
| Pin access without `sync_read` | Error: "no active context" |
| `sync_write` without `sync_read` | Error: "compare buffer undefined" |
| Double `sync_read` without `sync_write` | Error: "sync_read called twice" |
| Access after `ctx_destroy` | Error: "context destroyed" |

## C API

### Context Lifecycle

```c
/* Create context for current thread */
hal_ctx_t *hal_ctx_create(int comp_id);

/* Destroy context - must be called before thread exit */
void hal_ctx_destroy(hal_ctx_t *ctx);
```

### Sync Operations

```c
/* Copy shared memory → local buffer (before snapshot) */
int hal_ctx_sync_read(hal_ctx_t *ctx);

/* Copy local buffer → shared memory (diff only) */
int hal_ctx_sync_write(hal_ctx_t *ctx);
```

### Pin Creation (Handle-Based)

```c
/* Returns handle (offset), not pointer */
int hal_pin_float_new_handle(const char *name, hal_pin_dir_t dir,
                             hal_pin_handle_t *handle, int comp_id);
int hal_pin_bit_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id);
int hal_pin_s32_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id);
int hal_pin_u32_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id);
```

### Pin Access (Context-Aware)

```c
/* Read from local buffer - only valid after sync_read */
hal_float_t hal_ctx_pin_float_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_s32_t hal_ctx_pin_s32_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_u32_t hal_ctx_pin_u32_get(hal_ctx_t *ctx, hal_pin_handle_t pin);

/* Write to local buffer - only valid after sync_read */
void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_float_t val);
void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_bit_t val);
void hal_ctx_pin_s32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_s32_t val);
void hal_ctx_pin_u32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_u32_t val);
```

### C Example: Single-Threaded Component

```c
#include "hal.h"

static int comp_id;
static hal_ctx_t *ctx;
static hal_pin_handle_t pin_in, pin_out;

int main(int argc, char *argv[]) {
    comp_id = hal_init("mycomp");
    if (comp_id < 0) return 1;

    hal_pin_float_new_handle("in", HAL_IN, &pin_in, comp_id);
    hal_pin_float_new_handle("out", HAL_OUT, &pin_out, comp_id);
    hal_ready(comp_id);

    ctx = hal_ctx_create(comp_id);
    if (!ctx) {
        hal_exit(comp_id);
        return 1;
    }

    while (!done) {
        hal_ctx_sync_read(ctx);

        float val = hal_ctx_pin_float_get(ctx, pin_in);
        hal_ctx_pin_float_set(ctx, pin_out, val * 2.0);

        hal_ctx_sync_write(ctx);
        usleep(1000);
    }

    hal_ctx_destroy(ctx);
    hal_exit(comp_id);
    return 0;
}
```

### C Example: Multi-Threaded Component

```c
/* Each thread creates and manages its own context */
void *worker_thread(void *arg) {
    int thread_num = *(int *)arg;

    /* Each thread has its own context */
    hal_ctx_t *ctx = hal_ctx_create(comp_id);
    if (!ctx) return NULL;

    while (!quit_flag) {
        hal_ctx_sync_read(ctx);
        process_pins(ctx, thread_num);
        hal_ctx_sync_write(ctx);
        usleep(period);
    }

    hal_ctx_destroy(ctx);
    return NULL;
}
```

## Python API

### Context Management

```python
# Create context for current thread
ctx = h.ctx_create()

# Sync operations
ctx.sync_read()   # shared → local
ctx.sync_write()  # local → shared (diff)

# Destroy when done
ctx.destroy()
```

### Pin Access

```python
# Pin access only valid between sync_read and sync_write
ctx.sync_read()
value = h['input']        # Read from local buffer
h['output'] = value * 2   # Write to local buffer
ctx.sync_write()
```

### Context Manager (Convenience)

```python
# Context manager calls sync_read on enter, sync_write on exit
with ctx:
    h['output'] = h['input'] * 2
```

### Python Example: Explicit Sync

```python
#!/usr/bin/env python3
import hal
import time

h = hal.component("mycomp")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

ctx = h.ctx_create()

try:
    while True:
        ctx.sync_read()
        h['out'] = h['in'] * 2.0
        ctx.sync_write()
        time.sleep(0.1)
except KeyboardInterrupt:
    pass
finally:
    ctx.destroy()
```

### Python Example: Context Manager

```python
#!/usr/bin/env python3
import hal
import time

h = hal.component("mycomp")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

ctx = h.ctx_create()

try:
    while True:
        with ctx:  # sync_read on enter, sync_write on exit
            h['out'] = h['in'] * 2.0
        time.sleep(0.1)
except KeyboardInterrupt:
    pass
finally:
    ctx.destroy()
```

### Python Example: Multi-Threaded

```python
#!/usr/bin/env python3
import hal
import threading
import time

h = hal.component("multithread")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out1", hal.HAL_FLOAT, hal.HAL_OUT)
h.newpin("out2", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

running = True

def worker(pin_name, mult):
    ctx = h.ctx_create()  # Each thread creates own context
    try:
        while running:
            with ctx:
                h[pin_name] = h['in'] * mult
            time.sleep(0.05)
    finally:
        ctx.destroy()

t1 = threading.Thread(target=worker, args=('out1', 2.0))
t2 = threading.Thread(target=worker, args=('out2', 3.0))
t1.start()
t2.start()

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    running = False

t1.join()
t2.join()
```

### Python Error Cases

```python
ctx = h.ctx_create()

# ERROR: Access without sync_read
h['in']  # Raises: "no active context for pin access"

# ERROR: sync_write without sync_read  
ctx.sync_write()  # Raises: "sync_write called without sync_read"

# ERROR: Double sync_read
ctx.sync_read()
ctx.sync_read()  # Raises: "sync_read called twice without sync_write"
```

## Go API

### Context Management

```go
// Create context for current goroutine
ctx, err := comp.NewContext()

// Sync operations
ctx.SyncRead()   // shared → local
ctx.SyncWrite()  // local → shared (diff)

// Destroy when done
ctx.Destroy()
```

### Pin Access

```go
// Pin access requires context parameter
val, err := inPin.Get(ctx)
err = outPin.Set(ctx, val * 2.0)

// MustGet/MustSet panic on error (for cleaner code)
val := inPin.MustGet(ctx)
outPin.MustSet(ctx, val * 2.0)
```

### Cycle Helper

```go
// Cycle: sync_read, fn, sync_write
err := ctx.Cycle(func() error {
    val := inPin.MustGet(ctx)
    outPin.MustSet(ctx, val * 2.0)
    return nil
})
```

### Go Example

```go
package main

import (
    "log"
    "time"
    "linuxcnc.org/hal"
)

func main() {
    comp, _ := hal.NewComponent("mycomp")
    defer comp.Exit()

    inPin, _ := hal.NewPin[float64](comp, "in", hal.In)
    outPin, _ := hal.NewPin[float64](comp, "out", hal.Out)
    comp.Ready()

    ctx, _ := comp.NewContext()
    defer ctx.Destroy()

    for comp.Running() {
        ctx.Cycle(func() error {
            outPin.MustSet(ctx, inPin.MustGet(ctx) * 2.0)
            return nil
        })
        time.Sleep(10 * time.Millisecond)
    }
}
```

## halcompile (.comp files)

### Realtime Components

For realtime `.comp` files, halcompile generates code that integrates with
the RT thread executor. The executor handles sync calls automatically:

```c
// Generated in RT thread executor
void execute_thread(hal_thread_t *thread) {
    hal_ctx_sync_read(thread->ctx);

    for_each_function(f) {
        f->funct(f->arg, period);  // Component code unchanged
    }

    hal_ctx_sync_write(thread->ctx);
}
```

**Realtime .comp files require NO changes.**

### Userspace Components with Default Main Loop

For userspace `.comp` files using the default main loop pattern (no custom
`user_mainloop`), halcompile generates sync calls automatically:

```c
// Generated wrapper
int main(int argc, char **argv) {
    // ... init ...
    hal_ready(comp_id);

    hal_ctx_t *__hal_ctx = hal_ctx_create(comp_id);

    while (!__comp_done) {
        hal_ctx_sync_read(__hal_ctx);    // GENERATED

        FOR_ALL_INSTS() {
            // User's FUNCTION code
        }

        hal_ctx_sync_write(__hal_ctx);   // GENERATED
        usleep(__comp_period);
    }

    hal_ctx_destroy(__hal_ctx);
    hal_exit(comp_id);
}
```

**These .comp files require NO changes.**

### Userspace Components with Custom user_mainloop()

For userspace `.comp` files that define `user_mainloop()`, the user controls
the loop structure. **halcompile CANNOT automatically inject sync calls.**

The user MUST add explicit sync calls:

```c
// In .comp file with option userspace yes and custom user_mainloop

EXTRA_SETUP() {
    // Called once at startup
    // Context is passed as __hal_ctx (generated)
}

void user_mainloop(void) {
    // User controls the loop - MUST add sync calls manually
    while (!done) {
        hal_ctx_sync_read(__hal_ctx);    // USER MUST ADD

        FOR_ALL_INSTS() {
            out = in * gain;  // Pin access
        }

        hal_ctx_sync_write(__hal_ctx);   // USER MUST ADD
        usleep(period);
    }
}
```

halcompile will:
1. Generate `hal_ctx_t *__hal_ctx` as a global variable
2. Create the context before calling `user_mainloop()`
3. Destroy the context after `user_mainloop()` returns
4. **NOT** inject sync calls inside `user_mainloop()` - user must add these

### .comp Migration Summary

| .comp Type | Sync Calls | User Action Required |
|------------|------------|---------------------|
| Realtime | RT executor handles | None |
| Userspace (default loop) | halcompile generates | None |
| Userspace (custom `user_mainloop`) | User must add | Add `hal_ctx_sync_read/write(__hal_ctx)` |

### Identifying .comp Files Requiring Manual Migration

Files with both:
- `option userspace yes`
- Custom `user_mainloop()` function

Known files:

| File | Description |
|------|-------------|
| `src/hal/user_comps/thermistor.comp` | Thermistor temperature estimator |
| `src/hal/user_comps/pi500_vfd/pi500_vfd.comp` | Powtran PI500 VFD driver |
| `src/hal/user_comps/wj200_vfd/wj200_vfd.comp` | Hitachi WJ200 VFD driver |

## C Userspace Components (Manual Main Loop)

Components written in C with their own `main()` function require full migration:
1. Change from pointer-based pins to handle-based pins
2. Create context at startup
3. Add sync calls to main loop
4. Destroy context at shutdown

### Components Requiring Migration

| Component | File | Complexity |
|-----------|------|------------|
| mb2hal | `src/hal/user_comps/mb2hal/` | High (multi-threaded) |
| shuttle | `src/hal/user_comps/shuttle.c` | Medium |
| svd-ps_vfd | `src/hal/user_comps/svd-ps_vfd.c` | Medium |
| hy_gt_vfd | `src/hal/user_comps/hy_gt_vfd.c` | Medium |
| hy_vfd | `src/hal/user_comps/huanyang-vfd/hy_vfd.c` | Medium |
| classicladder | `src/hal/classicladder/classicladder.c` | High |
| halui | `src/emc/usr_intf/halui.cc` | High |

### Migration Pattern

**Before:**
```c
struct haldata {
    hal_float_t *speed_cmd;   // Pointer to shared memory
    hal_bit_t *enable;
};

int main() {
    hal_pin_float_new("speed-cmd", HAL_IN, &haldata->speed_cmd, comp_id);

    while (!done) {
        if (*haldata->enable) {
            process(*haldata->speed_cmd);
        }
        usleep(1000);
    }
}
```

**After:**
```c
struct haldata {
    hal_pin_handle_t speed_cmd;   // Handle (offset), not pointer
    hal_pin_handle_t enable;
};

int main() {
    hal_pin_float_new_handle("speed-cmd", HAL_IN, &haldata->speed_cmd, comp_id);

    hal_ctx_t *ctx = hal_ctx_create(comp_id);

    while (!done) {
        hal_ctx_sync_read(ctx);

        if (hal_ctx_pin_bit_get(ctx, haldata->enable)) {
            process(hal_ctx_pin_float_get(ctx, haldata->speed_cmd));
        }

        hal_ctx_sync_write(ctx);
        usleep(1000);
    }

    hal_ctx_destroy(ctx);
}
```

## Double-Buffer Diff Performance

Memory comparison is extremely fast on modern CPUs:
- Sequential access pattern (cache-friendly)
- Can use SIMD (AVX2/AVX-512) for 32-64 bytes per instruction
- Typical HAL data: ~4KB → compare time: ~50-100 nanoseconds
- Negligible compared to RT periods (typically 1ms+)

Benefits over dirty-bitmap approach:
- Zero overhead on writes during execution (latency-critical phase)
- Simpler implementation (no bitmap management)
- Better debugging (can inspect before/after state)

## Implementation Phases

### Phase 1: Core API
- Add `hal_ctx_t` and context management functions to `hal.h`
- Implement context create/destroy in `hal_lib.c`
- Implement sync_read/sync_write with double-buffer diff
- Add handle-based pin creation functions

### Phase 2: Language Bindings
- Update `halmodule.cc` for Python context API
- Update `hal-go` for Go context API
- Update `halcompile.g` for automatic sync generation

### Phase 3: Component Migration
- Migrate C userspace components
- Update .comp files with custom `user_mainloop()`
- Update documentation and examples

### Phase 4: Testing
- Unit tests for context API
- Integration tests for all component types
- Performance benchmarks

## Files Changed

| File | Change |
|------|--------|
| `src/hal/hal.h` | Add context API, handle-based pin functions |
| `src/hal/hal_lib.c` | Implement context management and sync |
| `src/hal/halmodule.cc` | Python context API |
| `src/hal/hal-go/context.go` | New: Go context implementation |
| `src/hal/hal-go/pin.go` | Modify for context-aware access |
| `src/hal/utils/halcompile.g` | Generate context and sync for userspace |

## Error Messages Reference

| Error | Cause | Solution |
|-------|-------|----------|
| "no active context for pin access" | Pin read/write without `sync_read` | Call `sync_read` first |
| "sync_write called without sync_read" | `sync_write` before `sync_read` | Call `sync_read` first |
| "sync_read called twice without sync_write" | Double `sync_read` | Call `sync_write` between reads |
| "context has been destroyed" | Use after `ctx_destroy` | Don't use destroyed context |
| "failed to create HAL context" | Resource allocation failed | Check system resources |
