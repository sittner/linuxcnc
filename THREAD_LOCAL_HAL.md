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

## Solution: Thread-Local Copy with Dirty Bitmap

Each RT thread and userspace context maintains:
1. A **working buffer** (1MB copy of HAL shared memory)
2. A **dirty bitmap** (128KB, 1 bit per byte) tracking local modifications

Key insight: Pins don't store data directly - they point to **signals**. Parameters store data directly. Both signals and parameters are allocated from HAL shared memory using **offsets**, not pointers.

## Sync Responsibility Matrix

| Component Type | Loop Control | Sync Strategy | User Changes Required |
|----------------|--------------|---------------|----------------------|
| **RT (.comp, .c)** | RT executor | **Automatic ✅** | **Yes** - Function signature changes to `(void *arg, hal_ctx_t *ctx)`, use handle-based pin access |
| **Userspace .comp** | User's `user_mainloop()` | **Manual ⚠️** | Add `hal_ctx_sync_read()` / `hal_ctx_sync_write()` calls |
| **Python halmodule** | User's `while` loop | **Manual ⚠️** | Add `h.sync_read()` / `h.sync_write()` calls |
| **Go components** | User's loop | **Manual ⚠️** | Add sync calls in main loop |

## Memory Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                Master HAL Shared Memory (1MB)                   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ hal_data header                                          │   │
│  ├──────────────────────────────────────────────────────────┤   │
│  │ pin structs (point to signals via offset)                │   │
│  ├──────────────────────────────────────────────────────────┤   │
│  │ param structs (contain data directly)                    │   │
│  ├──────────────────────────────────────────────────────────┤   │
│  │ signal data (actual pin values live here)                │   │
│  ├──────────────────────────────────────────────────────────┤   │
│  │ free space                                               │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                 │
│  allocation_bitmap (16KB) - 1 bit per 8-byte block              │
│  Tracks which regions contain signal/param data                 │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ sync_read: copy allocated regions
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              Thread-Local Context (~1.125MB)                    │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ working_buf (1MB) - same layout as master                │   │
│  │ Access via: base + offset (same offsets work everywhere) │   │
│  └──────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ dirty_bitmap (128KB) - 1 bit per byte                    │   │
│  │ Tracks which bytes were modified locally                 │   │
│  └──────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ Thread timing info:                                      │   │
│  │   - period_ns, actual_period_ns                          │   │
│  │   - iteration_count, overruns                            │   │
│  │   - thread_name                                          │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### Offsets, Not Pointers

All data access uses offsets into HAL memory:
- `handle = offset` returned by pin/param creation
- Access: `*(type *)(base + offset)`
- Same offset works with master HAL or any context's working_buf

### What Gets Copied

| Data Type | Location | Copied by sync? |
|-----------|----------|-----------------|
| Pin struct | HAL memory | No (metadata only) |
| Signal data | HAL memory | Yes (via allocation_bitmap) |
| Param data | HAL memory | Yes (via allocation_bitmap) |

### Dirty Tracking (1-byte granularity)

Why not cache-line (64-byte) or 8-byte granularity? **Concurrent writes from other threads would be lost.**

Example of the problem with coarse granularity:
```
8-byte block contains: [float A] [float B]

Thread 1 context:     Thread 2 writes B in master:
  modifies A            B = 9.0
  dirty bit set

sync_write copies entire block:
  Master now has Thread 1's stale B value - Thread 2's write lost!
```

With 1-byte granularity, only modified bytes are written back.

### Precomputed Dirty Bitmap Access

To minimize pin write overhead, signal/param structs store precomputed bitmap access info:

```c
typedef struct hal_sig {
    // ... existing fields ...
    
    // Precomputed for fast dirty marking
    uint32_t dirty_offset;    // Index into dirty_bitmap array
    uint32_t dirty_mask[2];   // Masks for 1 or 2 words (handles boundary)
} hal_sig_t;
```

Pin write operation (optimized):
```c
static inline void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t h, hal_float_t val) {
    // Write value to working buffer
    *(hal_float_t *)(ctx->working_buf + h.data_offset) = val;
    
    // Mark dirty - just 2 OR operations using precomputed values
    hal_sig_t *sig = get_signal(h);
    ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
    ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
}
```

Cost: ~6 instructions (~9 cycles) per pin write.
## Sync Operations

### sync_read

```c
int hal_ctx_sync_read(hal_ctx_t *ctx) {
    uint64_t *alloc = hal_data->allocation_bitmap;
    
    // Copy only allocated 8-byte blocks
    for (size_t block = 0; block < HAL_SIZE / 8; block++) {
        if (alloc[block / 64] & (1UL << (block % 64))) {
            size_t offset = block * 8;
            memcpy(ctx->working_buf + offset, 
                   hal_shmem_base + offset, 8);
        }
    }
    
    // Clear dirty bitmap - fresh read, no local changes
    memset(ctx->dirty_bitmap, 0, HAL_SIZE / 8);
    
    ctx->valid = 1;
    return 0;
}
```

### sync_write

```c
int hal_ctx_sync_write(hal_ctx_t *ctx) {
    uint64_t *alloc = hal_data->allocation_bitmap;
    uint32_t *dirty = ctx->dirty_bitmap;
    
    // For each allocated 8-byte block
    for (size_t block = 0; block < HAL_SIZE / 8; block++) {
        if (!(alloc[block / 64] & (1UL << (block % 64)))) continue;
        
        size_t byte_start = block * 8;
        
        // Check if any byte in this block is dirty
        uint8_t dirty_bits = get_dirty_bits(dirty, byte_start, 8);
        
        if (!dirty_bits) continue;
        
        // Copy only dirty bytes
        for (int i = 0; i < 8; i++) {
            if (dirty_bits & (1 << i)) {
                ((uint8_t *)hal_shmem_base)[byte_start + i] = 
                    ((uint8_t *)ctx->working_buf)[byte_start + i];
            }
        }
    }
    
    ctx->valid = 0;
    return 0;
}
```

## RT Thread Integration

### Breaking Change: New Function Signature

```c
// Old signature (deprecated)
typedef void (*hal_funct_t)(void *arg, long period);

// New signature
typedef void (*hal_funct_t)(void *arg, hal_ctx_t *ctx);
```

Period is now accessed via context:
```c
long period = hal_ctx_period(ctx);
```

### Context Structure

```c
typedef struct hal_ctx {
    // Buffers
    void *working_buf;           // 1MB - copy of HAL memory
    uint32_t *dirty_bitmap;      // 128KB - tracks local modifications
    
    // Thread timing info
    const char *thread_name;
    long period_ns;              // Configured period
    long actual_period_ns;       // Measured actual period
    unsigned long iteration_count;
    unsigned long overruns;
    
    // State
    unsigned char valid;         // sync_read was called
} hal_ctx_t;
```

### Thread Runner

```c
static void thread_task(void *arg) {
    hal_thread_t *thread = arg;
    
    while (thread->running) {
        hal_ctx_sync_read(thread->ctx);
        
        // Update timing info
        thread->ctx->iteration_count++;
        
        // Call all functions in thread
        for (funct = first_funct; funct; funct = next_funct) {
            funct->funct(funct->arg, thread->ctx);
        }
        
        hal_ctx_sync_write(thread->ctx);
        
        rtapi_wait();
    }
}
```

## Performance Analysis

### Memory Overhead Per Context

| Component | Size |
|-----------|------|
| working_buf | 1MB |
| dirty_bitmap | 128KB |
| **Total** | ~1.125MB |

### Timing (per cycle)

#### Pin Write Overhead
- ~6 instructions per write
- 100 pins: ~600 ns
- Negligible

#### sync_read (copy allocated regions)

| HAL Usage | x86 | RPi4 |
|-----------|-----|------|
| Light (10KB) | ~1 µs | ~4 µs |
| Medium (50KB) | ~3 µs | ~20 µs |
| Heavy (200KB) | ~12 µs | ~80 µs |

#### sync_write (copy dirty bytes)

| Pins Modified | x86 | RPi4 |
|---------------|-----|------|
| 10 | <1 µs | ~2 µs |
| 50 | ~2 µs | ~8 µs |
| 200 | ~8 µs | ~30 µs |

#### Total Overhead (1ms thread, medium usage)

| Platform | Total | % of 1ms |
|----------|-------|----------|
| x86 | ~5 µs | 0.5% |
| RPi4 | ~30 µs | 3% |

## C API

### Context Lifecycle

```c
/* Create context (for userspace components) */
hal_ctx_t *hal_ctx_create(int comp_id);

/* Destroy context */
void hal_ctx_destroy(hal_ctx_t *ctx);
```

### Handle-based Pin/Param Creation

```c
/* Create pin and return handle (for new context-based access) */
hal_pin_handle_t hal_pin_float_new_handle(const char *name, hal_pin_dir_t dir, int comp_id);
hal_pin_handle_t hal_pin_bit_new_handle(const char *name, hal_pin_dir_t dir, int comp_id);
hal_pin_handle_t hal_pin_s32_new_handle(const char *name, hal_pin_dir_t dir, int comp_id);
hal_pin_handle_t hal_pin_u32_new_handle(const char *name, hal_pin_dir_t dir, int comp_id);

/* Create param and return handle */
hal_param_handle_t hal_param_float_new_handle(const char *name, hal_param_dir_t dir, int comp_id);
hal_param_handle_t hal_param_bit_new_handle(const char *name, hal_param_dir_t dir, int comp_id);
hal_param_handle_t hal_param_s32_new_handle(const char *name, hal_param_dir_t dir, int comp_id);
hal_param_handle_t hal_param_u32_new_handle(const char *name, hal_param_dir_t dir, int comp_id);
```

### Sync Operations

```c
/* Copy allocated regions from master to working_buf */
int hal_ctx_sync_read(hal_ctx_t *ctx);

/* Copy dirty bytes from working_buf to master */
int hal_ctx_sync_write(hal_ctx_t *ctx);
```

### Context Accessors

```c
/* Get thread period */
long hal_ctx_period(hal_ctx_t *ctx);

/* Get iteration count */
unsigned long hal_ctx_iteration(hal_ctx_t *ctx);

/* Get overrun count */
unsigned long hal_ctx_overruns(hal_ctx_t *ctx);
```

### Pin Access (Context-Aware)

```c
/* Read from working_buf */
hal_float_t hal_ctx_pin_float_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_s32_t hal_ctx_pin_s32_get(hal_ctx_t *ctx, hal_pin_handle_t pin);
hal_u32_t hal_ctx_pin_u32_get(hal_ctx_t *ctx, hal_pin_handle_t pin);

/* Write to working_buf + mark dirty */
void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_float_t val);
void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_bit_t val);
void hal_ctx_pin_s32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_s32_t val);
void hal_ctx_pin_u32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_u32_t val);
```

### Parameter Access (Context-Aware)

```c
/* Read param from working_buf */
hal_float_t hal_ctx_param_float_get(hal_ctx_t *ctx, hal_param_handle_t param);
hal_bit_t hal_ctx_param_bit_get(hal_ctx_t *ctx, hal_param_handle_t param);
hal_s32_t hal_ctx_param_s32_get(hal_ctx_t *ctx, hal_param_handle_t param);
hal_u32_t hal_ctx_param_u32_get(hal_ctx_t *ctx, hal_param_handle_t param);

/* Write param to working_buf + mark dirty */
void hal_ctx_param_float_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_float_t val);
void hal_ctx_param_bit_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_bit_t val);
void hal_ctx_param_s32_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_s32_t val);
void hal_ctx_param_u32_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_u32_t val);
```

## RT Component Example

### Before (old API)
```c
static hal_float_t *in, *out;
static hal_float_t gain;

void update(void *arg, long period) {
    *out = *in * gain;
}
```

### After (new API)
```c
static hal_pin_handle_t in_h, out_h;
static hal_param_handle_t gain_h;

void update(void *arg, hal_ctx_t *ctx) {
    hal_float_t in = hal_ctx_pin_float_get(ctx, in_h);
    hal_float_t gain = hal_ctx_param_float_get(ctx, gain_h);
    
    hal_ctx_pin_float_set(ctx, out_h, in * gain);
}
```

Note: For RT components, `sync_read` and `sync_write` are called by the thread runner, not by individual component functions.

## Userspace Component Example

Userspace components control their own main loop via `user_mainloop()`. They must call sync explicitly.

### Before (current code)
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

### After (with sync calls)
```c
void user_mainloop(void) {
    while(1) {
        hal_ctx_sync_read();   // Sync inputs from shared memory
        
        FOR_ALL_INSTS() {
            out = in;  // existing component logic unchanged
        }
        
        hal_ctx_sync_write();  // Sync outputs to shared memory
        usleep(1000);
    }
}
```

**Key Points:**
- Add `hal_ctx_sync_read()` at the start of each loop iteration
- Add `hal_ctx_sync_write()` at the end of each loop iteration
- Place sync calls **outside** the `FOR_ALL_INSTS()` block

## Python API

The Python `hal` module provides sync methods for userspace components.

### `h.sync_read()`
```python
h.sync_read()
```
Synchronizes input pins from shared memory to thread-local storage. Call at the start of your main loop iteration.

### `h.sync_write()`
```python
h.sync_write()
```
Synchronizes output pins from thread-local storage to shared memory. Call at the end of your main loop iteration.

### `h.synced()` (Context Manager)
```python
with h.synced():
    # pin operations here
```
Convenience context manager that calls `sync_read()` on entry and `sync_write()` on exit.

### Python Example
```python
import hal
import time

h = hal.component("mycomp")
h.newpin("in", hal.HAL_FLOAT, hal.HAL_IN)
h.newpin("out", hal.HAL_FLOAT, hal.HAL_OUT)
h.ready()

while True:
    h.sync_read()
    h['out'] = h['in'] * 2.0
    h.sync_write()
    time.sleep(0.001)
```

## halcompile Integration

halcompile must be updated to:

1. Generate new function signature: `void update(void *arg, hal_ctx_t *ctx)`
2. Use handle-based pin/param creation
3. Generate `hal_ctx_pin_*_get/set` calls instead of pointer dereferences
4. Replace `period` parameter access with `hal_ctx_period(ctx)`

### .comp Before
```
component example;
pin in float input;
pin out float output;
function _;
;;
FUNCTION(_) {
    output = input * 2.0;
}
```

### Generated Code After
```c
static hal_pin_handle_t input_h, output_h;

void _(void *arg, hal_ctx_t *ctx) {
    hal_float_t input = hal_ctx_pin_float_get(ctx, input_h);
    hal_ctx_pin_float_set(ctx, output_h, input * 2.0);
}
```

## Why Userspace Needs Manual Sync

Userspace components control their own main loop. The HAL framework cannot inject sync calls because it doesn't know when your loop iterations begin and end. Only you know the logical boundaries of your processing cycle.

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

**Benefits of full thread-local:**
- All pin types behave consistently (no special cases for strings vs. numerics)
- Bugs are obvious - forgotten sync = zero/empty values
- Simpler mental model: "call sync, period"
- Future-proof for new pin types
- No legacy "direct mode" code paths to maintain

## Implementation Phases

### Phase 1: Master HAL Changes
- Add `allocation_bitmap` to `hal_data_t`
- Update signal/param allocation to set bitmap bits
- Add `dirty_offset`, `dirty_mask[2]` to signal/param structs
- Precompute dirty access on allocation
- **Status:** Planned

### Phase 2: Context Implementation
- Implement `hal_ctx_t` with working_buf + dirty_bitmap
- Implement `hal_ctx_create/destroy`
- Implement `hal_ctx_sync_read/write`
- Implement `hal_ctx_pin_*_get/set` with dirty marking
- **Status:** Planned

### Phase 3: Thread Integration
- Change `hal_funct_t` signature (breaking change)
- Add `ctx` to `hal_thread_t`
- Update thread creation to create context
- Update thread runner to call sync and pass context
- **Status:** Planned

### Phase 4: halcompile Update
- Generate new function signature
- Generate handle-based pin/param creation
- Generate context-aware pin access
- Replace period parameter with `hal_ctx_period(ctx)`
- **Status:** Planned

### Phase 5: Component Migration
- Migrate `counter.c` as first example
- Update remaining RT components
- Update documentation
- **Status:** Planned

## Files Requiring Updates (Phase 5 Inventory)

The following files need to be updated during Phase 5 (Component Migration).

### Userspace .comp Files

| File | Description |
|------|-------------|
| `src/hal/user_comps/thermistor.comp` | Thermistor temperature estimator |
| `src/hal/user_comps/pi500_vfd/pi500_vfd.comp` | Powtran PI500 VFD modbus driver |
| `src/hal/user_comps/wj200_vfd/wj200_vfd.comp` | Hitachi WJ200 VFD modbus driver |
| `docs/src/hal/rand.comp` | Example/documentation random component |

### Python HAL Module

| File | Changes Needed |
|------|----------------|
| `lib/python/hal.py` | Add `sync_read()`, `sync_write()`, and `synced()` context manager |
| `src/hal/halmodule.cc` | Add C implementation of sync methods |

### Python Components

| File | Description |
|------|-------------|
| `src/hal/user_comps/sim-torch.py` | Simulated torch for plasma cutting |
| `src/hal/user_comps/mqtt-publisher.py` | MQTT publisher component |
| `src/hal/user_comps/pmx485.py` | Powermax RS485 plasma driver |

## Advanced Sync Patterns

### Read-Only Components

By only calling `sync_read()` and omitting `sync_write()`, you create a component that is architecturally prevented from modifying HAL state:

```python
while True:
    h.sync_read()  # Only sync_read - component is guaranteed read-only
    log_position(h['axis-x-pos'], h['axis-y-pos'])
    # No sync_write() - this component cannot affect machine state
    time.sleep(0.1)
```

### Write-Only Components (Rare)

A component that only generates outputs could use only `sync_write()`:

```python
while True:
    h['signal'] = math.sin(time.time() * frequency)
    h.sync_write()  # Only sync_write - doesn't need external inputs
    time.sleep(0.001)
```

## Frequently Asked Questions

### Q: Do I need to change my RT component code?
**A:** Yes. RT components require the new function signature `(void *arg, hal_ctx_t *ctx)` and handle-based pin access via `hal_ctx_pin_*_get/set()`. However, the RT executor still handles sync automatically.

### Q: What happens if I forget sync calls in userspace components?
**A:** Your component will read initial/zero values for all input pins, and output pins will never update in HAL. This "fail-fast" behavior is intentional.

### Q: What about HAL parameters?
**A:** Parameters follow the same thread-local model as pins and will be included in sync operations.

### Q: Do HAL streams need sync?
**A:** No. HAL streams have their own lock-free FIFO mechanism and are independent of the pin sync model.

## Files to Modify

| File | Changes |
|------|---------|
| `src/hal/hal_priv.h` | Add `allocation_bitmap` to `hal_data_t`, dirty fields to signal/param |
| `src/hal/hal.h` | Add `hal_ctx_t`, context API, new `hal_funct_t` signature |
| `src/hal/hal_lib.c` | Implement context, sync, update allocator for bitmap |
| `src/rtapi/rtapi_app.cc` | Update thread runner |
| `src/hal/halmodule.cc` | Python context bindings |
| `src/hal/utils/halcompile.g` | Generate new code patterns |
| `src/hal/components/counter.c` | First migration example |

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Pin access without `sync_read` | Error: "context not synced" |
| `sync_write` without `sync_read` | Error: "sync_write without sync_read" |
| Access after `ctx_destroy` | Error: "context destroyed" |

## Additional Resources

For user-facing migration guidance, detailed examples, halcompile integration details, and comprehensive FAQ, see the migration guide: <a>docs/src/hal/THREAD_LOCAL_HAL.md</a>
