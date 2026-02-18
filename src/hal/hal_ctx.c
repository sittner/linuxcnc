/** HAL Context Implementation - Phase 2
    This file implements the thread-local HAL context API using the
    Phase 1 infrastructure (allocation_bitmap, dirty tracking fields).
*/

/********************************************************************
* Description:  hal_ctx.c
*               Thread-local HAL context API implementation
*
* License: LGPL Version 2
*    
* Copyright (c) 2026 All rights reserved.
********************************************************************/

#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_ctx_internal.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/***********************************************************************
*                     CONTEXT LIFECYCLE                                *
***********************************************************************/

hal_ctx_t *hal_ctx_create(int comp_id) {
    hal_ctx_t *ctx = malloc(sizeof(hal_ctx_t));
    if (!ctx) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to allocate context structure\n");
        return NULL;
    }
    
    ctx->working_buf = malloc(HAL_SIZE);
    if (!ctx->working_buf) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to allocate working buffer\n");
        free(ctx);
        return NULL;
    }
    
    /* Allocate dirty bitmap: 1 bit per byte of HAL memory, stored as uint32_t words */
    size_t dirty_words = HAL_DIRTY_BITMAP_SIZE / sizeof(uint32_t);
    ctx->dirty_bitmap = calloc(dirty_words, sizeof(uint32_t));
    if (!ctx->dirty_bitmap) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to allocate dirty bitmap\n");
        free(ctx->working_buf);
        free(ctx);
        return NULL;
    }
    
    ctx->thread_name = NULL;
    ctx->period_ns = 0;
    ctx->actual_period_ns = 0;
    ctx->iteration_count = 0;
    ctx->overruns = 0;
    ctx->comp_id = comp_id;
    ctx->valid = 0;
    
    return ctx;
}

void hal_ctx_destroy(hal_ctx_t *ctx) {
    if (ctx) {
        free(ctx->dirty_bitmap);
        free(ctx->working_buf);
        free(ctx);
    }
}

/***********************************************************************
*                     SYNC OPERATIONS (OPTIMIZED)                      *
***********************************************************************/

int hal_ctx_sync_read(hal_ctx_t *ctx) {
    if (!ctx) {
        return -EINVAL;
    }
    
    if (!hal_data) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: hal_ctx_sync_read called before HAL init\n");
        return -EINVAL;
    }
    
    uint64_t *alloc = (uint64_t *)hal_data->allocation_bitmap;
    uint64_t *dst = (uint64_t *)ctx->working_buf;
    uint64_t *src = (uint64_t *)hal_shmem_base;
    
    /* Only scan up to actual allocation boundary (not full HAL_SIZE) */
    size_t max_block = (hal_data->shmem_bot + 7) / 8;
    size_t max_word = (max_block + 63) / 64;
    
    for (size_t w = 0; w < max_word; w++) {
        uint64_t bits = alloc[w];
        if (bits == 0) continue;  /* Skip 64 blocks at once */
        
        /* Fast iteration using count-trailing-zeros */
        while (bits) {
            size_t bit = __builtin_ctzll(bits);  /* Find lowest set bit */
            size_t block = w * 64 + bit;
            
            /* Direct 8-byte assignment (faster than memcpy) */
            dst[block] = src[block];
            
            bits &= bits - 1;  /* Clear lowest set bit */
        }
    }
    
    /* Clear only the portion of dirty bitmap that's in use */
    /* dirty_bitmap has 1 bit per byte of HAL memory */
    size_t dirty_bytes = (hal_data->shmem_bot + 7) / 8;
    memset(ctx->dirty_bitmap, 0, dirty_bytes);
    
    ctx->valid = 1;
    return 0;
}

int hal_ctx_sync_write(hal_ctx_t *ctx) {
    if (!ctx) {
        return -EINVAL;
    }
    
    if (!hal_data) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: hal_ctx_sync_write called before HAL init\n");
        return -EINVAL;
    }
    
    uint64_t *alloc = (uint64_t *)hal_data->allocation_bitmap;
    uint64_t *dst = (uint64_t *)hal_shmem_base;
    uint64_t *src = (uint64_t *)ctx->working_buf;
    uint32_t *dirty = ctx->dirty_bitmap;
    
    /* Only scan up to actual allocation boundary */
    size_t max_block = (hal_data->shmem_bot + 7) / 8;
    size_t max_word = (max_block + 63) / 64;
    
    for (size_t w = 0; w < max_word; w++) {
        uint64_t bits = alloc[w];
        if (bits == 0) continue;  /* Skip 64 blocks at once */
        
        /* Fast iteration using count-trailing-zeros */
        while (bits) {
            size_t bit = __builtin_ctzll(bits);
            size_t block = w * 64 + bit;
            size_t byte_offset = block * 8;  /* byte offset in HAL memory */
            
            /* Check if any of the 8 bytes in this block are dirty */
            /* dirty_bitmap has 1 bit per byte of HAL memory */
            /* Each uint32_t word covers 32 bytes, so check 8 bits in appropriate word */
            uint32_t word_idx = byte_offset / 32;
            uint32_t bit_offset = byte_offset % 32;
            uint32_t mask = 0xFF << bit_offset;  /* 8 bits for 8 bytes */
            
            if (dirty[word_idx] & mask) {
                /* At least one byte in this block is dirty, write the whole block */
                dst[block] = src[block];
                /* Clear dirty bits for this block */
                dirty[word_idx] &= ~mask;
                /* If block spans word boundary, clear bits in next word too */
                if (bit_offset + 8 > 32) {
                    uint32_t overflow_mask = (1 << ((bit_offset + 8) - 32)) - 1;
                    dirty[word_idx + 1] &= ~overflow_mask;
                }
            }
            
            bits &= bits - 1;  /* Clear lowest set bit */
        }
    }
    
    ctx->valid = 0;
    return 0;
}

/***********************************************************************
*                     CONTEXT ACCESSORS                                *
***********************************************************************/

long hal_ctx_period(hal_ctx_t *ctx) {
    return ctx ? ctx->period_ns : 0;
}

unsigned long hal_ctx_iteration(hal_ctx_t *ctx) {
    return ctx ? ctx->iteration_count : 0;
}

unsigned long hal_ctx_overruns(hal_ctx_t *ctx) {
    return ctx ? ctx->overruns : 0;
}

/***********************************************************************
*                     PIN CREATION (HANDLE-BASED)                      *
***********************************************************************/

int hal_pin_bit_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id) {
    hal_bit_t **data_ptr_addr;
    hal_pin_t *pin;
    int ret;
    
    /* Allocate space for the pointer in shared memory */
    data_ptr_addr = (hal_bit_t **)hal_malloc(sizeof(hal_bit_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    /* Create the pin using existing API */
    ret = hal_pin_bit_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    /* Find the pin we just created and store pointer */
    rtapi_mutex_get(&(hal_data->mutex));
    pin = halpr_find_pin_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!pin) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to find pin '%s' after creation\n", name);
        return -EINVAL;
    }
    
    handle->_pin = (void *)pin;
    handle->type = HAL_BIT;
    
    return 0;
}

int hal_pin_float_new_handle(const char *name, hal_pin_dir_t dir,
                             hal_pin_handle_t *handle, int comp_id) {
    hal_float_t **data_ptr_addr;
    hal_pin_t *pin;
    int ret;
    
    data_ptr_addr = (hal_float_t **)hal_malloc(sizeof(hal_float_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    ret = hal_pin_float_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    rtapi_mutex_get(&(hal_data->mutex));
    pin = halpr_find_pin_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!pin) return -EINVAL;
    
    handle->_pin = (void *)pin;
    handle->type = HAL_FLOAT;
    return 0;
}

int hal_pin_s32_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id) {
    hal_s32_t **data_ptr_addr;
    hal_pin_t *pin;
    int ret;
    
    data_ptr_addr = (hal_s32_t **)hal_malloc(sizeof(hal_s32_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    ret = hal_pin_s32_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    rtapi_mutex_get(&(hal_data->mutex));
    pin = halpr_find_pin_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!pin) return -EINVAL;
    
    handle->_pin = (void *)pin;
    handle->type = HAL_S32;
    return 0;
}

int hal_pin_u32_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id) {
    hal_u32_t **data_ptr_addr;
    hal_pin_t *pin;
    int ret;
    
    data_ptr_addr = (hal_u32_t **)hal_malloc(sizeof(hal_u32_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    ret = hal_pin_u32_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    rtapi_mutex_get(&(hal_data->mutex));
    pin = halpr_find_pin_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!pin) return -EINVAL;
    
    handle->_pin = (void *)pin;
    handle->type = HAL_U32;
    return 0;
}

/***********************************************************************
*                     PARAMETER CREATION (HANDLE-BASED)                *
***********************************************************************/

int hal_param_bit_new_handle(const char *name, hal_param_dir_t dir,
                             hal_param_handle_t *handle, int comp_id) {
    hal_bit_t *data_ptr;
    hal_param_t *param;
    int ret;
    
    /* Allocate space for the parameter value in shared memory */
    data_ptr = (hal_bit_t *)hal_malloc(sizeof(hal_bit_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    /* Create the parameter using existing API */
    ret = hal_param_bit_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    /* Find the param we just created and store pointer */
    rtapi_mutex_get(&(hal_data->mutex));
    param = halpr_find_param_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!param) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to find param '%s' after creation\n", name);
        return -EINVAL;
    }
    
    handle->_param = (void *)param;
    handle->type = HAL_BIT;
    
    return 0;
}

int hal_param_float_new_handle(const char *name, hal_param_dir_t dir,
                               hal_param_handle_t *handle, int comp_id) {
    hal_float_t *data_ptr;
    hal_param_t *param;
    int ret;
    
    data_ptr = (hal_float_t *)hal_malloc(sizeof(hal_float_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    ret = hal_param_float_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    rtapi_mutex_get(&(hal_data->mutex));
    param = halpr_find_param_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!param) return -EINVAL;
    
    handle->_param = (void *)param;
    handle->type = HAL_FLOAT;
    return 0;
}

int hal_param_s32_new_handle(const char *name, hal_param_dir_t dir,
                             hal_param_handle_t *handle, int comp_id) {
    hal_s32_t *data_ptr;
    hal_param_t *param;
    int ret;
    
    data_ptr = (hal_s32_t *)hal_malloc(sizeof(hal_s32_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    ret = hal_param_s32_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    rtapi_mutex_get(&(hal_data->mutex));
    param = halpr_find_param_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!param) return -EINVAL;
    
    handle->_param = (void *)param;
    handle->type = HAL_S32;
    return 0;
}

int hal_param_u32_new_handle(const char *name, hal_param_dir_t dir,
                             hal_param_handle_t *handle, int comp_id) {
    hal_u32_t *data_ptr;
    hal_param_t *param;
    int ret;
    
    data_ptr = (hal_u32_t *)hal_malloc(sizeof(hal_u32_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    ret = hal_param_u32_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    rtapi_mutex_get(&(hal_data->mutex));
    param = halpr_find_param_by_name(name);
    rtapi_mutex_give(&(hal_data->mutex));
    
    if (!param) return -EINVAL;
    
    handle->_param = (void *)param;
    handle->type = HAL_U32;
    return 0;
}

/***********************************************************************
*                     PIN ACCESS (CONTEXT-AWARE)                       *
***********************************************************************/

hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t h) {
    if (!ctx) return 0;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return 0;  /* Unlinked - return default */
    
    return *(hal_bit_t *)(ctx->working_buf + sig->data_ptr);
}

void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t h, hal_bit_t val) {
    if (!ctx) return;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return;  /* Unlinked - nothing to do */
    
    /* Write to working buffer */
    *(hal_bit_t *)(ctx->working_buf + sig->data_ptr) = val;
    
    /* Mark dirty using signal's precomputed dirty info */
    ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
    ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
}

hal_float_t hal_ctx_pin_float_get(hal_ctx_t *ctx, hal_pin_handle_t h) {
    if (!ctx) return 0.0;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return 0.0;  /* Unlinked - return default */
    
    return *(hal_float_t *)(ctx->working_buf + sig->data_ptr);
}

void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t h, hal_float_t val) {
    if (!ctx) return;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return;  /* Unlinked - nothing to do */
    
    /* Write to working buffer */
    *(hal_float_t *)(ctx->working_buf + sig->data_ptr) = val;
    
    /* Mark dirty using signal's precomputed dirty info */
    ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
    ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
}

hal_s32_t hal_ctx_pin_s32_get(hal_ctx_t *ctx, hal_pin_handle_t h) {
    if (!ctx) return 0;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return 0;  /* Unlinked - return default */
    
    return *(hal_s32_t *)(ctx->working_buf + sig->data_ptr);
}

void hal_ctx_pin_s32_set(hal_ctx_t *ctx, hal_pin_handle_t h, hal_s32_t val) {
    if (!ctx) return;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return;  /* Unlinked - nothing to do */
    
    /* Write to working buffer */
    *(hal_s32_t *)(ctx->working_buf + sig->data_ptr) = val;
    
    /* Mark dirty using signal's precomputed dirty info */
    ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
    ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
}

hal_u32_t hal_ctx_pin_u32_get(hal_ctx_t *ctx, hal_pin_handle_t h) {
    if (!ctx) return 0;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return 0;  /* Unlinked - return default */
    
    return *(hal_u32_t *)(ctx->working_buf + sig->data_ptr);
}

void hal_ctx_pin_u32_set(hal_ctx_t *ctx, hal_pin_handle_t h, hal_u32_t val) {
    if (!ctx) return;
    
    hal_pin_t *pin = (hal_pin_t *)h._pin;
    hal_sig_t *sig = SHMPTR(pin->signal);
    if (!sig) return;  /* Unlinked - nothing to do */
    
    /* Write to working buffer */
    *(hal_u32_t *)(ctx->working_buf + sig->data_ptr) = val;
    
    /* Mark dirty using signal's precomputed dirty info */
    ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
    ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
}

/***********************************************************************
*                     PARAMETER ACCESS (CONTEXT-AWARE)                 *
***********************************************************************/

hal_bit_t hal_ctx_param_bit_get(hal_ctx_t *ctx, hal_param_handle_t h) {
    if (!ctx) return 0;
    
    hal_param_t *param = (hal_param_t *)h._param;
    return *(hal_bit_t *)(ctx->working_buf + param->data_ptr);
}

void hal_ctx_param_bit_set(hal_ctx_t *ctx, hal_param_handle_t h, hal_bit_t val) {
    if (!ctx) return;
    
    hal_param_t *param = (hal_param_t *)h._param;
    /* Write value to working buffer */
    *(hal_bit_t *)(ctx->working_buf + param->data_ptr) = val;
    
    /* Mark dirty using param's precomputed dirty info */
    ctx->dirty_bitmap[param->dirty_offset]     |= param->dirty_mask[0];
    ctx->dirty_bitmap[param->dirty_offset + 1] |= param->dirty_mask[1];
}

hal_float_t hal_ctx_param_float_get(hal_ctx_t *ctx, hal_param_handle_t h) {
    if (!ctx) return 0.0;
    
    hal_param_t *param = (hal_param_t *)h._param;
    return *(hal_float_t *)(ctx->working_buf + param->data_ptr);
}

void hal_ctx_param_float_set(hal_ctx_t *ctx, hal_param_handle_t h, hal_float_t val) {
    if (!ctx) return;
    
    hal_param_t *param = (hal_param_t *)h._param;
    /* Write value to working buffer */
    *(hal_float_t *)(ctx->working_buf + param->data_ptr) = val;
    
    /* Mark dirty using param's precomputed dirty info */
    ctx->dirty_bitmap[param->dirty_offset]     |= param->dirty_mask[0];
    ctx->dirty_bitmap[param->dirty_offset + 1] |= param->dirty_mask[1];
}

hal_s32_t hal_ctx_param_s32_get(hal_ctx_t *ctx, hal_param_handle_t h) {
    if (!ctx) return 0;
    
    hal_param_t *param = (hal_param_t *)h._param;
    return *(hal_s32_t *)(ctx->working_buf + param->data_ptr);
}

void hal_ctx_param_s32_set(hal_ctx_t *ctx, hal_param_handle_t h, hal_s32_t val) {
    if (!ctx) return;
    
    hal_param_t *param = (hal_param_t *)h._param;
    /* Write value to working buffer */
    *(hal_s32_t *)(ctx->working_buf + param->data_ptr) = val;
    
    /* Mark dirty using param's precomputed dirty info */
    ctx->dirty_bitmap[param->dirty_offset]     |= param->dirty_mask[0];
    ctx->dirty_bitmap[param->dirty_offset + 1] |= param->dirty_mask[1];
}

hal_u32_t hal_ctx_param_u32_get(hal_ctx_t *ctx, hal_param_handle_t h) {
    if (!ctx) return 0;
    
    hal_param_t *param = (hal_param_t *)h._param;
    return *(hal_u32_t *)(ctx->working_buf + param->data_ptr);
}

void hal_ctx_param_u32_set(hal_ctx_t *ctx, hal_param_handle_t h, hal_u32_t val) {
    if (!ctx) return;
    
    hal_param_t *param = (hal_param_t *)h._param;
    /* Write value to working buffer */
    *(hal_u32_t *)(ctx->working_buf + param->data_ptr) = val;
    
    /* Mark dirty using param's precomputed dirty info */
    ctx->dirty_bitmap[param->dirty_offset]     |= param->dirty_mask[0];
    ctx->dirty_bitmap[param->dirty_offset + 1] |= param->dirty_mask[1];
}

/***********************************************************************
*                     SYMBOL EXPORTS                                   *
***********************************************************************/

#ifdef RTAPI
/* Export symbols for realtime and userspace */

/* Context lifecycle */
EXPORT_SYMBOL(hal_ctx_create);
EXPORT_SYMBOL(hal_ctx_destroy);

/* Sync operations */
EXPORT_SYMBOL(hal_ctx_sync_read);
EXPORT_SYMBOL(hal_ctx_sync_write);

/* Context accessors */
EXPORT_SYMBOL(hal_ctx_period);
EXPORT_SYMBOL(hal_ctx_iteration);
EXPORT_SYMBOL(hal_ctx_overruns);

/* Pin creation (handle-based) */
EXPORT_SYMBOL(hal_pin_bit_new_handle);
EXPORT_SYMBOL(hal_pin_float_new_handle);
EXPORT_SYMBOL(hal_pin_s32_new_handle);
EXPORT_SYMBOL(hal_pin_u32_new_handle);

/* Pin access (context-aware) */
EXPORT_SYMBOL(hal_ctx_pin_bit_get);
EXPORT_SYMBOL(hal_ctx_pin_bit_set);
EXPORT_SYMBOL(hal_ctx_pin_float_get);
EXPORT_SYMBOL(hal_ctx_pin_float_set);
EXPORT_SYMBOL(hal_ctx_pin_s32_get);
EXPORT_SYMBOL(hal_ctx_pin_s32_set);
EXPORT_SYMBOL(hal_ctx_pin_u32_get);
EXPORT_SYMBOL(hal_ctx_pin_u32_set);

/* Parameter creation (handle-based) */
EXPORT_SYMBOL(hal_param_bit_new_handle);
EXPORT_SYMBOL(hal_param_float_new_handle);
EXPORT_SYMBOL(hal_param_s32_new_handle);
EXPORT_SYMBOL(hal_param_u32_new_handle);

/* Parameter access (context-aware) */
EXPORT_SYMBOL(hal_ctx_param_bit_get);
EXPORT_SYMBOL(hal_ctx_param_bit_set);
EXPORT_SYMBOL(hal_ctx_param_float_get);
EXPORT_SYMBOL(hal_ctx_param_float_set);
EXPORT_SYMBOL(hal_ctx_param_s32_get);
EXPORT_SYMBOL(hal_ctx_param_s32_set);
EXPORT_SYMBOL(hal_ctx_param_u32_get);
EXPORT_SYMBOL(hal_ctx_param_u32_set);

#endif /* RTAPI */
