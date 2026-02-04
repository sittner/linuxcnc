/********************************************************************
* Description: tp_platform_standard.c
*   Standard C library implementation of platform abstraction
*   For use in tests and standalone applications
********************************************************************/
#include "tp_platform.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// Standard C library logging wrappers
static void std_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "TP ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void std_log_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "TP WARN: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void std_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("TP INFO: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

static void std_log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("TP DEBUG: ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

// Software fallback for FMA if not available
// Note: This is a simple fallback that lacks the precision guarantees of
// hardware FMA. The separate multiply and add operations can introduce
// intermediate rounding errors that true FMA (fused multiply-add) avoids.
// This is acceptable for the TP which already handles floating-point
// precision issues, but users requiring exact FMA semantics should ensure
// their platform provides hardware FMA support.
#ifndef FP_FAST_FMA
static double software_fma(double x, double y, double z) {
    // Simple software fallback: x * y + z
    // Lacks precision of hardware FMA but sufficient for TP
    return x * y + z;
}
#endif

// Static platform configuration using standard C library
static tp_platform_config_t standard_platform = {
    // Math functions (standard C library)
    .sin = sin,
    .cos = cos,
    .tan = tan,
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
    
    // S-curve additions
#ifdef FP_FAST_FMA
    .fma = fma,
#else
    .fma = software_fma,
#endif
    .exp = exp,
    .log = log,
    
    // Logging (to stdout/stderr)
    .log_error = std_log_error,
    .log_warning = std_log_warning,
    .log_info = std_log_info,
    .log_debug = std_log_debug,
    
    // Memory (standard C library)
    .malloc = malloc,
    .free = free,
};

tp_platform_config_t* tp_get_standard_platform(void) {
    return &standard_platform;
}
