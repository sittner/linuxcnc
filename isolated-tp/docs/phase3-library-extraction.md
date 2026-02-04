# Phase 3: Library Extraction

## Overview

**Goal**: Physically separate TP code into a standalone library with clean build configuration.

**Duration**: 1-2 weeks

**Risk Level**: MODERATE

**Key Principle**: Create library structure while maintaining full LinuxCNC integration.

---

## Detailed Tasks

### Task 3.1: Create Library Directory Structure (1-2 days)

**New Directory Structure**:
```
lib/
└── tp/
    ├── README.md               # Library documentation
    ├── meson.build            # Build configuration
    ├── include/               # Public headers
    │   ├── tp.h
    │   ├── tp_types.h
    │   ├── tc.h
    │   ├── tc_types.h
    │   ├── tp_platform.h
    │   ├── tp_motion_interface.h
    │   └── tp_callbacks.h
    ├── src/                   # Implementation
    │   ├── tp.c
    │   ├── tc.c
    │   ├── tcq.c
    │   ├── blendmath.c
    │   ├── spherical_arc.c
    │   ├── tp_platform_rtapi.c
    │   └── tp_platform_standard.c
    └── tests/                 # Library-specific tests (optional here)
        └── meson.build
```

**Implementation**:
```bash
# Create directories
mkdir -p lib/tp/include lib/tp/src lib/tp/tests

# Copy files (initially, will move later)
cp src/emc/tp/*.h lib/tp/include/
cp src/emc/tp/*.c lib/tp/src/

# Keep originals for now during transition
```

---

### Task 3.2: Create Library Build Configuration (2-3 days)

**File**: `lib/tp/meson.build`

```meson
# TP Library Build Configuration

project('libtp', 'c',
    version: '1.0.0',
    license: 'GPL-2.0',
)

# Include directories
tp_inc = include_directories('include')

# Dependencies
# Posemath library (from LinuxCNC or as subproject)
posemath_dep = dependency('posemath', required: true)

# Check for RTAPI (optional - for RTAPI platform)
rtapi_dep = dependency('rtapi', required: false)

# Source files
tp_sources = [
    'src/tp.c',
    'src/tc.c',
    'src/tcq.c',
    'src/blendmath.c',
    'src/spherical_arc.c',
]

# Platform implementations
if rtapi_dep.found()
    tp_sources += ['src/tp_platform_rtapi.c']
endif
tp_sources += ['src/tp_platform_standard.c']

# Compile as shared library
libtp = library('tp',
    tp_sources,
    include_directories: tp_inc,
    dependencies: [posemath_dep, rtapi_dep],
    version: meson.project_version(),
    install: true,
)

# Declare dependency for other parts of build
tp_dep = declare_dependency(
    link_with: libtp,
    include_directories: tp_inc,
    dependencies: [posemath_dep],
)

# Install headers
install_headers(
    'include/tp.h',
    'include/tp_types.h',
    'include/tc.h',
    'include/tc_types.h',
    'include/tp_platform.h',
    'include/tp_motion_interface.h',
    'include/tp_callbacks.h',
    subdir: 'linuxcnc/tp'
)

# Export as pkg-config
pkg = import('pkgconfig')
pkg.generate(
    libtp,
    description: 'LinuxCNC Trajectory Planner Library',
    name: 'libtp',
    filebase: 'linuxcnc-tp',
    subdirs: 'linuxcnc/tp',
)
```

---

### Task 3.3: Update Main LinuxCNC Build (1-2 days)

**Update main `meson.build`**:
```meson
# Add TP library as subproject
subdir('lib/tp')

# Motion module now depends on TP library
motion_deps = [
    tp_dep,  # <-- Use TP library
    rtapi_dep,
    hal_dep,
    # ... other deps
]

motion_sources = [
    'src/emc/motion/control.c',
    'src/emc/motion/axis.c',
    # ... etc (no more tp.c here!)
]
```

**Update Include Paths**:
```c
// In motion/control.c and other motion files:

// OLD:
#include "tp.h"  // From src/emc/tp/

// NEW:
#include <linuxcnc/tp/tp.h>  // From library
```

---

### Task 3.4: Create Mock Implementations (3-4 days)

**Purpose**: Enable unit testing without full LinuxCNC stack

**File**: `tests/mocks/tp_platform_mock.c`

```c
#include <linuxcnc/tp/tp_platform.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Simple logging that can be verified in tests
static char last_error[256] = "";
static char last_warning[256] = "";

static void mock_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_error, sizeof(last_error), fmt, args);
    fprintf(stderr, "MOCK ERROR: %s\n", last_error);
    va_end(args);
}

static void mock_log_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_warning, sizeof(last_warning), fmt, args);
    fprintf(stderr, "MOCK WARN: %s\n", last_warning);
    va_end(args);
}

static void mock_log_info(const char *fmt, ...) {
    printf("MOCK INFO: ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

static void mock_log_debug(const char *fmt, ...) {
    // Debug logging disabled in tests by default
}

static tp_platform_config_t mock_platform = {
    .sin = sin,
    .cos = cos,
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
    
    .log_error = mock_log_error,
    .log_warning = mock_log_warning,
    .log_info = mock_log_info,
    .log_debug = mock_log_debug,
    
    .malloc = malloc,
    .free = free,
};

tp_platform_config_t* tp_get_mock_platform(void) {
    return &mock_platform;
}

// Test helpers
const char* tp_mock_get_last_error(void) {
    return last_error;
}

void tp_mock_clear_errors(void) {
    last_error[0] = '\0';
    last_warning[0] = '\0';
}
```

**File**: `tests/mocks/tp_motion_mock.c`

```c
#include <linuxcnc/tp/tp_motion_interface.h>

// Simple mock motion status
static tp_motion_status_t mock_status = {
    .stepping = false,
    .maxFeedScale = 1.0,
    .on_soft_limit = false,
    // ... reasonable defaults
};

static tp_motion_config_t mock_config = {
    .trajCycleTime = 0.001,  // 1ms
    .numJoints = 3,
    .max_velocity = 100.0,
    .max_acceleration = 1000.0,
};

void tp_motion_populate_status_mock(TP_STRUCT *tp) {
    tp->motion_status = mock_status;
}

void tp_motion_set_config_mock(TP_STRUCT *tp) {
    tp->motion_config = mock_config;
}

// Setters for tests to control mock behavior
void tp_mock_set_stepping(bool stepping) {
    mock_status.stepping = stepping;
}

void tp_mock_set_feed_scale(double scale) {
    mock_status.maxFeedScale = scale;
}
```

**File**: `tests/mocks/tp_callbacks_mock.c`

```c
#include <linuxcnc/tp/tp_callbacks.h>
#include <stdio.h>

// Mock callbacks with logging/verification
static int dio_calls = 0;
static int aio_calls = 0;

static void mock_dio_write(int index, char value) {
    printf("MOCK DIO[%d] = %d\n", index, value);
    dio_calls++;
}

static void mock_aio_write(int index, double value) {
    printf("MOCK AIO[%d] = %.3f\n", index, value);
    aio_calls++;
}

static void mock_set_rotary_unlock(int axis, int state) {
    printf("MOCK Rotary[%d] unlock = %d\n", axis, state);
}

static int mock_get_rotary_is_unlocked(int axis) {
    return 0;  // Always locked in mock
}

static double mock_get_axis_vel_limit(int axis) {
    return 100.0;  // Fixed limit for testing
}

static double mock_get_axis_acc_limit(int axis) {
    return 1000.0;  // Fixed limit for testing
}

static tp_callbacks_t mock_callbacks = {
    .dio_write = mock_dio_write,
    .aio_write = mock_aio_write,
    .set_rotary_unlock = mock_set_rotary_unlock,
    .get_rotary_is_unlocked = mock_get_rotary_is_unlocked,
    .get_axis_vel_limit = mock_get_axis_vel_limit,
    .get_axis_acc_limit = mock_get_axis_acc_limit,
};

tp_callbacks_t* tp_get_mock_callbacks(void) {
    return &mock_callbacks;
}

// Test verification helpers
int tp_mock_get_dio_call_count(void) {
    return dio_calls;
}

void tp_mock_reset_counts(void) {
    dio_calls = 0;
    aio_calls = 0;
}
```

---

### Task 3.5: Integration Testing (2-3 days)

**Test TP Library Standalone**:
```c
// Standalone test program
#include <linuxcnc/tp/tp.h>
#include <linuxcnc/tp/tp_platform.h>

int main() {
    TP_STRUCT tp;
    
    tpCreate(&tp, 32, 0);
    tp.platform = tp_get_standard_platform();
    
    // Simple test
    EmcPose start = {0};
    EmcPose end = {100, 0, 0, 0, 0, 0, 0, 0, 0};
    
    tpSetPos(&tp, &start);
    tpAddLine(&tp, end, 0, 50.0, 100.0, 1000.0, 0xFF, 0, -1, tag);
    
    while (!tpIsDone(&tp)) {
        tpRunCycle(&tp, 1000000);
    }
    
    printf("TP library works standalone!\n");
    return 0;
}
```

**Compile and Test**:
```bash
# Build library only
cd lib/tp
meson setup build
ninja -C build

# Run standalone test
gcc -o test_standalone test_standalone.c -I./include -L./build -ltp -lposemath -lm
./test_standalone
```

**Test LinuxCNC Integration**:
```bash
# Build full LinuxCNC with library
cd /path/to/linuxcnc
meson setup build
ninja -C build

# Run integration tests
ninja -C build test
```

---

## Phase 3 Testing & Verification

### Test Checklist

- [ ] TP library builds standalone
- [ ] TP library runs standalone (simple test)
- [ ] LinuxCNC builds with TP library
- [ ] All LinuxCNC integration tests pass
- [ ] External module compiles and links
- [ ] Mock implementations work in tests
- [ ] Performance unchanged (benchmark)
- [ ] Memory usage unchanged
- [ ] No linker errors
- [ ] Headers install correctly

### Build Testing

```bash
# Test 1: Library builds standalone
cd lib/tp && meson setup build && ninja -C build

# Test 2: Library installs
sudo ninja -C build install

# Test 3: Pkg-config works
pkg-config --cflags --libs linuxcnc-tp

# Test 4: LinuxCNC builds with library
cd /path/to/linuxcnc && meson setup build && ninja -C build

# Test 5: Tests pass
ninja -C build test
```

---

## Success Criteria

**Phase 3 is complete when**:
- ✅ TP library builds standalone
- ✅ TP library has clean public API
- ✅ LinuxCNC uses TP library
- ✅ Mock implementations enable testing
- ✅ All tests pass
- ✅ External modules work
- ✅ Performance validated
- ✅ Documentation updated
- ✅ Ready for Phase 4 (unit testing)

---

## Effort Breakdown

| Task | Estimated Hours |
|------|----------------|
| 3.1: Directory structure | 8-12 |
| 3.2: Build configuration | 16-24 |
| 3.3: Update LinuxCNC build | 12-16 |
| 3.4: Mock implementations | 24-32 |
| 3.5: Integration testing | 16-24 |
| Debugging & fixes | 16-24 |
| **Total** | **92-132 hours** |

**Calendar Time**: 1-2 weeks

---

## Next Steps

After Phase 3:
1. Code review and merge
2. Update build documentation
3. Proceed to Phase 4: Comprehensive unit testing
