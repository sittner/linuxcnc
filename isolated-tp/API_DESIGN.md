# TP Library API Design

## Overview

This document defines the public API for the isolated trajectory planner library. The design balances clean abstraction with backward compatibility, enabling both standalone usage and continued integration with LinuxCNC.

---

## Design Principles

1. **Explicit Dependencies**: All dependencies injected through configuration structures
2. **Thread-Safe**: Each TP instance is independent with its own state
3. **Platform Agnostic**: Abstraction layer for platform-specific functionality
4. **Testable**: Can be used without RTAPI/HAL infrastructure
5. **Backward Compatible**: Existing LinuxCNC code continues to work

---

## Library Interface Overview

### Public API Surface

The TP library exposes:
- **Initialization functions**: Create, configure, destroy TP instances
- **Configuration functions**: Set parameters, limits, callbacks
- **Motion commands**: Add trajectory segments (lines, arcs, etc.)
- **Control functions**: Pause, resume, abort motion
- **Query functions**: Get status, position, queue depth
- **Execution function**: Run one trajectory cycle

### Thread-Safety Considerations

**Design**: Each TP_STRUCT instance is independent
- No shared global state
- No static variables (after refactoring)
- Multiple TP instances can coexist
- Caller responsible for synchronization of single TP instance

**Usage Pattern**:
```c
// Each thread can have its own TP instance
TP_STRUCT tp1, tp2;
tpCreate(&tp1, 32, 1);
tpCreate(&tp2, 32, 2);

// Both can run independently
tpRunCycle(&tp1, period);  // Thread 1
tpRunCycle(&tp2, period);  // Thread 2
```

**Synchronization Note**: If multiple threads access the same TP instance, caller must provide locking (same as current LinuxCNC usage).

### Error Handling Approach

**Return Codes**: All functions return `tp_err_t`
```c
typedef enum {
    TP_ERR_OK = 0,           // Success
    TP_ERR_FAIL = -1,        // General failure
    TP_ERR_MISSING_INPUT = -2,
    TP_ERR_GEOM = -5,        // Geometric error
    TP_ERR_TOLERANCE = -7,
    // ... see tp_types.h for complete list
} tp_err_t;
```

**Error Reporting**: Via logging callbacks in platform config
```c
if (error_condition) {
    TP_LOG_ERR("TP: Invalid parameter: %d", param);
    return TP_ERR_INVALID;
}
```

**Philosophy**: Return error codes for expected failures, log for unexpected conditions

---

## Core Structures

### 1. TP Platform Abstraction

#### Purpose
Abstract platform-specific functionality (math functions, logging, memory allocation if needed) to enable standalone usage and testing.

#### Definition

```c
/**
 * Platform abstraction configuration
 * 
 * Provides function pointers for platform-specific operations,
 * allowing TP library to work with RTAPI, standard C library,
 * or custom implementations.
 */
typedef struct tp_platform_config {
    // ========================================
    // Math Functions
    // ========================================
    // Standard mathematical operations
    // For RTAPI: point to rtapi_sin, rtapi_cos, etc.
    // For standard: point to sin, cos, etc. from <math.h>
    
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
    
    // ========================================
    // Logging
    // ========================================
    // Logging at different severity levels
    // For RTAPI: wrap rtapi_print_msg
    // For standard: wrap printf/fprintf
    // Format string follows printf conventions
    
    void (*log_error)(const char *fmt, ...);
    void (*log_warning)(const char *fmt, ...);
    void (*log_info)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);
    
    // ========================================
    // Memory Allocation (Optional)
    // ========================================
    // Currently TP doesn't do dynamic allocation
    // Reserved for future use if needed
    
    void* (*malloc)(size_t size);
    void  (*free)(void *ptr);
    
} tp_platform_config_t;

/**
 * Get default RTAPI platform configuration
 * 
 * Returns a configuration structure populated with RTAPI
 * implementations of all platform functions.
 */
tp_platform_config_t* tp_get_rtapi_platform(void);

/**
 * Get standard C library platform configuration
 * 
 * Returns a configuration structure populated with standard
 * C library implementations (useful for testing/standalone).
 */
tp_platform_config_t* tp_get_standard_platform(void);
```

#### Usage Example

```c
// In LinuxCNC (using RTAPI):
tp_platform_config_t *platform = tp_get_rtapi_platform();
tp_set_platform(&tp, platform);

// In standalone test:
tp_platform_config_t *platform = tp_get_standard_platform();
tp_set_platform(&tp, platform);

// In custom application:
tp_platform_config_t custom_platform = {
    .sin = my_sin_implementation,
    .cos = my_cos_implementation,
    // ...
    .log_error = my_error_logger,
    // ...
};
tp_set_platform(&tp, &custom_platform);
```

#### Convenience Macros

For internal TP code, provide macros that abstract the platform calls:

```c
// In tp_platform.h
#define TP_SIN(x)       (tp->platform->sin(x))
#define TP_COS(x)       (tp->platform->cos(x))
#define TP_SQRT(x)      (tp->platform->sqrt(x))
#define TP_FABS(x)      (tp->platform->fabs(x))
// ... etc

#define TP_LOG_ERR(...)   (tp->platform->log_error(__VA_ARGS__))
#define TP_LOG_WARN(...)  (tp->platform->log_warning(__VA_ARGS__))
#define TP_LOG_INFO(...)  (tp->platform->log_info(__VA_ARGS__))
#define TP_LOG_DBG(...)   (tp->platform->log_debug(__VA_ARGS__))
```

**Migration**: Replace `rtapi_sin(x)` with `TP_SIN(x)` throughout TP code.

---

### 2. Motion Controller Interface

#### Purpose
Define the explicit interface between TP and motion controller. TP reads motion status and configuration; this structure makes that explicit and allows mocking for tests.

#### Status Structure (Read Each Cycle)

```c
/**
 * Motion controller status
 * 
 * Contains motion controller state that TP needs to read
 * each cycle. Updated before calling tpRunCycle().
 */
typedef struct tp_motion_status {
    // ========================================
    // General Motion State
    // ========================================
    
    bool stepping;          // True if in stepping mode (MDI step-by-step)
    double maxFeedScale;    // Feed override value (0.0 to 1.0+)
                           // Applied to programmed velocities
    
    EmcPose carte_pos_cmd;  // Current commanded Cartesian position
                           // Used for certain TP calculations
    
    // ========================================
    // Spindle Status (per spindle)
    // ========================================
    // LinuxCNC supports multiple spindles
    
    struct {
        double speed;           // Current spindle speed (RPM)
        double revs;            // Spindle revolution count (for sync)
        bool at_speed;          // True if spindle at commanded speed
        bool index_enable;      // Spindle index pulse enable status
    } spindle[EMCMOT_MAX_SPINDLES];
    
    // ========================================
    // Limits and Flags
    // ========================================
    
    bool on_soft_limit;     // True if any axis is on soft limit
    
} tp_motion_status_t;
```

#### Configuration Structure (Set Once)

```c
/**
 * Motion controller configuration
 * 
 * Contains motion controller configuration that TP needs.
 * Set once during initialization and whenever configuration changes.
 */
typedef struct tp_motion_config {
    // ========================================
    // Timing
    // ========================================
    
    double trajCycleTime;   // Trajectory cycle time (seconds)
                           // Typically 0.001 (1ms) for servo systems
    
    // ========================================
    // System Capabilities
    // ========================================
    
    int numJoints;          // Number of joints in the system
    int kinematics_type;    // Type of kinematics (for info)
    
    // ========================================
    // System Limits
    // ========================================
    // Note: Per-axis limits are provided via callbacks
    
    double max_velocity;        // System maximum velocity
    double max_acceleration;    // System maximum acceleration
    
} tp_motion_config_t;
```

#### Adapter Functions

```c
/**
 * Populate motion status from motion controller
 * 
 * Called by motion controller before each tpRunCycle() to
 * update TP with current motion state.
 * 
 * @param tp TP instance to update
 * @param emcmot_status Motion controller status structure (LinuxCNC-specific)
 */
void tp_motion_populate_status(TP_STRUCT *tp, 
                                emcmot_status_t *emcmot_status);

/**
 * Set motion configuration
 * 
 * Called during initialization to provide motion configuration to TP.
 * 
 * @param tp TP instance to configure
 * @param emcmot_config Motion controller config structure (LinuxCNC-specific)
 */
void tp_motion_set_config(TP_STRUCT *tp,
                          emcmot_config_t *emcmot_config);
```

#### Usage Example

```c
// In LinuxCNC motion controller:

// One-time setup:
tp_motion_set_config(&tp, &emcmotConfig);

// Each cycle:
tp_motion_populate_status(&tp, &emcmotStatus);
tpRunCycle(&tp, period);
```

#### For Testing

```c
// In unit test:
tp_motion_status_t test_status = {
    .stepping = false,
    .maxFeedScale = 1.0,
    .carte_pos_cmd = {0, 0, 0, 0, 0, 0, 0, 0, 0},
    .spindle = {{.speed = 0, .revs = 0, .at_speed = false}},
    .on_soft_limit = false,
};

// Directly set (bypass adapter)
tp.motion_status = test_status;
tpRunCycle(&tp, 1000000);  // Run cycle
```

---

### 3. Callback Interface

#### Purpose
Define callbacks that TP uses to interact with the rest of the system (axes, I/O, etc.). Already well-abstracted in current code; this formalizes it.

#### Callback Function Types

```c
/**
 * Digital output write callback
 * 
 * Called when TP needs to set a digital output.
 * 
 * @param index Digital output index (0 to EMCMOT_MAX_DIO-1)
 * @param value Output value (0 or 1)
 */
typedef void (*tp_dio_write_fn)(int index, char value);

/**
 * Analog output write callback
 * 
 * Called when TP needs to set an analog output.
 * 
 * @param index Analog output index (0 to EMCMOT_MAX_AIO-1)
 * @param value Output value (typically -10.0 to +10.0 V, application-specific)
 */
typedef void (*tp_aio_write_fn)(int index, double value);

/**
 * Set rotary axis unlock callback
 * 
 * Called to unlock a rotary axis for unlimited rotation.
 * 
 * @param axis Axis number
 * @param state Unlock state (1 = unlock, 0 = lock)
 */
typedef void (*tp_set_rotary_unlock_fn)(int axis, int state);

/**
 * Get rotary axis unlock status callback
 * 
 * Query whether a rotary axis is unlocked.
 * 
 * @param axis Axis number  
 * @return 1 if unlocked, 0 if locked
 */
typedef int (*tp_get_rotary_is_unlocked_fn)(int axis);

/**
 * Get axis velocity limit callback
 * 
 * Query the velocity limit for a specific axis.
 * Used in blend calculations and acceleration planning.
 * 
 * @param axis Axis number (0-8 for X,Y,Z,A,B,C,U,V,W)
 * @return Velocity limit in machine units/second
 */
typedef double (*tp_get_axis_vel_limit_fn)(int axis);

/**
 * Get axis acceleration limit callback
 * 
 * Query the acceleration limit for a specific axis.
 * Used in blend calculations and acceleration planning.
 * 
 * @param axis Axis number (0-8 for X,Y,Z,A,B,C,U,V,W)
 * @return Acceleration limit in machine units/second²
 */
typedef double (*tp_get_axis_acc_limit_fn)(int axis);
```

#### Callback Structure

```c
/**
 * TP callback functions
 * 
 * Collection of all callback functions that TP uses to
 * interact with the motion controller and I/O system.
 * 
 * All callbacks are optional (can be NULL if functionality not needed).
 */
typedef struct tp_callbacks {
    tp_dio_write_fn              dio_write;
    tp_aio_write_fn              aio_write;
    tp_set_rotary_unlock_fn      set_rotary_unlock;
    tp_get_rotary_is_unlocked_fn get_rotary_is_unlocked;
    tp_get_axis_vel_limit_fn     get_axis_vel_limit;
    tp_get_axis_acc_limit_fn     get_axis_acc_limit;
} tp_callbacks_t;

/**
 * Set TP callbacks
 * 
 * Configure the callback functions for a TP instance.
 * Should be called during initialization.
 * 
 * @param tp TP instance
 * @param callbacks Callback structure (copied into TP)
 * @return TP_ERR_OK on success
 */
int tp_set_callbacks(TP_STRUCT *tp, const tp_callbacks_t *callbacks);
```

#### Usage Example

```c
// Define callback implementations
void my_dio_write(int index, char value) {
    // Implementation
}

double my_axis_get_vel_limit(int axis) {
    return axis_limits[axis].max_vel;
}

// Create callback structure
tp_callbacks_t callbacks = {
    .dio_write = my_dio_write,
    .aio_write = my_aio_write,
    .set_rotary_unlock = my_set_rotary_unlock,
    .get_rotary_is_unlocked = my_get_rotary_is_unlocked,
    .get_axis_vel_limit = my_axis_get_vel_limit,
    .get_axis_acc_limit = my_axis_get_acc_limit,
};

// Set callbacks
tp_set_callbacks(&tp, &callbacks);
```

#### For Testing (Mock Callbacks)

```c
// Simple mock that logs calls
void mock_dio_write(int index, char value) {
    printf("DIO[%d] = %d\n", index, value);
}

double mock_axis_get_vel_limit(int axis) {
    return 100.0;  // Fixed limit for testing
}

// No-op callbacks
tp_callbacks_t mock_callbacks = {
    .dio_write = mock_dio_write,
    .aio_write = NULL,  // Optional callbacks can be NULL
    .set_rotary_unlock = NULL,
    .get_rotary_is_unlocked = NULL,
    .get_axis_vel_limit = mock_axis_get_vel_limit,
    .get_axis_acc_limit = mock_axis_get_vel_limit,  // Same for simplicity
};

tp_set_callbacks(&tp, &mock_callbacks);
```

---

## Public API Functions

### Initialization and Cleanup

```c
/**
 * Create and initialize a TP instance
 * 
 * Allocates queue and initializes TP structure. Must be called
 * before any other TP functions.
 * 
 * @param tp TP structure to initialize (caller-allocated)
 * @param queueSize Size of trajectory queue (typically 32-64)
 * @param id TP instance ID (for logging/debugging)
 * @return TP_ERR_OK on success, error code on failure
 */
int tpCreate(TP_STRUCT *tp, int queueSize, int id);

/**
 * Initialize TP to default state
 * 
 * Resets TP to initial conditions. Called internally by tpCreate.
 * Can also be called to reinitialize without destroying.
 * 
 * @param tp TP instance
 * @return TP_ERR_OK on success
 */
int tpInit(TP_STRUCT *tp);

/**
 * Clear TP queue and state
 * 
 * Removes all queued trajectory segments and resets state.
 * Does not free resources (use tpDestroy for that).
 * 
 * @param tp TP instance
 * @return TP_ERR_OK on success
 */
int tpClear(TP_STRUCT *tp);

/**
 * Destroy TP instance
 * 
 * Frees all resources associated with TP instance.
 * TP cannot be used after this call without recreating.
 * 
 * @param tp TP instance to destroy
 */
void tpDestroy(TP_STRUCT *tp);
```

### Configuration

```c
/**
 * Set TP platform configuration
 * 
 * Configure platform-specific functions (math, logging).
 * Should be called before using TP.
 * 
 * @param tp TP instance
 * @param platform Platform configuration structure
 * @return TP_ERR_OK on success
 */
int tp_set_platform(TP_STRUCT *tp, tp_platform_config_t *platform);

/**
 * Set TP cycle time
 * 
 * Configure the trajectory planning cycle time.
 * Must match the rate at which tpRunCycle is called.
 * 
 * @param tp TP instance
 * @param secs Cycle time in seconds (typically 0.001 for 1kHz)
 * @return TP_ERR_OK on success
 */
int tpSetCycleTime(TP_STRUCT *tp, double secs);

/**
 * Set maximum velocity
 * 
 * Set the system maximum velocity limit.
 * 
 * @param tp TP instance
 * @param vmax Maximum velocity (machine units/sec)
 * @param ini_maxvel Initial max velocity from INI file (for reference)
 * @return TP_ERR_OK on success
 */
int tpSetVmax(TP_STRUCT *tp, double vmax, double ini_maxvel);

/**
 * Set velocity limit (feed override)
 * 
 * Set current velocity limit (affected by feed override).
 * 
 * @param tp TP instance
 * @param limit Velocity limit (machine units/sec)
 * @return TP_ERR_OK on success
 */
int tpSetVlimit(TP_STRUCT *tp, double limit);

/**
 * Set maximum acceleration
 * 
 * Set the system maximum acceleration limit.
 * 
 * @param tp TP instance
 * @param amax Maximum acceleration (machine units/sec²)
 * @return TP_ERR_OK on success
 */
int tpSetAmax(TP_STRUCT *tp, double amax);

/**
 * Set termination condition
 * 
 * Configure how trajectory segments terminate and blend.
 * 
 * @param tp TP instance
 * @param cond Termination condition (STOP, EXACT, BLEND)
 * @param tolerance Blending tolerance (machine units)
 * @return TP_ERR_OK on success
 */
int tpSetTermCond(TP_STRUCT *tp, int cond, double tolerance);

/**
 * Set current position
 * 
 * Set the current position of the machine.
 * Typically called during homing or after motion abort.
 * 
 * @param tp TP instance
 * @param pos Current position
 * @return TP_ERR_OK on success
 */
int tpSetPos(TP_STRUCT *tp, EmcPose const *pos);
```

### Motion Commands

```c
/**
 * Add linear motion segment
 * 
 * Add a linear move to the trajectory queue.
 * 
 * @param tp TP instance
 * @param end End position
 * @param canon_motion_type Motion type (TRAVERSE, FEED, etc.)
 * @param vel Programmed velocity (machine units/sec)
 * @param ini_maxvel INI max velocity for this motion type
 * @param acc Programmed acceleration (machine units/sec²)
 * @param enables Axis enable mask (which axes are active)
 * @param atspeed Wait for spindle at speed before starting
 * @param indexrotary Rotary axis index (-1 if none)
 * @param tag State tag for synchronization
 * @return TP_ERR_OK on success, error code on failure
 */
int tpAddLine(TP_STRUCT *tp, 
              EmcPose end,
              int canon_motion_type,
              double vel,
              double ini_maxvel,
              double acc,
              unsigned char enables,
              char atspeed,
              int indexrotary,
              struct state_tag_t tag);

/**
 * Add circular arc motion segment
 * 
 * Add a circular or helical arc to the trajectory queue.
 * 
 * @param tp TP instance
 * @param end End position
 * @param center Arc center point (in XYZ plane)
 * @param normal Normal vector to arc plane
 * @param turn Number of full turns (typically 0 or 1)
 * @param canon_motion_type Motion type
 * @param vel Programmed velocity (machine units/sec)
 * @param ini_maxvel INI max velocity
 * @param acc Programmed acceleration (machine units/sec²)
 * @param enables Axis enable mask
 * @param atspeed Wait for spindle at speed
 * @param tag State tag
 * @return TP_ERR_OK on success, error code on failure
 */
int tpAddCircle(TP_STRUCT *tp,
                EmcPose end,
                PmCartesian center,
                PmCartesian normal,
                int turn,
                int canon_motion_type,
                double vel,
                double ini_maxvel,
                double acc,
                unsigned char enables,
                char atspeed,
                struct state_tag_t tag);

/**
 * Add rigid tapping motion
 * 
 * Add a rigid tapping (synchronized with spindle) segment.
 * 
 * @param tp TP instance
 * @param end End position
 * @param vel Programmed velocity
 * @param ini_maxvel INI max velocity
 * @param acc Programmed acceleration
 * @param enables Axis enable mask
 * @param scale Spindle scale factor
 * @param tag State tag
 * @return TP_ERR_OK on success
 */
int tpAddRigidTap(TP_STRUCT *tp,
                  EmcPose end,
                  double vel,
                  double ini_maxvel,
                  double acc,
                  unsigned char enables,
                  double scale,
                  struct state_tag_t tag);

/**
 * Set digital output on trajectory segment
 * 
 * Configure a digital output to be set when next segment executes.
 * 
 * @param tp TP instance
 * @param index DIO index
 * @param start Value at segment start
 * @param end Value at segment end
 * @return TP_ERR_OK on success
 */
int tpSetDout(TP_STRUCT *tp, int index, unsigned char start, unsigned char end);

/**
 * Set analog output on trajectory segment
 * 
 * Configure an analog output to be set when next segment executes.
 * 
 * @param tp TP instance  
 * @param index AIO index
 * @param start Value at segment start
 * @param end Value at segment end
 * @return TP_ERR_OK on success
 */
int tpSetAout(TP_STRUCT *tp, unsigned char index, double start, double end);

/**
 * Set spindle synchronization
 * 
 * Enable spindle-synchronized motion for next segment.
 * 
 * @param tp TP instance
 * @param spindle Spindle index (0-based)
 * @param sync Synchronization ratio (units per revolution)
 * @param wait Wait for spindle index pulse
 * @return TP_ERR_OK on success
 */
int tpSetSpindleSync(TP_STRUCT *tp, int spindle, double sync, int wait);
```

### Control

```c
/**
 * Run one trajectory planning cycle
 * 
 * Execute one cycle of trajectory planning. Should be called
 * at regular intervals matching the configured cycle time.
 * This is the main TP execution function.
 * 
 * @param tp TP instance
 * @param period Period in nanoseconds (for time calculations)
 * @return TP_ERR_OK on success
 */
int tpRunCycle(TP_STRUCT *tp, long period);

/**
 * Pause trajectory execution
 * 
 * Pause motion at next possible point. Motion will decelerate
 * to a stop and hold position.
 * 
 * @param tp TP instance
 * @return TP_ERR_OK on success
 */
int tpPause(TP_STRUCT *tp);

/**
 * Resume trajectory execution
 * 
 * Resume motion after pause. Motion will accelerate from
 * current position.
 * 
 * @param tp TP instance
 * @return TP_ERR_OK on success
 */
int tpResume(TP_STRUCT *tp);

/**
 * Abort trajectory execution
 * 
 * Immediately stop all motion and clear queue.
 * Position will be at current location when abort occurs.
 * 
 * @param tp TP instance
 * @return TP_ERR_OK on success
 */
int tpAbort(TP_STRUCT *tp);

/**
 * Set run direction
 * 
 * Set trajectory run direction (forward or reverse).
 * Used for run-from-line and similar features.
 * 
 * @param tp TP instance
 * @param dir Direction (TC_DIR_FORWARD or TC_DIR_REVERSE)
 * @return TP_ERR_OK on success
 */
int tpSetRunDir(TP_STRUCT *tp, tc_direction_t dir);
```

### State Queries

```c
/**
 * Get current position
 * 
 * Query the current commanded position.
 * 
 * @param tp TP instance
 * @param pos Output: current position
 * @return TP_ERR_OK on success
 */
int tpGetPos(TP_STRUCT const *tp, EmcPose *pos);

/**
 * Check if TP is done
 * 
 * Returns true if all queued motion is complete and TP is idle.
 * 
 * @param tp TP instance
 * @return 1 if done, 0 if motion in progress
 */
int tpIsDone(TP_STRUCT *tp);

/**
 * Check if TP is moving
 * 
 * Returns true if TP is currently executing motion.
 * 
 * @param tp TP instance
 * @return 1 if moving, 0 if stationary
 */
int tpIsMoving(TP_STRUCT const *tp);

/**
 * Get queue depth
 * 
 * Returns number of segments in queue.
 * 
 * @param tp TP instance
 * @return Number of queued segments
 */
int tpQueueDepth(TP_STRUCT const *tp);

/**
 * Get active depth
 * 
 * Returns number of segments currently active (executing or blending).
 * 
 * @param tp TP instance
 * @return Number of active segments
 */
int tpActiveDepth(TP_STRUCT const *tp);

/**
 * Get current motion type
 * 
 * Returns the motion type of the currently executing segment.
 * 
 * @param tp TP instance
 * @return Motion type (TC_LINEAR, TC_CIRCULAR, etc.)
 */
int tpGetMotionType(TP_STRUCT *tp);

/**
 * Get executing segment ID
 * 
 * Returns the ID of the currently executing segment.
 * 
 * @param tp TP instance
 * @return Segment ID
 */
int tpGetExecId(TP_STRUCT *tp);

/**
 * Get executing segment tag
 * 
 * Returns the state tag of the currently executing segment.
 * 
 * @param tp TP instance
 * @return State tag
 */
struct state_tag_t tpGetExecTag(TP_STRUCT const *tp);
```

---

## Migration Compatibility

### How Existing LinuxCNC Code Continues to Work

The new API is designed to be backward compatible with existing LinuxCNC code. The migration strategy involves:

1. **Keep existing function signatures** (at least temporarily)
2. **Add new functions alongside old ones**
3. **Provide compatibility adapters**
4. **Deprecate gradually over multiple releases**

### Compatibility Layer

#### Old Interface (Still Supported)

```c
// Old global-based interface (motion module calls these)
void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig);
void tpMotFunctions(void(*pDioWrite)(int,char),
                   void(*pAioWrite)(int,double),
                   void(*pSetRotaryUnlock)(int,int),
                   int( *pGetRotaryUnlock)(int),
                   double(*paxis_get_vel_limit)(int),
                   double(*paxis_get_acc_limit)(int));
```

**Implementation**: These forward to new interface internally

```c
// Compatibility implementation (in tp.c)
static TP_STRUCT *default_tp = NULL;  // For compatibility

void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig) {
    if (default_tp) {
        tp_motion_set_config(default_tp, pconfig);
        // Store pointers for populate during tpRunCycle
    }
}

void tpMotFunctions(/* ... */) {
    if (default_tp) {
        tp_callbacks_t callbacks = {
            .dio_write = pDioWrite,
            // ... map all callbacks
        };
        tp_set_callbacks(default_tp, &callbacks);
    }
}
```

#### New Interface (Preferred)

```c
// New explicit interface
int tp_set_platform(TP_STRUCT *tp, tp_platform_config_t *platform);
int tp_set_callbacks(TP_STRUCT *tp, const tp_callbacks_t *callbacks);
void tp_motion_set_config(TP_STRUCT *tp, emcmot_config_t *config);
void tp_motion_populate_status(TP_STRUCT *tp, emcmot_status_t *status);
```

### Migration Path for LinuxCNC Core

**Phase 1**: Add new API, keep old working (compatibility layer)
- Current code works unchanged
- New code can use new API

**Phase 2**: Update motion module to use new API internally
- Old external API still works (forwarding)
- Internal usage is clean

**Phase 3**: Deprecate old API
- Document migration path
- Provide warnings (compile-time or runtime)
- Give multiple release cycle notice

**Phase 4**: (Optional) Remove old API in major version
- Clean codebase
- Only new API remains

### Migration Path for External Modules

External modules like `tpcomp.comp` need to continue working.

**Approach 1**: Keep compatibility shims indefinitely
- External modules don't need changes
- Some global state acceptable for this case

**Approach 2**: Provide migration guide for external modules
- Document how to update to new API
- Provide examples
- Support old API for N releases

**Recommended**: Approach 1 (keep compatibility)
- External modules are hard to update
- Compatibility cost is low
- Better user experience

### Example: External Module Update (Optional)

If an external module wants to use new API:

```c
// Old style (still works):
// Uses global tpMotData/tpMotFunctions

// New style (if module wants to update):
TP_STRUCT my_tp;
tp_platform_config_t *platform = tp_get_rtapi_platform();
tp_callbacks_t callbacks = { /* ... */ };

tpCreate(&my_tp, 32, 0);
tp_set_platform(&my_tp, platform);
tp_set_callbacks(&my_tp, &callbacks);
// ... use my_tp instance explicitly
```

### Version Compatibility Strategy

**Semantic Versioning**:
- Major: Breaking changes (old API removed)
- Minor: New features (new API added)
- Patch: Bug fixes

**Compatibility Promise**:
- Old API works for at least 2 major versions
- New API is stable once introduced
- Deprecation warnings before removal
- Clear migration guide

**Example Timeline**:
- v2.9: Current version
- v3.0: Add new API, keep old (this project)
- v3.1-3.x: Both APIs work, recommend new
- v4.0: Deprecate old API (warnings)
- v4.1-4.x: Both work, old is deprecated
- v5.0: (Optionally) remove old API

---

## Usage Examples

### Basic Standalone Usage

```c
#include <linuxcnc/tp/tp.h>
#include <linuxcnc/tp/tp_platform.h>

int main() {
    // Create TP instance
    TP_STRUCT tp;
    tpCreate(&tp, 32, 0);
    
    // Configure platform (use standard C library)
    tp_platform_config_t *platform = tp_get_standard_platform();
    tp_set_platform(&tp, platform);
    
    // Configure callbacks (mock or no-op for standalone)
    tp_callbacks_t callbacks = {
        .get_axis_vel_limit = mock_vel_limit,
        .get_axis_acc_limit = mock_acc_limit,
        // Others can be NULL
    };
    tp_set_callbacks(&tp, &callbacks);
    
    // Configure TP
    tpSetCycleTime(&tp, 0.001);  // 1ms
    tpSetVmax(&tp, 100.0, 100.0);
    tpSetAmax(&tp, 1000.0);
    
    // Set initial position
    EmcPose start = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    tpSetPos(&tp, &start);
    
    // Add a motion
    EmcPose end = {100, 0, 0, 0, 0, 0, 0, 0, 0};  // 100mm in X
    tpAddLine(&tp, end, 0, 50.0, 100.0, 500.0, 0xFF, 0, -1, tag);
    
    // Run trajectory
    EmcPose pos;
    while (!tpIsDone(&tp)) {
        tpRunCycle(&tp, 1000000);  // 1ms in nanoseconds
        tpGetPos(&tp, &pos);
        printf("Position: X=%.3f\n", pos.tran.x);
    }
    
    // Cleanup
    tpDestroy(&tp);
    return 0;
}
```

### LinuxCNC Integration

```c
// In motion controller initialization:

// Create TP
tpCreate(&emcmotDebug->queue, emcmotConfig->tpQueueSize, 0);

// Configure platform (RTAPI)
tp_platform_config_t *platform = tp_get_rtapi_platform();
tp_set_platform(&emcmotDebug->queue, platform);

// Configure callbacks (existing axis functions)
tp_callbacks_t callbacks = {
    .dio_write = emcmotDioWrite,
    .aio_write = emcmotAioWrite,
    .set_rotary_unlock = emcmotSetRotaryUnlock,
    .get_rotary_is_unlocked = emcmotGetRotaryIsUnlocked,
    .get_axis_vel_limit = axis_get_vel_limit,
    .get_axis_acc_limit = axis_get_acc_limit,
};
tp_set_callbacks(&emcmotDebug->queue, &callbacks);

// Set configuration
tp_motion_set_config(&emcmotDebug->queue, &emcmotConfig);

// Each cycle:
tp_motion_populate_status(&emcmotDebug->queue, &emcmotStatus);
tpRunCycle(&emcmotDebug->queue, period);
```

### Unit Test Example

```c
#include "greatest.h"  // Test framework
#include "tp.h"
#include "test_mocks.h"

TEST test_simple_line_motion(void) {
    TP_STRUCT tp;
    
    // Setup with mocks
    setup_tp_with_mocks(&tp);
    
    // Initial position
    EmcPose start = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    tpSetPos(&tp, &start);
    
    // Add line: 100mm in X at 50mm/s
    EmcPose end = {100, 0, 0, 0, 0, 0, 0, 0, 0};
    int result = tpAddLine(&tp, end, 0, 50.0, 100.0, 1000.0, 0xFF, 0, -1, tag);
    ASSERT_EQ(TP_ERR_OK, result);
    
    // Run trajectory
    EmcPose pos;
    int cycles = 0;
    while (!tpIsDone(&tp) && cycles < 5000) {
        tpRunCycle(&tp, 1000000);  // 1ms
        tpGetPos(&tp, &pos);
        cycles++;
    }
    
    // Verify final position
    ASSERT_IN_RANGE(100.0, pos.tran.x - 0.1, pos.tran.x + 0.1);
    ASSERT_IN_RANGE(0.0, pos.tran.y - 0.01, pos.tran.y + 0.01);
    
    // Verify motion completed
    ASSERT(tpIsDone(&tp));
    
    tpDestroy(&tp);
    PASS();
}
```

---

## Future Considerations

### Potential API Extensions

**Advanced Features**:
- Multi-threading support (work-stealing queue?)
- Streaming interface (for very long programs)
- Look-ahead depth control
- Real-time constraints configuration

**Observability**:
- Performance metrics API
- Debug event callbacks
- Trajectory visualization data

**Flexibility**:
- Custom blend algorithms (plugin interface?)
- Alternative acceleration profiles
- Jerk limiting configuration

### API Stability Promise

Once the TP library API is released (after Phase 3), we commit to:
- Semantic versioning
- Backward compatibility for old API (at least 2 major versions)
- Clear deprecation process
- Migration guides for any breaking changes
- API documentation kept current

---

## Conclusion

This API design provides:
- ✅ Clean abstraction from platform dependencies
- ✅ Explicit dependency injection
- ✅ Thread-safe, multi-instance capable
- ✅ Fully testable with mocks
- ✅ Backward compatible with existing LinuxCNC code
- ✅ Clear migration path
- ✅ Well-documented interfaces

The design balances the goals of:
- **Isolation**: TP can be built standalone
- **Compatibility**: Existing code continues to work
- **Testability**: Easy to unit test with mocks  
- **Clarity**: Explicit dependencies and interfaces
- **Practicality**: Realistic migration path

With this API design, the TP library can be successfully isolated while maintaining full backward compatibility with LinuxCNC and providing a clean interface for new users.
