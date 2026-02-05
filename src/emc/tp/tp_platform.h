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
 * These macros wrap RTAPI math functions. Currently they are direct
 * aliases, but this abstraction allows:
 * 1. Future replacement with standard library functions for testing
 * 2. Platform-specific optimizations
 * 3. Easier identification of math dependencies in TP code
 *
 * Usage: Replace rtapi_sqrt(x) with TP_SQRT(x), etc.
 */

/* Basic math functions */
#define TP_SQRT(x)      rtapi_sqrt(x)
#define TP_FABS(x)      rtapi_fabs(x)
#define TP_CEIL(x)      rtapi_ceil(x)
#define TP_FLOOR(x)     rtapi_floor(x)

/* Trigonometric functions */
#define TP_SIN(x)       rtapi_sin(x)
#define TP_COS(x)       rtapi_cos(x)
#define TP_TAN(x)       rtapi_tan(x)
#define TP_ASIN(x)      rtapi_asin(x)
#define TP_ACOS(x)      rtapi_acos(x)
#define TP_ATAN(x)      rtapi_atan(x)
#define TP_ATAN2(y, x)  rtapi_atan2(y, x)

/* Exponential and logarithmic functions */
#define TP_EXP(x)       rtapi_exp(x)
#define TP_LOG(x)       rtapi_log(x)
#define TP_POW(x, y)    rtapi_pow(x, y)

/* Fused multiply-add (x * y + z) - important for numerical precision */
#define TP_FMA(x, y, z) rtapi_fma(x, y, z)

/* Min/max functions */
#define TP_FMIN(x, y)   rtapi_fmin(x, y)
#define TP_FMAX(x, y)   rtapi_fmax(x, y)

/* Copy sign: returns x with sign of y */
#define TP_COPYSIGN(x, y) rtapi_copysign(x, y)

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
