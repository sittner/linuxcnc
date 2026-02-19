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

## Full Migration Example: siggen.c

This section shows a **complete, real-world migration** of the `siggen.c` component from the old pointer-based API to the new handle-based context-aware API. This demonstrates the full scope of changes needed for Phase 3 migration.

### Data Structure Migration

**Before (Pointer-based):**
```c
typedef struct {
    hal_float_t *square;      /* pin: output */
    hal_float_t *sawtooth;    /* pin: output */
    hal_float_t *triangle;    /* pin: output */
    hal_float_t *sine;        /* pin: output */
    hal_float_t *cosine;      /* pin: output */
    hal_bit_t *clock;         /* pin: output */
    hal_float_t *frequency;   /* pin: frequency */
    hal_float_t *amplitude;   /* pin: amplitude */
    hal_float_t *offset;      /* pin: offset */
    hal_bit_t *reset;         /* pin: reset */
    double index;             /* position within output cycle */
} hal_siggen_t;
```

**After (Handle-based):**
```c
typedef struct {
    hal_pin_handle_t square;      /* pin handle: output */
    hal_pin_handle_t sawtooth;    /* pin handle: output */
    hal_pin_handle_t triangle;    /* pin handle: output */
    hal_pin_handle_t sine;        /* pin handle: output */
    hal_pin_handle_t cosine;      /* pin handle: output */
    hal_pin_handle_t clock;       /* pin handle: output */
    hal_pin_handle_t frequency;   /* pin handle: frequency */
    hal_pin_handle_t amplitude;   /* pin handle: amplitude */
    hal_pin_handle_t offset;      /* pin handle: offset */
    hal_pin_handle_t reset;       /* pin handle: reset */
    double index;                 /* position within output cycle */
} hal_siggen_t;
```

**Key Changes:**
- All `hal_float_t *` and `hal_bit_t *` changed to `hal_pin_handle_t`
- Comments updated to indicate "pin handle" instead of "pin"
- Local data members (like `index`) remain unchanged

### Realtime Function Migration

**Before (Pointer-based with period parameter):**
```c
static void calc_siggen(void *arg, long period)
{
    hal_siggen_t *siggen;
    double tmp1, tmp2;

    siggen = arg;
    tmp1 = period * 0.000000001;

    /* Access pins directly through pointers */
    tmp2 = *(siggen->frequency) * tmp1;
    
    /* Limit frequency */
    if (tmp2 > 0.5) {
        *(siggen->frequency) = 0.5 / tmp1;  /* Direct write to input pin */
        tmp2 = 0.5;
    }
    
    /* Check reset */
    if (*(siggen->reset)) {
        siggen->index = 0.5;
    } else {
        siggen->index += tmp2;
    }
    
    /* ... calculations ... */
    
    /* Write outputs directly */
    *(siggen->square) = (tmp1 * *(siggen->amplitude)) + *(siggen->offset);
    *(siggen->clock) = clock_val;
    /* ... more outputs ... */
}
```

**After (Handle-based with context parameter):**
```c
static void calc_siggen(void *arg, hal_ctx_t *ctx)
{
    hal_siggen_t *siggen;
    double tmp1, tmp2;
    hal_float_t frequency, amplitude, offset;
    hal_bit_t reset;
    hal_bit_t clock_val;

    siggen = arg;
    
    /* Get period from context */
    long period = hal_ctx_period(ctx);
    tmp1 = period * 0.000000001;

    /* Read all input pins once at start using context-aware getters */
    frequency = hal_ctx_pin_float_get(ctx, siggen->frequency);
    amplitude = hal_ctx_pin_float_get(ctx, siggen->amplitude);
    offset = hal_ctx_pin_float_get(ctx, siggen->offset);
    reset = hal_ctx_pin_bit_get(ctx, siggen->reset);

    /* Limit frequency calculation to comply with Nyquist limit */
    tmp2 = frequency * tmp1;
    if (tmp2 > 0.5) {
        /* Just clamp the calculation result - no write-back to input pin */
        tmp2 = 0.5;
    }
    
    /* Check reset */
    if (reset) {
        siggen->index = 0.5;
    } else {
        siggen->index += tmp2;
    }
    
    /* ... calculations ... */
    
    /* Write outputs using context-aware setters */
    hal_ctx_pin_float_set(ctx, siggen->square, (tmp1 * amplitude) + offset);
    hal_ctx_pin_bit_set(ctx, siggen->clock, clock_val);
    /* ... more outputs ... */
}
```

**Key Changes:**
1. **Function signature**: `(void *arg, long period)` → `(void *arg, hal_ctx_t *ctx)`
2. **Period access**: Add `long period = hal_ctx_period(ctx);` to get period from context
3. **Input pins**: Read once at start into local variables using `hal_ctx_pin_float_get()` and `hal_ctx_pin_bit_get()`
4. **Intermediate calculations**: Use local variables instead of writing back to input pins
5. **Output pins**: Write using `hal_ctx_pin_float_set()` and `hal_ctx_pin_bit_set()`

### Pin Creation Migration

**Before (Format string API):**
```c
static int export_siggen(int num, hal_siggen_t *addr, char *prefix)
{
    int retval;
    char buf[HAL_NAME_LEN + 1];

    /* Create pins with format strings */
    retval = hal_pin_float_newf(HAL_OUT, &(addr->square), comp_id,
                                "%s.square", prefix);
    if (retval != 0) {
        return retval;
    }
    
    retval = hal_pin_bit_newf(HAL_IN, &(addr->reset), comp_id,
                              "%s.reset", prefix);
    if (retval != 0) {
        return retval;
    }
    
    /* Initialize pins directly */
    *(addr->square) = 0.0;
    *(addr->frequency) = 1.0;
    *(addr->amplitude) = 1.0;
    *(addr->offset) = 0.0;
    addr->index = 0.0;
    
    /* ... export function ... */
}
```

**After (Handle-based API):**
```c
static int export_siggen(int num, hal_siggen_t *addr, char *prefix)
{
    int retval;
    char buf[HAL_NAME_LEN + 1];

    /* Create pins with handle-based API - construct name first */
    rtapi_snprintf(buf, sizeof(buf), "%s.square", prefix);
    retval = hal_pin_float_new_handle(buf, HAL_OUT, &(addr->square), comp_id);
    if (retval != 0) {
        return retval;
    }
    
    rtapi_snprintf(buf, sizeof(buf), "%s.reset", prefix);
    retval = hal_pin_bit_new_handle(buf, HAL_IN, &(addr->reset), comp_id);
    if (retval != 0) {
        return retval;
    }
    
    /* Initialize only local data - pins are initialized by HAL */
    addr->index = 0.0;
    
    /* ... export function ... */
}
```

**Key Changes:**
1. **Pin creation**: `hal_pin_*_newf()` → `hal_pin_*_new_handle()`
2. **Name construction**: Use `rtapi_snprintf()` to build name string first, then pass to `hal_pin_*_new_handle()`
3. **Pin initialization**: Remove all pin value initialization (e.g., `*(addr->square) = 0.0;`) - HAL handles this internally
4. **Keep local initialization**: Still initialize local data members (e.g., `addr->index = 0.0;`)

### Migration Gotchas

1. **Read inputs once**: For efficiency and correctness, read input pins once at the start of the function into local variables. This avoids multiple context lookups and ensures consistent values throughout the function.

2. **No direct writes to inputs**: In the old API, you could write to input pins (even though it's not good practice). In the new API, this doesn't make sense - use local variables for intermediate calculations.
   
   **Example from siggen.c**: The old code wrote back to the frequency input pin to clamp it:
   ```c
   /* Old API - wrote back to input pin */
   if (tmp2 > 0.5) {
       *(siggen->frequency) = 0.5 / tmp1;  /* Modify input pin */
       tmp2 = 0.5;
   }
   ```
   
   In the new API, we can't write to input pins, so we just clamp the calculation:
   ```c
   /* New API - just clamp the calculation */
   if (tmp2 > 0.5) {
       tmp2 = 0.5;  /* Only modify local calculation */
   }
   ```

3. **Pin names are strings, not format strings**: The `hal_pin_*_new_handle()` functions take a complete name string (not a format string like the old `hal_pin_*_newf()` functions which accepted printf-style variadic arguments). You must construct the complete pin name using `rtapi_snprintf()` first, then pass it to the creation function.
   
   ```c
   /* Old API - format string with variadic args */
   hal_pin_float_newf(HAL_OUT, &pin, comp_id, "%s.output-%d", prefix, index);
   
   /* New API - pre-formatted string */
   char name[HAL_NAME_LEN + 1];
   rtapi_snprintf(name, sizeof(name), "%s.output-%d", prefix, index);
   hal_pin_float_new_handle(name, HAL_OUT, &handle, comp_id);
   ```

4. **Don't initialize pin values**: The handle-based API manages pin initialization internally. Only initialize local data members in your structure.

5. **Handle type is opaque**: Treat `hal_pin_handle_t` as an opaque type. Don't try to inspect or manipulate its internal structure.

## Handle-Based Pin Creation Patterns

When migrating to the handle-based API, follow these patterns:

### Pattern 1: Simple Pin Creation

```c
char name[HAL_NAME_LEN + 1];
hal_pin_handle_t my_pin;

rtapi_snprintf(name, sizeof(name), "component.pin");
retval = hal_pin_float_new_handle(name, HAL_OUT, &my_pin, comp_id);
if (retval != 0) {
    return retval;
}
```

### Pattern 2: Pin Creation with Prefix

```c
char name[HAL_NAME_LEN + 1];
hal_pin_handle_t my_pin;

rtapi_snprintf(name, sizeof(name), "%s.output", prefix);
retval = hal_pin_float_new_handle(name, HAL_OUT, &my_pin, comp_id);
if (retval != 0) {
    return retval;
}
```

### Pattern 3: Multiple Pins in Loop

```c
char name[HAL_NAME_LEN + 1];

for (int i = 0; i < count; i++) {
    rtapi_snprintf(name, sizeof(name), "component.input-%d", i);
    retval = hal_pin_float_new_handle(name, HAL_IN, &(data[i].input), comp_id);
    if (retval != 0) {
        return retval;
    }
}
```

## Pin Access Patterns in RT Functions

### Pattern 1: Read All Inputs First

Best practice: Read all input pins at the start into local variables.

```c
static void my_function(void *arg, hal_ctx_t *ctx)
{
    my_data_t *data = arg;
    
    /* Read all inputs */
    hal_float_t input1 = hal_ctx_pin_float_get(ctx, data->input1);
    hal_float_t input2 = hal_ctx_pin_float_get(ctx, data->input2);
    hal_bit_t enable = hal_ctx_pin_bit_get(ctx, data->enable);
    
    /* Do calculations with local variables */
    hal_float_t result = input1 * input2;
    
    /* Write outputs */
    hal_ctx_pin_float_set(ctx, data->output, result);
}
```

### Pattern 2: Conditional Output Writing

```c
static void my_function(void *arg, hal_ctx_t *ctx)
{
    my_data_t *data = arg;
    hal_bit_t enable = hal_ctx_pin_bit_get(ctx, data->enable);
    
    if (enable) {
        hal_float_t input = hal_ctx_pin_float_get(ctx, data->input);
        hal_ctx_pin_float_set(ctx, data->output, input * 2.0);
    } else {
        hal_ctx_pin_float_set(ctx, data->output, 0.0);
    }
}
```

### Pattern 3: Using Period from Context

```c
static void my_function(void *arg, hal_ctx_t *ctx)
{
    my_data_t *data = arg;
    
    /* Get period for time-based calculations */
    long period = hal_ctx_period(ctx);
    double dt = period * 0.000000001;  /* Convert ns to seconds */
    
    hal_float_t velocity = hal_ctx_pin_float_get(ctx, data->velocity);
    
    /* Integrate velocity to position */
    data->position += velocity * dt;
    
    hal_ctx_pin_float_set(ctx, data->position_out, data->position);
}
```

## Migration Progress

### Fully Migrated (Handle-Based Context-Aware API)

Components that have completed **full Phase 3 migration** (both function signature AND handle-based pin access):

- ✅ `siggen.c` - Signal generator component (reference implementation)

### Pending Migration

Components that still need Phase 3 migration:

- Core motion components (`motmod`, `tpmod`, `homemod`)
- Kinematics modules (`trivkins`, `genserkins`, etc.)
- Other HAL components in `src/hal/components/`
- Driver components in `src/hal/drivers/`

**Note**: Components may be at different migration stages:
- **Stage 0**: Not yet migrated (still using old `long period` parameter and pointer-based pins)
- **Stage 1**: Function signature updated to `(void *arg, hal_ctx_t *ctx)` but still using pointer-based pin access (data structure has `hal_float_t *` fields, accessed via `*(data->pin)`)
- **Stage 2**: Fully migrated to handle-based API (data structure has `hal_pin_handle_t` fields, accessed via `hal_ctx_pin_*_get/set()`) - like `siggen.c`

The goal is to eventually migrate all components to Stage 2 for optimal performance with thread-local contexts.

## Backward Compatibility

**This is a breaking change.** Components compiled before Phase 3 will NOT work with Phase 3 executors. All components must be recompiled with the new signature.

The migration is mechanical and straightforward - just update the function signature and get the period from context if needed.
