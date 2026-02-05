/********************************************************************
* Description: tp_platform.h
*   Platform abstraction layer for the trajectory planner.
*   
*   This header provides macros that wrap platform-specific functions
*   (currently RTAPI) to enable future portability and testability.
*   
*   Initially, all macros are simple aliases to RTAPI functions.
*   This allows incremental migration without behavior changes.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H

#include "rtapi_math.h"

/*
 * Math function abstractions
 * 
 * These macros wrap standard math functions provided by rtapi_math.h.
 * This abstraction allows:
 * 1. Platform-independent code (works in both kernel and userspace)
 * 2. Future platform-specific optimizations if needed
 * 3. Easier identification of math dependencies in TP code
 *
 * Usage: Use TP_SQRT(x), TP_SIN(x), etc. instead of calling math functions directly.
 */

/* Basic math functions */
#define TP_SQRT(x)      sqrt(x)
#define TP_FABS(x)      fabs(x)
#define TP_CEIL(x)      ceil(x)
#define TP_FLOOR(x)     floor(x)

/* Trigonometric functions */
#define TP_SIN(x)       sin(x)
#define TP_COS(x)       cos(x)
#define TP_TAN(x)       tan(x)
#define TP_ASIN(x)      asin(x)
#define TP_ACOS(x)      acos(x)
#define TP_ATAN(x)      atan(x)
#define TP_ATAN2(y, x)  atan2(y, x)

/* Exponential and logarithmic functions */
#define TP_EXP(x)       exp(x)
#define TP_LOG(x)       log(x)
#define TP_POW(x, y)    pow(x, y)

/* Fused multiply-add (x * y + z) - fallback implementation
 * Note: This uses the standard expression form rather than fma() function,
 * which may not be available in kernel builds. This fallback lacks the
 * numerical precision and performance benefits of a true FMA operation. */
#define TP_FMA(x, y, z) ((x) * (y) + (z))

/* Min/max functions */
#define TP_FMIN(x, y)   fmin(x, y)
#define TP_FMAX(x, y)   fmax(x, y)

/* Copy sign: returns x with sign of y */
#define TP_COPYSIGN(x, y) copysign(x, y)

/*
 * Future additions (not yet implemented):
 * 
 * Logging abstractions:
 * #define TP_LOG_ERR(...)  rtapi_print_msg(RTAPI_MSG_ERR, __VA_ARGS__)
 * #define TP_LOG_WARN(...) rtapi_print_msg(RTAPI_MSG_WARN, __VA_ARGS__)
 * #define TP_LOG_DBG(...)  rtapi_print_msg(RTAPI_MSG_DBG, __VA_ARGS__)
 *
 * These will be added in a later PR when migrating logging calls.
 */

#endif /* TP_PLATFORM_H */
