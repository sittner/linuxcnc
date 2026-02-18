## Summary

Updates THREAD_LOCAL_HAL.md with the finalized architecture based on extensive design discussion. This replaces the previous compare-buffer approach with a dirty bitmap design that correctly handles concurrent writes.

## Key Design Changes

### From Compare-Buffer to Dirty Bitmap

**Previous design** (flawed): Compare working_buf vs compare_buf to detect changes.
- Problem: Can't distinguish "I changed it" from "someone else changed it"
- Risk: Concurrent writes from other threads get lost

**New design** (correct): Track exactly which bytes we modified.
- Only write back bytes we explicitly changed
- Other threads' writes are preserved

### Memory Architecture

| Component | Size | Purpose |
|-----------|------|---------|
| Master allocation_bitmap | 16KB | 1 bit per 8-byte block, tracks allocated regions |
| Context working_buf | 1MB | Full HAL memory copy |
| Context dirty_bitmap | 128KB | 1 bit per byte, tracks local modifications |

### Why 1-byte Dirty Granularity (Not Cache-Line)

Coarse granularity loses concurrent writes:

```
8-byte block: [A=1.0][B=2.0]

Thread 1 context:     Thread 2 writes B in master:
  modifies A            B = 9.0
  dirty bit set

sync_write with 8-byte granularity copies entire block:
  Master: [A=5.0][B=2.0]  ← B=9.0 LOST!
```

### Precomputed Dirty Bitmap Access

To minimize pin write overhead, signal/param structs store precomputed values:

```c
uint32_t dirty_offset;    // Index into bitmap array
uint32_t dirty_mask[2];   // Handles word boundaries
```

**Cost**: ~6 instructions per pin write (2 loads, 2 ORs, 2 stores)

### Breaking Change: New Function Signature

```c
// Old
typedef void (*hal_funct_t)(void *arg, long period);

// New  
typedef void (*hal_funct_t)(void *arg, hal_ctx_t *ctx);
```

Period accessed via `hal_ctx_period(ctx)`. This is intentional - cleaner API, all thread info in context.

## Performance Analysis

### Per-Cycle Overhead (1ms thread, 50KB HAL usage)

| Platform | Total Overhead | % of Cycle |
|----------|---------------|------------|
| x86 | ~5 µs | 0.5% |
| RPi4 | ~30 µs | 3% |

Full 1MB copies would use 80%+ of cycle time on RPi4 - bitmap optimization is mandatory for ARM.

## Implementation Phases

1. **Master HAL**: Add allocation_bitmap, dirty fields to structs
2. **Context**: Implement working_buf + dirty_bitmap + sync operations  
3. **Thread Integration**: New signature, context per thread
4. **halcompile**: Generate new code patterns
5. **Migration**: counter.c first, then remaining components

## Related

- Builds on context API from previous PRs
- Required before migrating first RT component