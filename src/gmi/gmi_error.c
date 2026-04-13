// GMI Error Implementation
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gmi_error.h"
#include <stdio.h>

static char http_error_buf[64];

const char *gmi_strerror(int err) {
    if (err >= 0) {
        return "Success";
    }

    switch (err) {
    case GMI_ERR_ALLOC:
        return "Memory allocation failed";
    case GMI_ERR_CURL:
        return "CURL operation failed";
    case GMI_ERR_JSON:
        return "JSON parse/encode error";
    case GMI_ERR_OVERFLOW:
        return "Buffer overflow";
    case GMI_ERR_INVALID:
        return "Invalid argument";
    case GMI_ERR_NOT_FOUND:
        return "Resource not found";
    case GMI_ERR_TIMEOUT:
        return "Operation timed out";
    case GMI_ERR_IO:
        return "I/O error";
    default:
        // HTTP status codes are positive
        if (err >= 100 && err < 600) {
            snprintf(http_error_buf, sizeof(http_error_buf), "HTTP error %d", err);
            return http_error_buf;
        }
        return "Unknown error";
    }
}
