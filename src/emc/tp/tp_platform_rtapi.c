/********************************************************************
* Description: tp_platform_rtapi.c
*   RTAPI implementation of platform abstraction
********************************************************************/
#include "tp_platform.h"
#include "rtapi.h"
#include "rtapi_math.h"
#include <stdarg.h>

// RTAPI logging wrappers
static void rtapi_log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg(RTAPI_MSG_ERR, fmt, args);
    va_end(args);
}

static void rtapi_log_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg(RTAPI_MSG_WARN, fmt, args);
    va_end(args);
}

static void rtapi_log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg(RTAPI_MSG_INFO, fmt, args);
    va_end(args);
}

static void rtapi_log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    rtapi_print_msg(RTAPI_MSG_DBG, fmt, args);
    va_end(args);
}

// Static platform configuration using RTAPI
static tp_platform_config_t rtapi_platform = {
    // Math functions
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
    .fma = fma,
    .exp = exp,
    .log = log,
    
    // Logging
    .log_error = rtapi_log_error,
    .log_warning = rtapi_log_warning,
    .log_info = rtapi_log_info,
    .log_debug = rtapi_log_debug,
    
    // Memory (NULL for now - TP doesn't use dynamic allocation)
    .malloc = NULL,
    .free = NULL,
};

tp_platform_config_t* tp_get_rtapi_platform(void) {
    return &rtapi_platform;
}
