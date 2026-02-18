# Thread-Local HAL Context API

## Overview

This implementation provides a thread-local HAL context API as a shim layer on top of the existing single-image HAL. It enables gradual migration of components to the new API without modifying the core HAL implementation.

## Key Benefits

1. **Thread-local data isolation**: Each thread maintains its own copy of HAL data
2. **Reduced cache contention**: Minimizes conflicts between RT threads
3. **Deterministic latency**: No mid-cycle data changes from other threads
4. **Efficient writes**: Double-buffer diff writes only changed values
5. **Fail-early errors**: Invalid usage detected immediately with clear error messages

## Architecture

### Components

```
┌─────────────────────────────────────────────────────────────┐
│ Application Code                                             │
│  - Uses handle-based API                                     │
│  - Calls sync_read/sync_write explicitly                     │
└─────────────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│ HAL Context Shim Layer (hal_ctx.c)                          │
│  - Handle registry: maps handles → shared memory            │
│  - Context buffers: thread-local before/after copies        │
│  - Sync operations: read/write with diff detection          │
└─────────────────────────────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│ Existing HAL Core (hal_lib.c)                               │
│  - Shared memory management                                  │
│  - Pin/param creation and storage                            │
│  - Signal linking                                            │
└─────────────────────────────────────────────────────────────┘
```

### State Machine

Contexts enforce proper sync sequencing through a state machine:

```
   ┌─────────┐
   │ CREATED │ ← hal_ctx_create()
   └────┬────┘
        │
        │ hal_ctx_sync_read()
        ▼
   ┌────────┐
   │  READ  │ ← Pin/param access allowed here
   └────┬───┘
        │
        │ hal_ctx_sync_write()
        ▼
   ┌─────────┐
   │ WRITTEN │ ← Must sync_read again
   └─────────┘
```

**Error Conditions:**
- Calling sync_read twice → Error
- Calling sync_write without sync_read → Error  
- Accessing pins/params outside READ state → Error

## API Usage

### 1. Create Pins/Params with Handles

Instead of pointer-based API:
```c
// Old API
hal_bit_t *my_pin;
hal_pin_bit_new("component.pin", HAL_OUT, &my_pin, comp_id);
*my_pin = 1;  // Direct shared memory access
```

Use handle-based API:
```c
// New API
hal_pin_handle_t my_pin_h;
hal_pin_bit_new_handle("component.pin", HAL_OUT, &my_pin_h, comp_id);
// Access through context (see below)
```

### 2. Create a Context

```c
hal_ctx_t *ctx = hal_ctx_create(comp_id);
if (!ctx) {
    // Handle error
}
```

### 3. Sync Read/Write Cycle

```c
while (running) {
    // 1. Sync read - copy shared memory to thread-local
    if (hal_ctx_sync_read(ctx) < 0) {
        // Handle error
    }
    
    // 2. Read input values
    hal_bit_t in = hal_ctx_pin_bit_get(ctx, input_pin_h);
    hal_float_t param = hal_ctx_param_float_get(ctx, param_h);
    
    // 3. Compute outputs
    hal_bit_t out = !in;  // Example: invert
    
    // 4. Write output values
    hal_ctx_pin_bit_set(ctx, output_pin_h, out);
    
    // 5. Sync write - diff and write changes to shared memory
    if (hal_ctx_sync_write(ctx) < 0) {
        // Handle error
    }
    
    // Sleep/wait for next cycle
    usleep(1000);
}
```

### 4. Cleanup

```c
hal_ctx_destroy(ctx);
hal_exit(comp_id);
```

## API Reference

### Context Lifecycle

- `hal_ctx_t *hal_ctx_create(int comp_id)` - Create a context
- `void hal_ctx_destroy(hal_ctx_t *ctx)` - Destroy a context

### Sync Operations

- `int hal_ctx_sync_read(hal_ctx_t *ctx)` - Copy shared → local
- `int hal_ctx_sync_write(hal_ctx_t *ctx)` - Diff and write local → shared

### Pin Creation (Handle-Based)

- `int hal_pin_bit_new_handle(...)` - Create bit pin, return handle
- `int hal_pin_float_new_handle(...)` - Create float pin, return handle
- `int hal_pin_s32_new_handle(...)` - Create s32 pin, return handle
- `int hal_pin_u32_new_handle(...)` - Create u32 pin, return handle

### Pin Access (Context-Aware)

- `hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t pin)` - Get bit value
- `void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_bit_t val)` - Set bit value
- Similar for float, s32, u32 types

### Parameter Creation (Handle-Based)

- `int hal_param_bit_new_handle(...)` - Create bit param, return handle
- `int hal_param_float_new_handle(...)` - Create float param, return handle
- `int hal_param_s32_new_handle(...)` - Create s32 param, return handle
- `int hal_param_u32_new_handle(...)` - Create u32 param, return handle

### Parameter Access (Context-Aware)

- `hal_bit_t hal_ctx_param_bit_get(hal_ctx_t *ctx, hal_param_handle_t param)` - Get bit value
- `void hal_ctx_param_bit_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_bit_t val)` - Set bit value
- Similar for float, s32, u32 types

## Implementation Details

### Handle Registry

A global `handle_registry` array maps integer handles to shared memory locations:

```c
typedef struct {
    void **shmem_ptr_addr;  // Address of pointer to shared memory
    hal_type_t type;        // Data type
    int is_param;           // 1 if parameter, 0 if pin
} handle_entry_t;
```

When a pin/param is created with `*_new_handle()`:
1. The existing `hal_pin_*_new()` or `hal_param_*_new()` is called
2. The resulting shared memory pointer is stored in the registry
3. An integer handle (index) is returned

### Context Buffers

Each context maintains two buffers:

- **before**: Snapshot at sync_read time
- **after**: Working copy, modified during cycle

Buffer layout is packed: all pins/params are stored sequentially by handle order.

### Sync Read

```
Shared Memory → before buffer
before buffer → after buffer
State = READ
```

### Sync Write

```
For each pin/param:
    if after != before:
        after → Shared Memory
State = WRITTEN
```

Only changed values are written, minimizing shared memory traffic.

## Testing

A test program is provided in `src/hal/hal_ctx_test.c`:

```bash
# Compile and run (once build system is set up):
./hal_ctx_test
```

The test verifies:
- Pin/param creation with handles
- Context lifecycle
- Sync read/write operations
- Error condition handling
- Value read/write through context

## Files

- `src/hal/hal_ctx.c` - Implementation
- `src/hal/hal_ctx_internal.h` - Internal structures
- `src/hal/hal.h` - API declarations (added)
- `src/hal/Submakefile` - Build rules (updated)
- `src/hal/hal_ctx_test.c` - Test program

## Future Work

1. **RT Thread Support**: Add automatic sync injection in RT executor
2. **Performance Optimization**: Use SIMD for buffer operations
3. **Component-Specific Contexts**: Allow filtering pins/params by component
4. **Migration Tools**: Automated conversion from old to new API
5. **Python Bindings**: Expose context API to Python components

## Design Principles

1. **Fail Early**: Invalid usage = immediate error (no silent fallback)
2. **Explicit API**: User must explicitly call sync functions
3. **Hard Errors**: sync_write without sync_read = error (compare buffer undefined)
4. **Thread Independence**: Each thread has its own context
5. **Zero Core Changes**: Shim layer only, no HAL core modifications

## Related Documentation

- `docs/src/hal/THREAD_LOCAL_HAL.md` - Full migration guide
- HAL component documentation - For general HAL concepts
