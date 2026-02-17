# Thread-Local HAL Architecture

## Motivation

HAL (Hardware Abstraction Layer) currently uses direct shared memory access for pin/signal/parameter data. This creates challenges for:

1. **Thread Safety**: Multiple threads accessing HAL data can cause race conditions
2. **String Support**: Variable-length strings don't fit well in the current fixed-size atomic data model
3. **Determinism**: Locks during real-time execution can cause latency issues

## Proposed Architecture

### Core Concept

Each thread maintains a local copy of HAL data. Synchronization with a master copy happens at defined points (thread loop boundaries), not during data access.

```
┌─────────────────────────────────────────────────────────────────┐
│                      Master HAL Data                             │
│  (shared memory - source of truth)                               │
│  ┌─────────┬─────────┬─────────┬─────────┐                      │
│  │ pins    │ signals │ params  │ strings │                      │
│  └─────────┴─────────┴─────────┴─────────┘                      │
│                         ▲                                        │
│            ┌────────────┼────────────┐                          │
│            │ sync       │ sync       │ sync                     │
│            ▼            ▼            ▼                          │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐            │
│  │ Thread 1     │ │ Thread 2     │ │ Thread N     │            │
│  │ Local Copy   │ │ Local Copy   │ │ Local Copy   │            │
│  │              │ │              │ │              │            │
│  │ dirty_bitmap │ │ dirty_bitmap │ │ dirty_bitmap │            │
│  └──────────────┘ └──────────────┘ └──────────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

### Thread Loop Model

```
┌─────────────────────────────────────────────────────────────────┐
│  1. hal_thread_sync_read()  - pull changes from master          │
│  2. ... thread work (all reads/writes to local copy) ...        │
│  3. hal_thread_sync_write() - push dirty data to master         │
└─────────────────────────────────────────────────────────────────┘
```

### Benefits

| Benefit | Description |
|---------|-------------|
| **Thread-safe by design** | Each thread works on local copy, no races during execution |
| **Lock-free hot path** | Reads/writes during thread loop need no synchronization |
| **Predictable sync points** | All sync happens at defined points (loop boundaries) |
| **Natural string support** | Strings become just another data type with same sync mechanism |
| **Scalable** | More threads don't increase contention during execution |
| **Deterministic for RT** | No locks or waits during thread execution |

## Migration Plan

### Phase 1: New API (Abstraction Layer)

Introduce accessor functions that wrap current implementation for both **pins** and **parameters**. This creates the abstraction layer without changing behavior.

#### Pin Accessors

```c
// hal_api.h - Pin accessor API

// Getters
static inline hal_bit_t hal_pin_get_bit(hal_bit_t **pin);
static inline hal_float_t hal_pin_get_float(hal_float_t **pin);
static inline hal_s32_t hal_pin_get_s32(hal_s32_t **pin);
static inline hal_u32_t hal_pin_get_u32(hal_u32_t **pin);

// Setters
static inline void hal_pin_set_bit(hal_bit_t **pin, hal_bit_t value);
static inline void hal_pin_set_float(hal_float_t **pin, hal_float_t value);
static inline void hal_pin_set_s32(hal_s32_t **pin, hal_s32_t value);
static inline void hal_pin_set_u32(hal_u32_t **pin, hal_u32_t value);
```

#### Parameter Accessors

Parameters differ from pins - they use single pointers (not double pointers) and are not linked to signals.

```c
// hal_api.h - Parameter accessor API

// Getters
static inline hal_bit_t hal_param_get_bit(hal_bit_t *param);
static inline hal_float_t hal_param_get_float(hal_float_t *param);
static inline hal_s32_t hal_param_get_s32(hal_s32_t *param);
static inline hal_u32_t hal_param_get_u32(hal_u32_t *param);

// Setters
static inline void hal_param_set_bit(hal_bit_t *param, hal_bit_t value);
static inline void hal_param_set_float(hal_float_t *param, hal_float_t value);
static inline void hal_param_set_s32(hal_s32_t *param, hal_s32_t value);
static inline void hal_param_set_u32(hal_u32_t *param, hal_u32_t value);
```

#### Thread Sync Points

```c
// Thread sync points (no-op in Phase 1)
static inline void hal_thread_sync_read(void);
static inline void hal_thread_sync_write(void);
```

#### Phase 1 Implementation (thin wrappers)

```c
// Pins use double pointer
static inline hal_float_t hal_pin_get_float(hal_float_t **pin) {
    return **pin;  // Direct access - same as before
}

static inline void hal_pin_set_float(hal_float_t **pin, hal_float_t value) {
    **pin = value;  // Direct access - same as before
}

// Parameters use single pointer
static inline hal_float_t hal_param_get_float(hal_float_t *param) {
    return *param;  // Direct access - same as before
}

static inline void hal_param_set_float(hal_float_t *param, hal_float_t value) {
    *param = value;  // Direct access - same as before
}

static inline void hal_thread_sync_read(void) {
    // No-op in Phase 1
}

static inline void hal_thread_sync_write(void) {
    // No-op in Phase 1
}
```

## Phase 2: Component Conversion (Subdivided)

Convert all HAL components to use the new API. This is a mechanical transformation divided into waves for early issue detection.

### Phase 2a: Pilot Components (First Wave)

Simple components from both RT and userspace to validate the approach:

| Type | Component | File | Notes |
|------|-----------|------|-------|
| RT | supply | `src/hal/components/supply.c` | Simplest RT, 4 pins |
| RT | counter | `src/hal/components/counter.c` | Pins + params, 2 functions |
| Userspace | shuttle | `src/hal/user_comps/shuttle.c` | Simple poll loop |

**Status**: ✅ Complete

### Phase 2b: Second Wave

Slightly more complex components:

| Type | Component | File | Notes |
|------|-----------|------|-------|
| RT | siggen | `src/hal/components/siggen.c` | Multiple outputs |
| Userspace | sampler_usr | `src/hal/components/sampler_usr.c` | Stream-based |
| Userspace | sendkeys | `src/hal/user_comps/sendkeys.c` | Event-driven |

### Phase 2c: Third Wave (Complex)

Components with complex patterns:

| Type | Component | File | Notes |
|------|-----------|------|-------|
| Userspace | VFD drivers | `src/hal/user_comps/*_vfd.c` | Modbus pattern |
| Userspace | mb2hal | `src/hal/user_comps/mb2hal/` | Multi-threaded |
| RT | pid | `src/hal/components/pid.c` | Many pins, critical |

### Userspace Sync Placement Guidelines

For userspace components, sync calls must be placed at appropriate points in the main loop:

1. **Simple poll loop**: `sync_read` at loop start, `sync_write` at loop end (before sleep/delay)
2. **Event-driven (select/poll)**: `sync_read` after event detection, `sync_write` after all processing
3. **Multi-threaded**: Each worker thread syncs, OR use coordinator thread
4. **GUI integration**: Use idle handlers for sync

### Pin Conversion Patterns

```
**pin             →  hal_pin_get_TYPE(&pin)
**pin = value     →  hal_pin_set_TYPE(&pin, value)
```

### Parameter Conversion Patterns

```
*param           →  hal_param_get_TYPE(&param)
*param = value   →  hal_param_set_TYPE(&param, value)
```

### Example Conversion

**Before:**
```c
static hal_float_t *position_cmd;    // Pin (double pointer after hal_pin_new)
static hal_float_t *position_fb;     // Pin
static hal_bit_t *enable;            // Pin
static hal_float_t scale;            // Parameter (direct value)

static void update(void *arg, long period) {
    if (*enable) {
        *position_cmd = calculate_pos() * scale;
        float fb = *position_fb;
    }
}
```

**After:**
```c
#include "hal_api.h"

static hal_float_t *position_cmd;
static hal_float_t *position_fb;
static hal_bit_t *enable;
static hal_float_t scale;

static void update(void *arg, long period) {
    hal_thread_sync_read();
    
    if (hal_pin_get_bit(&enable)) {
        float s = hal_param_get_float(&scale);
        hal_pin_set_float(&position_cmd, calculate_pos() * s);
        float fb = hal_pin_get_float(&position_fb);
    }
    
    hal_thread_sync_write();
}
```

## Phase 3: Introduce Thread-Local HAL

Replace the API implementation with thread-local storage. Components don't change - just recompile.

#### Ensuring Non-Migrated Components Fail to Compile

To detect components that weren't migrated, change the pin/param pointer types to opaque handles:

```c
// Phase 3: Opaque handle prevents direct dereference
typedef struct {
    void *_opaque;  // Cannot dereference!
} hal_pin_handle_t;

typedef struct {
    void *_opaque;
} hal_param_handle_t;
```

Non-migrated code will fail:
```c
// Old code - COMPILER ERROR in Phase 3
**pin = value;    // Error: cannot dereference 'void *'
*param = value;   // Error: cannot dereference 'void *'

// Migrated code - works fine
hal_pin_set_float(&pin, value);
hal_param_set_float(&param, value);
```

#### Thread-Local Implementation

```c
// Thread-local instance
static __thread hal_thread_instance_t *tls_instance = NULL;

typedef struct {
    hal_data_t local_data;        // Local copy of HAL data
    uint8_t *dirty_bitmap;        // Track modified data
    size_t dirty_bitmap_size;
    uint64_t sync_version;        // Track sync state
    int thread_id;
} hal_thread_instance_t;
```

#### Userspace Component Handling

Userspace components manage their own loop and need explicit access to thread-local initialization:

```c
// hal_init() creates the thread-local copy for userspace components
int hal_init(const char *name) {
    int comp_id = hal_init_internal(name);
    
    // Initialize thread-local instance for this component
    tls_instance = hal_thread_instance_create();
    
    return comp_id;
}

// Userspace component example
int main() {
    int comp_id = hal_init("my-component");  // Creates thread-local copy
    
    hal_pin_float_new("my-component.output", HAL_OUT, &output_pin, comp_id);
    
    hal_ready(comp_id);
    
    while (!done) {
        hal_thread_sync_read();   // Sync from master
        
        float value = hal_pin_get_float(&input_pin);
        hal_pin_set_float(&output_pin, value * 2.0);
        
        hal_thread_sync_write();  // Sync to master
        
        usleep(1000);
    }
    
    hal_exit(comp_id);
    return 0;
}
```

#### Realtime Component Handling

For realtime components, the HAL thread infrastructure handles sync:

```c
// In HAL thread execution (hal_lib.c)
static void hal_thread_execute(void *arg, long period) {
    hal_thread_t *thread = arg;
    
    hal_thread_sync_read();  // Sync before running functions
    
    // Execute all functions attached to this thread
    for (funct = thread->funct_list; funct; funct = funct->next) {
        funct->funct(funct->arg, period);
    }
    
    hal_thread_sync_write();  // Sync after all functions complete
}
```

### Phase 4: Implement String Support

With thread-local HAL in place, strings become straightforward for both pins and parameters.

#### String Pin API

```c
typedef int hal_string_t;  // Handle to string in thread-local storage

int hal_pin_string_new(const char *name, hal_pin_dir_t dir,
                       hal_string_t **data_ptr_addr, int comp_id);

size_t hal_string_get(hal_string_t *str, char *dest, size_t max_len);
int hal_string_set(hal_string_t *str, const char *value);
int hal_string_setf(hal_string_t *str, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
size_t hal_string_len(hal_string_t *str);
```

#### String Parameter API

```c
int hal_param_string_new(const char *name, hal_param_dir_t dir,
                         hal_string_t *data_addr, int comp_id);

// For HAL_RW string params - settable via halcmd:
// halcmd setp mycomp.description "Some text"
```

#### Implementation

```c
// String storage in thread-local instance
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} hal_string_local_t;

size_t hal_string_get(hal_string_t *str, char *dest, size_t max_len) {
    hal_string_local_t *local = &tls_instance->strings[*str];
    size_t copy_len = local->len < max_len ? local->len : max_len - 1;
    memcpy(dest, local->data, copy_len);
    dest[copy_len] = '\0';
    return copy_len;
}

int hal_string_setf(hal_string_t *str, const char *fmt, ...) {
    hal_string_local_t *local = &tls_instance->strings[*str];
    va_list ap;
    
    // Calculate required size
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    
    // Grow buffer if needed
    if (needed + 1 > local->capacity) {
        local->data = realloc(local->data, needed * 2 + 1);
        local->capacity = needed * 2 + 1;
    }
    
    // Format string
    va_start(ap, fmt);
    local->len = vsnprintf(local->data, local->capacity, fmt, ap);
    va_end(ap);
    
    // Mark dirty for sync
    bitmap_set(tls_instance->dirty_bitmap, STRING_DIRTY_BASE + *str);
    
    return local->len;
}
```

#### Usage Example

```c
static hal_string_t *status_pin;
static hal_string_t status_param;  // Parameter version

hal_pin_string_new("mycomp.status", HAL_OUT, &status_pin, comp_id);
hal_param_string_new("mycomp.description", HAL_RW, &status_param, comp_id);

// In update function
hal_string_setf(status_pin, "Temp=%.1f RPM=%d State=%s", temp, rpm, state_name);

// Reader
char buf[256];
hal_string_get(other_status_pin, buf, sizeof(buf));
```

## HAL Streams

HAL streams (`hal_stream_t`) are an **existing mechanism** for FIFO-based communication between components:

```c
typedef struct {
    int comp_id, shmem_id;
    struct hal_stream_shm *fifo;
} hal_stream_t;

int hal_stream_create(hal_stream_t *stream, int comp, int key, int depth, const char *typestring);
int hal_stream_read(hal_stream_t *stream, union hal_stream_data *buf, unsigned *sampleno);
int hal_stream_write(hal_stream_t *stream, union hal_stream_data *buf);
```

### Decision: Streams Remain Unchanged

HAL streams are **already designed for asynchronous cross-component communication** with their own lock-free FIFO mechanism. They:

- Have single reader / single writer semantics
- Use atomic operations internally
- Are independent of the pin/param synchronization model

**Recommendation**: HAL streams do not need thread-local treatment and should remain unchanged. They already solve a different problem (streaming data) than pins/params (shared state).

## Synchronization Details

### Dirty Bitmap

Track which data has been modified locally:

```c
typedef struct {
    uint8_t *bitmap;
    size_t size;
} hal_dirty_bitmap_t;

#define DIRTY_BIT_GRANULARITY 64  // Cache line size

static inline void bitmap_set(uint8_t *bitmap, size_t index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

static inline bool bitmap_test(uint8_t *bitmap, size_t index) {
    return bitmap[index / 8] & (1 << (index % 8));
}
```

### Sync Implementation

```c
void hal_sync_from_master(hal_thread_instance_t *inst) {
    hal_master_t *master = hal_master;
    
    uint64_t master_version = atomic_load(&master->version);
    
    if (master_version == inst->sync_version) {
        return;  // Fast path - nothing changed
    }
    
    // Copy changed regions from master to local
    rtapi_mutex_get(&master->read_mutex);
    memcpy(&inst->local_data, &master->data, sizeof(hal_data_t));
    rtapi_mutex_give(&master->read_mutex);
    
    inst->sync_version = master_version;
}

void hal_sync_to_master(hal_thread_instance_t *inst) {
    hal_master_t *master = hal_master;
    
    if (bitmap_is_empty(inst->dirty_bitmap)) {
        return;  // Fast path - nothing to write
    }
    
    rtapi_mutex_get(&master->write_mutex);
    
    // Only copy dirty regions
    for_each_set_bit(bit, inst->dirty_bitmap) {
        size_t offset = bit * DIRTY_BIT_GRANULARITY;
        size_t size = DIRTY_BIT_GRANULARITY;
        memcpy((char*)&master->data + offset,
               (char*)&inst->local_data + offset,
               size);
    }
    
    atomic_fetch_add(&master->version, 1);
    
    rtapi_mutex_give(&master->write_mutex);
    
    bitmap_clear_all(inst->dirty_bitmap);
}
```

### Write Conflict Policy

When multiple threads write the same data, use **last write wins** (matching current HAL behavior):

```c
// Option 1: Last write wins (simple, current behavior)
// - Just overwrite master with local dirty data
// - No conflict detection
```

## Summary

| Phase | Work | Risk | Deliverable |
|-------|------|------|-------------|
| 1 | Define API, thin wrappers for pins AND params | None | `hal_api.h` with accessor functions |
| 2 | Convert all components | Low | All components use new API |
| 3 | Thread-local implementation | Medium | Thread-safe HAL |
| 4 | Add strings (pins and params) | Low | `hal_string_*` API |

## HAL Entity Coverage

| Entity | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|--------|---------|---------|---------|---------|
| Pins | `hal_pin_get/set_*` | Convert | Thread-local | String pins |
| Parameters | `hal_param_get/set_*` | Convert | Thread-local | String params |
| Signals | (via pins) | (via pins) | (via pins) | - |
| Streams | Unchanged | Unchanged | Unchanged | Unchanged |

## Open Questions

1. **Granularity of dirty tracking**: Per-pin vs per-cache-line vs per-region?
2. **String memory limits**: Maximum string length? Maximum number of string pins/params?
3. **Halcmd integration**: How to display/set strings in halcmd?
4. **Python bindings**: Extend halmodule.cc for string support?
5. **Parameter sync direction**: Should HAL_RO params sync differently than HAL_RW?

## References

- Current HAL implementation: `src/hal/hal_lib.c`, `src/hal/hal.h`
- HAL_PORT (existing streaming type): Lock-free ring buffer pattern
- HAL_STREAM: Existing FIFO for sampler/streamer components
- Thread-local storage: `__thread` keyword, POSIX thread-specific data
