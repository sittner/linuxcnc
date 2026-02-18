/** HAL Context Implementation
    This file implements the thread-local HAL context API as a shim
    layer on top of the existing single-image HAL.
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

/* Initial capacity for entry registry */
#define INITIAL_CAPACITY 16

/* Global registry to map handles to pointers */
typedef struct {
    void **shmem_ptr_addr;  /* Address of pointer to shared memory */
    hal_type_t type;
    int is_param;
} handle_entry_t;

static handle_entry_t *handle_registry = NULL;
static int num_handles = 0;
static int max_handles = 0;

/* Get size of a HAL type in bytes */
static size_t hal_type_size(hal_type_t type) {
    switch(type) {
        case HAL_BIT:   return sizeof(hal_bit_t);
        case HAL_FLOAT: return sizeof(hal_float_t);
        case HAL_S32:   return sizeof(hal_s32_t);
        case HAL_U32:   return sizeof(hal_u32_t);
        default:        return 0;
    }
}

/* Register a new handle */
static int register_handle(void **shmem_ptr_addr, hal_type_t type, int is_param) {
    /* Expand registry if needed */
    if (num_handles >= max_handles) {
        int new_max = max_handles == 0 ? INITIAL_CAPACITY : max_handles * 2;
        handle_entry_t *new_registry = realloc(handle_registry, 
                                               new_max * sizeof(handle_entry_t));
        if (!new_registry) {
            return -ENOMEM;
        }
        handle_registry = new_registry;
        max_handles = new_max;
    }
    
    /* Add entry */
    handle_registry[num_handles].shmem_ptr_addr = shmem_ptr_addr;
    handle_registry[num_handles].type = type;
    handle_registry[num_handles].is_param = is_param;
    
    return num_handles++;
}

/***********************************************************************
*                     PIN CREATION (HANDLE-BASED)                      *
***********************************************************************/

int hal_pin_bit_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id) {
    hal_bit_t **data_ptr_addr;
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
    
    /* Register and return handle */
    ret = register_handle((void **)data_ptr_addr, HAL_BIT, 0);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

int hal_pin_float_new_handle(const char *name, hal_pin_dir_t dir,
                             hal_pin_handle_t *handle, int comp_id) {
    hal_float_t **data_ptr_addr;
    int ret;
    
    data_ptr_addr = (hal_float_t **)hal_malloc(sizeof(hal_float_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    ret = hal_pin_float_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    ret = register_handle((void **)data_ptr_addr, HAL_FLOAT, 0);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

int hal_pin_s32_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id) {
    hal_s32_t **data_ptr_addr;
    int ret;
    
    data_ptr_addr = (hal_s32_t **)hal_malloc(sizeof(hal_s32_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    ret = hal_pin_s32_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    ret = register_handle((void **)data_ptr_addr, HAL_S32, 0);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

int hal_pin_u32_new_handle(const char *name, hal_pin_dir_t dir,
                           hal_pin_handle_t *handle, int comp_id) {
    hal_u32_t **data_ptr_addr;
    int ret;
    
    data_ptr_addr = (hal_u32_t **)hal_malloc(sizeof(hal_u32_t *));
    if (!data_ptr_addr) {
        return -ENOMEM;
    }
    
    ret = hal_pin_u32_new(name, dir, data_ptr_addr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    ret = register_handle((void **)data_ptr_addr, HAL_U32, 0);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

/***********************************************************************
*                     PARAMETER CREATION (HANDLE-BASED)                *
***********************************************************************/

int hal_param_bit_new_handle(const char *name, hal_param_dir_t dir,
                             hal_param_handle_t *handle, int comp_id) {
    hal_bit_t *data_ptr;
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
    
    /* For params, we create a synthetic double-pointer entry */
    hal_bit_t **ptr_addr = (hal_bit_t **)hal_malloc(sizeof(hal_bit_t *));
    if (!ptr_addr) {
        return -ENOMEM;
    }
    *ptr_addr = data_ptr;
    
    /* Register and return handle */
    ret = register_handle((void **)ptr_addr, HAL_BIT, 1);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

int hal_param_float_new_handle(const char *name, hal_param_dir_t dir,
                               hal_param_handle_t *handle, int comp_id) {
    hal_float_t *data_ptr;
    int ret;
    
    data_ptr = (hal_float_t *)hal_malloc(sizeof(hal_float_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    ret = hal_param_float_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    hal_float_t **ptr_addr = (hal_float_t **)hal_malloc(sizeof(hal_float_t *));
    if (!ptr_addr) {
        return -ENOMEM;
    }
    *ptr_addr = data_ptr;
    
    ret = register_handle((void **)ptr_addr, HAL_FLOAT, 1);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

int hal_param_s32_new_handle(const char *name, hal_param_dir_t dir,
                             hal_param_handle_t *handle, int comp_id) {
    hal_s32_t *data_ptr;
    int ret;
    
    data_ptr = (hal_s32_t *)hal_malloc(sizeof(hal_s32_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    ret = hal_param_s32_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    hal_s32_t **ptr_addr = (hal_s32_t **)hal_malloc(sizeof(hal_s32_t *));
    if (!ptr_addr) {
        return -ENOMEM;
    }
    *ptr_addr = data_ptr;
    
    ret = register_handle((void **)ptr_addr, HAL_S32, 1);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

int hal_param_u32_new_handle(const char *name, hal_param_dir_t dir,
                             hal_param_handle_t *handle, int comp_id) {
    hal_u32_t *data_ptr;
    int ret;
    
    data_ptr = (hal_u32_t *)hal_malloc(sizeof(hal_u32_t));
    if (!data_ptr) {
        return -ENOMEM;
    }
    
    ret = hal_param_u32_new(name, dir, data_ptr, comp_id);
    if (ret < 0) {
        return ret;
    }
    
    hal_u32_t **ptr_addr = (hal_u32_t **)hal_malloc(sizeof(hal_u32_t *));
    if (!ptr_addr) {
        return -ENOMEM;
    }
    *ptr_addr = data_ptr;
    
    ret = register_handle((void **)ptr_addr, HAL_U32, 1);
    if (ret < 0) {
        return ret;
    }
    
    *handle = ret;
    return 0;
}

/***********************************************************************
*                     CONTEXT LIFECYCLE                                *
***********************************************************************/

hal_ctx_t *hal_ctx_create(int comp_id) {
    hal_ctx_t *ctx;
    size_t total_size = 0;
    int i;
    
    /* Allocate context structure */
    ctx = (hal_ctx_t *)malloc(sizeof(hal_ctx_t));
    if (!ctx) {
        rtapi_print_msg(RTAPI_MSG_ERR, 
            "HAL: ERROR: Failed to allocate context structure\n");
        return NULL;
    }
    
    ctx->comp_id = comp_id;
    ctx->state = HAL_CTX_STATE_CREATED;
    ctx->num_entries = 0;
    ctx->max_entries = 0;
    ctx->entries = NULL;
    ctx->before = NULL;
    ctx->after = NULL;
    ctx->buffer_size = 0;
    
    /* Build entry list from global handle registry */
    for (i = 0; i < num_handles; i++) {
        /* Expand entries array if needed */
        if (ctx->num_entries >= ctx->max_entries) {
            int new_max = ctx->max_entries == 0 ? INITIAL_CAPACITY : ctx->max_entries * 2;
            hal_ctx_entry_t *new_entries = realloc(ctx->entries, 
                                                   new_max * sizeof(hal_ctx_entry_t));
            if (!new_entries) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                    "HAL: ERROR: Failed to allocate entry array\n");
                hal_ctx_destroy(ctx);
                return NULL;
            }
            ctx->entries = new_entries;
            ctx->max_entries = new_max;
        }
        
        /* Add entry with offset */
        ctx->entries[ctx->num_entries].shmem_ptr_addr = handle_registry[i].shmem_ptr_addr;
        ctx->entries[ctx->num_entries].offset = total_size;
        ctx->entries[ctx->num_entries].type = handle_registry[i].type;
        ctx->entries[ctx->num_entries].is_param = handle_registry[i].is_param;
        
        total_size += hal_type_size(handle_registry[i].type);
        ctx->num_entries++;
    }
    
    ctx->buffer_size = total_size;
    
    /* Allocate buffers */
    if (total_size > 0) {
        ctx->before = malloc(total_size);
        ctx->after = malloc(total_size);
        
        if (!ctx->before || !ctx->after) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                "HAL: ERROR: Failed to allocate context buffers\n");
            hal_ctx_destroy(ctx);
            return NULL;
        }
        
        /* Initialize buffers to zero */
        memset(ctx->before, 0, total_size);
        memset(ctx->after, 0, total_size);
    }
    
    return ctx;
}

void hal_ctx_destroy(hal_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->before) {
        free(ctx->before);
    }
    if (ctx->after) {
        free(ctx->after);
    }
    if (ctx->entries) {
        free(ctx->entries);
    }
    free(ctx);
}

/***********************************************************************
*                     SYNC OPERATIONS                                  *
***********************************************************************/

int hal_ctx_sync_read(hal_ctx_t *ctx) {
    int i;
    
    if (!ctx) {
        return -EINVAL;
    }
    
    /* Check state - cannot call sync_read twice */
    if (ctx->state == HAL_CTX_STATE_READ) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: hal_ctx_sync_read called twice without sync_write\n");
        return -EINVAL;
    }
    
    /* Copy all values from shared memory to before buffer */
    for (i = 0; i < ctx->num_entries; i++) {
        hal_ctx_entry_t *entry = &ctx->entries[i];
        void *src = *entry->shmem_ptr_addr;  /* Dereference to get actual data pointer */
        void *dst = (char *)ctx->before + entry->offset;
        size_t size = hal_type_size(entry->type);
        
        if (src) {
            memcpy(dst, src, size);
        }
    }
    
    /* Copy before to after */
    memcpy(ctx->after, ctx->before, ctx->buffer_size);
    
    /* Update state */
    ctx->state = HAL_CTX_STATE_READ;
    
    return 0;
}

int hal_ctx_sync_write(hal_ctx_t *ctx) {
    int i;
    
    if (!ctx) {
        return -EINVAL;
    }
    
    /* Check state - must have called sync_read first */
    if (ctx->state != HAL_CTX_STATE_READ) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: hal_ctx_sync_write called without prior sync_read\n");
        return -EINVAL;
    }
    
    /* Compare after vs before and write only changes */
    for (i = 0; i < ctx->num_entries; i++) {
        hal_ctx_entry_t *entry = &ctx->entries[i];
        void *before_val = (char *)ctx->before + entry->offset;
        void *after_val = (char *)ctx->after + entry->offset;
        void *dst = *entry->shmem_ptr_addr;
        size_t size = hal_type_size(entry->type);
        
        /* Only write if value changed */
        if (dst && memcmp(before_val, after_val, size) != 0) {
            memcpy(dst, after_val, size);
        }
    }
    
    /* Update state */
    ctx->state = HAL_CTX_STATE_WRITTEN;
    
    return 0;
}

/***********************************************************************
*                     PIN ACCESS (CONTEXT-AWARE)                       *
***********************************************************************/

/* Helper macro to check context state */
#define CHECK_CTX_STATE(ctx, retval) \
    do { \
        if (!ctx || ctx->state != HAL_CTX_STATE_READ) { \
            rtapi_print_msg(RTAPI_MSG_ERR, \
                "HAL: ERROR: Context access without active sync_read\n"); \
            return (retval); \
        } \
    } while(0)

#define CHECK_CTX_STATE_VOID(ctx) \
    do { \
        if (!ctx || ctx->state != HAL_CTX_STATE_READ) { \
            rtapi_print_msg(RTAPI_MSG_ERR, \
                "HAL: ERROR: Context access without active sync_read\n"); \
            return; \
        } \
    } while(0)

#define CHECK_HANDLE(handle, ctx, retval) \
    do { \
        if (handle < 0 || handle >= ctx->num_entries) { \
            rtapi_print_msg(RTAPI_MSG_ERR, \
                "HAL: ERROR: Invalid handle %d\n", handle); \
            return (retval); \
        } \
    } while(0)

#define CHECK_HANDLE_VOID(handle, ctx) \
    do { \
        if (handle < 0 || handle >= ctx->num_entries) { \
            rtapi_print_msg(RTAPI_MSG_ERR, \
                "HAL: ERROR: Invalid handle %d\n", handle); \
            return; \
        } \
    } while(0)

hal_bit_t hal_ctx_pin_bit_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    CHECK_CTX_STATE(ctx, 0);
    CHECK_HANDLE(pin, ctx, 0);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_bit_t *val = (hal_bit_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_pin_bit_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_bit_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(pin, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_bit_t *dst = (hal_bit_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

hal_float_t hal_ctx_pin_float_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    CHECK_CTX_STATE(ctx, 0.0);
    CHECK_HANDLE(pin, ctx, 0.0);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_float_t *val = (hal_float_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_pin_float_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_float_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(pin, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_float_t *dst = (hal_float_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

hal_s32_t hal_ctx_pin_s32_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    CHECK_CTX_STATE(ctx, 0);
    CHECK_HANDLE(pin, ctx, 0);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_s32_t *val = (hal_s32_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_pin_s32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_s32_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(pin, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_s32_t *dst = (hal_s32_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

hal_u32_t hal_ctx_pin_u32_get(hal_ctx_t *ctx, hal_pin_handle_t pin) {
    CHECK_CTX_STATE(ctx, 0);
    CHECK_HANDLE(pin, ctx, 0);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_u32_t *val = (hal_u32_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_pin_u32_set(hal_ctx_t *ctx, hal_pin_handle_t pin, hal_u32_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(pin, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[pin];
    hal_u32_t *dst = (hal_u32_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

/***********************************************************************
*                     PARAMETER ACCESS (CONTEXT-AWARE)                 *
***********************************************************************/

hal_bit_t hal_ctx_param_bit_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    CHECK_CTX_STATE(ctx, 0);
    CHECK_HANDLE(param, ctx, 0);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_bit_t *val = (hal_bit_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_param_bit_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_bit_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(param, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_bit_t *dst = (hal_bit_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

hal_float_t hal_ctx_param_float_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    CHECK_CTX_STATE(ctx, 0.0);
    CHECK_HANDLE(param, ctx, 0.0);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_float_t *val = (hal_float_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_param_float_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_float_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(param, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_float_t *dst = (hal_float_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

hal_s32_t hal_ctx_param_s32_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    CHECK_CTX_STATE(ctx, 0);
    CHECK_HANDLE(param, ctx, 0);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_s32_t *val = (hal_s32_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_param_s32_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_s32_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(param, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_s32_t *dst = (hal_s32_t *)((char *)ctx->after + entry->offset);
    *dst = val;
}

hal_u32_t hal_ctx_param_u32_get(hal_ctx_t *ctx, hal_param_handle_t param) {
    CHECK_CTX_STATE(ctx, 0);
    CHECK_HANDLE(param, ctx, 0);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_u32_t *val = (hal_u32_t *)((char *)ctx->after + entry->offset);
    return *val;
}

void hal_ctx_param_u32_set(hal_ctx_t *ctx, hal_param_handle_t param, hal_u32_t val) {
    CHECK_CTX_STATE_VOID(ctx);
    CHECK_HANDLE_VOID(param, ctx);
    
    hal_ctx_entry_t *entry = &ctx->entries[param];
    hal_u32_t *dst = (hal_u32_t *)((char *)ctx->after + entry->offset);
    *dst = val;
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
