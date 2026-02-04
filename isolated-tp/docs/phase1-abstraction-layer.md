# Phase 1: Create Abstraction Layer

## Overview

**Goal**: Create abstraction headers that decouple TP from RTAPI and motion module specifics without changing existing TP code behavior.

**Duration**: 1-2 weeks

**Risk Level**: LOW

**Key Principle**: This phase adds new code but doesn't change existing TP implementation. All changes are additive and non-breaking.

---

## Detailed Tasks

### Task 1.1: Create tp_platform.h

**Duration**: 2-3 days

**Objective**: Abstract all RTAPI dependencies (math functions, logging, etc.)

#### File to Create

**Location**: `src/emc/tp/tp_platform.h`

**Content**:
```c
/********************************************************************
* Description: tp_platform.h
*   Platform abstraction for trajectory planner
*   Decouples TP from RTAPI and enables standalone usage
*
* Author: LinuxCNC Contributors
* License: GPL Version 2
* System: Linux
********************************************************************/
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H

#include <stddef.h>  // for size_t
#include <stdarg.h>  // for va_list

/**
 * Platform abstraction configuration
 * 
 * Provides function pointers for platform-specific operations.
 * For RTAPI: pointers to rtapi_sin, rtapi_cos, etc.
 * For standard C: pointers to sin, cos, etc.
 * For custom: pointers to custom implementations.
 */
typedef struct tp_platform_config {
    // ========================================
    // Math Functions
    // ========================================
    double (*sin)(double x);
    double (*cos)(double x);
    double (*tan)(double x);
    double (*sqrt)(double x);
    double (*fabs)(double x);
    double (*atan2)(double y, double x);
    double (*asin)(double x);
    double (*acos)(double x);
    double (*pow)(double x, double y);
    double (*fmax)(double x, double y);
    double (*fmin)(double x, double y);
    double (*floor)(double x);
    double (*ceil)(double x);
    double (*fmod)(double x, double y);
    double (*hypot)(double x, double y);
    
    // ========================================
    // Logging
    // ========================================
    // Log levels: ERR = critical, WARN = warning, 
    //             INFO = informational, DBG = debug
    void (*log_error)(const char *fmt, ...);
    void (*log_warning)(const char *fmt, ...);
    void (*log_info)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);
    
    // ========================================
    // Memory Allocation (Reserved)
    // ========================================
    // Currently unused (TP doesn't do dynamic allocation)
    // Reserved for future use
    void* (*malloc)(size_t size);
    void  (*free)(void *ptr);
    
} tp_platform_config_t;

/**
 * Convenience macros for TP code
 * These replace direct rtapi_math calls
 */
#define TP_SIN(x)      (tp->platform->sin(x))
#define TP_COS(x)      (tp->platform->cos(x))
#define TP_TAN(x)      (tp->platform->tan(x))
#define TP_SQRT(x)     (tp->platform->sqrt(x))
#define TP_FABS(x)     (tp->platform->fabs(x))
#define TP_ATAN2(y,x)  (tp->platform->atan2(y,x))
#define TP_ASIN(x)     (tp->platform->asin(x))
#define TP_ACOS(x)     (tp->platform->acos(x))
#define TP_POW(x,y)    (tp->platform->pow(x,y))
#define TP_FMAX(x,y)   (tp->platform->fmax(x,y))
#define TP_FMIN(x,y)   (tp->platform->fmin(x,y))
#define TP_FLOOR(x)    (tp->platform->floor(x))
#define TP_CEIL(x)     (tp->platform->ceil(x))
#define TP_FMOD(x,y)   (tp->platform->fmod(x,y))
#define TP_HYPOT(x,y)  (tp->platform->hypot(x,y))

#define TP_LOG_ERR(...)   (tp->platform->log_error(__VA_ARGS__))
#define TP_LOG_WARN(...)  (tp->platform->log_warning(__VA_ARGS__))
#define TP_LOG_INFO(...)  (tp->platform->log_info(__VA_ARGS__))
#define TP_LOG_DBG(...)   (tp->platform->log_debug(__VA_ARGS__))

/**
 * Get RTAPI platform configuration
 * 
 * Returns platform config using RTAPI implementations.
 * For use in LinuxCNC real-time context.
 */
tp_platform_config_t* tp_get_rtapi_platform(void);

/**
 * Get standard C library platform configuration
 * 
 * Returns platform config using standard C library.
 * For use in tests and standalone applications.
 */
tp_platform_config_t* tp_get_standard_platform(void);

#endif // TP_PLATFORM_H
```

#### Implementation File

**Location**: `src/emc/tp/tp_platform_rtapi.c`

**Content**:
```c
/********************************************************************
* Description: tp_platform_rtapi.c
*   RTAPI implementation of platform abstraction
********************************************************************/
#include "tp_platform.h"
#include "rtapi.h"
#include "rtapi_math.h"

// RTAPI logging wrapper
static void rtapi_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg_va(RTAPI_MSG_ERR, fmt, args);
    va_end(args);
}

static void rtapi_log_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg_va(RTAPI_MSG_WARN, fmt, args);
    va_end(args);
}

static void rtapi_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg_va(RTAPI_MSG_INFO, fmt, args);
    va_end(args);
}

static void rtapi_log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg_va(RTAPI_MSG_DBG, fmt, args);
    va_end(args);
}

// Static platform configuration using RTAPI
static tp_platform_config_t rtapi_platform = {
    // Math functions
    .sin = rtapi_sin,
    .cos = rtapi_cos,
    .tan = rtapi_tan,
    .sqrt = rtapi_sqrt,
    .fabs = rtapi_fabs,
    .atan2 = rtapi_atan2,
    .asin = rtapi_asin,
    .acos = rtapi_acos,
    .pow = rtapi_pow,
    .fmax = rtapi_fmax,
    .fmin = rtapi_fmin,
    .floor = rtapi_floor,
    .ceil = rtapi_ceil,
    .fmod = rtapi_fmod,
    .hypot = rtapi_hypot,
    
    // Logging
    .log_error = rtapi_log_error,
    .log_warning = rtapi_log_warning,
    .log_info = rtapi_log_info,
    .log_debug = rtapi_log_debug,
    
    // Memory (NULL for now - TP doesn't use dynamic allocation)
    .malloc = NULL,
    .free = NULL,
};

tp_platform_config_t* tp_get_rtapi_platform(void) {
    return &rtapi_platform;
}
```

**Location**: `src/emc/tp/tp_platform_standard.c`

**Content**:
```c
/********************************************************************
* Description: tp_platform_standard.c
*   Standard C library implementation of platform abstraction
*   For use in tests and standalone applications
********************************************************************/
#include "tp_platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Standard C library logging wrappers
static void std_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "TP ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void std_log_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "TP WARN: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void std_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("TP INFO: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

static void std_log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("TP DEBUG: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

// Static platform configuration using standard C library
static tp_platform_config_t standard_platform = {
    // Math functions (standard C library)
    .sin = sin,
    .cos = cos,
    .tan = tan,
    .sqrt = sqrt,
    .fabs = fabs,
    .atan2 = atan2,
    .asin = asin,
    .acos = acos,
    .pow = pow,
    .fmax = fmax,
    .fmin = fmin,
    .floor = floor,
    .ceil = ceil,
    .fmod = fmod,
    .hypot = hypot,
    
    // Logging (to stdout/stderr)
    .log_error = std_log_error,
    .log_warning = std_log_warning,
    .log_info = std_log_info,
    .log_debug = std_log_debug,
    
    // Memory (standard C library)
    .malloc = malloc,
    .free = free,
};

tp_platform_config_t* tp_get_standard_platform(void) {
    return &standard_platform;
}
```

#### Changes to Build System

**Add to `src/emc/tp/Submakefile`**:
```make
# Platform abstraction implementations
../bin/liblinuxcnc.so.0: objects/emc/tp/tp_platform_rtapi.o
../bin/liblinuxcnc.so.0: objects/emc/tp/tp_platform_standard.o
```

**Testing After This Task**:
```bash
# Should compile cleanly
make
# No functional changes yet
```

---

### Task 1.2: Create tp_motion_interface.h

**Duration**: 3-4 days

**Objective**: Define explicit interface to motion controller data

#### File to Create

**Location**: `src/emc/tp/tp_motion_interface.h`

**Content** (see API_DESIGN.md for complete definition):
```c
/********************************************************************
* Description: tp_motion_interface.h
*   Explicit interface between TP and motion controller
*   Defines exactly what TP reads from motion module
********************************************************************/
#ifndef TP_MOTION_INTERFACE_H
#define TP_MOTION_INTERFACE_H

#include "posemath.h"
#include "emcpose.h"
#include "emcmotcfg.h"

/**
 * Motion controller status (what TP reads each cycle)
 */
typedef struct tp_motion_status {
    bool stepping;
    double maxFeedScale;
    EmcPose carte_pos_cmd;
    
    // Spindle status (per spindle)
    struct {
        double speed;
        double revs;
        bool at_speed;
        bool index_enable;
    } spindle[EMCMOT_MAX_SPINDLES];
    
    bool on_soft_limit;
} tp_motion_status_t;

/**
 * Motion controller configuration (what TP needs to know)
 */
typedef struct tp_motion_config {
    double trajCycleTime;
    int numJoints;
    int kinematics_type;
    
    double max_velocity;
    double max_acceleration;
} tp_motion_config_t;

// Forward declaration
struct _TP_STRUCT;
typedef struct _TP_STRUCT TP_STRUCT;

/**
 * Populate TP motion status from motion controller
 * (Adapter function - implementation in tp.c or motion module)
 */
void tp_motion_populate_status(TP_STRUCT *tp, void *emcmot_status);

/**
 * Set TP motion configuration from motion controller
 * (Adapter function - implementation in tp.c or motion module)
 */
void tp_motion_set_config(TP_STRUCT *tp, void *emcmot_config);

#endif // TP_MOTION_INTERFACE_H
```

#### Adapter Implementation

**Add to `src/emc/tp/tp.c`**:
```c
#include "tp_motion_interface.h"
#include "motion.h"  // For emcmot_status_t and emcmot_config_t

void tp_motion_populate_status(TP_STRUCT *tp, void *emcmot_status_void) {
    emcmot_status_t *emcmot_status = (emcmot_status_t *)emcmot_status_void;
    
    // Extract needed fields from motion status
    tp->motion_status.stepping = emcmot_status->stepping;
    tp->motion_status.maxFeedScale = emcmot_status->maxFeedScale;
    tp->motion_status.carte_pos_cmd = emcmot_status->carte_pos_cmd;
    tp->motion_status.on_soft_limit = emcmot_status->on_soft_limit;
    
    // Extract spindle status
    for (int i = 0; i < EMCMOT_MAX_SPINDLES; i++) {
        tp->motion_status.spindle[i].speed = emcmot_status->spindle[i].speed;
        tp->motion_status.spindle[i].revs = emcmot_status->spindle[i].revs;
        tp->motion_status.spindle[i].at_speed = emcmot_status->spindle[i].at_speed;
        tp->motion_status.spindle[i].index_enable = emcmot_status->spindle[i].index_enable;
    }
}

void tp_motion_set_config(TP_STRUCT *tp, void *emcmot_config_void) {
    emcmot_config_t *emcmot_config = (emcmot_config_t *)emcmot_config_void;
    
    // Extract needed config fields
    tp->motion_config.trajCycleTime = emcmot_config->trajCycleTime;
    tp->motion_config.numJoints = emcmot_config->numJoints;
    tp->motion_config.kinematics_type = emcmot_config->kinematics_type;
    
    // Max values (may also be set via tpSetVmax/tpSetAmax)
    tp->motion_config.max_velocity = emcmot_config->limitVel;
    tp->motion_config.max_acceleration = emcmot_config->limitAccel;
}
```

**Testing**: Compile, no functional changes yet

---

### Task 1.3: Create tp_callbacks.h

**Duration**: 1-2 days

**Objective**: Formalize callback interface

#### File to Create

**Location**: `src/emc/tp/tp_callbacks.h`

**Content**:
```c
/********************************************************************
* Description: tp_callbacks.h
*   Callback interface for TP
*   Defines callbacks from TP to motion controller/axes
********************************************************************/
#ifndef TP_CALLBACKS_H
#define TP_CALLBACKS_H

// Callback function types
typedef void (*tp_dio_write_fn)(int index, char value);
typedef void (*tp_aio_write_fn)(int index, double value);
typedef void (*tp_set_rotary_unlock_fn)(int axis, int state);
typedef int  (*tp_get_rotary_is_unlocked_fn)(int axis);
typedef double (*tp_get_axis_vel_limit_fn)(int axis);
typedef double (*tp_get_axis_acc_limit_fn)(int axis);

/**
 * TP callbacks structure
 * All callbacks are optional (can be NULL if functionality not needed)
 */
typedef struct tp_callbacks {
    tp_dio_write_fn              dio_write;
    tp_aio_write_fn              aio_write;
    tp_set_rotary_unlock_fn      set_rotary_unlock;
    tp_get_rotary_is_unlocked_fn get_rotary_is_unlocked;
    tp_get_axis_vel_limit_fn     get_axis_vel_limit;
    tp_get_axis_acc_limit_fn     get_axis_acc_limit;
} tp_callbacks_t;

// Forward declaration
struct _TP_STRUCT;
typedef struct _TP_STRUCT TP_STRUCT;

/**
 * Set TP callbacks
 * @param tp TP instance
 * @param callbacks Callback structure (copied into TP)
 * @return 0 on success
 */
int tp_set_callbacks(TP_STRUCT *tp, const tp_callbacks_t *callbacks);

#endif // TP_CALLBACKS_H
```

#### Implementation

**Add to `src/emc/tp/tp.c`**:
```c
#include "tp_callbacks.h"

int tp_set_callbacks(TP_STRUCT *tp, const tp_callbacks_t *callbacks) {
    if (!tp || !callbacks) {
        return -1;
    }
    
    // Copy callbacks into TP structure
    tp->callbacks = *callbacks;
    
    return 0;
}
```

**Testing**: Compile successfully

---

### Task 1.4: Update tp_types.h to Include New Fields

**Duration**: 1 day

**Objective**: Add platform, motion interface, and callbacks fields to TP_STRUCT (but don't use them yet)

#### Changes to `src/emc/tp/tp_types.h`

```c
// Add at top of file
#include "tp_platform.h"
#include "tp_motion_interface.h"
#include "tp_callbacks.h"

// In TP_STRUCT definition, add new fields:
typedef struct {
    TC_QUEUE_STRUCT queue;
    tp_spindle_t spindle;
    
    EmcPose currentPos;
    EmcPose goalPos;
    
    int queueSize;
    // ... existing fields ...
    
    // NEW FIELDS (Phase 1 - not used yet, just added to structure)
    tp_platform_config_t *platform;      // Platform abstraction
    tp_callbacks_t callbacks;             // Callbacks to motion controller
    tp_motion_status_t motion_status;     // Motion controller status
    tp_motion_config_t motion_config;     // Motion controller config
    
} TP_STRUCT;
```

**Testing**: Compile successfully. No functional impact yet.

---

## Phase 1 Testing & Verification

### Build Testing

```bash
# Clean build
make clean
make

# Should build successfully with no warnings
# All new files compile
# No functional changes
```

### Integration Testing

```bash
# Run existing LinuxCNC test suite
runtests

# All tests should pass (no behavioral changes)
```

### Validation Checklist

- [ ] tp_platform.h compiles standalone
- [ ] tp_motion_interface.h compiles standalone
- [ ] tp_callbacks.h compiles standalone
- [ ] tp_platform_rtapi.c compiles and links
- [ ] tp_platform_standard.c compiles and links
- [ ] TP_STRUCT compiles with new fields
- [ ] All existing LinuxCNC tests pass
- [ ] No compiler warnings
- [ ] No functional changes (behavior identical to before)

---

## Effort Breakdown

| Task | Estimated Hours | Actual Hours |
|------|----------------|--------------|
| 1.1: tp_platform.h | 16-24 | |
| 1.2: tp_motion_interface.h | 20-30 | |
| 1.3: tp_callbacks.h | 8-12 | |
| 1.4: Update tp_types.h | 8 | |
| Testing & debugging | 16-24 | |
| **Total** | **68-98 hours** | |

**Calendar Time**: 1-2 weeks (considering review, testing, iterations)

---

## Success Criteria

**Phase 1 is complete when**:
- ✅ All abstraction headers created and documented
- ✅ RTAPI and standard C platform implementations exist
- ✅ Adapter functions implemented
- ✅ TP_STRUCT updated with new fields
- ✅ Everything compiles cleanly
- ✅ All existing tests pass
- ✅ No functional changes (verified by tests)
- ✅ Code reviewed and approved
- ✅ Ready for Phase 2 (refactoring to use abstractions)

---

## Next Steps

After Phase 1 completion:
1. Review and merge Phase 1 changes
2. Update documentation if needed
3. Proceed to Phase 2: Refactor TP code to use new abstractions
4. Continue following MIGRATION_PLAN.md timeline

---

## Notes

- This phase is **additive only** - no existing code behavior changes
- All changes can be reverted easily if needed (just remove new files)
- Creates foundation for Phase 2 refactoring
- Low risk, high value preparation work
