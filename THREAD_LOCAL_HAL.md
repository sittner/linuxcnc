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
┌─────────────────────────────────────────────────────────────┐
│  1. hal_thread_sync_read()  - pull changes from master      │
│  2. ... thread work (all reads/writes to local copy) ...    │
│  3. hal_thread_sync_write() - push dirty data to master     │
└─────────────────────────────────────────────────────────────┘
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

Introduce accessor functions that wrap current implementation. This creates the abstraction layer without changing behavior.

```c
// hal_api.h - New accessor API

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

// Thread sync points (no-op in Phase 1)
static inline void hal_thread_sync_read(void);
static inline void hal_thread_sync_write(void);
```

**Phase 1 Implementation** (thin wrappers over existing behavior):

```c
static inline hal_float_t hal_pin_get_float(hal_float_t **pin) {
    return **pin;  // Direct access - same as before
}

static inline void hal_pin_set_float(hal_float_t **pin, hal_float_t value) {
    **pin = value;  // Direct access - same as before
}

static inline void hal_thread_sync_read(void) {
    // No-op in Phase 1
}

static inline void hal_thread_sync_write(void) {
    // No-op in Phase 1
}
```

### Phase 2: Convert All Components

Convert all HAL components to use the new API. This is a mechanical transformation:

**Before:**
```c
static hal_float_t *position_cmd;
static hal_float_t *position_fb;
static hal_bit_t *enable;

static void update(void *arg, long period) {
    if (*enable) {
        *position_cmd = calculate_pos();
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

static void update(void *arg, long period) {
    hal_thread_sync_read();
    
    if (hal_pin_get_bit(&enable)) {
        hal_pin_set_float(&position_cmd, calculate_pos());
        float fb = hal_pin_get_float(&position_fb);
    }
    
    hal_thread_sync_write();
}
```

**Conversion patterns:**
```
*pin              →  hal_pin_get_TYPE(&pin)
*pin = value      →  hal_pin_set_TYPE(&pin, value)
```

### Phase 3: Introduce Thread-Local HAL

Replace the API implementation with thread-local storage. Components don't change - just recompile.

**Ensuring Non-Migrated Components Fail to Compile:**

To detect components that weren't migrated, change the pin pointer type to an opaque handle:

```c
// Phase 3: Opaque handle prevents direct dereference
typedef struct {
    void *_opaque;  // Cannot dereference!
} hal_pin_handle_t;

// Change pin creation to return handle
int hal_pin_float_new(const char *name, hal_pin_dir_t dir,
    hal_pin_handle_t *handle, int comp_id);
```

Non-migrated code will fail:
```c
// Old code - COMPILER ERROR in Phase 3
**pin = value;  // Error: cannot dereference 'void *'

// Migrated code - works fine
hal_pin_set_float(pin, value);
```

**Thread-Local Implementation:**

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

static inline hal_float_t hal_pin_get_float(hal_pin_handle_t *pin) {
    // Read from thread-local copy
    return tls_instance->local_data.pins[pin->index].value.f;
}

static inline void hal_pin_set_float(hal_pin_handle_t *pin, hal_float_t value) {
    // Write to thread-local copy
    tls_instance->local_data.pins[pin->index].value.f = value;
    // Mark dirty for sync
    bitmap_set(tls_instance->dirty_bitmap, pin->dirty_index);
}

static inline void hal_thread_sync_read(void) {
    hal_sync_from_master(tls_instance);
}

static inline void hal_thread_sync_write(void) {
    hal_sync_to_master(tls_instance);
}
```

**Userspace Component Handling:**

Userspace components manage their own loop and need explicit access to thread-local initialization:

```c
// hal_init() creates the thread-local copy for userspace components
int hal_init(const char *name) {
    int comp_id = hal_init_internal(name);
    
    // Initialize thread-local instance for this component
    tls_instance = hal_thread_instance_create();
    
    return comp_id;
}

// hal_exit() cleans up thread-local copy
void hal_exit(int comp_id) {
    hal_thread_instance_destroy(tls_instance);
    tls_instance = NULL;
    
    hal_exit_internal(comp_id);
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
    
    hal_exit(comp_id);  // Cleanup thread-local copy
    return 0;
}
```

**Realtime Component Handling:**

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

With thread-local HAL in place, strings become straightforward:

```c
typedef int hal_string_t;  // Handle to string in thread-local storage

// String storage in thread-local instance
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} hal_string_local_t;

// API
int hal_pin_string_new(const char *name, hal_pin_dir_t dir,
                       hal_string_t **data_ptr_addr, int comp_id);

size_t hal_string_get(hal_string_t *str, char *dest, size_t max_len);
int hal_string_set(hal_string_t *str, const char *value);
int hal_string_setf(hal_string_t *str, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
size_t hal_string_len(hal_string_t *str);
```

**Implementation:**

```c
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

**Usage Example:**

```c
static hal_string_t *status_pin;

hal_pin_string_new("mycomp.status", HAL_OUT, &status_pin, comp_id);

// In update function
hal_string_setf(status_pin, "Temp=%.1f RPM=%d State=%s", temp, rpm, state_name);

// Reader
char buf[256];
hal_string_get(other_status_pin, buf, sizeof(buf));
```

## Synchronization Details

### Dirty Bitmap

Track which data has been modified locally:

```c
typedef struct {
    uint8_t *bitmap;
    size_t size;
} hal_dirty_bitmap_t;

// One bit per cache line (or per pin/signal for finer granularity)
#define DIRTY_BIT_GRANULARITY 64  // Cache line size

static inline void bitmap_set(uint8_t *bitmap, size_t index) {
    bitmap[index / 8] |= (1 << (index % 8));
}

static inline bool bitmap_test(uint8_t *bitmap, size_t index) {
    return bitmap[index / 8] & (1 << (index % 8));
}

static inline void bitmap_clear_all(uint8_t *bitmap, size_t size) {
    memset(bitmap, 0, size);
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

When multiple threads write the same data:

```c
// Option 1: Last write wins (simple, current behavior)
// - Just overwrite master with local dirty data
// - No conflict detection

// Option 2: Detect and warn
void hal_sync_to_master_detect_conflicts(hal_thread_instance_t *inst) {
    for_each_dirty(bit) {
        if (master_modified_since_our_read(bit)) {
            rtapi_print_msg(RTAPI_MSG_WARN,
                "HAL: Write conflict detected at offset %zu\n", bit);
        }
        // Still write, but warn
    }
}

// Option 3: Priority-based (RT threads win)
// - Track writer priority per region
// - Lower priority writes discarded if higher priority wrote
```

Recommended: Start with **Option 1** (last write wins) for simplicity, matching current HAL behavior.

## Summary

| Phase | Work | Risk | Deliverable |
|-------|------|------|-------------|
| 1 | Define API, thin wrappers | None | `hal_api.h` with accessor functions |
| 2 | Convert all components | Low | All components use new API |
| 3 | Thread-local implementation | Medium | Thread-safe HAL |
| 4 | Add strings | Low | `hal_string_*` API |

## Open Questions

1. **Granularity of dirty tracking**: Per-pin vs per-cache-line vs per-region?
2. **String memory limits**: Maximum string length? Maximum number of string pins?
3. **Halcmd integration**: How to display/set strings in halcmd?
4. **Python bindings**: Extend halmodule.cc for string support?

## References

- Current HAL implementation: `src/hal/hal_lib.c`, `src/hal/hal.h`
- HAL_PORT (existing streaming type): Lock-free ring buffer pattern
- Thread-local storage: `__thread` keyword, POSIX thread-specific data
