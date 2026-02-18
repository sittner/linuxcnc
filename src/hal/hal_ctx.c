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
    
    ctx->dirty_bitmap = calloc(HAL_DIRTY_BITMAP_SIZE / sizeof(uint32_t), sizeof(uint32_t));
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
    size_t dirty_bytes = (max_block + 7) / 8;
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
    uint8_t *dirty = (uint8_t *)ctx->dirty_bitmap;
    
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
            size_t offset = block * 8;
            
            /* Check if any byte dirty in this block (fast 8-byte check) */
            if (*(uint64_t *)(dirty + offset)) {
                dst[block] = src[block];
                *(uint64_t *)(dirty + offset) = 0;  /* Clear dirty flags */
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
    
    /* Find the pin we just created to get signal info */
    rtapi_mutex_get(&(hal_data->mutex));
    pin = halpr_find_pin_by_name(name);
    if (!pin) {
        rtapi_mutex_give(&(hal_data->mutex));
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to find pin '%s' after creation\n", name);
        return -EINVAL;
    }
    
    /* Get the data offset from the pin's current data pointer */
    volatile void *data_ptr = *data_ptr_addr;
    handle->data_offset = SHMOFF((void*)data_ptr);
    handle->type = HAL_BIT;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    if (!pin) {
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    
    volatile void *data_ptr = *data_ptr_addr;
    handle->data_offset = SHMOFF((void*)data_ptr);
    handle->type = HAL_FLOAT;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    if (!pin) {
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    
    volatile void *data_ptr = *data_ptr_addr;
    handle->data_offset = SHMOFF((void*)data_ptr);
    handle->type = HAL_S32;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    if (!pin) {
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    
    volatile void *data_ptr = *data_ptr_addr;
    handle->data_offset = SHMOFF((void*)data_ptr);
    handle->type = HAL_U32;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    
    /* Find the param we just created to get its offset */
    rtapi_mutex_get(&(hal_data->mutex));
    param = halpr_find_param_by_name(name);
    if (!param) {
        rtapi_mutex_give(&(hal_data->mutex));
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: Failed to find param '%s' after creation\n", name);
        return -EINVAL;
    }
    
    handle->data_offset = SHMOFF(data_ptr);
    handle->type = HAL_BIT;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    if (!param) {
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    
    handle->data_offset = SHMOFF(data_ptr);
    handle->type = HAL_FLOAT;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    if (!param) {
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    
    handle->data_offset = SHMOFF(data_ptr);
    handle->type = HAL_S32;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
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
    if (!param) {
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    
    handle->data_offset = SHMOFF(data_ptr);
    handle->type = HAL_U32;
    
    rtapi_mutex_give(&(hal_data->mutex));
    
    return 0;
}

/***********************************************************************
*                     HELPER: Find signal/param for dirty marking      *
***********************************************************************/

/* Helper to find signal from pin handle data offset */
static hal_sig_t *find_signal_by_data_offset(int data_offset) {
    hal_sig_t *sig;
    rtapi_intptr_t next;
    
    /* Scan signal list to find one with matching data_ptr */
    rtapi_mutex_get(&(hal_data->mutex));
    next = hal_data->sig_list_ptr;
    while (next != 0) {
        sig = SHMPTR(next);
        if (sig->data_ptr == data_offset) {
            rtapi_mutex_give(&(hal_data->mutex));
            return sig;
        }
        next = sig->next_ptr;
    }
    rtapi_mutex_give(&(hal_data->mutex));
    return NULL;
}

/* Helper to find param by data offset */
static hal_param_t *find_param_by_data_offset(int data_offset) {
    hal_param_t *param;
    rtapi_intptr_t next;
    
    /* Scan param list to find one with matching data_ptr */
    rtapi_mutex_get(&(hal_data->mutex));
    next = hal_data->param_list_ptr;
    while (next != 0) {
        param = SHMPTR(next);
        if (param->data_ptr == data_offset) {
            rtapi_mutex_give(&(hal_data->mutex));
            return param;
        }
        next = param->next_ptr;
    }
    rtapi_mutex_give(&(hal_data->mutex));
    return NULL;
}

/***********************************************************************
*                     PIN ACCESS (CONTEXT-AWARE)                       *
***********************************************************************/

hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    if (!ctx) {
        return 0;
    }
    
    hal_bit_t *val = (hal_bit_t *)(ctx->working_buf + pin.data_offset);
    return *val;
}

void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_bit_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_bit_t *)(ctx->working_buf + pin.data_offset) = val;
    
    /* Mark dirty using precomputed values from signal */
    hal_sig_t *sig = find_signal_by_data_offset(pin.data_offset);
    if (sig) {
        ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
        ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
    }
}

hal_float_t hal_ctx_pin_float_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    if (!ctx) {
        return 0.0;
    }
    
    hal_float_t *val = (hal_float_t *)(ctx->working_buf + pin.data_offset);
    return *val;
}

void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_float_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_float_t *)(ctx->working_buf + pin.data_offset) = val;
    
    /* Mark dirty using precomputed values from signal */
    hal_sig_t *sig = find_signal_by_data_offset(pin.data_offset);
    if (sig) {
        ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
        ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
    }
}

hal_s32_t hal_ctx_pin_s32_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    if (!ctx) {
        return 0;
    }
    
    hal_s32_t *val = (hal_s32_t *)(ctx->working_buf + pin.data_offset);
    return *val;
}

void hal_ctx_pin_s32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_s32_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_s32_t *)(ctx->working_buf + pin.data_offset) = val;
    
    /* Mark dirty using precomputed values from signal */
    hal_sig_t *sig = find_signal_by_data_offset(pin.data_offset);
    if (sig) {
        ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
        ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
    }
}

hal_u32_t hal_ctx_pin_u32_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    if (!ctx) {
        return 0;
    }
    
    hal_u32_t *val = (hal_u32_t *)(ctx->working_buf + pin.data_offset);
    return *val;
}

void hal_ctx_pin_u32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_u32_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_u32_t *)(ctx->working_buf + pin.data_offset) = val;
    
    /* Mark dirty using precomputed values from signal */
    hal_sig_t *sig = find_signal_by_data_offset(pin.data_offset);
    if (sig) {
        ctx->dirty_bitmap[sig->dirty_offset]     |= sig->dirty_mask[0];
        ctx->dirty_bitmap[sig->dirty_offset + 1] |= sig->dirty_mask[1];
    }
}

/***********************************************************************
*                     PARAMETER ACCESS (CONTEXT-AWARE)                 *
***********************************************************************/

hal_bit_t hal_ctx_param_bit_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    if (!ctx) {
        return 0;
    }
    
    hal_bit_t *val = (hal_bit_t *)(ctx->working_buf + param.data_offset);
    return *val;
}

void hal_ctx_param_bit_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_bit_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_bit_t *)(ctx->working_buf + param.data_offset) = val;
    
    /* Mark dirty using precomputed values from param */
    hal_param_t *p = find_param_by_data_offset(param.data_offset);
    if (p) {
        ctx->dirty_bitmap[p->dirty_offset]     |= p->dirty_mask[0];
        ctx->dirty_bitmap[p->dirty_offset + 1] |= p->dirty_mask[1];
    }
}

hal_float_t hal_ctx_param_float_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    if (!ctx) {
        return 0.0;
    }
    
    hal_float_t *val = (hal_float_t *)(ctx->working_buf + param.data_offset);
    return *val;
}

void hal_ctx_param_float_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_float_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_float_t *)(ctx->working_buf + param.data_offset) = val;
    
    /* Mark dirty using precomputed values from param */
    hal_param_t *p = find_param_by_data_offset(param.data_offset);
    if (p) {
        ctx->dirty_bitmap[p->dirty_offset]     |= p->dirty_mask[0];
        ctx->dirty_bitmap[p->dirty_offset + 1] |= p->dirty_mask[1];
    }
}

hal_s32_t hal_ctx_param_s32_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    if (!ctx) {
        return 0;
    }
    
    hal_s32_t *val = (hal_s32_t *)(ctx->working_buf + param.data_offset);
    return *val;
}

void hal_ctx_param_s32_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_s32_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_s32_t *)(ctx->working_buf + param.data_offset) = val;
    
    /* Mark dirty using precomputed values from param */
    hal_param_t *p = find_param_by_data_offset(param.data_offset);
    if (p) {
        ctx->dirty_bitmap[p->dirty_offset]     |= p->dirty_mask[0];
        ctx->dirty_bitmap[p->dirty_offset + 1] |= p->dirty_mask[1];
    }
}

hal_u32_t hal_ctx_param_u32_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    if (!ctx) {
        return 0;
    }
    
    hal_u32_t *val = (hal_u32_t *)(ctx->working_buf + param.data_offset);
    return *val;
}

void hal_ctx_param_u32_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_u32_t val) {
    if (!ctx) {
        return;
    }
    
    /* Write value to working buffer */
    *(hal_u32_t *)(ctx->working_buf + param.data_offset) = val;
    
    /* Mark dirty using precomputed values from param */
    hal_param_t *p = find_param_by_data_offset(param.data_offset);
    if (p) {
        ctx->dirty_bitmap[p->dirty_offset]     |= p->dirty_mask[0];
        ctx->dirty_bitmap[p->dirty_offset + 1] |= p->dirty_mask[1];
    }
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
