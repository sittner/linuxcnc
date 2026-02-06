# Isolated TP - File Manifest

This document lists all files included in the isolated-tp directory and their source locations in the LinuxCNC repository.

## Directory Structure

Total files: 56 source/header files + 3 build/doc files

## Core TP Module Files

### src/tp/ (Core Trajectory Planner)

| File | Source | Description |
|------|--------|-------------|
| tp.c | src/emc/tp/tp.c | Main trajectory planner implementation |
| tp.h | src/emc/tp/tp.h | TP public API header |
| tc.c | src/emc/tp/tc.c | Trajectory segment (TC) implementation |
| tc.h | src/emc/tp/tc.h | TC data structures and API |
| tc_types.h | src/emc/tp/tc_types.h | TC type definitions |
| tcq.c | src/emc/tp/tcq.c | TC queue management |
| tcq.h | src/emc/tp/tcq.h | TC queue API |
| blendmath.c | src/emc/tp/blendmath.c | Blend mathematics calculations |
| blendmath.h | src/emc/tp/blendmath.h | Blend math API |
| spherical_arc.c | src/emc/tp/spherical_arc.c | Spherical arc interpolation |
| spherical_arc.h | src/emc/tp/spherical_arc.h | Spherical arc API |
| sp_scurve.c | src/emc/tp/sp_scurve.c | S-curve velocity profiling (modified) |
| sp_scurve.h | src/emc/tp/sp_scurve.h | S-curve API |
| tp_types.h | src/emc/tp/tp_types.h | TP type definitions |
| tp_debug.h | src/emc/tp/tp_debug.h | TP debugging macros |
| tpmod.c | src/emc/tp/tpmod.c | HAL wrapper module (optional) |

**Modifications:**
- sp_scurve.c: Added `#include <stdbool.h>` for standalone mode

## Abstraction Interface Files

### src/interfaces/ (Abstraction Layers)

| File | Source | Description |
|------|--------|-------------|
| tp_platform.h | src/emc/tp/tp_platform.h | Platform/math macros |
| tp_rtapi_interface.h | src/emc/tp/tp_rtapi_interface.h | RTAPI print abstraction |
| tp_motion_interface.h | src/emc/tp/tp_motion_interface.h | Motion module access interface |
| tp_motion_interface.c | src/emc/tp/tp_motion_interface.c | Motion interface implementation |
| tp_hal_interface.h | src/emc/tp/tp_hal_interface.h | HAL function abstraction |

## Posemath Library Files

### src/posemath/ (Position/Orientation Math)

| File | Source | Description |
|------|--------|-------------|
| posemath.h | src/libnml/posemath/posemath.h | Posemath public API |
| _posemath.c | src/libnml/posemath/_posemath.c | Posemath implementation |
| gomath.c | src/libnml/posemath/gomath.c | Geometric math functions |
| gomath.h | src/libnml/posemath/gomath.h | Gomath API |
| gotypes.h | src/libnml/posemath/gotypes.h | Go library types |
| sincos.c | src/libnml/posemath/sincos.c | Sine/cosine functions |
| sincos.h | src/libnml/posemath/sincos.h | Sincos API |

## NML Interface Files

### src/nml_intf/ (NML Interface Types)

| File | Source | Description |
|------|--------|-------------|
| emcpose.c | src/emc/nml_intf/emcpose.c | EmcPose utility functions |
| emcpose.h | src/emc/nml_intf/emcpose.h | EmcPose API |
| emcpos.h | src/emc/nml_intf/emcpos.h | EmcPose type definition |
| motion_types.h | src/emc/nml_intf/motion_types.h | Motion type constants |

## Motion Module Files

### src/motion/ (Motion Controller Interfaces)

| File | Source | Description |
|------|--------|-------------|
| motion.h | src/emc/motion/motion.h | Motion data structures |
| emcmotcfg.h | src/emc/motion/emcmotcfg.h | Motion configuration constants |
| simple_tp.h | src/emc/motion/simple_tp.h | Simple TP interface |
| state_tag.h | src/emc/motion/state_tag.h | State tagging structures |
| mot_priv.h | **Created (stub)** | Motion private header stub |
| axis.h | **Created (stub)** | Axis interface stub |

**New Files Created:**
- mot_priv.h: Stub for motion private declarations
- axis.h: Stub for axis function declarations

## Kinematics Files

### src/kinematics/ (Kinematics Support)

| File | Source | Description |
|------|--------|-------------|
| cubic.h | src/emc/kinematics/cubic.h | Cubic interpolation |
| kinematics.h | src/emc/kinematics/kinematics.h | Kinematics interface |

## RTAPI Compatibility Layer

### src/rtapi/ (RTAPI Headers)

| File | Source | Description |
|------|--------|-------------|
| rtapi.h | src/rtapi/rtapi.h | RTAPI main header |
| rtapi_math.h | src/rtapi/rtapi_math.h | Math functions/constants |
| rtapi_string.h | src/rtapi/rtapi_string.h | String functions |
| rtapi_gfp.h | src/rtapi/rtapi_gfp.h | GFP flags |
| rtapi_limits.h | src/rtapi/rtapi_limits.h | Numeric limits |
| rtapi_bool.h | src/rtapi/rtapi_bool.h | Boolean type |
| rtapi_byteorder.h | src/rtapi/rtapi_byteorder.h | Byte order macros |
| rtapi_app.h | **Created (stub)** | Module macros stub |
| hal.h | **Created (stub)** | HAL types/functions stub |

**New Files Created:**
- rtapi_app.h: Stub for EXPORT_SYMBOL and module macros
- hal.h: Stub for HAL types and basic functions

## Standalone Stubs

### src/stubs/ (Standalone Mode Implementations)

| File | Source | Description |
|------|--------|-------------|
| tp_standalone_stubs.c | src/emc/tp/tests/tp_standalone_stubs.c (modified) | Stub implementations |
| tp_standalone_stubs.h | src/emc/tp/tests/tp_standalone_stubs.h | Stub declarations |

**Modifications:**
- Updated include path from `../../../emc/motion/emcmotcfg.h` to `emcmotcfg.h`

## Test Files

### tests/ (Test Suite)

| File | Source | Description |
|------|--------|-------------|
| test_tp_standalone.c | src/emc/tp/tests/test_tp_standalone.c (modified) | Test program |
| Makefile | **Created** | Test build system |
| .gitignore | **Created** | Ignore build artifacts |

**Modifications:**
- test_tp_standalone.c: 
  - Changed `#include "../tp.h"` to `#include "tp.h"` (and similar for other headers)
  - Added `#include "motion_types.h"` for EMC_MOTION_TYPE constants

## Build System Files

### Root Directory

| File | Description |
|------|-------------|
| Makefile | Top-level build system |
| README.md | Comprehensive documentation |
| .gitignore | (in tests/) Exclude *.o and test_tp_standalone |

## Summary

- **Total source files:** 52 (.c and .h files)
- **Files copied unchanged:** 43
- **Files with minor modifications:** 3 (sp_scurve.c, test_tp_standalone.c, tp_standalone_stubs.c)
- **Files created as stubs:** 4 (mot_priv.h, axis.h, rtapi_app.h, hal.h)
- **Build/documentation files:** 3 (Makefile x2, README.md)

## Verification

All files build successfully with:
```bash
cd isolated-tp
make clean && make test
```

Result: ✅ **All tests PASSED**

---

*Generated: 2025-02-06*
*LinuxCNC Isolated TP Module*
