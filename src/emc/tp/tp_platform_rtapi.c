/********************************************************************
* Description: tp_platform_rtapi.c
*   RTAPI implementation of platform abstraction
********************************************************************/
#include "tp_platform.h"
#include "rtapi.h"
#include "rtapi_math.h"
#include <stdarg.h>

// RTAPI logging wrappers
// Note: Buffer size chosen to match typical RTAPI message lengths.
// Longer messages will be truncated but this is acceptable for TP logging.
#define TP_LOG_BUF_SIZE 512

static void rtapi_log_error(const char *fmt, ...) {
    char buf[TP_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    rtapi_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    rtapi_print_msg(RTAPI_MSG_ERR, "%s", buf);
}

static void rtapi_log_warning(const char *fmt, ...) {
    char buf[TP_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    rtapi_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    rtapi_print_msg(RTAPI_MSG_WARN, "%s", buf);
}

static void rtapi_log_info(const char *fmt, ...) {
    char buf[TP_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    rtapi_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    rtapi_print_msg(RTAPI_MSG_INFO, "%s", buf);
}

static void rtapi_log_debug(const char *fmt, ...) {
    char buf[TP_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    rtapi_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    rtapi_print_msg(RTAPI_MSG_DBG, "%s", buf);
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
