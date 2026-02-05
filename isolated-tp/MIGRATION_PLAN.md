# TP Isolation Migration Plan

## Base Commit

This migration is based on **LinuxCNC master @ aef0cfa51b2892484fbcd6dd8242c7aafe9a282b**
- Includes complete S-curve implementation
- Includes all 2025-2026 bug fixes
- Config system already integrated

## Executive Summary

### Overall Feasibility: MODERATE to HIGH

The LinuxCNC trajectory planner (TP) can be successfully isolated into a standalone library with moderate effort. The codebase shows good signs of modularity:

- **Already modular components**: posemath library, blendmath, spherical_arc, tcq
- **Well-defined interface**: existing callback system (tpMotFunctions/tpMotData)
- **Self-contained logic**: TP algorithms don't deeply depend on motion controller internals
- **Manageable size**: ~8,610 LOC in 6 core files

### Files to Port (6 files, ~8,610 lines)

1. **blendmath.c** (~1,860 lines)
   - Circular arc blend calculations
   - Velocity/acceleration planning
   - S-curve integration points

2. **tp.c** (~4,100 lines)
   - Main trajectory planner logic
   - Queue management
   - Blend arc creation
   - S-curve velocity calculations

3. **tc.c** (~1,200 lines)
   - Trajectory component implementation
   - Segment execution
   - Progress tracking

4. **spherical_arc.c** (~200 lines)
   - 3D arc geometry
   - SLERP interpolation

5. **tcq.c** (~250 lines)
   - TC queue operations
   - FIFO management

6. **sp_scurve.c** (~1,000 lines) ← NEW
   - S-curve trajectory planning
   - Jerk-limited motion
   - Cubic equation solver
   - Integration with main TP

Total: ~8,610 lines of C code

### External Headers Required

1. **motion.h** - Motion system types + S-curve config
2. **rtapi_math.h** - Math functions (sin, cos, sqrt, fma, etc.)
3. **posemath.h** - Geometry operations
4. **sp_scurve.h** - S-curve API (self-contained)
5. **mot_priv.h** - Motion internals (emcmotStatus for planner type)

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

### Estimated Effort: 5-6 days (File Porting Approach)

**Alternative: Direct File Porting**

If porting files directly to create isolated library:

| Task | Duration | Complexity |
|------|----------|------------|
| Port sp_scurve.c (with abstractions) | 1 day | Low-Moderate |
| Port blendmath.c | 1 day | Moderate |
| Port spherical_arc.c, tc.c, tcq.c | 1 day | Low |
| Port tp.c | 1 day | Moderate |
| Testing & Integration | 1-2 days | Moderate |
| **Total** | **5-6 days** | **Moderate** |

**Traditional Approach: In-Place Refactoring**

For comparison, the traditional refactoring approach:

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

## Safe In-Place Migration Strategy (RECOMMENDED)

This section documents the **safest, most reviewable approach** for migrating LinuxCNC's trajectory planner code. While the document describes multiple approaches (Direct File Porting and Traditional In-Place Refactoring), this strategy ensures LinuxCNC remains fully buildable and functional at every single commit.

### Core Principles

**Every commit must keep LinuxCNC fully buildable and functional.** This is the golden rule that guides all migration work.

1. **Change one thing at a time**: Each PR should have a single, clear purpose
2. **Add before removing**: Introduce new code first, then migrate usage, never remove before replacing
3. **Test at each step**: Validate changes immediately to catch regressions early
4. **Make changes reversible**: Each PR should be independently revertable without breaking the build
5. **Verify behavior preservation**: Use assertions and tests to confirm no behavior changes during refactoring

### Step-by-Step Granularity Example

To illustrate the atomic granularity required, here's how to break down adding `tp_platform.h`:

#### Step 1: Create Empty Abstraction Header (Zero Behavior Change)

Create `tp_platform.h` that initially just wraps existing RTAPI functions:

```c
// tp_platform.h - Platform abstraction layer
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H

#include "rtapi_math.h"

// Math function aliases - currently identical to RTAPI
#define TP_SQRT(x)      rtapi_sqrt(x)
#define TP_SIN(x)       rtapi_sin(x)
#define TP_COS(x)       rtapi_cos(x)
#define TP_ATAN2(y, x)  rtapi_atan2(y, x)
#define TP_FABS(x)      rtapi_fabs(x)
#define TP_FMA(x, y, z) rtapi_fma(x, y, z)

#endif // TP_PLATFORM_H
```

**Verification**:
```bash
cd src && make clean && make
# Expected: Compiles successfully, no behavior change
```

**Commit**: "Add tp_platform.h with RTAPI aliases (no functional change)"

#### Step 2: Include Header in ONE File (No Usage Yet)

Add the include to `spherical_arc.c` but don't use any macros yet:

```c
// spherical_arc.c
#include "rtapi_math.h"
#include "tp_platform.h"  // Include but don't use yet - preparing for migration

// ... rest of file unchanged ...
```

**Verification**:
```bash
cd src && make clean && make
# Expected: Compiles successfully, identical behavior
```

**Commit**: "Include tp_platform.h in spherical_arc.c (no functional change)"

#### Step 3: Replace ONE Function Call

Replace a single call in `spherical_arc.c`:

```c
// Before:
double mag = rtapi_sqrt(x*x + y*y + z*z);

// After:
double mag = TP_SQRT(x*x + y*y + z*z);
```

**Verification**:
```bash
cd src && make clean && make
cd tests && ./runtests spherical_arc
# Expected: All tests pass, identical behavior
```

**Commit**: "Use TP_SQRT in spherical_arc.c (no functional change)"

#### Step 4: Systematically Replace Remaining Calls

Continue replacing each RTAPI math call with TP_* equivalent in `spherical_arc.c`:

```c
// Replace rtapi_sin -> TP_SIN
// Replace rtapi_cos -> TP_COS
// Replace rtapi_atan2 -> TP_ATAN2
// etc.
```

**Verification** (after each small batch):
```bash
cd src && make clean && make
cd tests && ./runtests spherical_arc
```

**Commit**: "Complete TP_* macro migration in spherical_arc.c"

#### Step 5: Move to Next File

Repeat steps 2-4 for `blendmath.c`, then `tc.c`, then `tcq.c`, then `tp.c`, then `sp_scurve.c`.

**Key Insight**: Each step is so small that if something breaks, you know exactly what caused it.

### Verification Strategy

During migration, add temporary verification code to ensure behavioral equivalence:

#### Example 1: Verifying State Synchronization

When adding `tp_sync_motion_state()` to copy global state:

```c
// In tp.c
static void tp_sync_motion_state(TP_STRUCT *tp) {
    // Copy state from global emcmotStatus to tp->motion_state
    tp->motion_state.stepping = emcmotStatus->stepping;
    tp->motion_state.maxFeedScale = emcmotStatus->maxFeedScale;
    tp->motion_state.planner_type = emcmotStatus->planner_type;
    // ... copy all required fields ...
    
    // TEMPORARY VERIFICATION: Confirm our copy matches the original
    #ifdef TP_MIGRATION_VERIFY
    assert(tp->motion_state.stepping == emcmotStatus->stepping);
    assert(tp->motion_state.maxFeedScale == emcmotStatus->maxFeedScale);
    assert(tp->motion_state.planner_type == emcmotStatus->planner_type);
    // This assert fires if we miss a field or copy incorrectly
    #endif
}
```

**Usage**:
```bash
# Build with verification enabled during migration
cd src && make EXTRA_CFLAGS="-DTP_MIGRATION_VERIFY" clean all
cd tests && ./runtests

# Once verified, build without verification for production
cd src && make clean all
```

**Remove verification code** only after the migration is complete and stable for several releases.

#### Example 2: Verifying Callback Equivalence

When migrating from direct global access to callbacks:

```c
// Old code:
double max_vel = emcmotStatus->vel;

// New code with verification:
double max_vel = tp->callbacks.get_max_velocity(tp->user_data);

#ifdef TP_MIGRATION_VERIFY
double old_max_vel = emcmotStatus->vel;
assert(fabs(max_vel - old_max_vel) < 1e-10);  // Should be identical
#endif
```

#### Example 3: Numerical Equivalence Checks

For math function replacements:

```c
// Verify TP_SQRT matches rtapi_sqrt
#ifdef TP_MIGRATION_VERIFY
double test_val = 16.0;
double old_result = rtapi_sqrt(test_val);
double new_result = TP_SQRT(test_val);
assert(old_result == new_result);  // Should be bitwise identical
#endif
```

### PR Strategy Table

Break the migration into small, reviewable pull requests. Each PR should take 30-60 minutes to review.

| PR # | Description | Risk Level | Lines Changed | What to Test | Est. Time |
|------|-------------|------------|---------------|--------------|-----------|
| 1 | Add `tp_platform.h` (RTAPI aliases only) | None | +20 | Compiles | 15 min |
| 2 | Include header in `spherical_arc.c` | None | +1 | Compiles | 10 min |
| 3 | Use `TP_*` macros in `spherical_arc.c` | Very Low | ~15 | Full test suite | 1 hour |
| 4 | Include header in `blendmath.c` | None | +1 | Compiles | 10 min |
| 5 | Use `TP_*` macros in `blendmath.c` | Low | ~30 | Full test suite | 1 hour |
| 6 | Include header in `tcq.c` | None | +1 | Compiles | 10 min |
| 7 | Use `TP_*` macros in `tcq.c` | Very Low | ~10 | Full test suite | 45 min |
| 8 | Include header in `tc.c` | None | +1 | Compiles | 10 min |
| 9 | Use `TP_*` macros in `tc.c` | Low | ~20 | Full test suite | 1 hour |
| 10 | Include header in `sp_scurve.c` | None | +1 | Compiles | 10 min |
| 11 | Use `TP_*` macros in `sp_scurve.c` | Low | ~25 | Full test suite + S-curve tests | 1.5 hours |
| 12 | Include header in `tp.c` | None | +1 | Compiles | 10 min |
| 13 | Use `TP_*` macros in `tp.c` (batch 1) | Low | ~40 | Full test suite | 1.5 hours |
| 14 | Use `TP_*` macros in `tp.c` (batch 2) | Low | ~40 | Full test suite | 1.5 hours |
| 15 | Create `tp_motion_interface.h` | None | +50 | Compiles | 30 min |
| 16 | Add motion state struct to `TP_STRUCT` | Very Low | +15 | Compiles | 30 min |
| 17 | Implement `tp_sync_motion_state()` with verification | Low | +30 | Full test suite | 2 hours |
| 18 | Migrate first global pointer access to state struct | Moderate | ~20 | Full test suite | 2 hours |
| ... | Continue incremental migration | ... | ... | ... | ... |

**Total PRs**: Approximately 40-50 for complete migration  
**Review Effort**: 10-15 minutes per PR on average  
**Testing Effort**: 1 hour per PR with code changes

**Key Benefits**:
- Small PRs are easy to review and understand
- Each PR can be reverted independently
- Test failures clearly point to the change that caused them
- Reviewers can give feedback early and often
- Reduces risk of large merge conflicts

### Testing Checklist

For each PR that changes code (not just headers), run this complete checklist:

#### 1. Compile Check
```bash
cd /home/runner/work/linuxcnc/linuxcnc/src
make clean
make
# Expected: Clean build with no new warnings
```

#### 2. Unit Tests (if applicable)
```bash
cd /home/runner/work/linuxcnc/linuxcnc/unit_tests
./run_tests.py
# Expected: All tests pass, no new failures
```

#### 3. Integration Tests
```bash
cd /home/runner/work/linuxcnc/linuxcnc/tests
./runtests
# Expected: All tests pass
# Note: Run specific subsystem tests for targeted changes:
./runtests tp           # For TP-related changes
./runtests trajectory   # For trajectory changes
./runtests blend        # For blending changes
```

#### 4. Smoke Test (Manual Verification)
```bash
# Start LinuxCNC simulator
linuxcnc configs/sim/axis/axis.ini

# Perform basic operations:
# 1. Machine home (all axes)
# 2. Manual jog (X, Y, Z axes)
# 3. MDI: G0 X10 Y10 (rapid move)
# 4. MDI: G1 X0 Y0 F100 (feed move)
# 5. Run simple G-code program (e.g., ngc_files/examples/circle.ngc)
# 6. Verify smooth motion, no jerks or unusual behavior
```

#### 5. Performance Check (for changes to hot paths)
```bash
# Run performance-sensitive test cases
cd /home/runner/work/linuxcnc/linuxcnc/tests
./runtests performance

# Monitor timing:
# - Trajectory cycle time should remain consistent
# - No new latency spikes in real-time thread
```

#### 6. Regression Check
```bash
# Compare behavior with baseline commit
git checkout <baseline-commit>
# Run test program, save output
./save_baseline_results.sh

git checkout <your-branch>
# Run same test program, compare output
./compare_with_baseline.sh
# Expected: Identical trajectory output
```

### Rollback Procedure

Each PR must be independently revertable. Follow this procedure if issues are discovered:

#### 1. Identify the Problem PR

```bash
# Check recent commits
git log --oneline -10

# Identify which PR introduced the issue
# Use git bisect if necessary
git bisect start
git bisect bad HEAD
git bisect good <known-good-commit>
# ... follow bisect process ...
```

#### 2. Verify Revert is Safe

```bash
# Create a test revert on a branch
git checkout -b test-revert
git revert <problem-commit-sha>

# Verify the revert compiles and tests pass
cd src && make clean && make
cd tests && ./runtests

# If successful, the revert is safe
```

#### 3. Perform the Revert

```bash
# Switch to main branch
git checkout main
git pull

# Revert the problematic commit
git revert <problem-commit-sha>

# Push the revert
git push origin main
```

#### 4. Document and Investigate

```markdown
## Revert Reason

**Reverted commit**: <sha> - "Description"
**Reason**: <detailed explanation of the problem>
**Symptoms**: <what broke, test failures, etc.>
**Next steps**: 
- [ ] Investigate root cause
- [ ] Create fix
- [ ] Re-apply with fix in new PR
```

#### 5. Prevention for Future PRs

- **Run full test suite** before submitting PR
- **Test on multiple configurations** (different machines, real hardware if possible)
- **Let PR sit for 24 hours** before merging to allow for additional review
- **Monitor CI/CD results** carefully
- **Keep PRs small** to make reverts less disruptive

### Migration Phase Breakdown

Apply this strategy to each phase of the migration:

#### Phase 0: Preparation (Before Any Code Changes)
1. Document current test coverage
2. Establish baseline performance metrics
3. Set up CI/CD for automated testing
4. Create feature branch

#### Phase 1: Platform Abstraction Layer (PRs 1-14)
- `tp_platform.h` creation and migration
- Risk: Very Low
- Duration: 2-3 weeks (with review cycles)

#### Phase 2: Motion Interface Abstraction (PRs 15-25)
- `tp_motion_interface.h` creation
- State structure migration
- Risk: Low-Moderate
- Duration: 3-4 weeks

#### Phase 3: Callback System Migration (PRs 26-35)
- Replace global pointers with callbacks
- Risk: Moderate
- Duration: 3-4 weeks

#### Phase 4: Final Isolation (PRs 36-45)
- Extract to standalone library
- Update build system
- Risk: Low
- Duration: 2-3 weeks

**Total Duration**: 10-14 weeks with thorough testing and review

### Success Criteria

A migration step is successful when:

- ✅ LinuxCNC compiles without new warnings
- ✅ All existing tests pass
- ✅ Manual smoke test shows normal operation
- ✅ Code review approved by at least one maintainer
- ✅ No performance regression (< 1% change in cycle time)
- ✅ Documentation updated (if applicable)
- ✅ Commit message clearly describes the change and rationale

### Why This Approach Succeeds

1. **Psychological Safety**: Developers can make changes confidently knowing each step is validated
2. **Easy Debugging**: Problems are immediately obvious because changes are minimal
3. **Continuous Integration**: The codebase is always in a working state
4. **Reviewable**: Small PRs are actually reviewed thoroughly, catching issues early
5. **Recoverable**: Any step can be reverted without cascading failures
6. **Measurable**: Progress is clear and quantifiable

**Remember**: The goal is not to go fast—it's to go safely and never break the build.

---

## S-Curve Integration

### In blendmath.c:
```c
#include "sp_scurve.h"

// Called from blend calculations:
double findSCurveVSpeed(double distance, double a_max, double j_max, double *v_req);
double calcSCurveSpeedWithT(double a, double j, double t);
```

### In tp.c:
```c
extern emcmot_status_t *emcmotStatus;

#define GET_TRAJ_PLANNER_TYPE() (emcmotStatus->planner_type)
// Returns: 0 = trapezoidal, 1 = S-curve
```

### Abstraction Strategy:
- Create `tp_platform.h` with planner type getter
- Wrap S-curve functions with `tp_` prefix
- Make `emcmotStatus` access optional via callbacks

## Timeline Revision

### Current Estimate (Based on Master aef0cfa)
- Phase 1: 3-4 days (6 files, S-curve included)
- Phase 2: Testing - 2 days
- **Total: 5-6 days**

**Savings:** 3-4 days eliminated by using master's S-curve implementation!

### What We Don't Need to Port

✅ **Already in Master:**
- S-curve INI parameter parsing (`[TRAJ] PLANNER_TYPE`, `MAX_JERK`)
- HAL pin integration for jerk limits
- S-curve unit tests
- Configuration validation
- Documentation for S-curve usage

---

## Migration Phases

## Phase 1: Create Abstraction Layer (1-2 weeks)

**Goal**: Introduce abstraction headers that decouple TP from RTAPI and motion module specifics, without changing existing TP code behavior.

### Alternative Approach: Direct File Porting

If taking the direct file porting approach instead of in-place refactoring:

#### Step 1: Setup Isolated TP Directory

Create new directory structure:
```
isolated-tp/
├── src/
│   ├── sp_scurve.c
│   ├── blendmath.c
│   ├── tc.c
│   ├── tcq.c
│   ├── spherical_arc.c
│   └── tp.c
├── include/
│   ├── sp_scurve.h
│   ├── tp_platform.h
│   └── tp_config.h
└── tests/
```

#### Step 2: Port Files in Order

**Order changed to handle dependencies:**

1. **sp_scurve.c** (Day 1, 6 hours)
   - Self-contained math library
   - Minimal dependencies
   - Refactor math functions to tp_platform.h
   - Remove mot_priv.h dependency

2. **blendmath.c** (Day 1-2, 6 hours)
   - Depends on: sp_scurve.h (now available)
   - Remove motion.h includes
   - Wrap S-curve calls

3. **spherical_arc.c** (Day 2, 2 hours)
   - Minimal dependencies
   - Straightforward port

4. **tc.c** (Day 2-3, 4 hours)
   - Segment implementation
   - Minimal external dependencies

5. **tcq.c** (Day 3, 2 hours)
   - Simple queue operations
   - No S-curve dependencies

6. **tp.c** (Day 3-4, 8 hours)
   - Main planner logic
   - S-curve integration
   - Depends on all above

**Total Porting Time:** 3-4 days for 6 files

### Traditional Approach: Tasks

#### 1.1 Create `tp_platform.h` (2-3 days)

**Purpose**: Abstract all RTAPI dependencies

**Interface**:
```c
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H

// Platform configuration structure
typedef struct {
    // Math functions (can use standard or RTAPI versions)
    double (*sin)(double);
    double (*cos)(double);
    double (*sqrt)(double);
    double (*atan2)(double, double);
    double (*fabs)(double);
    // ... other math functions as needed
    
    // Logging
    void (*log_error)(const char *fmt, ...);
    void (*log_warning)(const char *fmt, ...);
    void (*log_info)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);
} tp_platform_config_t;

// Global platform config (set once at initialization)
extern tp_platform_config_t *tp_platform;

// Convenience macros that match current usage
#define TP_SIN(x)   (tp_platform->sin(x))
#define TP_COS(x)   (tp_platform->cos(x))
#define TP_SQRT(x)  (tp_platform->sqrt(x))
// ... etc

#define TP_LOG_ERR(...)  (tp_platform->log_error(__VA_ARGS__))
#define TP_LOG_WARN(...) (tp_platform->log_warning(__VA_ARGS__))

#endif
```

**Changes Required**:
- Create new file `src/emc/tp/tp_platform.h`
- Implement default RTAPI-based configuration in tp.c
- No changes to existing TP algorithm code yet

**Success Criteria**:
- Header compiles cleanly
- Can be included without RTAPI headers
- Default configuration uses existing RTAPI functions

#### 1.2 Create `tp_motion_interface.h` (3-4 days)

**Purpose**: Define explicit interface to motion controller data

**Interface**:
```c
#ifndef TP_MOTION_INTERFACE_H
#define TP_MOTION_INTERFACE_H

#include "posemath.h"
#include "emcpose.h"

// Motion controller status (what TP reads)
typedef struct {
    bool stepping;              // Is motion controller in stepping mode?
    double maxFeedScale;        // Feed override (0.0 to 1.0)
    EmcPose carte_pos_cmd;      // Current commanded position
    
    // Spindle status
    double spindle_speed;       // Current spindle speed (RPM)
    bool spindle_at_speed;      // Is spindle at commanded speed?
    double spindle_revs;        // Spindle revolution count
    bool spindle_index_enable;  // Spindle index pulse status
    
    // Limits and status
    bool on_soft_limit;         // Is any axis on soft limit?
} tp_motion_status_t;

// Motion controller configuration (what TP needs to know)
typedef struct {
    double trajCycleTime;       // Trajectory cycle time (seconds)
    int numJoints;              // Number of joints in system
    
    // Limits
    double max_velocity;        // System max velocity
    double max_acceleration;    // System max acceleration
} tp_motion_config_t;

// Function to populate status from motion module
void tp_motion_get_status(tp_motion_status_t *status);

// Function to get configuration
void tp_motion_get_config(tp_motion_config_t *config);

#endif
```

**Changes Required**:
- Create new file `src/emc/tp/tp_motion_interface.h`
- Implement adapter functions in tp.c that extract data from `emcmotStatus` and `emcmotConfig`
- Document exactly what TP reads from motion module

**Success Criteria**:
- Interface compiles independently
- Adapter functions correctly extract all needed data
- Documentation clearly lists all dependencies

#### 1.3 Create `tp_callbacks.h` (1-2 days)

**Purpose**: Formalize callback interface (this already exists, just needs documentation)

**Interface**:
```c
#ifndef TP_CALLBACKS_H
#define TP_CALLBACKS_H

// Callback function types
typedef void (*tp_dio_write_fn)(int index, char value);
typedef void (*tp_aio_write_fn)(int index, double value);
typedef void (*tp_set_rotary_unlock_fn)(int axis, int state);
typedef int  (*tp_get_rotary_is_unlocked_fn)(int axis);
typedef double (*tp_get_axis_vel_limit_fn)(int axis);
typedef double (*tp_get_axis_acc_limit_fn)(int axis);

// Callback structure
typedef struct {
    tp_dio_write_fn              dio_write;
    tp_aio_write_fn              aio_write;
    tp_set_rotary_unlock_fn      set_rotary_unlock;
    tp_get_rotary_is_unlocked_fn get_rotary_is_unlocked;
    tp_get_axis_vel_limit_fn     get_axis_vel_limit;
    tp_get_axis_acc_limit_fn     get_axis_acc_limit;
} tp_callbacks_t;

// Set callbacks (called once at initialization)
void tp_set_callbacks(const tp_callbacks_t *callbacks);

#endif
```

**Changes Required**:
- Create new file `src/emc/tp/tp_callbacks.h`
- Refactor existing `tpMotFunctions()` to use this structure
- Keep existing function signature for compatibility

**Success Criteria**:
- Existing callback mechanism works unchanged
- New structure-based interface is cleaner
- Both old and new interfaces coexist temporarily

### Phase 1 Testing & Verification

**Regression Testing**:
1. Build LinuxCNC with new headers
2. Run all existing integration tests
3. Verify no behavioral changes
4. Performance benchmarking (should be identical)

**Success Criteria**:
- All builds succeed
- All tests pass
- Zero functional changes
- Abstraction headers are in place for Phase 2

**Estimated Effort**: 1-2 weeks (6-9 days of coding + testing)

### Files Affected in Phase 1

| File | Lines Changed | Type |
|------|---------------|------|
| `src/emc/tp/tp_platform.h` | +150 | New file |
| `src/emc/tp/tp_motion_interface.h` | +80 | New file |
| `src/emc/tp/tp_callbacks.h` | +50 | New file |
| `src/emc/tp/tp.c` | +100 | Adapter code |
| **Total** | **~380 LOC** | **Minor additions** |

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
