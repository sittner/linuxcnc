# Standalone TP Test Build

## Overview

This directory contains a standalone test build for the LinuxCNC Trajectory Planner (TP) module. The goal is to validate that the TP can be compiled and run independently from the full LinuxCNC build system, using the abstraction layers created in previous phases.

## Purpose

This standalone build serves multiple purposes:
1. **Validation** - Proves the abstraction layers (`tp_platform.h`, `tp_motion_interface.h`, `tp_rtapi_interface.h`) work as intended
2. **Documentation** - Identifies remaining dependencies that still couple TP to LinuxCNC infrastructure
3. **Foundation** - Provides a basis for future unit testing of the TP module
4. **Exploration** - Reveals what additional abstractions may be needed for full decoupling

## Files

### Test Infrastructure
- **`Makefile`** - Standalone build system (no LinuxCNC build required)
- **`test_tp_standalone.c`** - Simple test program exercising basic TP functionality
- **`tp_standalone_stubs.h`** - Stub declarations for external dependencies
- **`tp_standalone_stubs.c`** - Stub implementations (returns dummy values)
- **`README.md`** - This file

### TP Files Compiled
The following core TP files are successfully compiled in standalone mode:
- `tp.c` - Main trajectory planner
- `tc.c` - Trajectory segment handling  
- `tcq.c` - Trajectory queue management
- `blendmath.c` - Blend calculations
- `spherical_arc.c` - Spherical arc mathematics
- `sp_scurve.c` - S-curve velocity profile calculations
- `tp_motion_interface.c` - Motion interface abstraction

### Dependencies Included
The build also requires these LinuxCNC components:
- **Posemath library** (`_posemath.c`, `gomath.c`, `sincos.c`) - Pose and geometry math
- **EmcPose utilities** (`emcpose.c`) - Pose manipulation functions

## Building

### Requirements
- GCC compiler
- Standard C math library (`libm`)
- No other LinuxCNC build dependencies needed

### Build Instructions

```bash
cd src/emc/tp/tests
make
```

The Makefile is completely standalone and doesn't depend on the LinuxCNC build system.

### Cleaning

```bash
make clean
```

## Running the Test

After building:

```bash
./test_tp_standalone
```

Or use the convenience target:

```bash
make test
```

Expected output:
```
==========================================
TP Standalone Test Program
==========================================

Initializing motion interface stubs...
Registering motion functions...

Test: Basic TP initialization
  PASS: tpCreate
  PASS: tpInit
  ...
Test: Basic TP operations - PASSED

Test: S-curve velocity calculations
  PASS: findSCurveVSpeed basic case
  ...
Test: S-curve velocity - PASSED

Test: S-curve distance calculations
  ...
Test: S-curve distance - PASSED

Test: Blend math utilities
  ...
Test: Blend math utilities - PASSED

Test: Trajectory segment (TC) basic operations
  ...
Test: TC basic operations - PASSED

Test: Queue (TCQ) operations
  ...
Test: Queue operations - PASSED

Test: Integration - Multi-segment motion
  ...
Test: Integration - Multi-segment motion - PASSED

==========================================
All tests PASSED
==========================================
```

## Test Coverage

The expanded test suite now includes comprehensive tests for:

### 1. Basic TP Operations (`test_tp_basic`)
- TP creation and initialization
- Setting cycle time, velocity, and acceleration limits
- Adding line segments to the queue
- Queue depth checking
- Clearing the TP

### 2. S-Curve Trajectory Planning (`test_scurve_velocity`, `test_scurve_distance`)
- **Velocity Calculations**:
  - `findSCurveVSpeed()` - Calculate velocity for given distance
  - `findSCurveVSpeedWithEndSpeed()` - Calculate velocity with non-zero end velocity
  - Tests with typical values, short distances, and high jerk
- **Distance Calculations**:
  - `stoppingDist()` - Calculate stopping distance
  - `finishWithSpeedDist()` - Distance to reach target velocity
  - Edge case: zero velocity

### 3. Blend Math Utilities (`test_blendmath_utils`)
- `findIntersectionAngle()` - Calculate intersection angle between unit vectors
- `pmCartCartParallel()` - Detect parallel vectors
- `pmCartCartAntiParallel()` - Detect anti-parallel vectors
- `saturate()` - Clamping function
- Tests with 90-degree angles, parallel, and anti-parallel cases

### 4. Trajectory Segment (TC) Operations (`test_tc_basic`)
- `tcInit()` - Initialize trajectory segment
- `tcClearFlags()` - Clear segment flags
- `tcGetEndpoint()` - Retrieve endpoint
- `tcGetStartpoint()` - Retrieve startpoint
- `tcInitKinkProperties()` - Initialize kink handling

### 5. Queue (TCQ) Operations (`test_tcq_operations`)
- `tcqCreate()` - Create queue
- `tcqInit()` - Initialize queue
- `tcqLen()` - Get queue length
- `tcqPut()` - Add items to queue
- `tcqItem()` - Access queue items
- `tcqLast()` - Access last item
- `tcqPopBack()` - Remove from back
- `tcqFull()` - Check full condition
- Tests for empty, single item, multiple items, and full queue

### 6. Integration Tests (`test_integration_multisegment`)
- Multi-segment motion planning
- Adding multiple line segments
- Running TP cycles
- Clearing queue after execution

### 7. Circular Arc Tests (`test_circular_arc`)
- `tpAddCircle()` - Add circular arc segment
- Quarter circle motion in XY plane
- TP execution with arc segments
- Tests normal vector and center point handling

### 8. Edge Cases and Error Handling (`test_edge_cases`)
- Zero velocity stopping distance
- Very small distance S-curve calculations (0.001mm)
- Empty queue handling (`tpIsDone`)
- Zero-length move handling (degenerate case)
- TP abort functionality
- Ensures robust error handling without crashes

### Test Helper Macros

The test suite includes helper macros for assertions:
- `ASSERT_TRUE(cond, msg)` - Assert condition is true
- `ASSERT_EQ(a, b, msg)` - Assert equality (integers)
- `ASSERT_NEAR(a, b, tol, msg)` - Assert floating-point values are close

These macros provide clear failure messages showing expected vs. actual values.

## Dependencies and Findings

### Successfully Abstracted

The following dependencies have been successfully abstracted through the interface headers:

1. **Math Functions** (via `tp_platform.h`)
   - All standard math operations (sin, cos, sqrt, etc.)
   - Works with both RTAPI and standard C math library

2. **Logging/Printing** (via `tp_rtapi_interface.h`)
   - `TP_PRINT()` and `TP_PRINT_MSG()` macros
   - Maps to `printf()` in standalone mode
   - Maps to RTAPI functions in kernel mode

3. **Motion Interface** (via `tp_motion_interface.h`)
   - Access to motion module parameters (planner type, jerk limit, cycle time)
   - Writing motion status (velocity, acceleration, distance to go)
   - Fully stubbed for standalone testing

### Remaining Dependencies

The following dependencies are still present but manageable:

1. **Motion Types and Constants**
   - Requires `motion.h` and `motion_types.h` for type definitions
   - Constants like `EMC_MOTION_TYPE_FEED`, `EMC_MOTION_TYPE_ARC`
   - Could be extracted to a separate types header for full independence

2. **Motion Status/Config Structures**
   - TP requires `emcmot_status_t` and `emcmot_config_t` structures
   - These are passed via `tpMotData()` but not heavily accessed (via motion interface instead)
   - Structures can be allocated and zeroed for testing

3. **Posemath Library**
   - TP heavily depends on posemath for geometric calculations
   - This is a reasonable dependency (geometry library)
   - Posemath itself is fairly independent (LGPL licensed)

4. **Include Paths**
   - Still requires several LinuxCNC header directories:
     - `rtapi/` - For RTAPI types and ULAPI support
     - `emc/motion/` - For motion types
     - `emc/kinematics/` - For cubic.h
     - `hal/` - For HAL types
   - These are for type definitions only, not runtime dependencies

5. **EXPORT_SYMBOL Macros**
   - TP uses `EXPORT_SYMBOL()` for kernel module exports
   - Causes harmless warnings in userspace (undefined macro)
   - Could be wrapped in `#ifndef TP_STANDALONE` if desired

### Functions Requiring Stubs

The following functions need stub implementations for standalone operation:

1. **Axis Functions** (`axis.h`)
   - `axis_get_vel_limit(int axis)` - Returns per-axis velocity limits
   - `axis_get_acc_limit(int axis)` - Returns per-axis acceleration limits

2. **DIO/AIO Functions** 
   - `dioWrite(int, char)` - Digital I/O writing
   - `aioWrite(int, double)` - Analog I/O writing

3. **Rotary Axis Functions**
   - `setRotaryUnlock(int, int)` - Unlock rotary axis
   - `getRotaryUnlock(int)` - Check if rotary axis is unlocked

These are registered via `tpMotFunctions()` and stubbed in `tp_standalone_stubs.c`.

## Compilation Characteristics

### Compiler Warnings

The build produces some expected warnings:

1. **`TP_STANDALONE` redefined** - Harmless, defined both in code and via `-D` flag
2. **`EXPORT_SYMBOL` warnings** - Expected in userspace, these are for kernel modules
   - Could be suppressed by defining `EXPORT_SYMBOL` as empty macro

### Build Size

The resulting executable is approximately 200-300 KB, demonstrating that the TP is reasonably self-contained.

## Success Criteria

✅ **Success**: The test compiles and runs, proving TP can be isolated

The build demonstrates:
- TP core logic is decoupled from RTAPI
- Abstraction layers work correctly
- TP can run in userspace with stub interfaces
- No kernel dependencies at runtime

## Related Testing

### Unit Tests
LinuxCNC also has unit tests in `unit_tests/tp/` which test individual TP functions using the "greatest" testing framework. Those tests focus on:
- Testing specific math functions in isolation
- Numerical accuracy and edge cases
- Individual component behavior

### Integration Testing
This standalone test (`src/emc/tp/tests/`) serves a different purpose:
- Tests the full TP module integration
- Validates abstraction layers work end-to-end
- Provides a foundation for testing TP behavior without full LinuxCNC
- Demonstrates TP can be compiled independently

Both approaches are valuable and complement each other.

### Potential Improvements

1. **Reduce Header Dependencies**
   - Extract type definitions to standalone headers
   - Reduce need for motion/, kinematics/, hal/ include paths

2. **Additional Test Coverage**
   - ✅ **DONE**: Comprehensive S-curve tests (velocity, distance calculations)
   - ✅ **DONE**: Blend math utility tests (angles, parallel detection)
   - ✅ **DONE**: Queue operation tests (full/empty, push/pop)
   - ✅ **DONE**: Integration tests (multi-segment motion)
   - TODO: Circular arc segment tests
   - TODO: Rigid tap segment tests
   - TODO: Full S-curve profile calculation (`calcSCurve()`)
   - TODO: Blend geometry calculation tests
   - TODO: Velocity profile verification (respects limits)
   - TODO: Position continuity checks across blends

3. **Mock Motion Interface**
   - Current stubs return dummy values
   - Could create configurable mocks for testing different scenarios

4. **Eliminate EXPORT_SYMBOL Warnings**
   - Wrap exports in `#ifndef TP_STANDALONE`
   - Or define `EXPORT_SYMBOL` as empty in standalone builds

5. **Type Portability**
   - Consider extracting `EmcPose`, `state_tag_t`, etc. to standalone headers
   - Would reduce dependency on motion.h

## Conclusion

This standalone build successfully demonstrates that the LinuxCNC Trajectory Planner can be compiled and executed independently from the full LinuxCNC system. The abstraction layers created in previous phases are working as designed.

The remaining dependencies are primarily:
- **Type definitions** (could be extracted)
- **Posemath library** (reasonable geometry dependency)
- **Include paths** (for type definitions, not code)

The TP module is now well-positioned for:
- Independent unit testing
- Potential reuse in other projects
- Easier development and debugging
- Future portability improvements

---

**Build Status**: ✅ **Working**  
**Test Status**: ✅ **Passing**  
**Decoupling Status**: 🟢 **Substantially Achieved**
