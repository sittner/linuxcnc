# Phase 2: Refactor TP Code

## Overview

**Goal**: Eliminate global variables and make all dependencies explicit through TP_STRUCT and function parameters.

**Duration**: 2-3 weeks

**Risk Level**: MODERATE

**Key Principle**: Replace implicit global dependencies with explicit dependencies passed through structures and parameters.

---

## Detailed Tasks

### Task 2.1: Replace Global Motion Pointers (3-4 days)

**Current State**:
```c
// In tp.c - GLOBALS!
static emcmot_status_t *emcmotStatus;
static emcmot_config_t *emcmotConfig;

// Used throughout:
if (emcmotStatus->stepping) { ... }
```

**Target State**:
```c
// NO globals - all in TP_STRUCT
// In tp.c functions:
if (tp->motion_status.stepping) { ... }
```

**Implementation Steps**:

1. **Update tpRunCycle() to populate motion status**:
```c
int tpRunCycle(TP_STRUCT *tp, long period) {
    // At start of cycle, update motion status from globals (if set)
    if (_compat_emcmot_status) {
        tp_motion_populate_status(tp, _compat_emcmot_status);
    }
    
    // Rest of function now uses tp->motion_status instead of emcmotStatus
    if (tp->motion_status.stepping) {
        // ... stepping mode logic
    }
    
    double feed_scale = tp->motion_status.maxFeedScale;
    // ... etc
}
```

2. **Replace ~50 access sites in tp.c**:
```bash
# Example replacements:
emcmotStatus->stepping → tp->motion_status.stepping
emcmotStatus->maxFeedScale → tp->motion_status.maxFeedScale
emcmotConfig->trajCycleTime → tp->motion_config.trajCycleTime
```

3. **Update internal functions to accept tp parameter**:
```c
// Before:
static int tpComputeBlendVelocity(TC_STRUCT *tc, ...) {
    // Uses emcmotStatus implicitly
}

// After:
static int tpComputeBlendVelocity(TP_STRUCT *tp, TC_STRUCT *tc, ...) {
    // Uses tp->motion_status explicitly
}
```

**Estimated Sites to Update**: ~50-70 in tp.c

---

### Task 2.2: Replace Global Callback Pointers (2-3 days)

**Current State**:
```c
static void (*_DioWrite)(int, char);
static void (*_AioWrite)(int, double);
// ... etc

// Usage:
_DioWrite(index, value);
```

**Target State**:
```c
// Usage:
tp->callbacks.dio_write(index, value);
```

**Implementation**:
- Update all callback sites (~20-30 locations)
- Remove static globals
- Update tpMotFunctions() to use tp_set_callbacks()

---

### Task 2.3: Update RTAPI Math/Logging Calls (4-5 days)

**Current State**:
```c
#include "rtapi_math.h"
// Direct calls:
double result = rtapi_sqrt(x);
rtapi_print_msg(RTAPI_MSG_ERR, "Error: %s", msg);
```

**Target State**:
```c
// Use platform abstraction:
double result = TP_SQRT(x);
TP_LOG_ERR("Error: %s", msg);
```

**Implementation**:
1. Add `#include "tp_platform.h"` to tp.c, tc.c, blendmath.c
2. Replace ~200 rtapi_math calls with TP_ macros
3. Replace ~50 rtapi_print_msg calls with TP_LOG_ macros
4. Ensure tp->platform is set (via compatibility layer initially)

**Search and Replace Guide**:
```bash
# Math functions:
rtapi_sin( → TP_SIN(
rtapi_cos( → TP_COS(
rtapi_sqrt( → TP_SQRT(
rtapi_fabs( → TP_FABS(
rtapi_atan2( → TP_ATAN2(

# Logging:
rtapi_print_msg(RTAPI_MSG_ERR, → TP_LOG_ERR(
rtapi_print_msg(RTAPI_MSG_WARN, → TP_LOG_WARN(
```

---

### Task 2.4: Update Initialization Functions (2 days)

**Update tpCreate()**:
```c
int tpCreate(TP_STRUCT *tp, int queueSize, int id) {
    // ... existing initialization ...
    
    // NEW: Set default platform (RTAPI for compatibility)
    tp->platform = tp_get_rtapi_platform();
    
    // NEW: Initialize callback structure to NULL
    memset(&tp->callbacks, 0, sizeof(tp_callbacks_t));
    
    // NEW: Store as default instance for compatibility
    _default_tp_instance = tp;
    
    return TP_ERR_OK;
}
```

**Keep Compatibility Functions**:
```c
// Old interface still works via compatibility layer
void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig) {
    _compat_emcmot_status = pstatus;
    _compat_emcmot_config = pconfig;
    
    if (_default_tp_instance) {
        tp_motion_set_config(_default_tp_instance, pconfig);
    }
}
```

---

### Task 2.5: Update Other TP Files (2-3 days)

**tc.c Updates**:
- Add TP_STRUCT parameter to tc functions that need it
- Replace RTAPI calls with TP_ macros
- Update ~10-20 functions

**blendmath.c Updates**:
- Replace RTAPI math calls with TP_ macros (or accept platform pointer)
- ~50-100 call sites
- May keep as-is if self-contained (evaluate during implementation)

**spherical_arc.c Updates**:
- Similar to blendmath.c
- ~20-30 call sites

---

## Phase 2 Testing & Verification

### Unit Testing (New)

**Create First Unit Tests**:
```c
// tests/tp/test_tp_basic.c
#include "greatest.h"
#include "tp.h"
#include "test_mocks.h"

TEST test_tp_with_mock_platform(void) {
    TP_STRUCT tp;
    tpCreate(&tp, 32, 0);
    
    // Override with mock platform
    tp.platform = tp_get_standard_platform();
    
    // Test that TP works with mocked platform
    EmcPose start = {0};
    EmcPose end = {100, 0, 0, 0, 0, 0, 0, 0, 0};
    
    tpSetPos(&tp, &start);
    tpAddLine(&tp, end, 0, 50.0, 100.0, 1000.0, 0xFF, 0, -1, tag);
    
    // Should work without RTAPI
    ASSERT_EQ(TP_ERR_OK, tpRunCycle(&tp, 1000000));
    PASS();
}
```

### Regression Testing

**Test Suite**:
1. Build LinuxCNC with refactored TP
2. Run all existing integration tests
3. Performance benchmarking (should be identical)
4. External module test (tpcomp.comp)

**Acceptance Criteria**:
- All tests pass
- Performance within 1% of baseline
- External modules work
- No warnings

---

## Refactoring Patterns

### Pattern 1: Global to Structure Member

**Before**:
```c
static emcmot_status_t *emcmotStatus;

void some_function(TC_STRUCT *tc) {
    if (emcmotStatus->stepping) { ... }
}
```

**After**:
```c
void some_function(TP_STRUCT *tp, TC_STRUCT *tc) {
    if (tp->motion_status.stepping) { ... }
}
```

### Pattern 2: RTAPI to Platform Abstraction

**Before**:
```c
#include "rtapi_math.h"
double x = rtapi_sqrt(value);
```

**After**:
```c
#include "tp_platform.h"
double x = TP_SQRT(value);
```

### Pattern 3: Logging

**Before**:
```c
rtapi_print_msg(RTAPI_MSG_ERR, "Error: %d", code);
```

**After**:
```c
TP_LOG_ERR("Error: %d", code);
```

---

## Success Criteria

**Phase 2 is complete when**:
- ✅ No static global pointers to motion module
- ✅ All dependencies explicit in TP_STRUCT
- ✅ RTAPI dependencies abstracted
- ✅ All existing tests pass
- ✅ Performance validated (< 1% regression)
- ✅ External modules still work
- ✅ Code reviewed and approved
- ✅ Basic unit tests created

---

## Effort Breakdown

| Task | Estimated Hours |
|------|----------------|
| 2.1: Replace global pointers | 24-32 |
| 2.2: Replace callbacks | 16-20 |
| 2.3: RTAPI abstraction | 32-40 |
| 2.4: Update initialization | 16 |
| 2.5: Other TP files | 16-24 |
| Testing & debugging | 32-48 |
| **Total** | **136-180 hours** |

**Calendar Time**: 2-3 weeks

---

## Next Steps

After Phase 2:
1. Code review
2. Merge to main branch
3. Proceed to Phase 3: Library extraction
