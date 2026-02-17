# Thread-Local HAL Data Architecture

## Overview

This document describes the thread-local HAL data copy mechanism for improved
real-time determinism. The key insight is that **no changes to pin/param access
syntax are required** - only sync function calls at thread boundaries.

## Problem Statement

Current HAL accesses shared memory directly, which can cause:
- Cache contention between RT threads
- Non-deterministic latency from memory barriers
- Potential for mid-cycle data changes

## Solution: Double-Buffer Diff

Each RT thread maintains two local copies of HAL data:
- **before**: Snapshot at start of cycle (sync_read)
- **after**: Working copy modified during execution

At cycle end, sync_write compares before/after and writes only changed values
back to shared memory.

## Key Design Decisions

### No Accessor Functions Needed

Existing pin/param access syntax works unchanged:
```c
// These work exactly as before:
*(pin->output) = value;        // Write pin
value = *(pin->input);         // Read pin  
param->scale = 1.5;            // Write param
value = param->offset;         // Read param
```

The pointers are redirected to thread-local buffers transparently.

### Sync Functions Only

The only API addition is two sync functions:
```c
void hal_thread_sync_read(void);   // shared → local (start of cycle)
void hal_thread_sync_write(void);  // local → shared (end of cycle, diff only)
```

### RT Components: Zero Code Changes

RT components require **no changes**. The RT thread executor calls sync
functions automatically:

```c
// In RT thread executor (hal_lib.c)
void execute_thread(hal_thread_t *thread) {
    hal_thread_sync_read();      // Added once here
    
    for_each_function(f) {
        f->funct(f->arg, period); // Component code UNCHANGED
    }
    
    hal_thread_sync_write();     // Added once here
}
```

### Userspace Components: Add Sync Calls

Userspace components with their own main loop need sync calls added:

```c
// Before (unchanged logic):
while (running) {
    hal_thread_sync_read();      // ADD THIS
    
    // ... existing code unchanged ...
    *(out->value) = process(*(in->value));
    
    hal_thread_sync_write();     // ADD THIS
    usleep(period);
}
```

### halcompile: Automatic for .comp Files

The halcompile tool will generate sync calls in the userspace wrapper,
so .comp files require **no changes**.

### Python/Go Bindings: Transparent

Bindings integrate sync into their read/write layer - user code unchanged.

## Implementation Phases

### Phase 1: API Foundation (Current)
- Add `hal_thread_sync_read()` / `hal_thread_sync_write()` declarations to hal.h
- Implement as no-ops (stubs)
- Update documentation

### Phase 2: Userspace Migration
- Modify halcompile to generate sync calls for userspace .comp
- Update Python halmodule.cc to integrate sync
- Update Go hal-go bindings
- Manually add sync to userspace C components

### Phase 3: Full Implementation  
- Implement thread-local buffer allocation
- Implement double-buffer diff sync logic
- Integrate into RT thread executor
- Performance testing and optimization

## Userspace Component Migration

### Components Using halcompile (.comp files)
**No changes required** - halcompile generates sync calls.

### Components with Manual Main Loop
Add sync calls to main loop. Known components requiring migration:

| Component | File | Status |
|-----------|------|--------|
| shuttle | `src/hal/user_comps/shuttle.c` | Pending |
| mb2hal | `src/hal/user_comps/mb2hal/` | Pending |
| xhc-hb04 | `src/hal/user_comps/xhc-hb04.cc` | Pending |
| VFD drivers | `src/hal/user_comps/*_vfd.c` | Pending |

### Python Components
**No changes required** - handled by halmodule.cc.

### Go Components  
**No changes required** - handled by hal-go bindings.

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

## Language Bindings

### halcompile (.comp files)
Modify `src/hal/utils/halcompile.g` to generate sync calls in userspace
component wrappers. RT components get sync from executor automatically.

### Python (halmodule.cc)
Integrate sync into `pyhal_read_common()` / `pyhal_write_common()` in
`src/hal/halmodule.cc`. User code unchanged.

### Go (hal-go)
Add sync calls to the Go component's internal loop handling.
User code unchanged.

## Future Considerations: String Support

HAL currently supports numeric types only. String support options for future:

### Option A: HAL String Pin Type
```c
typedef char hal_string_t[HAL_STRING_LEN];
// Fixed-size buffer, sync'd like other pin types
```

### Option B: String Stream
Use HAL's existing `hal_stream` mechanism for string data.

### Option C: External String Storage  
Strings stored outside HAL, pins hold references/IDs only.

Decision deferred until after thread-local implementation is complete.

## Files Changed

| File | Change |
|------|--------|
| `src/hal/hal.h` | Add sync function declarations |
| `src/hal/hal_lib.c` | Add sync function stub implementations |
| `src/hal/hal_api.h` | **REMOVED** (accessor macros not needed) |
| `THREAD_LOCAL_HAL.md` | Complete rewrite (this document) |
