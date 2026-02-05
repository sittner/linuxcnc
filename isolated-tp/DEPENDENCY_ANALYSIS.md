# TP Dependency Analysis

## Overview

This document provides a comprehensive analysis of all dependencies in the LinuxCNC trajectory planner (TP) code. Understanding these dependencies is critical for successful isolation of the TP into a standalone library.

---

## Core TP Files

The trajectory planner consists of 6 core C files and their headers:

| File | LOC | Purpose | Self-Contained? |
|------|-----|---------|----------------|
| `tp.c` | 4,172 | Main trajectory planner logic | No - many dependencies |
| `tc.c` | 918 | Trajectory component (segment) implementation | No - moderate dependencies |
| `tcq.c` | 354 | Trajectory component queue | Mostly - minimal dependencies |
| `blendmath.c` | 1,860 | Blend calculation algorithms | Mostly - self-contained math |
| `spherical_arc.c` | 201 | Spherical arc calculations | Mostly - self-contained math |
| `tpmod.c` | 47 | Module interface stub | Minimal - just interface |
| **Total** | **7,552** | | |

### Header Files

| Header | Purpose | Complexity |
|--------|---------|------------|
| `tp.h` | Public TP API | Moderate |
| `tp_types.h` | TP data structures | High - many type deps |
| `tc.h` | TC API | Moderate |
| `tc_types.h` | TC data structures | Moderate |
| `tcq.h` | Queue API | Low |
| `blendmath.h` | Blend math API | Low |
| `spherical_arc.h` | Arc math API | Low |
| `tp_debug.h` | Debug macros | Low |

---

## Current Dependency Graph

```
┌─────────────────────────────────────────────────────────────┐
│                      LinuxCNC Motion Module                 │
│                                                              │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   │
│  │ motion.h     │   │ axis.h       │   │ motion_types.h│   │
│  └──────┬───────┘   └──────┬───────┘   └──────┬───────┘   │
│         │                  │                   │            │
│         └──────────────────┼───────────────────┘            │
│                            │                                │
└────────────────────────────┼────────────────────────────────┘
                             │
                    Global Pointers
                  (emcmotStatus, etc.)
                             │
┌────────────────────────────┼────────────────────────────────┐
│                Trajectory Planner (TP)                       │
│                                                              │
│  ┌──────────────┐         │                                 │
│  │   tp.c       │◄────────┘                                 │
│  │  (main)      │                                           │
│  └──┬─────────┬─┘                                           │
│     │         │                                              │
│     │         └────────┐                                     │
│     │                  │                                     │
│  ┌──▼─────┐   ┌───────▼─────┐   ┌──────────┐              │
│  │  tc.c  │   │ blendmath.c │   │  tcq.c   │              │
│  └────────┘   └─────────────┘   └──────────┘              │
│                                                              │
│  Dependencies:                                               │
│  • RTAPI (rtapi.h, rtapi_math.h, rtapi_bool.h)             │
│  • Motion module types (emcpose.h, emcmotcfg.h)            │
│  • State tracking (state_tag.h)                             │
│  • Callback interface (axis.h)                              │
└──────────────────────────────────────────────────────────────┘
                             │
                             │ Uses (already standalone)
                             │
                    ┌────────▼────────┐
                    │   Posemath      │
                    │   Library       │
                    │  (geometry)     │
                    └─────────────────┘
```

---

## Dependency Categories

## 1. RTAPI Coupling (HIGH IMPACT)

### Severity: HIGH
### Isolation Complexity: EASY
### Impact on All Files: YES

### Description
All TP files include RTAPI headers for basic functionality:

**Files Affected**: All TP files (tp.c, tc.c, tcq.c, blendmath.c, spherical_arc.c)

### Specific Dependencies

#### rtapi.h
**Usage**: Logging and printing
```c
#include "rtapi.h"

// Used throughout tp.c:
rtapi_print_msg(RTAPI_MSG_ERR, "TP: error message");
rtapi_print_msg(RTAPI_MSG_WARN, "TP: warning");
rtapi_print_msg(RTAPI_MSG_DBG, "TP: debug info");
```

**Frequency**: ~50+ calls across tp.c, ~10 in tc.c

**Isolation Strategy**: 
- Create `tp_platform.h` with logging callbacks
- Provide RTAPI implementation and mock implementation
- Replace all `rtapi_print_msg()` calls with macros

#### rtapi_math.h  
**Usage**: Mathematical functions
```c
#include "rtapi_math.h"

// Used extensively in blendmath.c, tp.c, tc.c:
rtapi_sqrt(x)
rtapi_fabs(x)
rtapi_sin(x)
rtapi_cos(x)
rtapi_atan2(y, x)
rtapi_acos(x)
rtapi_asin(x)
rtapi_pow(x, y)
```

**Frequency**: 100+ calls across all TP files

**Why RTAPI Math?**:
- Real-time friendly (may avoid some exceptions)
- Consistent across platforms
- For our purposes: can use standard math.h in tests

**Isolation Strategy**:
- Provide function pointers in `tp_platform_config_t`
- For RTAPI: point to rtapi_math functions
- For testing: point to standard library math functions
- Use macros for convenience: `TP_SQRT(x)` instead of `sqrt(x)`

**Example Abstraction**:
```c
// tp_platform.h
typedef struct {
    double (*sin)(double);
    double (*cos)(double);
    double (*sqrt)(double);
    // ... etc
} tp_platform_config_t;

#define TP_SQRT(x) (tp_platform->sqrt(x))

// For RTAPI build:
platform.sqrt = rtapi_sqrt;

// For standard build:
platform.sqrt = sqrt;
```

#### rtapi_bool.h
**Usage**: Boolean type definition
```c
#include <rtapi_bool.h>

bool some_flag;
```

**Frequency**: Used throughout for boolean variables

**Isolation Strategy**: 
- Use C99 `<stdbool.h>` directly in library
- Provide typedef in header if needed for compatibility

#### rtapi_limits.h
**Usage**: Numeric limits
```c
#include "rtapi_limits.h"

// Uses DBL_MAX, INT_MIN, etc.
```

**Isolation Strategy**:
- Use standard C `<limits.h>` and `<float.h>`
- These are portable across all C99 compilers

#### stdio.h
**Usage**: Standard I/O (included in tp.c)
```c
#include <stdio.h>
```

**Frequency**: Currently included but usage is minimal/conditional

**Isolation Strategy**:
- Verify actual usage in the codebase
- If only for debugging, can be removed or made conditional
- If needed, standard C library is acceptable for isolated TP

### Optional HAL Pin Support

**Status**: Currently disabled by default

**Code** (in tp.c):
```c
#define MAKE_TP_HAL_PINS
#undef  MAKE_TP_HAL_PINS  // Currently disabled

#ifdef MAKE_TP_HAL_PINS
#include "hal.h"
// ... hal_pin_u32_newf, etc.
#endif
```

**Description**: TP has provisions for creating HAL pins for debugging/monitoring, but this feature is currently disabled. When enabled, it would create additional dependencies on the HAL library.

**Isolation Strategy**:
- Keep as optional compile-time feature
- Document as conditional dependency
- For isolated TP: provide callback interface for HAL pin creation
- Default implementation: no-op stubs when HAL not available

**Impact**: LOW (currently disabled, can remain optional)

### Isolation Approach

**Phase 1**: Create abstraction layer
- Define `tp_platform.h` with function pointers for math and logging
- Create RTAPI implementation that wraps existing functions
- No changes to TP algorithm code yet

**Phase 2**: Update TP code to use abstraction
- Replace `rtapi_sqrt()` with `TP_SQRT()` macro
- Replace `rtapi_print_msg()` with `TP_LOG_ERR()` etc.
- ~200-300 call sites to update

**Effort Estimate**: 3-5 days for abstraction + updates

---

## 2. Motion Module Coupling (CRITICAL)

### Severity: CRITICAL  
### Isolation Complexity: MODERATE
### Impact: Primarily tp.c

### Description
TP directly accesses motion module data structures through global pointers. This is the most significant coupling.

### Current Architecture

**Global Pointers** (in tp.c):
```c
static emcmot_status_t *emcmotStatus;  // Motion controller status
static emcmot_config_t *emcmotConfig;  // Motion controller config
emcmot_command_t *emcmotCommand;       // Motion controller command (declared but usage needs verification)
emcmot_hal_data_t *emcmot_hal_data;    // Motion controller HAL data (declared but usage needs verification)

// Set via:
void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig) {
    emcmotStatus = pstatus;
    emcmotConfig = pconfig;
}
```

**Note:** `emcmotCommand` and `emcmot_hal_data` are declared as global pointers in tp.c but their actual usage throughout the codebase needs verification. They may be unused or accessed only in conditional compilation paths.

### What TP Reads from Motion Module

**From emcmotStatus** (~20 fields used):
```c
// Stepping mode
if (emcmotStatus->stepping) { ... }

// Feed override
double scale = emcmotStatus->maxFeedScale;

// Current position
EmcPose pos = emcmotStatus->carte_pos_cmd;

// Spindle status
double spindle_speed = emcmotStatus->spindle[spindle].speed;
bool at_speed = emcmotStatus->spindle[spindle].at_speed;
double revs = emcmotStatus->spindle[spindle].revs;

// Limits and flags
bool on_limit = emcmotStatus->on_soft_limit;

// And others...
```

**From emcmotConfig** (~10 fields used):
```c
// Cycle time
double cycle_time = emcmotConfig->trajCycleTime;

// Number of joints
int num_joints = emcmotConfig->numJoints;

// Kinematics type
int kins_type = emcmotConfig->kinematics_type;

// And others...
```

### Usage Patterns

**In tpRunCycle()** (main loop):
```c
int tpRunCycle(TP_STRUCT * const tp, long period) {
    // Reads motion status extensively
    if (emcmotStatus->stepping) {
        // Different behavior in stepping mode
    }
    
    double feed_scale = emcmotStatus->maxFeedScale;
    // Apply feed override to velocities
    
    // Spindle synchronization
    if (tc->sync_type == TC_SYNC_POSITION) {
        double spindle_pos = emcmotStatus->spindle[spindle].revs;
        // Calculate synchronized motion
    }
}
```

**Frequency**: 50+ direct accesses to emcmotStatus in tp.c

### Current Initialization

**Setup** (called from motion module):
```c
// motion/control.c
tpMotData(&emcmotStatus, &emcmotConfig);
```

This stores global pointers that TP uses throughout execution.

### Isolation Strategy

**Step 1**: Define explicit interface in `tp_motion_interface.h`:
```c
// What TP needs from motion controller
typedef struct {
    // Status (read each cycle)
    bool stepping;
    double maxFeedScale;
    EmcPose carte_pos_cmd;
    
    // Spindle (per spindle)
    struct {
        double speed;
        double revs;
        bool at_speed;
        bool index_enable;
    } spindle[EMCMOT_MAX_SPINDLES];
    
    bool on_soft_limit;
} tp_motion_status_t;

typedef struct {
    // Configuration (read once at setup)
    double trajCycleTime;
    int numJoints;
    int kinematics_type;
} tp_motion_config_t;
```

**Step 2**: Add to TP_STRUCT:
```c
typedef struct {
    // ... existing fields ...
    
    tp_motion_status_t motion_status;  // Updated each cycle
    tp_motion_config_t motion_config;  // Set at initialization
} TP_STRUCT;
```

**Step 3**: Create adapter functions:
```c
// In tp.c or motion module
void tp_motion_get_status(TP_STRUCT *tp) {
    // Extract from emcmotStatus to tp->motion_status
    tp->motion_status.stepping = emcmotStatus->stepping;
    tp->motion_status.maxFeedScale = emcmotStatus->maxFeedScale;
    // ... etc
}

void tp_motion_set_config(TP_STRUCT *tp) {
    tp->motion_config.trajCycleTime = emcmotConfig->trajCycleTime;
    // ... etc
}
```

**Step 4**: Update TP code:
```c
// Before:
if (emcmotStatus->stepping) { ... }

// After:
if (tp->motion_status.stepping) { ... }
```

**Step 5**: Call adapter at start of tpRunCycle():
```c
int tpRunCycle(TP_STRUCT *tp, long period) {
    // First, refresh motion status for this cycle
    tp_motion_get_status(tp);
    
    // Now all accesses are through tp->motion_status
    if (tp->motion_status.stepping) { ... }
}
```

### Benefits of This Approach

1. **Explicit dependencies**: Clear what data TP needs
2. **Testable**: Can populate tp_motion_status_t with test data
3. **Thread-safe**: Each TP instance has its own copy
4. **Minimal overhead**: One copy per cycle (negligible)
5. **Backward compatible**: Motion module still works the same way

### Effort Estimate
- Define interfaces: 1 day
- Create adapters: 1 day  
- Update ~50 access sites in tp.c: 2-3 days
- Testing: 2 days
- **Total**: 6-7 days

---

## 3. Callback Function Pointers (MODERATE)

### Severity: MODERATE
### Isolation Complexity: LOW (already well abstracted!)
### Impact: tp.c, tc.c

### Description
TP uses function pointers to call back into motion module and axis code. This is actually a good design that's already mostly decoupled.

### Current Interface

**Callback Function Pointers** (tp.c):
```c
// Global function pointers
static void   (*_DioWrite)(int, char);
static void   (*_AioWrite)(int, double);
static void   (*_SetRotaryUnlock)(int, int);
static int    (*_GetRotaryIsUnlocked)(int);
static double (*_axis_get_vel_limit)(int);
static double (*_axis_get_acc_limit)(int);

// Initialization function
void tpMotFunctions(
    void   (*pDioWrite)(int, char),
    void   (*pAioWrite)(int, double),
    void   (*pSetRotaryUnlock)(int, int),
    int    (*pGetRotaryIsUnlocked)(int),
    double (*paxis_get_vel_limit)(int),
    double (*paxis_get_acc_limit)(int)
) {
    _DioWrite = pDioWrite;
    _AioWrite = pAioWrite;
    _SetRotaryUnlock = pSetRotaryUnlock;
    _GetRotaryIsUnlocked = pGetRotaryIsUnlocked;
    _axis_get_vel_limit = paxis_get_vel_limit;
    _axis_get_acc_limit = paxis_get_acc_limit;
}
```

**Usage in TP**:
```c
// Toggle digital output
_DioWrite(tc->dio[i].index, tc->dio[i].end);

// Set analog output
_AioWrite(tc->aio[i].index, tc->aio[i].end);

// Rotary axis unlock
_SetRotaryUnlock(axis, 1);

// Get axis limits
double vel_limit = _axis_get_vel_limit(axis);
double acc_limit = _axis_get_acc_limit(axis);
```

### Why This Is Good Design

1. **Already abstracted**: TP doesn't know implementation details
2. **Testable**: Easy to provide mock implementations
3. **Flexible**: Different motion controllers could provide different implementations
4. **Clean interface**: Function signatures are clear and simple

### Usage Frequency

- DIO write: ~5 call sites
- AIO write: ~3 call sites  
- Rotary unlock: ~2 call sites
- Get limits: ~20 call sites (in blend calculations)

### Isolation Strategy

**Option 1**: Keep as-is (it's already good)
- Just document the interface
- Provide mock implementations for testing
- No changes needed

**Option 2**: Convert to structure (minor improvement)
```c
// tp_callbacks.h
typedef struct {
    void   (*dio_write)(int index, char value);
    void   (*aio_write)(int index, double value);
    void   (*set_rotary_unlock)(int axis, int state);
    int    (*get_rotary_is_unlocked)(int axis);
    double (*get_axis_vel_limit)(int axis);
    double (*get_axis_acc_limit)(int axis);
} tp_callbacks_t;

void tp_set_callbacks(const tp_callbacks_t *callbacks);
```

**Recommendation**: Option 2 for consistency with other abstractions, but keep Option 1 function signature for backward compatibility.

### Mock Implementations for Testing

**Example Mock**:
```c
// test_mocks.c
static void mock_dio_write(int index, char value) {
    printf("DIO[%d] = %d\n", index, value);
    // Could store for verification in tests
}

static double mock_axis_get_vel_limit(int axis) {
    return 100.0;  // Default test value
}

static tp_callbacks_t test_callbacks = {
    .dio_write = mock_dio_write,
    .aio_write = mock_aio_write,
    .set_rotary_unlock = mock_set_rotary_unlock,
    .get_rotary_is_unlocked = mock_get_rotary_is_unlocked,
    .get_axis_vel_limit = mock_axis_get_vel_limit,
    .get_axis_acc_limit = mock_axis_get_acc_limit,
};
```

### Effort Estimate
- Create tp_callbacks.h: 1 day
- Refactor to use structure: 1 day (optional)
- Create mock implementations: 1 day
- **Total**: 1-3 days (depending on approach)

**Complexity**: LOW - This is the easiest dependency to handle!

---

## 4. Type Dependencies (LOW-MODERATE)

### Severity: MODERATE
### Isolation Complexity: MODERATE
### Impact: All TP files via headers

### Description
TP uses various types defined in motion module and EMC headers.

### Required Type Headers

#### emcpose.h
**Provides**: `EmcPose` structure
```c
typedef struct EmcPose {
    PmCartesian tran;  // Translation (x, y, z)
    PmRPY rot;         // Rotation (roll, pitch, yaw) - alternative: quaternion
    double a, b, c, u, v, w;  // Additional axes
} EmcPose;
```

**Usage**: Everywhere - this is the fundamental position type
**Frequency**: Used in every TP function

**Isolation Strategy**:
- `EmcPose` is actually fairly standalone
- Depends on `PmCartesian` and `PmRPY` from posemath (already standalone)
- Could copy definition to TP library OR
- Keep dependency on posemath (recommended - it's already modular)

#### emcmotcfg.h
**Provides**: Configuration constants
```c
#define EMCMOT_MAX_JOINTS 16
#define EMCMOT_MAX_SPINDLES 8
#define EMCMOT_MAX_DIO 64
#define EMCMOT_MAX_AIO 16
```

**Usage**: Array sizing in TP structures
**Frequency**: Used in TP_STRUCT, TC_STRUCT definitions

**Isolation Strategy**:
- Copy these constants to `tp_types.h`
- They're just #defines, simple to duplicate
- Keep same values for compatibility

#### motion_types.h
**Provides**: Enums and constants
```c
#define EMCMOT_MAX_AXIS 9

typedef enum {
    TC_LINEAR = 1,
    TC_CIRCULAR = 2,
    TC_RIGID_TAP = 3,
    // ...
} tc_motion_type_t;

typedef enum {
    TC_DIR_FORWARD = 1,
    TC_DIR_REVERSE = -1
} tc_direction_t;
```

**Usage**: TC type identification, motion types
**Frequency**: Used throughout tp.c and tc.c

**Isolation Strategy**:
- Some of these are really TP-specific (TC_LINEAR, etc.)
- Move TC-related types to `tc_types.h` (already exists)
- Copy needed motion types or create TP equivalents

#### state_tag.h
**Provides**: State tag structure for synchronization
```c
typedef struct state_tag_t {
    int fields;
} state_tag_t;
```

**Usage**: Tracking command state through TP
**Frequency**: Passed through TP, not heavily used internally

**Isolation Strategy**:
- This is really a motion module concept
- Could make it opaque to TP (just pass through)
- Or copy minimal definition to TP library

### Type Dependency Matrix

| Type | Defined In | Used In TP | Isolation Strategy | Complexity |
|------|-----------|------------|-------------------|------------|
| EmcPose | emcpose.h | Everywhere | Keep via posemath | Low |
| PmCartesian | posemath.h | Via EmcPose | Keep (already modular) | Low |
| TC_STRUCT | tc_types.h | tc.c, tp.c | Already in TP | Low |
| TP_STRUCT | tp_types.h | tp.c | Already in TP | Low |
| Motion type enums | motion_types.h | tp.c, tc.c | Copy to tc_types.h | Low |
| Config constants | emcmotcfg.h | tp_types.h | Copy needed constants | Low |
| state_tag_t | state_tag.h | tp.c, tc.c | Make opaque or copy | Low |

### Isolation Approach

**Step 1**: Audit exactly which types are needed
- Go through all TP files
- List every external type used
- Determine if truly needed or just passed through

**Step 2**: Categorize types
- **Keep via dependency**: EmcPose (via posemath library)
- **Copy definition**: Constants and simple types
- **Make opaque**: state_tag (just pass through)
- **Move to TP**: TC-specific enums

**Step 3**: Create self-contained headers
- `tp_types.h` includes needed constants
- `tc_types.h` includes TC enums
- Both include only posemath (clean dependency)

**Example Refactor**:
```c
// OLD: tp_types.h
#include "motion.h"
#include "emcmotcfg.h"
#include "motion_types.h"
#include "state_tag.h"

// NEW: tp_types.h
#include "posemath.h"
#include "emcpose.h"  // or define EmcPose here

// Copy needed constants
#define TP_MAX_SPINDLES 8
#define TP_MAX_DIO 64
#define TP_MAX_AIO 16

// state_tag is opaque
typedef struct state_tag_t state_tag_t;
```

### Effort Estimate
- Type audit: 1 day
- Categorize and plan: 1 day
- Copy/move definitions: 1 day
- Update includes: 1 day
- Test compilation: 1 day
- **Total**: 5 days

**Complexity**: MODERATE - Tedious but straightforward

---

## 5. Motion.h Dependency (MODERATE)

### Severity: MODERATE
### Isolation Complexity: MODERATE
### Impact: Primarily tp.c

### Description
TP includes `motion.h` which pulls in the entire motion module interface. This is broader than needed.

### What TP Actually Uses from motion.h

**Type Definitions**:
```c
// From motion.h (via includes)
emcmot_status_t   // Motion controller status structure
emcmot_config_t   // Motion controller config structure
```

**Constants**:
```c
#define MOTION_INVALID_ID INT_MIN
```

**Nothing Else**: TP doesn't use most of motion.h

### Current Include Chain

```c
// tp.c includes:
#include "motion.h"

// Which includes:
#include "posemath.h"
#include "emcpos.h"
#include "cubic.h"
#include "emcmotcfg.h"
#include "kinematics.h"
#include "simple_tp.h"
#include "rtapi_limits.h"
#include "rtapi_bool.h"
#include "state_tag.h"
#include "tp_types.h"
```

Only a tiny fraction is actually needed by TP!

### Isolation Strategy

**Approach**: Extract minimal subset

**Step 1**: Identify needed structures
- emcmot_status_t fields that TP reads
- emcmot_config_t fields that TP reads

**Step 2**: Create minimal interface in `tp_motion_interface.h`
```c
// Instead of including full motion.h, define what TP needs:

typedef struct {
    bool stepping;
    double maxFeedScale;
    // ... only fields TP actually reads
} tp_motion_status_t;

typedef struct {
    double trajCycleTime;
    // ... only fields TP actually reads  
} tp_motion_config_t;
```

**Step 3**: Remove motion.h include from tp.c
```c
// OLD:
#include "motion.h"

// NEW:
#include "tp_motion_interface.h"
```

**Step 4**: Provide adapter in motion module
```c
// motion/control.c
void tp_populate_status(TP_STRUCT *tp) {
    tp->motion_status.stepping = emcmotStatus.stepping;
    tp->motion_status.maxFeedScale = emcmotStatus.maxFeedScale;
    // ... map full structure to minimal interface
}
```

### Benefits

1. **Reduced coupling**: TP doesn't see motion module internals
2. **Faster compilation**: Smaller include chain
3. **Clearer interface**: Explicit about what's needed
4. **Easier testing**: Small interface to mock

### Effort Estimate
- Identify needed fields: 1 day
- Define minimal interface: 1 day
- Create adapters: 1 day
- Remove motion.h include: 1 day
- Test and fix issues: 1 day
- **Total**: 5 days

**Complexity**: MODERATE

---

## 6. Axis Module Dependency (LOW)

### Severity: LOW
### Isolation Complexity: ALREADY DONE
### Impact: Via callbacks only

### Description
TP needs to query axis limits (velocity, acceleration). This is already handled through callbacks.

### Current Interface

**In axis.h**:
```c
double axis_get_vel_limit(int axis);
double axis_get_acc_limit(int axis);
```

**In tp.c**:
```c
// Set via callback
static double (*_axis_get_vel_limit)(int);
static double (*_axis_get_acc_limit)(int);

// Used for blend calculations
double vel_limit = _axis_get_vel_limit(axis);
```

### Why This Is Already Good

1. **Abstracted**: TP only knows the function signature
2. **No direct dependency**: Goes through function pointers
3. **Testable**: Easy to mock

### What Needs To Be Done

**Nothing code-wise**, but for testing:
```c
// Mock implementation
double mock_axis_get_vel_limit(int axis) {
    // Return sensible test value
    return 1000.0;  // mm/s
}

double mock_axis_get_acc_limit(int axis) {
    return 5000.0;  // mm/s²
}
```

### Effort Estimate
- Document interface: 0.5 day
- Create mocks: 0.5 day
- **Total**: 1 day

**Complexity**: ALREADY DONE (just need test mocks)

---

## Good News - Already Modular Components

### Posemath Library

**Location**: `src/libnml/posemath/`

**Status**: Already standalone library!

**What It Provides**:
- PmCartesian, PmPose, PmRPY, PmQuaternion types
- Geometric transformations
- Vector/matrix operations
- Already used throughout LinuxCNC

**TP Usage**: Extensive - all geometric calculations

**Isolation Impact**: NONE - already isolated, just use it

**Example**:
```c
#include "posemath.h"

PmCartesian v1, v2, result;
pmCartCartSub(&v1, &v2, &result);
pmCartMag(&v1, &magnitude);
```

**This is exactly the model we want for TP!**

### Self-Contained Modules

Several TP modules are already quite isolated:

#### blendmath.c (1,860 LOC)
**Dependencies**: 
- rtapi_math.h (for math functions) - EASY to abstract
- posemath.h (already modular) - OK
- Few includes from TP itself

**Status**: Mostly self-contained math algorithms
**Effort to Isolate**: LOW - just abstract math functions

#### spherical_arc.c (201 LOC)
**Dependencies**:
- posemath.h - OK
- rtapi_math.h - EASY to abstract

**Status**: Very self-contained
**Effort to Isolate**: VERY LOW

#### tcq.c (354 LOC)
**Dependencies**:
- tc_types.h (TP internal) - OK
- rtapi.h (for logging) - EASY to abstract

**Status**: Self-contained queue implementation
**Effort to Isolate**: LOW

### Existing Modular Design Patterns

The TP already follows some good practices:

**1. Header/Implementation Separation**
- Clean public APIs in .h files
- Implementation details in .c files

**2. Data Structure Encapsulation**  
- TP_STRUCT and TC_STRUCT are well-defined
- Clear ownership and lifecycle

**3. Functional Decomposition**
- blendmath handles blend calculations
- tcq handles queue operations  
- Separation of concerns

**4. Callback Interface**
- Already abstracts motion module interaction
- Good foundation for further isolation

---

## Dependency Summary

### By Complexity

| Category | Complexity | Effort | Priority |
|----------|-----------|---------|----------|
| Callback Interface | **ALREADY DONE** | 1-3 days | Low |
| Axis Dependencies | **ALREADY DONE** | 1 day | Low |
| RTAPI Coupling | **EASY** | 3-5 days | High |
| Type Dependencies | **MODERATE** | 5 days | Medium |
| Motion.h Dependency | **MODERATE** | 5 days | Medium |
| Motion Module Coupling | **MODERATE** | 6-7 days | **CRITICAL** |

### By Impact

| Category | Impact | Files Affected | Call Sites |
|----------|--------|----------------|------------|
| Motion Module Coupling | **CRITICAL** | tp.c | ~50 |
| RTAPI Coupling | **HIGH** | All TP files | ~200 |
| Callback Interface | **MODERATE** | tp.c, tc.c | ~30 |
| Type Dependencies | **MODERATE** | All via headers | N/A |
| Motion.h Dependency | **MODERATE** | tp.c | 1 include |
| Axis Dependencies | **LOW** | Via callbacks | ~20 |

### By Effort

**Total Isolation Effort**: ~25-32 days
- Phase 1 (Abstraction): 6-9 days
- Phase 2 (Refactoring): 11-16 days  
- Phase 3 (Testing): 8-7 days

### Critical Path

1. **Motion Module Coupling** (MUST FIX)
   - Blocks everything else
   - Most pervasive

2. **RTAPI Coupling** (HIGH PRIORITY)
   - Affects all files
   - Needed for standalone compilation

3. **Type Dependencies** (MEDIUM PRIORITY)
   - Needed for clean headers
   - Enables standalone library

4. **Everything Else** (LOW PRIORITY)
   - Already mostly done (callbacks)
   - Or easy to handle (motion.h)

---

## Dependency Removal Strategy

### Phase 1: Abstraction Layer (No Code Changes)

**Create Headers**:
- ✓ tp_platform.h (RTAPI abstraction)
- ✓ tp_motion_interface.h (Motion module abstraction)
- ✓ tp_callbacks.h (Callback formalization)

**Result**: Foundation for decoupling, zero runtime impact

### Phase 2: Refactor TP Code (Significant Changes)

**Global Variable Elimination**:
- ✓ Add fields to TP_STRUCT
- ✓ Update all access sites (~50 in tp.c)
- ✓ Remove static globals

**RTAPI Abstraction Usage**:
- ✓ Replace rtapi_math calls with macros (~200 sites)
- ✓ Replace rtapi_print_msg with logging abstraction (~50 sites)

**Result**: TP code uses abstractions, still works with motion module

### Phase 3: Library Extraction (Build Changes)

**Physical Separation**:
- ✓ Create lib/tp/ directory
- ✓ Move source files
- ✓ Update build system
- ✓ Create mock implementations

**Result**: TP compiles as library, LinuxCNC uses library

### Phase 4: Verification

**Testing**:
- ✓ Unit tests with mocks
- ✓ Integration tests with real motion module
- ✓ Performance validation

**Result**: Confidence in isolation, ready for production

---

## Metrics

### Current State

| Metric | Value |
|--------|-------|
| Total TP LOC | 7,552 |
| External includes | 15+ headers |
| Global variables | 10 (motion pointers + callbacks) |
| RTAPI calls | 200+ |
| Motion module accesses | 50+ |
| Unit test coverage | < 10% |

### Target State

| Metric | Value |
|--------|-------|
| Total TP LOC | ~8,000 (includes abstractions) |
| External includes | 3 (posemath, emcpose, standard C) |
| Global variables | 0 (all in TP_STRUCT) |
| RTAPI calls | 0 (via abstraction) |
| Motion module accesses | 0 (via interface) |
| Unit test coverage | 60-80% |

### Improvement

| Aspect | Improvement |
|--------|-------------|
| Coupling | **HIGH → LOW** |
| Testability | **LOW → HIGH** |
| Reusability | **NONE → HIGH** |
| Maintainability | **MODERATE → HIGH** |
| Compile time (TP only) | **Minutes → Seconds** |

---

## Conclusion

The TP dependency analysis reveals:

**Good News**:
- Several components already well isolated (blendmath, tcq, spherical_arc)
- Callback interface is already a good abstraction
- Posemath library shows the path forward
- Reasonable code size (~7K LOC)

**Challenges**:
- Global pointers to motion module data
- RTAPI dependencies throughout
- Type dependencies require careful handling

**Feasibility**: **HIGH**
- No fundamental blockers
- Clear path to isolation
- Incremental approach possible
- Estimated 6-10 weeks effort

**Recommendation**: **PROCEED** with isolation following the 4-phase plan in MIGRATION_PLAN.md

The dependencies are manageable and the benefits are significant. This is an achievable refactoring that will substantially improve the TP codebase.
