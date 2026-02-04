# S-Curve Integration Notes

## Overview

The S-curve trajectory planner (`sp_scurve.c`) is fully integrated in master commit aef0cfa51b2892484fbcd6dd8242c7aafe9a282b.
This document describes how to port it to isolated TP.

## File Statistics

- **Lines of code:** ~1,000
- **Public functions:** 15
- **Internal functions:** 8
- **Dependencies:** math.h, simple_tp.h (optional)

## Key Functions

### Core S-Curve Calculation
```c
int findSCurveVSpeed(double distance, double a_max, double j_max, double *v_req);
```
Given distance, max acceleration, and max jerk, finds achievable velocity.

### Time-Constrained Speed
```c
double calcSCurveSpeedWithT(double a, double j, double t);
```
Calculates speed achievable in time `t` with acceleration `a` and jerk `j`.

### Full S-Curve Profile
```c
int calcSCurve(double S, double Vc, double Ve, double Vm, 
               double Ac, double Am, double Jm, double T,
               double n[8], double a[8], double v[8], 
               double *tn, double *verr, double *J2, double *J4);
```
Computes complete 7-phase S-curve profile.

## Dependencies to Abstract

### 1. Math Functions
```c
// Current usage:
fabs(), sqrt(), fma(), exp(), log(), acos(), cos(), sin()
floor(), ceil(), fmax(), fmin()

// Replace with:
tp_fabs(), tp_sqrt(), tp_fma(), tp_exp(), tp_log(), tp_acos()
tp_cos(), tp_sin(), tp_floor(), tp_ceil(), tp_fmax(), tp_fmin()
```

### 2. simple_tp.h (Optional)
```c
// Only used by getNext() function
// Can be disabled with #ifdef SIMPLE_TP_ENABLED
#include "simple_tp.h"
```

**Recommendation:** Disable `getNext()` in isolated TP - only used by simple planner, not main TP.

### 3. mot_priv.h (Remove)
```c
// Currently included but likely unused
#include "mot_priv.h"  // Remove this
```

## Integration with blendmath.c

**blendmath.c** calls S-curve functions for blend calculations:

```c
// In blendParamKinematics() around line 1150:
double findSCurveVPeak(double a_t_max, double j_t_max, double distance)
{
    double req_v;
    int result = findSCurveVSpeed(distance, a_t_max, j_t_max, &req_v);
    
    if (result != 1) {
        return findVPeak(a_t_max, distance);  // Fallback to trapezoidal
    }

    return fmin(req_v, findVPeak(a_t_max, distance));
}
```

## Integration with tp.c

**tp.c** uses S-curve for velocity calculations:

```c
// In tpComputeBlendSCurveVelocity():
*v_blend_this = fmin(v_reachable_this, 
                     calcSCurveSpeedWithT(acc_this, jerk, t_blend));
*v_blend_next = fmin(v_reachable_next, 
                     calcSCurveSpeedWithT(acc_next, jerk, t_blend));
```

## Planner Type Selection

S-curve vs trapezoidal is selected at runtime:

```c
// Current approach (in motion system):
extern emcmot_status_t *emcmotStatus;
#define GET_TRAJ_PLANNER_TYPE() (emcmotStatus->planner_type)

// Isolated TP approach:
int planner_type = tp_get_planner_type();  // 0=trapezoid, 1=S-curve
```

## Porting Checklist

- [ ] Copy `sp_scurve.c` to `isolated-tp/src/`
- [ ] Copy `sp_scurve.h` to `isolated-tp/include/`
- [ ] Add math function wrappers to `tp_platform.h`
- [ ] Replace all math calls with `TP_` prefix
- [ ] Remove `#include "mot_priv.h"`
- [ ] Make `simple_tp.h` optional (`#ifdef SIMPLE_TP_ENABLED`)
- [ ] Test cubic equation solver with unit tests
- [ ] Test S-curve velocity calculations
- [ ] Verify integration with `blendmath.c`
- [ ] Verify integration with `tp.c`

## Testing Strategy

### Unit Tests Needed

1. **Cubic Equation Solver**
   ```c
   // Test solve_cubic() accuracy
   test_solve_cubic_three_real_roots();
   test_solve_cubic_one_real_root();
   test_solve_cubic_edge_cases();
   ```

2. **S-Curve Velocity**
   ```c
   // Test findSCurveVSpeed()
   test_scurve_velocity_short_distance();
   test_scurve_velocity_long_distance();
   test_scurve_velocity_vs_trapezoidal();
   ```

3. **Time-Constrained Speed**
   ```c
   // Test calcSCurveSpeedWithT()
   test_scurve_speed_with_time();
   ```

### Integration Tests

1. Blend velocity calculation (with blendmath.c)
2. Trajectory planning (with tp.c)
3. Planner type switching (trapezoid ↔ S-curve)

## Known Issues / Gotchas

1. **Numerical Stability**
   - `solve_cubic()` uses Citardauq formula for stability
   - Don't replace with simpler cubic solver

2. **FMA Precision**
   - S-curve relies on fused multiply-add for precision
   - Fallback `x*y+z` is less accurate but acceptable

3. **simple_tp Dependency**
   - `getNext()` function is not used by main TP
   - Safe to disable with `#ifdef`

4. **Planner Type Access**
   - Must provide `tp_get_planner_type()` callback
   - Defaults to trapezoidal if callback not set

## Timeline

- **Refactoring:** 4 hours
- **Testing:** 2 hours
- **Integration:** 2 hours
- **Total:** 8 hours (1 day)

## Math Function Mapping

### Required for S-Curve

| Original | Abstraction | Notes |
|----------|-------------|-------|
| `fabs(x)` | `TP_FABS(x)` | Absolute value |
| `sqrt(x)` | `TP_SQRT(x)` | Square root |
| `fma(x,y,z)` | `TP_FMA(x,y,z)` | Fused multiply-add (x*y + z) |
| `exp(x)` | `TP_EXP(x)` | Exponential |
| `log(x)` | `TP_LOG(x)` | Natural logarithm |
| `acos(x)` | `TP_ACOS(x)` | Arc cosine |
| `cos(x)` | `TP_COS(x)` | Cosine |
| `sin(x)` | `TP_SIN(x)` | Sine |
| `floor(x)` | `TP_FLOOR(x)` | Floor function |
| `ceil(x)` | `TP_CEIL(x)` | Ceiling function |
| `fmax(x,y)` | `TP_FMAX(x,y)` | Maximum of two values |
| `fmin(x,y)` | `TP_FMIN(x,y)` | Minimum of two values |

### Implementation Notes

For platforms without hardware FMA support:

```c
// In tp_platform.h or implementation:
static inline double tp_fma(double x, double y, double z) {
    #ifdef HAVE_FMA
        return fma(x, y, z);
    #else
        return x * y + z;  // Software fallback
    #endif
}
```

## Code Example: Complete Refactoring

### Before (Original sp_scurve.c):
```c
#include <math.h>
#include "rtapi_math.h"
#include "mot_priv.h"

double solve_cubic_part(double a, double b, double c) {
    double disc = sqrt(b*b - 4*a*c);
    double x1 = fabs(-b + disc) / (2*a);
    double result = fma(x1, x1, c);
    return exp(log(result));
}
```

### After (Isolated TP version):
```c
#include "tp_platform.h"
// Remove: #include "rtapi_math.h"
// Remove: #include "mot_priv.h"

double solve_cubic_part(double a, double b, double c) {
    double disc = TP_SQRT(b*b - 4*a*c);
    double x1 = TP_FABS(-b + disc) / (2*a);
    double result = TP_FMA(x1, x1, c);
    return TP_EXP(TP_LOG(result));
}
```

## S-Curve Public API

These functions should be exposed in `sp_scurve.h`:

```c
// Core S-curve calculations
int findSCurveVSpeed(double distance, double a_max, double j_max, double *v_req);
double calcSCurveSpeedWithT(double a, double j, double t);
int calcSCurve(double S, double Vc, double Ve, double Vm, 
               double Ac, double Am, double Jm, double T,
               double n[8], double a[8], double v[8], 
               double *tn, double *verr, double *J2, double *J4);

// Profile computation
double calcSCurveT(double S, double Vm, double Ac, double Jm);
double calcSCurveS(double Vm, double Ac, double Jm, double T);

// Segment calculations  
double SCurveSegmentTime(double Vs, double Ve, double Am, double Jm);
double SCurveSegmentDist(double Vs, double Ve, double Am, double Jm);

// Utility functions
int checkSCurveInputs(double S, double Vc, double Ve, double Vm,
                      double Ac, double Am, double Jm);
```

## Dependencies Summary

### Headers Required
- `tp_platform.h` - Math and logging abstractions
- `tp_config.h` - Planner type configuration (optional)

### Headers NOT Required
- `mot_priv.h` - Motion controller internals (remove)
- `simple_tp.h` - Simple planner (make optional)
- `rtapi_math.h` - RTAPI math (replaced by tp_platform.h)

### Platform Functions Used
- Math: `sqrt, fabs, fma, exp, log, acos, cos, sin, floor, ceil, fmax, fmin`
- Configuration: `tp_get_planner_type()` (if runtime switching needed)

## Performance Considerations

1. **FMA Operations**: Use hardware FMA when available for better precision
2. **Cubic Solver**: Numerically stable algorithm - don't simplify
3. **No Dynamic Allocation**: All calculations use stack or static storage
4. **Inline-Friendly**: Most functions are small and suitable for inlining

## Future Enhancements

Potential improvements for isolated TP version:

1. **Configurable Precision**: Allow selection of float vs double
2. **SIMD Support**: Vectorize calculations where beneficial
3. **Profile Caching**: Cache computed S-curve profiles
4. **Alternative Solvers**: Provide multiple cubic solver algorithms

## References

- **Master Implementation**: `src/emc/tp/sp_scurve.c` @ aef0cfa51b2892
- **S-Curve Theory**: Trajectory planning with S-curve velocity profiles
- **Cubic Solver**: Citardauq formula for numerical stability
- **LinuxCNC Docs**: S-curve configuration (`[TRAJ] PLANNER_TYPE = 1`)
