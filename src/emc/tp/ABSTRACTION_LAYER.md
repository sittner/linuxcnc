# TP Abstraction Layer (Phase 1)

## Overview

Phase 1 of the Trajectory Planner (TP) abstraction layer adds new headers and structures to decouple TP from RTAPI and motion module specifics, without changing existing TP code behavior.

## New Files

### Headers

1. **tp_platform.h** (enhanced)
   - Added struct-based platform abstraction with function pointers
   - Kept existing inline function approach for backwards compatibility
   - Provides `tp_platform_config_t` structure for runtime platform selection
   - Functions: `tp_get_rtapi_platform()`, `tp_get_standard_platform()`

2. **tp_motion_interface.h** (new)
   - Defines `tp_motion_status_t` - motion controller status snapshot
   - Defines `tp_motion_config_t` - motion controller configuration
   - Adapter functions: `tp_motion_populate_status()`, `tp_motion_set_config()`

3. **tp_callbacks.h** (new)
   - Defines `tp_callbacks_t` - callback function pointers
   - Supports: DIO/AIO write, rotary unlock, axis limits
   - Function: `tp_set_callbacks()`

4. **tp_config.h** (new)
   - Runtime configuration access via callback getters
   - Planner type selection (trapezoidal vs S-curve)
   - Jerk limit configuration
   - Functions: `tp_get_planner_type()`, `tp_get_max_jerk()`

### Implementation Files

1. **tp_platform_rtapi.c** (new)
   - RTAPI implementation of platform abstraction
   - Uses RTAPI math and logging functions
   - Returns `tp_platform_config_t*` via `tp_get_rtapi_platform()`

2. **tp_platform_standard.c** (new)
   - Standard C library implementation
   - Uses standard math.h and stdio.h
   - Returns `tp_platform_config_t*` via `tp_get_standard_platform()`
   - Software fallback for FMA if not available

3. **tp_config.c** (new)
   - Runtime configuration implementation
   - Getter callbacks for planner configuration
   - Default values if not configured

### Modified Files

1. **tp_types.h**
   - Added includes for new abstraction headers
   - Added new fields to `TP_STRUCT`:
     - `tp_platform_config_t *platform` - Platform abstraction
     - `tp_callbacks_t callbacks` - Callbacks to motion controller
     - `tp_motion_status_t motion_status` - Motion status snapshot
     - `tp_motion_config_t motion_config` - Motion configuration

2. **tp.c**
   - Added adapter function implementations
   - `tp_motion_populate_status()` - Extract status from emcmot_status_t
   - `tp_motion_set_config()` - Extract config from emcmot_config_t
   - `tp_set_callbacks()` - Copy callbacks to TP structure

3. **Submakefile** (emc/tp)
   - Added new source files to USERSRCS

4. **Makefile** (src)
   - Added new object files to tpmod-objs for real-time module

## Key Design Decisions

### Forward Declaration Approach
- Used `void*` parameters in adapter functions to avoid circular dependencies
- Headers use opaque pointers where TP_STRUCT is needed before definition

### Struct-Based Platform
- Added alongside existing inline functions for backwards compatibility
- Enables runtime platform selection for standalone builds
- Zero overhead when using function pointers directly

### Motion Interface
- Extracted commonly used fields from emcmot_status_t and emcmot_config_t
- Arc blend configuration included for completeness
- DIO/AIO counts included for validation

## Usage Notes

**Phase 1 is additive only:**
- All new fields in TP_STRUCT are not yet used by existing code
- Existing TP functionality is unchanged
- No performance impact
- New code compiles cleanly with no warnings

**For Future Phases:**
- Phase 2 will refactor TP code to use these abstractions
- Existing direct RTAPI/motion dependencies will be replaced
- Standalone builds will become possible

## Testing

All new files have been verified to:
- Compile cleanly with `-Wall`
- Work with both RTAPI and ULAPI builds
- Not introduce any warnings
- Preserve existing TP functionality

## Build System

New files are included in:
- User space builds (via USERSRCS in Submakefile)
- Real-time module (tpmod-objs in src/Makefile)
- Header installation (automatic via Submakefile pattern rules)
