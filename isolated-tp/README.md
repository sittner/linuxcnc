# Isolated TP (Trajectory Planner) Module

## Overview

This directory contains a **completely self-contained, isolated version** of the LinuxCNC Trajectory Planner (TP) module. It can be built and tested without any dependencies on the LinuxCNC source tree.

The TP module is responsible for trajectory planning, motion blending, and S-curve velocity profiling for CNC machine tool control.

## Features

- ✅ **Zero LinuxCNC Dependencies**: Builds standalone without requiring the LinuxCNC source tree
- ✅ **Abstraction Layers**: Clean interfaces for platform, RTAPI, motion, and HAL interactions
- ✅ **Comprehensive Tests**: Includes test suite for validation
- ✅ **Portable**: Can be copied and used independently
- ✅ **Easy Integration**: Well-defined API for embedding in other projects

## Quick Start

### Build and Test

```bash
cd isolated-tp
make clean    # Clean build artifacts
make          # Build the TP library and tests
make test     # Build and run tests
```

### Expected Output

```
==========================================
All tests PASSED
==========================================
```

## Build Requirements

- **GCC** or compatible C compiler
- **make** build tool
- **Standard C libraries** (math, stdio, stdlib, etc.)
- **Linux/Unix-like system** (uses standard POSIX headers)

No LinuxCNC installation required!

## Directory Structure

```
isolated-tp/
├── Makefile                      # Top-level build system
├── README.md                     # This file
├── src/
│   ├── tp/                       # Core TP implementation
│   ├── interfaces/               # Abstraction layer headers
│   ├── posemath/                 # Position/orientation mathematics
│   ├── nml_intf/                 # NML interface types
│   ├── motion/                   # Motion controller interfaces
│   ├── kinematics/               # Kinematics support
│   ├── rtapi/                    # RTAPI compatibility layer
│   └── stubs/                    # Standalone mode stubs
└── tests/                        # Test suite
```

## Usage Example

```c
#define TP_STANDALONE
#include "tp.h"

// Create and initialize TP
TP_STRUCT tp;
tpCreate(&tp, DEFAULT_TC_QUEUE_SIZE, DEFAULT_TC_QUEUE_SIZE);
tpInit(&tp);

// Configure motion parameters
tpSetCycleTime(&tp, 0.001);  // 1ms cycle time
tpSetVmax(&tp, 100.0, 0.0);  // 100 units/sec max velocity
tpSetAmax(&tp, 1000.0);      // 1000 units/sec^2 max accel

// Set initial position and add motion
EmcPose start_pos = {{0, 0, 0}, 0, 0, 0, 0, 0, 0};
tpSetPos(&tp, start_pos);

EmcPose end_pos = {{100, 0, 0}, 0, 0, 0, 0, 0, 0};
tpAddLine(&tp, end_pos, EMC_MOTION_TYPE_FEED, 50.0, 0, 1, 0, -1);

// Run motion cycles
while (!tpIsDone(&tp)) {
    EmcPose current_pos;
    tpRunCycle(&tp, 0, 0);
    tpGetPos(&tp, &current_pos);
}
```

## Abstraction Layers

1. **Platform Abstraction** (`tp_platform.h`) - Math macros, optimizations
2. **RTAPI Interface** (`tp_rtapi_interface.h`) - Print/logging functions  
3. **Motion Interface** (`tp_motion_interface.h`) - Motion controller state
4. **HAL Interface** (`tp_hal_interface.h`) - Hardware abstraction

## Components

### Core TP Files

- **tp.c/h**: Main trajectory planner
- **tc.c/h**: Trajectory segment (TC)
- **tcq.c/h**: TC queue management
- **blendmath.c/h**: Blend calculations
- **spherical_arc.c/h**: Spherical arc interpolation
- **sp_scurve.c/h**: S-curve velocity profiling

### Test Suite

Validates:
- ✅ Basic TP initialization and configuration
- ✅ S-curve velocity and distance calculations
- ✅ Blend math utilities
- ✅ Trajectory segment operations
- ✅ Queue management
- ✅ Multi-segment motion integration
- ✅ Circular arc motion
- ✅ Edge cases and error handling

## Integration into Other Projects

1. Copy the entire `isolated-tp` directory
2. Include TP headers with `#define TP_STANDALONE`
3. Link against the TP objects
4. Implement motion interface callbacks (see `tp_motion_interface.h`)

## License

GPL Version 2 or later

Copyright (c) 2004-2025 LinuxCNC Developers

## Credits

Original LinuxCNC developers:
- Fred Proctor & Will Shackleford
- Robert W. Ellenberg (S-curve, blend improvements)
- Jeff Epler, Chris Radek
- Many other contributors

## Documentation

Additional documentation in parent directory:
- `API_DESIGN.md` - Detailed API design
- `DEPENDENCY_ANALYSIS.md` - Dependency analysis
- `MIGRATION_PLAN.md` - Migration strategy

## Support

For LinuxCNC: https://linuxcnc.org | https://forum.linuxcnc.org

---

**Based on LinuxCNC master branch (2025)**
