# TP Isolation Migration Plan

## Executive Summary

### Overall Feasibility: MODERATE to HIGH

The LinuxCNC trajectory planner (TP) can be successfully isolated into a standalone library with moderate effort. The codebase shows good signs of modularity:

- **Already modular components**: posemath library, blendmath, spherical_arc, tcq
- **Well-defined interface**: existing callback system (tpMotFunctions/tpMotData)
- **Self-contained logic**: TP algorithms don't deeply depend on motion controller internals
- **Manageable size**: ~7,000 LOC in 6 core files

**Key Challenges**:
- RTAPI dependencies (headers, math functions, logging)
- Global pointers to motion module data structures
- Type dependencies from motion module headers
- Lack of comprehensive unit tests

**Key Opportunities**:
- Existing callback interface is already a good abstraction
- Posemath library is already standalone
- Several modules (blendmath, tcq, spherical_arc) are already quite isolated
- Strong motivation for improved testability

### Estimated Effort: 6-10 weeks

| Phase | Duration | Complexity |
|-------|----------|------------|
| Phase 1: Create Abstraction Layer | 1-2 weeks | Low-Moderate |
| Phase 2: Refactor TP Code | 2-3 weeks | Moderate |
| Phase 3: Extract as Library | 1-2 weeks | Moderate |
| Phase 4: Unit Testing | 2-3 weeks | Moderate-High |
| **Total** | **6-10 weeks** | **Moderate** |

*Assumes one developer working full-time with review cycles*

### Risk Level: LOW-MODERATE

**Low Risk Factors**:
- Can be done incrementally without breaking changes
- Good existing test coverage at integration level
- Clear rollback path for each phase
- No changes to public LinuxCNC API initially

**Moderate Risk Factors**:
- Performance sensitivity in real-time control loop
- Need to maintain binary compatibility with external modules
- Extensive testing required to catch regressions

### Key Benefits

1. **Testability**: Unit test TP algorithms without full RTAPI/HAL stack
2. **Development Speed**: Faster iteration cycles without rebuilding entire system
3. **Code Quality**: Better architecture, clearer interfaces, reduced coupling
4. **Portability**: Reuse TP in simulators, external tools, other projects
5. **Maintainability**: Easier to understand, modify, and extend
6. **Performance**: Potential for better optimization when dependencies are clearer

### Justification

The trajectory planner is a critical component that implements complex motion algorithms (acceleration planning, blending, synchronization). Currently, it's tightly coupled to the motion controller, making it:

- **Hard to test**: Requires full RTAPI environment and motion module setup
- **Hard to develop**: Long rebuild cycles for simple algorithm changes  
- **Hard to reuse**: Can't be used in standalone tools or simulators
- **Hard to maintain**: Dependencies obscure the core algorithm logic

Isolating the TP addresses all these issues while preserving full backward compatibility with existing LinuxCNC code.

---

## Progress Tracking

### ✅ Completed
- **PR #1**: Initial platform abstraction layer (`tp_platform.h`)
- **PR #2**: Test infrastructure and validation
- **PR #3**: Refactor `blendmath.c` 
- **PR #4**: Batch refactor `tp.c`, `tc.c`, `spherical_arc.c`, `tcq.c`

### Phase 1 Status: **COMPLETE** ✅
All 5 core TP files now use `tp_platform.h` abstraction:
1. ✅ `blendmath.c` - 60 math + 2 logging replacements
2. ✅ `tp.c` - 45 math + 22 logging replacements  
3. ✅ `tc.c` - 8 math + 6 logging replacements
4. ✅ `spherical_arc.c` - 4 math replacements
5. ✅ `tcq.c` - header added (no replacements needed)

**Note**: Original plan mentioned `sp_scurve.c` but this file doesn't exist in our codebase. The 5 files above constitute the complete TP module.

### 🚧 In Progress
- Phase 2: HAL interface abstraction

### 📋 Upcoming
- Phase 3: Motion module isolation
- Phase 4: Final integration

---

## Migration Phases

## Phase 1: Platform Abstraction Layer ✅ COMPLETE

**Status**: All core files refactored (PRs #1-4 merged)

**Objective**: Isolate all OS/platform dependencies through `tp_platform.h`

**Files Completed**:
- ✅ `tp_platform.h` - Platform abstraction interface (PR #1)
- ✅ `blendmath.c` - Math & logging abstraction (PR #3)
- ✅ `tp.c` - Math & logging abstraction (PR #4)  
- ✅ `tc.c` - Math & logging abstraction (PR #4)
- ✅ `spherical_arc.c` - Math abstraction (PR #4)
- ✅ `tcq.c` - Header integration (PR #4)

**Changes Made**:
1. Created `tp_platform.h` with:
   - Math function wrappers (`tp_fabs`, `tp_sqrt`, `tp_fmin`, `tp_fmax`, `tp_sin`, `tp_cos`, `tp_acos`)
   - Logging macros (`TP_LOG_ERR`, `TP_LOG_INFO`, `TP_LOG_DBG`)
   - Platform detection (`TP_PLATFORM_LINUXCNC`, `TP_PLATFORM_STANDALONE`)
   
2. Replaced all direct math calls:
   - `fabs()` → `tp_fabs()`
   - `sqrt()` → `tp_sqrt()`
   - `fmin()` → `tp_fmin()`
   - `fmax()` → `tp_fmax()`
   - `sin()` → `tp_sin()`
   - `cos()` → `tp_cos()`
   - `acos()` → `tp_acos()`
   
3. Replaced all RTAPI logging:
   - `rtapi_print_msg(RTAPI_MSG_ERR, ...)` → `TP_LOG_ERR(...)`
   - `rtapi_print_msg(RTAPI_MSG_INFO, ...)` → `TP_LOG_INFO(...)`
   - `rtapi_print_msg(RTAPI_MSG_DBG, ...)` → `TP_LOG_DBG(...)`

**Exclusions Preserved** (as required):
- ❌ `pmSqrt()` - NOT replaced (posemath library)
- ❌ `pmSq()` - NOT replaced (posemath library)
- ❌ `tp_debug_print()` - NOT replaced (already abstracted)

**Validation**: LinuxCNC compiles and runs normally with all changes integrated.

## Detailed File Analysis

### Core TP Files (All Complete)

#### 1. `blendmath.c` ✅
- **Lines**: ~1860
- **Math calls**: 60 replacements
- **Logging**: 2 `TP_LOG_ERR` replacements
- **Status**: Refactored in PR #3

#### 2. `tp.c` ✅  
- **Lines**: ~4100
- **Math calls**: 45 replacements (`fmin`, `fmax`, `fabs`, `cos`, `sin`)
- **Logging**: 22 replacements (ERR, INFO, DBG levels)
- **Status**: Refactored in PR #4

#### 3. `tc.c` ✅
- **Lines**: ~1200
- **Math calls**: 8 replacements (`fmin`, `fmax`)
- **Logging**: 6 `TP_LOG_ERR` replacements
- **Status**: Refactored in PR #4

#### 4. `spherical_arc.c` ✅
- **Lines**: ~350
- **Math calls**: 4 replacements (`acos`, `sin`)
- **Logging**: None needed
- **Status**: Refactored in PR #4

#### 5. `tcq.c` ✅
- **Lines**: ~250
- **Math calls**: None present
- **Logging**: None needed
- **Status**: Header integrated in PR #4

**Total Changes**: 117 math function replacements + 30 logging replacements across 5 files

---

## Phase 2: Refactor TP Code (2-3 weeks)

**Goal**: Eliminate global variables and make all dependencies explicit through TP_STRUCT and function parameters.

### Tasks

#### 2.1 Replace Global Pointers (3-4 days)

**Current State**:
```c
// In tp.c
static emcmot_status_t *emcmotStatus;  // Global!
static emcmot_config_t *emcmotConfig;  // Global!

// Usage throughout tp.c
if (emcmotStatus->stepping) { ... }
```

**Target State**:
```c
// Add to TP_STRUCT
typedef struct {
    // ... existing fields ...
    
    // Platform and callbacks
    tp_platform_config_t *platform;
    tp_callbacks_t callbacks;
    
    // Motion interface
    tp_motion_status_t motion_status;
    tp_motion_config_t motion_config;
    
} TP_STRUCT;

// Usage
if (tp->motion_status.stepping) { ... }
```

**Changes Required**:
- Add new fields to `TP_STRUCT` in tp_types.h
- Update `tpCreate()` and `tpInit()` to initialize new fields
- Update `tpRunCycle()` to refresh motion_status at start of each cycle
- Replace all global pointer accesses with `tp->` accesses
- ~50-100 sites throughout tp.c to update

**Success Criteria**:
- No more static global pointers to motion module data
- All dependencies are explicit in TP_STRUCT
- Thread-safety improved (each TP instance is independent)

#### 2.2 Update Callback Usage (2-3 days)

**Current State**:
```c
static void (*_DioWrite)(int,char);  // Global function pointer

// Usage
_DioWrite(index, value);
```

**Target State**:
```c
// Usage
tp->callbacks.dio_write(index, value);
```

**Changes Required**:
- Update all callback sites (~20-30 locations)
- Remove static global function pointers
- Pass callbacks through tp_set_callbacks() to TP_STRUCT

**Success Criteria**:
- All callbacks accessed through TP_STRUCT
- No static callback globals
- Multiple TP instances can have different callbacks

#### 2.3 Refactor Internal Functions (4-5 days)

**Goal**: Update internal static functions to receive dependencies explicitly

**Current Pattern**:
```c
static int tpSomeInternalFunction(TC_STRUCT *tc) {
    // Uses global emcmotStatus implicitly
    if (emcmotStatus->stepping) { ... }
}
```

**Target Pattern**:
```c
static int tpSomeInternalFunction(TP_STRUCT *tp, TC_STRUCT *tc) {
    // Explicit dependency
    if (tp->motion_status.stepping) { ... }
}
```

**Changes Required**:
- Add `TP_STRUCT *tp` parameter to ~30-40 internal functions
- Update all call sites
- Some functions may need tp_platform or callbacks passed explicitly

**Affected Functions** (examples):
- `tpComputeBlendVelocity()`
- `tpHandleBlendArc()`
- `tpOptimizeBlends()`
- Many others in tp.c

**Success Criteria**:
- All internal functions have explicit dependencies
- No function relies on global state
- Code is more testable (functions can be tested in isolation)

#### 2.4 Update Initialization (2 days)

**Changes Required**:
- Update `tpCreate()` to accept platform config and callbacks
- Update `tpMotData()` to populate TP_STRUCT instead of globals
- Create migration helper functions for backward compatibility

**Backward Compatibility**:
```c
// Keep existing interface working
void tpMotData(emcmot_status_t *status, emcmot_config_t *config) {
    // Populate default TP instance
    // (Implementation uses internal global until motion module is updated)
}

// New interface (optional, for new code)
void tpSetMotionInterface(TP_STRUCT *tp, 
                          emcmot_status_t *status,
                          emcmot_config_t *config) {
    // Explicitly set for specific TP instance
}
```

### Phase 2 Testing & Verification

**Unit Testing** (new):
- Test internal functions in isolation with mock TP_STRUCT
- Verify calculations with known inputs/outputs
- Test edge cases without full motion controller

**Integration Testing**:
- Full LinuxCNC test suite
- Performance benchmarking
- Long-running tests for stability

**Regression Testing**:
- All existing tests must pass
- Binary compatibility with external modules
- Configuration files work unchanged

**Success Criteria**:
- All tests pass
- No performance regression (< 1% acceptable)
- Code is more maintainable and testable
- Global state eliminated from TP code

**Estimated Effort**: 2-3 weeks (11-16 days of coding + testing)

### Files Affected in Phase 2

| File | Lines Changed | Type |
|------|---------------|------|
| `src/emc/tp/tp_types.h` | ~50 | Modify TP_STRUCT |
| `src/emc/tp/tp.c` | ~500-800 | Refactor |
| `src/emc/tp/tc.c` | ~100 | Refactor |
| `src/emc/tp/blendmath.c` | ~50 | Minor updates |
| `src/emc/motion/control.c` | ~20 | Update initialization |
| **Total** | **~720-1020 LOC** | **Significant refactor** |

---

## Phase 3: Extract as Library (1-2 weeks)

**Goal**: Physically separate TP code into a library with clean build configuration.

### Tasks

#### 3.1 Create Library Directory Structure (1-2 days)

**Proposed Structure**:
```
lib/
└── tp/
    ├── include/
    │   ├── tp.h              # Public API
    │   ├── tp_types.h        # Public types
    │   ├── tc.h              # TC public interface
    │   └── blendmath.h       # Utility functions (if public)
    ├── src/
    │   ├── tp.c
    │   ├── tc.c
    │   ├── tcq.c
    │   ├── blendmath.c
    │   ├── spherical_arc.c
    │   └── tp_platform_rtapi.c  # RTAPI implementation
    ├── meson.build           # Build configuration
    └── README.md             # Library documentation
```

**Changes**:
- Create new directory structure
- Move/copy files (keeping originals temporarily for compatibility)
- Update include paths

#### 3.2 Create Build System Configuration (2-3 days)

**Meson Configuration** (`lib/tp/meson.build`):
```meson
# TP Library Build Configuration

tp_inc = include_directories('include')

# Dependencies
posemath_dep = dependency('posemath')  # or as subproject

# Source files
tp_sources = [
    'src/tp.c',
    'src/tc.c',
    'src/tcq.c',
    'src/blendmath.c',
    'src/spherical_arc.c',
]

# Compile library
libtp = library('tp',
    tp_sources,
    include_directories: tp_inc,
    dependencies: [posemath_dep],
    install: true,
)

# Declare dependency for other parts of LinuxCNC
tp_dep = declare_dependency(
    link_with: libtp,
    include_directories: tp_inc,
)

# Install headers
install_headers(
    'include/tp.h',
    'include/tp_types.h',
    'include/tc.h',
    subdir: 'linuxcnc/tp'
)
```

**Changes**:
- Create Meson build file
- Update main build to use library
- Ensure RTAPI dependency is optional (for testing)

#### 3.3 Update Include Paths (1-2 days)

**Changes Required**:
- Update all TP internal includes to use library paths
- Update motion module to include from library
- Keep backward compatibility with existing include paths

**Before**:
```c
#include "tp.h"           // From same directory
#include "motion.h"       // Direct dependency
```

**After**:
```c
#include <linuxcnc/tp/tp.h>      // From library
#include "tp_motion_interface.h" // Abstraction layer
```

#### 3.4 Create Mock Implementations for Testing (3-4 days)

**Purpose**: Enable unit testing without RTAPI/HAL

**Mock Platform** (`tests/mocks/tp_platform_mock.c`):
```c
#include <math.h>
#include <stdio.h>
#include "tp_platform.h"

static void mock_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

tp_platform_config_t mock_platform = {
    .sin = sin,
    .cos = cos,
    .sqrt = sqrt,
    // ... standard library functions
    .log_error = mock_log_error,
    // ... other logging
};

void tp_use_mock_platform(void) {
    tp_platform = &mock_platform;
}
```

**Mock Motion Interface**:
```c
// Simple mock for testing
static tp_motion_status_t mock_motion_status = {
    .stepping = false,
    .maxFeedScale = 1.0,
    // ... reasonable defaults
};

void tp_motion_get_status(tp_motion_status_t *status) {
    *status = mock_motion_status;
}

// Setters for tests to control behavior
void tp_mock_set_stepping(bool stepping) {
    mock_motion_status.stepping = stepping;
}
```

**Mock Callbacks**:
```c
// No-op callbacks for testing
static void mock_dio_write(int index, char value) {
    // Could log for verification
}

static tp_callbacks_t mock_callbacks = {
    .dio_write = mock_dio_write,
    // ... other no-op callbacks
};
```

### Phase 3 Testing & Verification

**Build Testing**:
1. Build library standalone (without RTAPI)
2. Build LinuxCNC using library
3. Verify both debug and release builds
4. Test on multiple architectures if possible

**Integration Testing**:
1. Run full LinuxCNC test suite with new library
2. Verify external modules still work
3. Performance benchmarking

**Success Criteria**:
- Library builds independently
- LinuxCNC builds and runs with library
- All tests pass
- No functional changes from user perspective
- Mock implementations enable basic unit testing

**Estimated Effort**: 1-2 weeks (7-10 days)

### Files Affected in Phase 3

| File/Directory | Lines/Files | Type |
|----------------|-------------|------|
| `lib/tp/` | ~7500 LOC | New library |
| `lib/tp/meson.build` | ~50 | New build config |
| `tests/mocks/` | ~300 | New mocks |
| `src/emc/motion/` | ~50 | Update includes |
| Main `meson.build` | ~20 | Library integration |
| **Total** | **~7920 LOC** | **Mostly moves** |

---

## Phase 4: Unit Testing (2-3 weeks)

**Goal**: Achieve comprehensive unit test coverage of TP algorithms using mock implementations.

### Tasks

#### 4.1 Test Infrastructure Setup (2-3 days)

**Test Framework**: Use existing framework (Greatest or similar)

**Directory Structure**:
```
tests/
└── tp/
    ├── mocks/
    │   ├── tp_platform_mock.c
    │   ├── tp_motion_mock.c
    │   └── tp_callbacks_mock.c
    ├── test_tp_basic.c
    ├── test_tp_blending.c
    ├── test_blendmath.c
    ├── test_tc.c
    ├── test_tcq.c
    ├── test_spherical_arc.c
    └── meson.build
```

**Test Harness**:
```c
// Common test setup
void setup_tp_test(TP_STRUCT *tp) {
    tp_use_mock_platform();
    tp_use_mock_motion();
    tp_use_mock_callbacks();
    
    tpCreate(tp, 32, 0);
    tpSetCycleTime(tp, 0.001);  // 1ms cycle
    // ... other setup
}
```

#### 4.2 Module-Specific Tests

##### 4.2.1 Blendmath Tests (Target: 80%+ coverage)

**Test Cases**:
- Blend calculations for various angles
- Tolerance handling
- Edge cases (zero length, straight blends)
- Performance validation

**Example**:
```c
TEST test_blend_calc_basic(void) {
    double v1 = 100.0;  // mm/s
    double v2 = 100.0;
    double theta = M_PI / 4;  // 45 degrees
    double acc = 1000.0;  // mm/s²
    
    double blend_vel = calculateBlendVel(v1, v2, theta, acc);
    
    ASSERT_IN_RANGE(blend_vel, 70.0, 75.0);  // Expected range
    PASS();
}
```

**Existing Tests**: Expand `unit_tests/tp/test_blendmath.c`

**Estimated**: 3-4 days

##### 4.2.2 TC (Trajectory Component) Tests (Target: 70%+ coverage)

**Test Cases**:
- Line segment creation and properties
- Arc segment creation and properties
- Velocity profiling
- Acceleration planning
- State transitions

**Example**:
```c
TEST test_tc_line_creation(void) {
    TC_STRUCT tc;
    EmcPose start = {0, 0, 0, 0, 0, 0};
    EmcPose end = {100, 0, 0, 0, 0, 0};  // 100mm in X
    
    int result = tcInit(&tc, TC_LINEAR, start, end, 
                        100.0, 50.0, /* vel, acc */
                        1, /* id */ 
                        0.001 /* cycle time */);
    
    ASSERT_EQ(result, TP_ERR_OK);
    ASSERT_EQ(tc.target, 100.0);
    ASSERT_IN_RANGE(tc.maxvel, 99.9, 100.1);
    PASS();
}
```

**Estimated**: 4-5 days

##### 4.2.3 TP (Trajectory Planner) Tests (Target: 60%+ coverage)

**Test Cases**:
- Simple line motion
- Multiple line segments with blending
- Arc motion
- Feed override behavior
- Pause/resume/abort
- Queue management

**Example**:
```c
TEST test_tp_simple_line(void) {
    TP_STRUCT tp;
    setup_tp_test(&tp);
    
    EmcPose start = {0, 0, 0, 0, 0, 0};
    EmcPose end = {100, 0, 0, 0, 0, 0};
    
    tpSetPos(&tp, &start);
    tpAddLine(&tp, end, 0, 100.0, 100.0, 1000.0, 0xFF, 0, 0, state_tag);
    
    // Simulate execution
    EmcPose pos;
    for (int i = 0; i < 200 && !tpIsDone(&tp); i++) {
        tpRunCycle(&tp, 1000000);  // 1ms in nanoseconds
        tpGetPos(&tp, &pos);
    }
    
    ASSERT_IN_RANGE(pos.tran.x, 99.9, 100.1);
    ASSERT(tpIsDone(&tp));
    PASS();
}
```

**Estimated**: 5-6 days

##### 4.2.4 TCQ (Queue) Tests (Target: 80%+ coverage)

**Test Cases**:
- Queue creation and initialization
- Enqueue/dequeue operations
- Queue full/empty handling
- Iterator behavior

**Estimated**: 2-3 days

##### 4.2.5 Spherical Arc Tests (Target: 80%+ coverage)

**Test Cases**:
- Arc computation for various geometries
- Edge cases (small arcs, large arcs)
- Numerical stability

**Estimated**: 2-3 days

#### 4.3 Integration Tests

**Test Cases**:
- Multi-segment paths with various blend types
- Complex motion patterns
- Stress tests (long queues, rapid changes)
- Regression tests from bug reports

**Estimated**: 3-4 days

#### 4.4 Performance Benchmarking

**Benchmarks**:
- TP cycle time (must be < 1ms for real-time)
- Blend calculation performance
- Queue operations throughput
- Memory usage

**Tools**:
- `perf` for profiling
- Custom timing harness
- Memory profilers (valgrind)

**Success Criteria**:
- No performance regression vs. current implementation
- All operations complete well within cycle time budget
- No memory leaks

**Estimated**: 2-3 days

### Phase 4 Testing & Verification

**Coverage Analysis**:
- Use gcov/lcov for coverage measurement
- Target coverage levels:
  - blendmath: 80%+
  - tc: 70%+
  - tp: 60%+
  - tcq: 80%+
  - spherical_arc: 80%+

**Continuous Integration**:
- Run unit tests on every commit
- Automated coverage reporting
- Performance regression detection

**Success Criteria**:
- Coverage targets met
- All tests pass consistently
- No flaky tests
- Performance benchmarks within acceptable range
- Documentation for all test cases

**Estimated Effort**: 2-3 weeks (14-21 days)

### Files Affected in Phase 4

| File/Directory | Lines | Type |
|----------------|-------|------|
| `tests/tp/` | ~2000-3000 | New tests |
| `tests/tp/mocks/` | ~500 | Mock implementations |
| `tests/tp/meson.build` | ~100 | Build config |
| CI configuration | ~50 | CI setup |
| **Total** | **~2650-3650 LOC** | **New test code** |

---

## Risk Mitigation

### 1. Regression Testing Strategy

**Approach**:
- Maintain full backward compatibility throughout all phases
- Run complete LinuxCNC test suite after every phase
- No changes to public APIs until final phase
- Keep old and new code paths side-by-side initially

**Specific Tests**:
- All existing LinuxCNC integration tests
- Real machine testing (if possible) with representative configs
- External module testing (tpcomp.comp, etc.)
- Performance benchmarking on target hardware

**Rollback Plan**:
- Each phase is independent and can be reverted
- Git branches for each phase allow easy rollback
- Compatibility shims can be kept indefinitely if needed

### 2. Performance Validation Approach

**Baseline**:
- Establish performance baseline before starting
- Measure TP cycle time, blend calculation time, etc.
- Document on representative hardware

**Continuous Monitoring**:
- Benchmark after each phase
- Automated performance regression tests
- Profile hot paths to ensure no degradation

**Acceptance Criteria**:
- TP cycle time: no regression (< 1% overhead acceptable)
- Blend calculations: no regression
- Queue operations: no regression
- Memory usage: no increase

**Mitigation**:
- Profile and optimize if regressions detected
- Use compiler optimizations appropriately
- Consider inlining for critical paths

### 3. Backwards Compatibility Maintenance

**Approach**:
- Keep all existing public APIs working
- Add new APIs alongside old ones
- Deprecate gradually over multiple releases
- Document migration path clearly

**Compatibility Layers**:
```c
// Old interface (keep working)
void tpMotData(emcmot_status_t *status, emcmot_config_t *config);
void tpMotFunctions(/* callback pointers */);

// New interface (preferred)
void tpSetPlatformConfig(TP_STRUCT *tp, tp_platform_config_t *config);
void tpSetCallbacks(TP_STRUCT *tp, const tp_callbacks_t *callbacks);

// Implementation bridges old to new
```

**External Module Compatibility**:
- tpcomp.comp and similar external modules must work unchanged
- Binary interface stability maintained
- Configuration file compatibility preserved

**Testing**:
- Test with external modules
- Test configuration file migration
- Document any required changes clearly

### 4. Rollback Procedures

**Per-Phase Rollback**:

**Phase 1**: Simple revert, just remove new header files
- Impact: None (headers not used yet)
- Time: < 1 hour

**Phase 2**: Revert commits, restore global variables  
- Impact: Medium (code changes throughout tp.c)
- Time: Few hours
- Fallback: Keep old implementation in branch

**Phase 3**: Remove library, restore direct compilation
- Impact: High (build system changes)
- Time: 1 day
- Fallback: Parallel build system during transition

**Phase 4**: Remove tests, no impact on production code
- Impact: None (tests are separate)
- Time: < 1 hour

**Emergency Rollback**:
- Maintain release branch without changes
- Document quick rollback procedure
- Test rollback procedure before deployment

---

## Success Criteria Summary

### Overall Project Success

The TP isolation project will be considered successful when ALL of the following criteria are met:

#### Functionality
- [ ] TP library compiles standalone (without motion module)
- [ ] All existing LinuxCNC integration tests pass
- [ ] External modules (tpcomp.comp) continue to work
- [ ] No user-visible behavioral changes
- [ ] Configuration files work unchanged

#### Performance
- [ ] No measurable performance regression (< 1% overhead)
- [ ] Real-time constraints still met (TP cycle < 1ms)
- [ ] Memory usage not increased significantly

#### Code Quality
- [ ] Unit test coverage targets met:
  - blendmath: ≥ 80%
  - tc: ≥ 70%
  - tp: ≥ 60%
  - tcq: ≥ 80%
  - spherical_arc: ≥ 80%
- [ ] All unit tests pass consistently
- [ ] Code review completed for all changes
- [ ] No new compiler warnings

#### Architecture
- [ ] Clear separation of concerns
- [ ] No global variables in TP code
- [ ] Explicit dependency injection
- [ ] Platform abstraction complete
- [ ] Mock implementations working

#### Documentation
- [ ] API documentation complete
- [ ] Migration guide written
- [ ] Test documentation current
- [ ] Code comments adequate
- [ ] This migration plan updated with actual results

#### Usability
- [ ] Library can be used in standalone tools
- [ ] Mock implementations enable easy testing
- [ ] Build time improved for TP-only changes
- [ ] Developer onboarding easier

### Phase-Specific Success Criteria

See individual phase descriptions above for detailed criteria.

---

## Timeline and Milestones

### Detailed Timeline (8-week example)

| Week | Phase | Milestone | Deliverables |
|------|-------|-----------|--------------|
| 1 | Phase 1 | Abstraction headers created | tp_platform.h, tp_motion_interface.h, tp_callbacks.h |
| 2 | Phase 1 | Abstraction tested and reviewed | Tests pass, code review complete |
| 3 | Phase 2 | Global variables eliminated | TP_STRUCT updated, tp.c refactored |
| 4 | Phase 2 | Internal functions refactored | All functions use explicit dependencies |
| 5 | Phase 2 | Phase 2 tested and reviewed | Regression tests pass, performance validated |
| 6 | Phase 3 | Library structure created | lib/tp/ directory, meson build working |
| 7 | Phase 3 | Mock implementations complete | Tests can run without RTAPI |
| 8 | Phase 4 | Unit tests written | Target coverage achieved |
| 9 | Phase 4 | Performance benchmarking | No regressions, optimization if needed |
| 10 | Release | Final integration and release | Documentation complete, release notes |

### Critical Path

The critical path is:
1. Phase 1 abstraction (blocks everything else)
2. Phase 2 refactoring (blocks library extraction)
3. Phase 3 library extraction (blocks testing)
4. Phase 4 testing (final validation)

Parallelization opportunities:
- Documentation can be done in parallel
- Some Phase 4 tests can be written during Phase 2-3

### Milestones

**M1: Abstraction Complete** (End of Week 2)
- All abstraction headers created and tested
- No functional changes
- Foundation for Phase 2

**M2: Refactoring Complete** (End of Week 5)
- Global state eliminated
- Dependencies explicit
- Code more testable

**M3: Library Extracted** (End of Week 7)
- Library builds standalone
- Mocks enable testing
- LinuxCNC uses library

**M4: Testing Complete** (End of Week 9)
- Coverage targets met
- Performance validated
- Ready for release

**M5: Release** (Week 10)
- Documentation complete
- Migration guide published
- Release notes written

---

## Appendix: Alternative Approaches Considered

### Approach A: Big Bang Rewrite
**Description**: Rewrite TP from scratch as standalone library

**Pros**:
- Clean design from the start
- No legacy constraints
- Could use modern C++ patterns

**Cons**:
- Very high risk
- Long development time
- Difficult to validate correctness
- Loss of proven algorithms
- **Rejected**: Too risky for critical motion control code

### Approach B: Wrapper Library
**Description**: Create wrapper library around existing TP code without refactoring

**Pros**:
- Fast to implement
- Low risk

**Cons**:
- Doesn't improve testability much
- Still tightly coupled internally
- Technical debt remains
- **Rejected**: Doesn't achieve testability goals

### Approach C: Extract Individual Modules First
**Description**: Start by extracting blendmath, tcq, etc. as separate libraries

**Pros**:
- Very incremental
- Low risk per step
- Some modules already isolated

**Cons**:
- Doesn't address core TP coupling
- Many small libraries to manage
- Main goal still not achieved
- **Partially Accepted**: Good idea but should be part of larger plan

### Selected Approach: Incremental Refactoring with Abstraction Layer

**Why**:
- Balances risk and reward
- Maintains backward compatibility
- Achieves testability goals
- Preserves proven algorithms
- Can be done incrementally
- Each phase has clear value

---

## Conclusion

This migration plan provides a clear, incremental path to isolate the LinuxCNC trajectory planner into a standalone, testable library. The 4-phase approach balances risk, effort, and reward while maintaining full backward compatibility throughout.

**Key Takeaways**:
- **Feasible**: Moderate effort with clear path forward
- **Incremental**: Each phase delivers value independently
- **Low Risk**: Extensive testing and rollback procedures
- **High Value**: Dramatically improves testability and maintainability

**Next Steps**:
1. Review and approve this migration plan
2. Begin Phase 1: Create abstraction layer
3. Execute phases sequentially with thorough testing
4. Update documentation as implementation progresses

**Success depends on**:
- Thorough testing at each phase
- Code review and community feedback
- Performance monitoring
- Maintaining backward compatibility
- Clear documentation

With disciplined execution, the TP isolation project can significantly improve LinuxCNC's trajectory planner while preserving all existing functionality.
