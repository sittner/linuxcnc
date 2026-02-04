/********************************************************************
* Description: tp_platform.h
*   Platform abstraction layer for TP (Trajectory Planner)
*   Isolates dependencies on RTAPI and other LinuxCNC components
*
*   This header provides a compatibility layer that allows the TP code
*   to be used both within LinuxCNC (with RTAPI) and potentially as a
*   standalone library in the future. It wraps:
*   - Math functions (inline wrappers for zero overhead)
*   - Logging functions (macros for compile-time format checking)
*
* Author: LinuxCNC Contributors
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
*
********************************************************************/

#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H

#include "rtapi.h"
#include "rtapi_math.h"
#include <math.h>

/***********************************************************************
*                         MATH FUNCTION ABSTRACTIONS                   *
************************************************************************/

/**
 * Math function wrappers - inline for zero overhead
 * 
 * These wrappers provide a platform-independent interface to mathematical
 * functions. Currently they map directly to the standard C math library
 * functions via rtapi_math.h, but in the future they can be redirected
 * to alternative implementations for standalone builds.
 * 
 * Using inline functions (rather than macros) provides:
 * - Type safety
 * - Better debugging
 * - No side-effect issues with argument evaluation
 * - Zero runtime overhead (inlined by compiler)
 */

/** Absolute value */
static inline double tp_fabs(double x) {
    return fabs(x);
}

/** Square root */
static inline double tp_sqrt(double x) {
    return sqrt(x);
}

/** Sine */
static inline double tp_sin(double x) {
    return sin(x);
}

/** Cosine */
static inline double tp_cos(double x) {
    return cos(x);
}

/** Tangent */
static inline double tp_tan(double x) {
    return tan(x);
}

/** Arc cosine */
static inline double tp_acos(double x) {
    return acos(x);
}

/** Arc sine */
static inline double tp_asin(double x) {
    return asin(x);
}

/** Arc tangent (two argument) */
static inline double tp_atan2(double y, double x) {
    return atan2(y, x);
}

/** Exponential function */
static inline double tp_exp(double x) {
    return exp(x);
}

/** Natural logarithm */
static inline double tp_log(double x) {
    return log(x);
}

/** Power function */
static inline double tp_pow(double x, double y) {
    return pow(x, y);
}

/** Fused multiply-add (x*y + z) */
static inline double tp_fma(double x, double y, double z) {
    return fma(x, y, z);
}

/** Minimum of two values */
static inline double tp_fmin(double x, double y) {
    return fmin(x, y);
}

/** Maximum of two values */
static inline double tp_fmax(double x, double y) {
    return fmax(x, y);
}

/***********************************************************************
*                         LOGGING ABSTRACTIONS                         *
************************************************************************/

/**
 * Logging macros - platform-independent message output
 * 
 * These macros provide a uniform logging interface that currently maps
 * to RTAPI logging functions. Using macros (rather than inline functions)
 * provides:
 * - Compile-time format string checking (compiler attributes)
 * - Variable argument list handling
 * - Zero overhead when logging is disabled
 * - File/line information preservation in debug builds
 * 
 * In standalone builds, these can be redirected to fprintf(), syslog(),
 * or other logging backends.
 * 
 * Usage examples:
 *   TP_LOG_ERR("Invalid motion type %d\n", type);
 *   TP_LOG_WARN("Velocity exceeds limit: %f > %f\n", vel, max_vel);
 *   TP_LOG_INFO("Trajectory planning complete\n");
 *   TP_LOG_DBG("Debug: position = %f\n", pos);
 */

/** Log error message (critical failures) */
#define TP_LOG_ERR(fmt, ...) \
    rtapi_print_msg(RTAPI_MSG_ERR, fmt, ##__VA_ARGS__)

/** Log warning message (recoverable issues) */
#define TP_LOG_WARN(fmt, ...) \
    rtapi_print_msg(RTAPI_MSG_WARN, fmt, ##__VA_ARGS__)

/** Log informational message (normal events) */
#define TP_LOG_INFO(fmt, ...) \
    rtapi_print_msg(RTAPI_MSG_INFO, fmt, ##__VA_ARGS__)

/** Log debug message (verbose diagnostic info) */
#define TP_LOG_DBG(fmt, ...) \
    rtapi_print_msg(RTAPI_MSG_DBG, fmt, ##__VA_ARGS__)

/***********************************************************************
*                         FUTURE EXPANSION NOTES                       *
************************************************************************/

/**
 * This abstraction layer is designed to support future standalone builds
 * of the TP library. When building without RTAPI:
 * 
 * 1. Math functions can remain as-is (they use standard C math.h)
 * 2. Logging macros can be redirected:
 *    #ifdef STANDALONE_BUILD
 *      #define TP_LOG_ERR(fmt, ...) fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__)
 *      #define TP_LOG_WARN(fmt, ...) fprintf(stderr, "WARN: " fmt, ##__VA_ARGS__)
 *      // etc.
 *    #endif
 * 
 * This header is part of Phase 1 of the TP isolation migration plan.
 * Subsequent steps will:
 * - Refactor existing TP code to use these abstractions
 * - Remove direct RTAPI dependencies from TP implementation files
 * - Create standalone build configurations
 */

#endif /* TP_PLATFORM_H */
