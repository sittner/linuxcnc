# Phase 3 Migration Guide: Function Signature Changes

## Overview

Phase 3 introduces a **BREAKING CHANGE** to all HAL realtime function signatures. Functions now receive a `hal_ctx_t *` context parameter instead of a `long` period parameter.

## Function Signature Change

### Before (Phase 2 and earlier)
```c
static void my_component_function(void *arg, long period)
{
    my_data_t *data = (my_data_t *)arg;
    
    // Use period directly
    float dt = period * 0.000000001;  // Convert ns to seconds
    
    // Access pins/params using direct pointers
    float input = *(data->input_pin);
    *(data->output_pin) = input * 2.0;
}
```

### After (Phase 3)
```c
static void my_component_function(void *arg, hal_ctx_t *ctx)
{
    my_data_t *data = (my_data_t *)arg;
    
    // Get period from context
    long period = hal_ctx_period(ctx);
    float dt = period * 0.000000001;  // Convert ns to seconds
    
    // Access pins/params using direct pointers (unchanged)
    float input = *(data->input_pin);
    *(data->output_pin) = input * 2.0;
    
    // NEW: Can also access context metadata
    unsigned long iteration = hal_ctx_iteration(ctx);
    unsigned long overruns = hal_ctx_overruns(ctx);
}
```

## What Changed

1. **Second parameter**: Changed from `long period` to `hal_ctx_t *ctx`
2. **Period access**: Get period using `hal_ctx_period(ctx)` instead of using the parameter directly
3. **New capabilities**: Can access iteration count and overrun count from context

## What Didn't Change

- Pin and parameter access using pointers remains the same
- The `void *arg` first parameter is unchanged
- Function registration with `hal_export_funct()` is unchanged

## Context API Functions

When your function receives `hal_ctx_t *ctx`, you can use:

| Function | Returns | Description |
|----------|---------|-------------|
| `hal_ctx_period(ctx)` | `long` | Thread period in nanoseconds |
| `hal_ctx_iteration(ctx)` | `unsigned long` | Iteration count (how many times thread has run) |
| `hal_ctx_overruns(ctx)` | `unsigned long` | Number of timing overruns |

## Migration Steps

For each realtime component:

1. Update the function signature from `(void *arg, long period)` to `(void *arg, hal_ctx_t *ctx)`
2. If the function uses `period`, add `long period = hal_ctx_period(ctx);` at the beginning
3. Recompile the component

## Example: Complete Component

```c
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"

/* Component data structure */
typedef struct {
    hal_float_t *input;
    hal_float_t *output;
    hal_float_t *gain;
} gain_component_t;

static gain_component_t *data;
static int comp_id;

/* Realtime function - UPDATED SIGNATURE */
static void gain_function(void *arg, hal_ctx_t *ctx)
{
    gain_component_t *d = (gain_component_t *)arg;
    
    /* Get period if needed */
    long period = hal_ctx_period(ctx);
    
    /* Access pins using pointers (unchanged) */
    float in = *(d->input);
    float g = *(d->gain);
    *(d->output) = in * g;
    
    /* Optional: log every 1000 iterations */
    if (hal_ctx_iteration(ctx) % 1000 == 0) {
        rtapi_print("Iteration %lu, period %ld ns\n", 
                    hal_ctx_iteration(ctx), period);
    }
}

int rtapi_app_main(void)
{
    comp_id = hal_init("gain");
    if (comp_id < 0) return -1;
    
    data = hal_malloc(sizeof(gain_component_t));
    if (data == NULL) {
        hal_exit(comp_id);
        return -1;
    }
    
    /* Create pins */
    if (hal_pin_float_new("gain.input", HAL_IN, &(data->input), comp_id) != 0) {
        hal_exit(comp_id);
        return -1;
    }
    if (hal_pin_float_new("gain.output", HAL_OUT, &(data->output), comp_id) != 0) {
        hal_exit(comp_id);
        return -1;
    }
    if (hal_pin_float_new("gain.gain", HAL_IO, &(data->gain), comp_id) != 0) {
        hal_exit(comp_id);
        return -1;
    }
    
    /* Export function - registration is unchanged */
    if (hal_export_funct("gain.funct", gain_function, data, 1, 0, comp_id) != 0) {
        hal_exit(comp_id);
        return -1;
    }
    
    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}
```

## Thread-Local Context Benefits

With Phase 3, each thread has its own context that provides:

1. **Automatic sync**: Thread executor calls `hal_ctx_sync_read()` before running functions and `hal_ctx_sync_write()` after
2. **Isolation**: Each thread works with its own copy of HAL data
3. **Determinism**: Reduced cache contention and memory barrier overhead
4. **Metadata**: Access to iteration count and overrun tracking

## Advanced: Handle-Based API

For NEW components, consider using the handle-based API for better performance with thread-local contexts:

```c
static void advanced_function(void *arg, hal_ctx_t *ctx)
{
    hal_pin_handle_t input_h = /* ... */;
    hal_pin_handle_t output_h = /* ... */;
    
    /* Access pins through context using handles */
    float input = hal_ctx_pin_float_get(ctx, input_h);
    hal_ctx_pin_float_set(ctx, output_h, input * 2.0);
}
```

See `src/hal/HAL_CONTEXT_API.md` for details on the handle-based API.

## Backward Compatibility

**This is a breaking change.** Components compiled before Phase 3 will NOT work with Phase 3 executors. All components must be recompiled with the new signature.

The migration is mechanical and straightforward - just update the function signature and get the period from context if needed.
