#ifndef HAL_CTX_INTERNAL_H
#define HAL_CTX_INTERNAL_H

/** HAL Context Internal Structures
    This file contains internal structures and definitions for the
    thread-local HAL context API. This is a shim layer on top of the
    existing single-image HAL that provides thread-local data copies
    for improved real-time determinism.
*/

/********************************************************************
* Description:  hal_ctx_internal.h
*               Internal structures for thread-local HAL context API
*
* License: LGPL Version 2
*    
* Copyright (c) 2026 All rights reserved.
********************************************************************/

#include "hal.h"
#include "hal_priv.h"
#include <stddef.h>
#include <stdint.h>

/* Size of dirty bitmap: 1 bit per byte of HAL memory */
#define HAL_DIRTY_BITMAP_SIZE   (HAL_SIZE / 8)  /* 128KB for 1MB HAL */

/** Thread-local HAL context structure */
struct hal_ctx {
    /* Buffers */
    char *working_buf;              /* HAL_SIZE copy of HAL memory */
    uint32_t *dirty_bitmap;         /* HAL_DIRTY_BITMAP_SIZE bits tracking modifications */
    
    /* Thread timing info (set by RT executor or user) */
    const char *thread_name;
    long period_ns;                 /* Configured period */
    long actual_period_ns;          /* Measured actual period */
    unsigned long iteration_count;
    unsigned long overruns;
    
    /* State */
    int comp_id;                    /* Component that owns this context */
    unsigned char valid;            /* Non-zero if sync_read was called */
};

#endif /* HAL_CTX_INTERNAL_H */
