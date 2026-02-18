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
#include <stddef.h>

/** Context state machine
 * Enforces proper sync_read/sync_write sequencing
 */
typedef enum {
    HAL_CTX_STATE_CREATED,   /* After create, before first sync_read */
    HAL_CTX_STATE_READ,      /* After sync_read, ready for access */
    HAL_CTX_STATE_WRITTEN,   /* After sync_write, must sync_read again */
} hal_ctx_state_t;

/** Pin/param registry entry
 * Maps handles to buffer offsets and shared memory locations
 */
typedef struct {
    void **shmem_ptr_addr;  /* Address of pointer to shared memory value */
    size_t offset;          /* Offset into before/after buffers */
    hal_type_t type;        /* Data type (HAL_BIT, HAL_FLOAT, etc.) */
    int is_param;           /* 1 if parameter, 0 if pin */
} hal_ctx_entry_t;

/** Thread-local HAL context structure
 * Each thread creates its own context for accessing HAL data
 */
struct hal_ctx {
    int comp_id;            /* Component ID this context is associated with */
    hal_ctx_state_t state;  /* Current state for sync enforcement */
    
    /* Double buffer for diff detection */
    void *before;           /* Snapshot at sync_read */
    void *after;            /* Working copy, modified during cycle */
    size_t buffer_size;     /* Size of each buffer in bytes */
    
    /* Pin/param registry */
    hal_ctx_entry_t *entries;  /* Array of entries */
    int num_entries;           /* Number of registered entries */
    int max_entries;           /* Allocated capacity */
};

#endif /* HAL_CTX_INTERNAL_H */
