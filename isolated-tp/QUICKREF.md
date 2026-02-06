# Isolated TP - Quick Reference

## Build Commands

```bash
# From isolated-tp directory
make          # Build everything
make clean    # Clean build artifacts
make test     # Build and run tests
make help     # Show available targets
```

## Test Commands

```bash
# Run tests
cd tests
make test

# Or run directly
./tests/test_tp_standalone
```

## File Organization

```
src/
  tp/          - Core TP implementation (16 files)
  interfaces/  - Abstraction layers (5 files)
  posemath/    - Math library (7 files)
  nml_intf/    - NML interfaces (4 files)
  motion/      - Motion headers (6 files)
  kinematics/  - Kinematics (2 files)
  rtapi/       - RTAPI compat (9 files)
  stubs/       - Standalone stubs (2 files)
tests/
  test_tp_standalone.c
  Makefile
```

## Key Abstraction Headers

- **tp_platform.h** - Math macros (min, max, square, etc.)
- **tp_rtapi_interface.h** - Print/logging (maps to printf)
- **tp_motion_interface.h** - Motion controller access
- **tp_hal_interface.h** - HAL functions (stubbed)

## Compilation

All files compile with `-DTP_STANDALONE -DULAPI`:

```bash
gcc -DTP_STANDALONE -DULAPI \
    -I src/tp \
    -I src/interfaces \
    -I src/posemath \
    -I src/nml_intf \
    -I src/motion \
    -I src/kinematics \
    -I src/rtapi \
    -I src/stubs \
    -c file.c -o file.o
```

## Simple Usage Example

```c
#define TP_STANDALONE
#include "tp.h"

int main() {
    TP_STRUCT tp;
    
    // Initialize
    tpCreate(&tp, 200, 200);
    tpInit(&tp);
    tpSetCycleTime(&tp, 0.001);
    tpSetVmax(&tp, 100.0, 0.0);
    tpSetAmax(&tp, 1000.0);
    
    // Set position and move
    EmcPose pos = {{0,0,0}, 0,0,0, 0,0,0};
    tpSetPos(&tp, pos);
    
    EmcPose end = {{100,0,0}, 0,0,0, 0,0,0};
    tpAddLine(&tp, end, EMC_MOTION_TYPE_FEED, 
              50.0, 0, 1, 0, -1);
    
    // Execute
    while (!tpIsDone(&tp)) {
        tpRunCycle(&tp, 0, 0);
        tpGetPos(&tp, &pos);
        // Use pos...
    }
    
    return 0;
}
```

## Test Results

All 9 test suites pass:
- ✅ Basic TP operations
- ✅ S-curve velocity calculations
- ✅ S-curve distance calculations
- ✅ Blend math utilities
- ✅ TC basic operations
- ✅ Queue operations
- ✅ Multi-segment motion
- ✅ Circular arc motion
- ✅ Edge cases

## Dependencies

**Build:** gcc, make, standard C libraries  
**Runtime:** libm (math library)  
**Platform:** Linux/Unix (uses POSIX headers)  
**LinuxCNC:** NOT REQUIRED ✓

## License

GPL v2+ - See individual file headers

## Documentation

- README.md - Complete usage guide
- FILE_MANIFEST.md - All files and sources
- API_DESIGN.md - API documentation (parent dir)

## Support

LinuxCNC: https://linuxcnc.org  
Forum: https://forum.linuxcnc.org

---

**Quick check:** `make clean && make test` → All tests PASSED ✅
