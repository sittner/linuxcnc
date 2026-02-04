# TP Isolation Compatibility Strategy

## Overview

This document describes how existing LinuxCNC code and external modules will continue to work during and after the TP isolation migration. A key principle of this migration is **full backward compatibility** throughout all phases.

---

## Core Compatibility Principles

1. **No Breaking Changes**: Existing LinuxCNC code continues to work unchanged
2. **Graceful Migration**: Old and new APIs coexist during transition
3. **External Module Support**: External TP modules (like tpcomp.comp) keep working
4. **Configuration Compatibility**: INI files and configurations remain valid
5. **Binary Compatibility**: ABI stability for external modules where possible

---

## LinuxCNC Core Compatibility

### Current LinuxCNC Integration

**How TP is Currently Used**:
```c
// In motion controller (motion/control.c):

// Initialization
tpCreate(&emcmotDebug->queue, emcmotConfig->tpQueueSize, 0);

// Set global pointers (current interface)
tpMotData(&emcmotStatus, &emcmotConfig);
tpMotFunctions(
    &emcmotDioWrite,
    &emcmotAioWrite,
    &emcmotSetRotaryUnlock,
    &emcmotGetRotaryIsUnlocked,
    &axis_get_vel_limit,
    &axis_get_acc_limit
);

// Each cycle
tpRunCycle(&emcmotDebug->queue, period);

// Add motion
tpAddLine(&emcmotDebug->queue, end, type, vel, maxvel, acc, enables, atspeed, indexrotary, tag);
```

### After Isolation: Existing Code Still Works

**Approach**: Maintain compatibility layer

**Implementation**:
```c
// tp.c - compatibility layer

// Static pointer for backward compatibility with old interface
static TP_STRUCT *_default_tp_instance = NULL;
static emcmot_status_t *_compat_emcmot_status = NULL;
static emcmot_config_t *_compat_emcmot_config = NULL;

// Old interface - still works!
void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig) {
    _compat_emcmot_status = pstatus;
    _compat_emcmot_config = pconfig;
    
    // If there's a default TP instance, configure it
    if (_default_tp_instance) {
        tp_motion_set_config(_default_tp_instance, pconfig);
    }
}

void tpMotFunctions(
    void(*pDioWrite)(int,char),
    void(*pAioWrite)(int,double),
    void(*pSetRotaryUnlock)(int,int),
    int( *pGetRotaryUnlock)(int),
    double(*paxis_get_vel_limit)(int),
    double(*paxis_get_acc_limit)(int)
) {
    if (_default_tp_instance) {
        tp_callbacks_t callbacks = {
            .dio_write = pDioWrite,
            .aio_write = pAioWrite,
            .set_rotary_unlock = pSetRotaryUnlock,
            .get_rotary_is_unlocked = pGetRotaryUnlock,
            .get_axis_vel_limit = paxis_get_vel_limit,
            .get_axis_acc_limit = paxis_get_acc_limit,
        };
        tp_set_callbacks(_default_tp_instance, &callbacks);
    }
}

// Enhanced tpCreate sets up default instance
int tpCreate(TP_STRUCT *tp, int queueSize, int id) {
    // New implementation
    int result = tpCreate_new(tp, queueSize, id);
    
    // Set as default for compatibility
    _default_tp_instance = tp;
    
    // If motion data already set, configure now
    if (_compat_emcmot_config) {
        tp_motion_set_config(tp, _compat_emcmot_config);
    }
    
    return result;
}

// tpRunCycle automatically updates motion status
int tpRunCycle(TP_STRUCT *tp, long period) {
    // Update motion status from global (if using old interface)
    if (tp == _default_tp_instance && _compat_emcmot_status) {
        tp_motion_populate_status(tp, _compat_emcmot_status);
    }
    
    // Run cycle with new implementation
    return tpRunCycle_new(tp, period);
}
```

**Result**: **Zero changes required** to existing LinuxCNC motion controller code

### Gradual Migration Path for LinuxCNC Core

While existing code continues to work, the LinuxCNC core can optionally migrate to the new, cleaner API over time:

**Phase 0**: Current state (pre-migration)
- Uses old global pointer interface

**Phase 1**: After TP isolation (compatibility maintained)
- Old interface still works via compatibility layer
- New interface available but optional

**Phase 2**: Gradual internal update (optional, future)
```c
// motion/control.c can optionally update to new interface:

// Create TP
tpCreate(&emcmotDebug->queue, emcmotConfig->tpQueueSize, 0);

// Use new explicit interface
tp_platform_config_t *platform = tp_get_rtapi_platform();
tp_set_platform(&emcmotDebug->queue, platform);

tp_callbacks_t callbacks = {
    .dio_write = emcmotDioWrite,
    .aio_write = emcmotAioWrite,
    .set_rotary_unlock = emcmotSetRotaryUnlock,
    .get_rotary_is_unlocked = emcmotGetRotaryIsUnlocked,
    .get_axis_vel_limit = axis_get_vel_limit,
    .get_axis_acc_limit = axis_get_acc_limit,
};
tp_set_callbacks(&emcmotDebug->queue, &callbacks);

// Each cycle
tp_motion_populate_status(&emcmotDebug->queue, &emcmotStatus);
tpRunCycle(&emcmotDebug->queue, period);
```

**Phase 3**: Deprecate old interface (far future, optional)
- Compile-time warnings when using old interface
- Document migration path
- Maintain for multiple major versions

**Phase 4**: Remove old interface (optional, major version bump)
- Only if ecosystem has migrated
- Provide clear timeline (years, not months)

---

## External Module Compatibility

### External TP Modules

LinuxCNC supports external trajectory planner modules via HAL components. The most notable example is `tpcomp.comp`.

### Current External Module Interface

**Example**: tpcomp.comp
```c
// External component that uses TP interface

// Uses same tpMotData/tpMotFunctions interface
tpMotData(&status, &config);
tpMotFunctions(&dio_write, &aio_write, ...);

// Uses TP API
tpCreate(&tp, queueSize, id);
tpAddLine(&tp, end, ...);
tpRunCycle(&tp, period);
```

### Compatibility Strategy for External Modules

**Approach**: Keep compatibility layer indefinitely for external modules

**Rationale**:
1. External modules are out of our control (may not be updated)
2. Breaking external modules harms users
3. Compatibility layer cost is low
4. Better user experience

**Implementation**:
- Compatibility layer (described above) supports external modules
- No changes required to external modules
- They continue to work with isolated TP library

### Testing External Module Compatibility

**Test Strategy**:
1. Maintain test external module in LinuxCNC tree
2. Test that it compiles and runs after each migration phase
3. Document any compatibility issues found
4. Fix compatibility layer if needed

**Test Module**:
```c
// tests/external_tp_module_test.comp
// Minimal external TP module for compatibility testing

component external_tp_module_test;
pin in float period;
pin out bit test_ok;

// Uses old TP interface
function _;
license "GPL";
;;

#include "tp.h"

static TP_STRUCT test_tp;
static emcmot_status_t test_status;
static emcmot_config_t test_config;

FUNCTION(_) {
    // Test that old interface works
    tpMotData(&test_status, &test_config);
    tpMotFunctions(&mock_dio, &mock_aio, ...);
    
    tpCreate(&test_tp, 32, 0);
    tpRunCycle(&test_tp, period);
    
    test_ok = tpIsDone(&test_tp);
}
```

### Binary Compatibility (ABI)

**Challenge**: Maintaining binary compatibility for compiled external modules

**TP_STRUCT Changes**:
- Phase 2 adds fields to TP_STRUCT
- Could break binary compatibility if external modules allocate TP_STRUCT

**Mitigation**:
1. **Recommended**: External modules should use `tpCreate()`, not allocate TP_STRUCT directly
   - This is current practice (good!)
   - TP_STRUCT is opaque to external modules
   
2. **If needed**: Add fields at end of structure (preserves layout)

3. **If needed**: Version the API/ABI
   ```c
   #define TP_API_VERSION 2
   int tpGetApiVersion(void);  // Returns TP_API_VERSION
   ```

**Current Analysis**: 
- External modules use tpCreate() (allocation inside TP library)
- External modules don't directly allocate TP_STRUCT
- **Risk: LOW** - binary compatibility should be maintained

---

## Configuration File Compatibility

### INI File Compatibility

**Current Configuration**: INI files configure trajectory planner parameters

**Example INI**:
```ini
[TRAJ]
COORDINATES = X Y Z
LINEAR_UNITS = mm
ANGULAR_UNITS = degree
MAX_VELOCITY = 100.0
MAX_ACCELERATION = 1000.0
```

**After Isolation**: **No changes required**

The motion controller still reads INI files and passes values to TP via the same API:
```c
// motion controller reads INI
double max_vel = inifile->Find("MAX_VELOCITY", "TRAJ");

// Passes to TP (same API as before)
tpSetVmax(&tp, max_vel, max_vel);
```

**Result**: All existing INI files work unchanged

### HAL Configuration Compatibility

**Current**: TP may expose HAL pins (optional)

```python
# HAL configuration
setp traj.max-velocity 100.0
setp traj.max-acceleration 1000.0
```

**After Isolation**: **No changes required**

HAL pins are created by motion controller, not TP library:
- Motion controller creates HAL pins (unchanged)
- Motion controller reads HAL pins (unchanged)
- Motion controller passes values to TP (unchanged)

**Result**: All existing HAL configurations work unchanged

---

## Deprecation Strategy

### Philosophy

- **Conservative**: Keep compatibility longer rather than shorter
- **Communicate**: Clearly document what's deprecated and why
- **Timeline**: Multiple release cycles before removal (if ever)
- **Migration Guide**: Provide clear path forward

### Deprecation Timeline (Proposal)

**Version N.M** (Current):
- ✅ Old API only

**Version N+1.0** (After TP isolation):
- ✅ Old API works (compatibility layer)
- ✅ New API available
- ✅ Documentation recommends new API for new code

**Version N+2.0** (6-12 months later):
- ✅ Old API works
- ⚠️  Compile-time deprecation warnings (can be disabled)
- ✅ New API is standard
- ✅ Migration guide published

**Version N+3.0** (12-24 months later):
- ✅ Old API still works
- ⚠️  Runtime deprecation warnings (logged once)
- ✅ Most internal LinuxCNC code uses new API

**Version N+4.0** (24+ months later):
- ❓ Optional: Consider removing old API
- ❓ Only if: Ecosystem has migrated
- ❓ Requires: Community consensus

**Recommended**: Keep compatibility layer indefinitely
- Cost is low (small amount of compatibility code)
- Benefit is high (external modules keep working)
- Aligns with LinuxCNC stability goals

### Deprecation Warnings

**Compile-Time Warning** (optional, future):
```c
// tp.h
#ifdef TP_WARN_DEPRECATED
#warning "tpMotData() is deprecated. Use tp_set_platform() and tp_set_callbacks() instead."
#endif

void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig)
#ifdef __GNUC__
    __attribute__((deprecated("Use tp_set_platform() and tp_set_callbacks()")))
#endif
;
```

**Runtime Warning** (optional, future):
```c
void tpMotData(emcmot_status_t *pstatus, emcmot_config_t *pconfig) {
    static bool warned = false;
    if (!warned) {
        TP_LOG_WARN("tpMotData() is deprecated. Use tp_set_platform() and tp_set_callbacks().");
        warned = true;
    }
    // ... compatibility implementation
}
```

---

## Migration Guide for External Module Developers

### If You Want to Keep Using Old API

**Answer**: Nothing to do! Your module continues to work.

**Compatibility Guarantee**: Old API will be supported for foreseeable future (years).

### If You Want to Migrate to New API

**Benefit**: Cleaner code, explicit dependencies, better testability

**Migration Steps**:

**Step 1**: Update includes
```c
// Old:
#include "tp.h"

// New (same file, but new features available):
#include "tp.h"
#include "tp_platform.h"  // If you want platform control
```

**Step 2**: Use new platform interface (optional)
```c
// Old:
// (TP used RTAPI automatically)

// New (explicit platform):
tp_platform_config_t *platform = tp_get_rtapi_platform();
tp_set_platform(&tp, platform);

// Or use standard library in user-space component:
tp_platform_config_t *platform = tp_get_standard_platform();
tp_set_platform(&tp, platform);
```

**Step 3**: Use new callback interface (optional)
```c
// Old:
tpMotFunctions(&dio_write, &aio_write, ...);

// New:
tp_callbacks_t callbacks = {
    .dio_write = dio_write,
    .aio_write = aio_write,
    .set_rotary_unlock = set_rotary_unlock,
    .get_rotary_is_unlocked = get_rotary_is_unlocked,
    .get_axis_vel_limit = axis_get_vel_limit,
    .get_axis_acc_limit = axis_get_acc_limit,
};
tp_set_callbacks(&tp, &callbacks);
```

**Step 4**: Use new motion interface (if applicable)
```c
// Old:
tpMotData(&status, &config);

// New:
tp_motion_set_config(&tp, &config);

// Each cycle:
tp_motion_populate_status(&tp, &status);
tpRunCycle(&tp, period);
```

**Step 5**: Test thoroughly
- Verify all functionality works
- Check performance is unchanged
- Test edge cases

**Documentation**: Full migration examples will be provided in API documentation

---

## Version Compatibility Matrix

| LinuxCNC Version | TP API Version | Old API | New API | External Modules | Notes |
|------------------|----------------|---------|---------|------------------|-------|
| 2.9.x (current) | 1.0 | ✅ Only | ❌ N/A | ✅ Works | Current state |
| 3.0.x (after isolation) | 2.0 | ✅ Compat | ✅ Available | ✅ Works | Compatibility layer |
| 3.1.x | 2.0 | ✅ Compat | ✅ Standard | ✅ Works | New API documented |
| 3.2.x | 2.0 | ⚠️ Deprecated | ✅ Standard | ✅ Works | Deprecation warnings |
| 4.0.x | 2.0 | ⚠️ Deprecated | ✅ Only | ✅ Works | Old API discouraged |
| 5.0.x (future?) | 2.0 | ❓ TBD | ✅ Only | ✅ Works | Old API removal? |

**Legend**:
- ✅ Fully supported
- ⚠️ Supported but deprecated
- ❌ Not available
- ❓ To be determined by community

---

## Testing Compatibility

### Test Strategy

**Phase-by-Phase Testing**:

**After Phase 1** (Abstraction layer):
- ✅ All existing LinuxCNC tests pass
- ✅ External module test compiles
- ✅ No functional changes

**After Phase 2** (Refactoring):
- ✅ All existing LinuxCNC tests pass
- ✅ External module test runs correctly
- ✅ Performance benchmarks unchanged

**After Phase 3** (Library extraction):
- ✅ All existing LinuxCNC tests pass
- ✅ External module test works with library
- ✅ Configuration files unchanged

**After Phase 4** (Unit testing):
- ✅ All existing tests pass
- ✅ New unit tests pass
- ✅ External modules still work

### Compatibility Test Suite

**Automated Tests**:
1. All existing LinuxCNC integration tests
2. External module compilation test
3. External module runtime test
4. Configuration file compatibility test
5. HAL pin compatibility test
6. Performance regression tests

**Test Coverage**:
```bash
# Run compatibility test suite
./tests/compatibility/test_all.sh

# Tests:
# - Build external TP module
# - Run external TP module
# - Verify INI file loading
# - Verify HAL configuration
# - Check performance metrics
# - Verify all existing tests pass
```

### Continuous Integration

**CI Pipeline**:
1. Build LinuxCNC with isolated TP
2. Run all existing tests
3. Build external module test
4. Run external module test
5. Performance benchmarking
6. Report any compatibility issues

**Acceptance Criteria**:
- ✅ All tests pass
- ✅ No performance regression (< 1%)
- ✅ External modules work
- ✅ Configurations load correctly

---

## Rollback Plan

### If Compatibility Issues Found

**Immediate**:
1. Revert problematic commit
2. Analyze issue
3. Fix compatibility layer
4. Re-apply with fix

**Each Phase Has Rollback**:
- Phase 1: Just remove new headers (1 hour)
- Phase 2: Revert refactoring commits (few hours)
- Phase 3: Remove library, restore direct build (1 day)
- Phase 4: Remove tests (1 hour)

### Emergency Rollback

**If Critical Issue Found in Production**:
1. Release branch without changes available
2. Documented quick rollback procedure
3. Communication plan for users
4. Fix plan for re-attempt

**Mitigation**: Thorough testing before release prevents need for emergency rollback

---

## Communication Plan

### Documentation

**For Users**:
- Release notes explain changes (or lack thereof)
- "What's new" emphasizes compatibility
- Configuration migration guide (even if just "no changes needed")

**For Developers**:
- API documentation for new interface
- Migration guide for internal developers
- Examples of both old and new usage

**For External Module Developers**:
- Compatibility guarantee statement
- Optional migration guide
- Support for questions

### Release Notes Template

```markdown
## LinuxCNC 3.0 Release Notes

### Trajectory Planner Improvements

The trajectory planner has been refactored into a standalone library,
improving testability and code quality.

**User Impact**: NONE - All existing configurations, INI files, and 
external modules continue to work without changes.

**Developer Impact**: New, cleaner API available for new code. Old API
continues to work indefinitely.

**External Module Developers**: Your modules continue to work unchanged.
Optional migration guide available in documentation.

See docs/isolated-tp/ for detailed information.
```

---

## Compatibility Guarantees

### What We Guarantee

1. ✅ **Existing LinuxCNC code works**: Motion controller, configurations, etc.
2. ✅ **External modules work**: tpcomp.comp and similar modules
3. ✅ **INI files work**: No configuration changes required
4. ✅ **HAL configurations work**: No changes required
5. ✅ **Performance**: No significant regression (< 1% acceptable)
6. ✅ **Backward compatibility**: Old API works for multiple major versions

### What We Don't Guarantee

1. ❌ **Internal implementation**: May change (that's the point!)
2. ❌ **Undocumented internals**: If you rely on implementation details
3. ❌ **Binary compatibility of TP_STRUCT layout**: If you allocate it directly (don't do this!)

### Compatibility Statement

**Official Statement**:

> LinuxCNC 3.0 maintains full backward compatibility with LinuxCNC 2.9.x for
> all documented public APIs, configuration files, and external modules.
> 
> The trajectory planner isolation is an internal refactoring that improves
> code quality without affecting users or breaking existing code.
> 
> External TP modules (such as tpcomp.comp) will continue to work without
> modification. The existing TP API will be supported indefinitely to ensure
> external module compatibility.
> 
> A new, improved API is available for developers who wish to take advantage
> of the better architecture, but migration is optional.

---

## Conclusion

### Compatibility is a Priority

This migration plan prioritizes compatibility throughout:
- ✅ No breaking changes at any phase
- ✅ Extensive compatibility testing
- ✅ Clear rollback procedures
- ✅ Long-term API support

### Users and Developers Protected

- **Users**: No impact, everything works as before
- **LinuxCNC Developers**: Can optionally migrate to new API
- **External Module Developers**: No changes required, optional migration available

### Confidence in Migration

Strong compatibility strategy enables:
- Low-risk migration
- Incremental approach
- Easy rollback if needed
- Community confidence

**Result**: TP isolation can proceed with confidence that compatibility is maintained.
