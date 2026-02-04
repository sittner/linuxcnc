# Phase 4: Unit Testing

## Overview

**Goal**: Achieve comprehensive unit test coverage of TP algorithms using mock implementations.

**Duration**: 2-3 weeks

**Risk Level**: MODERATE-HIGH (comprehensive testing is complex)

**Key Principle**: Test TP algorithms in isolation without requiring full LinuxCNC infrastructure.

---

## Test Infrastructure Setup (2-3 days)

### Test Framework

**Use**: Greatest (already in LinuxCNC) or similar lightweight C test framework

**Location**: `tests/tp/`

**Structure**:
```
tests/tp/
├── meson.build           # Test build configuration
├── mocks/                # Mock implementations
│   ├── tp_platform_mock.c
│   ├── tp_motion_mock.c
│   └── tp_callbacks_mock.c
├── helpers/              # Test helper functions
│   ├── test_setup.c
│   └── test_setup.h
├── test_blendmath.c      # Blend calculation tests (expand existing)
├── test_tc.c             # Trajectory component tests
├── test_tcq.c            # Queue tests
├── test_tp_basic.c       # Basic TP tests
├── test_tp_blending.c    # Blending tests
├── test_tp_spindle.c     # Spindle sync tests
├── test_spherical_arc.c  # Spherical arc tests
└── run_all_tests.sh      # Test runner script
```

### Test Harness

**File**: `tests/tp/helpers/test_setup.c`

```c
#include "test_setup.h"
#include <linuxcnc/tp/tp.h>

void setup_tp_for_test(TP_STRUCT *tp) {
    // Create TP
    tpCreate(tp, 32, 0);
    
    // Use standard platform (not RTAPI) for tests
    tp->platform = tp_get_standard_platform();
    
    // Use mock motion interface
    tp->motion_config.trajCycleTime = 0.001;
    tp->motion_config.max_velocity = 100.0;
    tp->motion_config.max_acceleration = 1000.0;
    tp->motion_status.maxFeedScale = 1.0;
    tp->motion_status.stepping = false;
    
    // Use mock callbacks
    tp_callbacks_t *callbacks = tp_get_mock_callbacks();
    tp_set_callbacks(tp, callbacks);
    
    // Configure TP
    tpSetCycleTime(tp, 0.001);
    tpSetVmax(tp, 100.0, 100.0);
    tpSetAmax(tp, 1000.0);
}
```

---

## Test Coverage Targets

### Module Coverage Goals

| Module | LOC | Target Coverage | Est. Test Cases |
|--------|-----|----------------|-----------------|
| blendmath.c | 1,860 | 80%+ | 50-80 |
| tc.c | 918 | 70%+ | 30-50 |
| tp.c | 3,697 | 60%+ | 80-120 |
| tcq.c | 354 | 80%+ | 20-30 |
| spherical_arc.c | 201 | 80%+ | 15-25 |
| **Total** | **7,030** | **~65% avg** | **195-305** |

---

## Test Suite Breakdown

### 1. Blendmath Tests (3-4 days)

**File**: `tests/tp/test_blendmath.c` (expand existing)

**Test Categories**:

**1.1 Basic Blend Calculations**:
```c
TEST test_blend_velocity_calculation(void) {
    double v1 = 100.0;  // Entry velocity
    double v2 = 100.0;  // Exit velocity
    double theta = M_PI / 4;  // 45 degree angle
    double acc = 1000.0;  // Acceleration limit
    
    double blend_vel = calculateBlendVel(v1, v2, theta, acc);
    
    // For 45 degrees, expect significant slowdown
    ASSERT_GT(blend_vel, 0);
    ASSERT_LT(blend_vel, v1);
    PASS();
}

TEST test_blend_velocity_straight_line(void) {
    // Straight line (180 degrees) should maintain full speed
    double blend_vel = calculateBlendVel(100.0, 100.0, M_PI, 1000.0);
    ASSERT_IN_RANGE(99.5, blend_vel, 100.0);
    PASS();
}

TEST test_blend_velocity_sharp_corner(void) {
    // 90 degree corner should slow significantly
    double blend_vel = calculateBlendVel(100.0, 100.0, M_PI/2, 1000.0);
    ASSERT_LT(blend_vel, 75.0);
    PASS();
}
```

**1.2 Tolerance Handling**:
- Blend within tolerance
- Blend exceeding tolerance
- Zero tolerance

**1.3 Edge Cases**:
- Zero length segments
- Very small angles
- Very large angles
- Unequal velocities

**Estimated**: 50-80 test cases, 3-4 days

---

### 2. TC (Trajectory Component) Tests (4-5 days)

**File**: `tests/tp/test_tc.c`

**Test Categories**:

**2.1 TC Creation**:
```c
TEST test_tc_line_creation(void) {
    TC_STRUCT tc;
    EmcPose start = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    EmcPose end = {100, 0, 0, 0, 0, 0, 0, 0, 0};
    
    int result = tcInit(&tc, TC_LINEAR, start, end, 
                        100.0,  // vel
                        1000.0, // acc
                        1,      // id
                        0.001); // cycle time
    
    ASSERT_EQ(TP_ERR_OK, result);
    ASSERT_EQ(tc.motion_type, TC_LINEAR);
    ASSERT_EQ(tc.target, 100.0);
    PASS();
}

TEST test_tc_arc_creation(void) {
    // Test circular arc creation
    TC_STRUCT tc;
    EmcPose start = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    EmcPose end = {100, 100, 0, 0, 0, 0, 0, 0, 0};
    PmCartesian center = {50, 50, 0};
    PmCartesian normal = {0, 0, 1};
    
    int result = tcInitCircle(&tc, start, end, center, normal, 0, ...);
    
    ASSERT_EQ(TP_ERR_OK, result);
    ASSERT_EQ(tc.motion_type, TC_CIRCULAR);
    // Verify arc properties
    PASS();
}
```

**2.2 Velocity Profiling**:
- Constant velocity
- Acceleration from rest
- Deceleration to stop
- Acceleration limits

**2.3 TC Execution**:
- Single step execution
- Multi-step execution
- Position queries

**2.4 TC Properties**:
- Length calculation
- Time estimation
- Velocity at position

**Estimated**: 30-50 test cases, 4-5 days

---

### 3. TP (Trajectory Planner) Tests (5-6 days)

**File**: `tests/tp/test_tp_basic.c`

**Test Categories**:

**3.1 Basic Motion**:
```c
TEST test_tp_single_line(void) {
    TP_STRUCT tp;
    setup_tp_for_test(&tp);
    
    EmcPose start = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    EmcPose end = {100, 0, 0, 0, 0, 0, 0, 0, 0};
    
    tpSetPos(&tp, &start);
    tpAddLine(&tp, end, 0, 50.0, 100.0, 1000.0, 0xFF, 0, -1, tag);
    
    // Execute trajectory
    EmcPose pos;
    int cycles = 0;
    while (!tpIsDone(&tp) && cycles < 10000) {
        tpRunCycle(&tp, 1000000);
        tpGetPos(&tp, &pos);
        cycles++;
    }
    
    // Verify reached target
    ASSERT_IN_RANGE(99.5, pos.tran.x, 100.5);
    ASSERT(tpIsDone(&tp));
    PASS();
}
```

**3.2 Queue Management**:
- Add multiple segments
- Queue full handling
- Queue empty handling
- Queue depth queries

**3.3 Control Functions**:
- Pause/resume
- Abort
- Feed override

**File**: `tests/tp/test_tp_blending.c`

**3.4 Blending Tests**:
```c
TEST test_tp_two_line_blend(void) {
    TP_STRUCT tp;
    setup_tp_for_test(&tp);
    
    EmcPose start = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    EmcPose mid = {100, 0, 0, 0, 0, 0, 0, 0, 0};
    EmcPose end = {100, 100, 0, 0, 0, 0, 0, 0, 0};
    
    tpSetPos(&tp, &start);
    tpSetTermCond(&tp, EMC_TRAJ_TERM_COND_BLEND, 1.0);
    
    tpAddLine(&tp, mid, 0, 50.0, 100.0, 1000.0, 0xFF, 0, -1, tag);
    tpAddLine(&tp, end, 0, 50.0, 100.0, 1000.0, 0xFF, 0, -1, tag);
    
    // Should blend - verify doesn't stop at corner
    bool stopped_at_corner = false;
    while (!tpIsDone(&tp)) {
        EmcPose pos;
        tpRunCycle(&tp, 1000000);
        tpGetPos(&tp, &pos);
        
        // Check if velocity goes to zero at corner
        // (should not for blended motion)
    }
    
    ASSERT_FALSE(stopped_at_corner);
    PASS();
}
```

**Estimated**: 80-120 test cases, 5-6 days

---

### 4. TCQ (Queue) Tests (2-3 days)

**File**: `tests/tp/test_tcq.c`

**Test Cases**:
- Queue creation
- Enqueue/dequeue
- Queue full
- Queue empty
- Iterator operations
- Peek operations

**Estimated**: 20-30 test cases, 2-3 days

---

### 5. Spherical Arc Tests (2-3 days)

**File**: `tests/tp/test_spherical_arc.c`

**Test Cases**:
- Arc calculation
- Small arcs
- Large arcs
- Edge cases
- Numerical stability

**Estimated**: 15-25 test cases, 2-3 days

---

## Performance Benchmarking (2-3 days)

### Benchmark Suite

**File**: `tests/tp/benchmark_tp.c`

```c
void benchmark_tp_cycle_time(void) {
    TP_STRUCT tp;
    setup_tp_for_test(&tp);
    
    // Load queue with realistic trajectory
    load_test_trajectory(&tp);
    
    // Benchmark TP cycle execution
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < 10000; i++) {
        tpRunCycle(&tp, 1000000);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    double avg_cycle = (elapsed / 10000.0) * 1e6;  // microseconds
    
    printf("Average cycle time: %.3f μs\n", avg_cycle);
    
    // Must be < 1000 μs (1ms) for real-time use
    ASSERT_LT(avg_cycle, 1000.0);
}
```

**Benchmarks**:
- TP cycle time
- Blend calculation time
- Queue operations time
- Memory usage

---

## Continuous Integration Setup (1-2 days)

### CI Configuration

**File**: `.github/workflows/tp-tests.yml`

```yaml
name: TP Library Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y meson ninja-build
      
      - name: Build TP library
        run: |
          cd lib/tp
          meson setup build
          ninja -C build
      
      - name: Run tests
        run: |
          cd tests/tp
          meson setup build
          ninja -C build test
      
      - name: Coverage report
        run: |
          ninja -C tests/tp/build coverage
          
      - name: Upload coverage
        uses: codecov/codecov-action@v2
```

---

## Success Criteria

**Phase 4 is complete when**:
- ✅ Test coverage targets met (60-80% by module)
- ✅ All tests pass consistently
- ✅ No flaky tests
- ✅ Performance benchmarks within acceptable range
- ✅ CI pipeline configured and running
- ✅ Coverage reports generated
- ✅ Documentation for tests complete
- ✅ Test maintenance guide written

---

## Effort Breakdown

| Task | Estimated Hours |
|------|----------------|
| Test infrastructure | 16-24 |
| Blendmath tests | 24-32 |
| TC tests | 32-40 |
| TP tests | 40-48 |
| TCQ tests | 16-24 |
| Spherical arc tests | 16-24 |
| Performance benchmarking | 16-24 |
| CI setup | 8-16 |
| Documentation | 8-16 |
| **Total** | **176-248 hours** |

**Calendar Time**: 2-3 weeks

---

## Next Steps

After Phase 4:
1. Review test coverage reports
2. Identify gaps and add tests
3. Performance optimization if needed
4. Final documentation update
5. **Migration complete!**

---

## Long-term Maintenance

**Ongoing**:
- Add tests for new features
- Add regression tests for bugs
- Keep coverage > 60%
- Monitor performance benchmarks
- Update mocks as needed
