#ifndef HAL_API_H
#define HAL_API_H

/***********************************************************************
*                        HAL ACCESSOR API                              *
*                                                                      *
* Purpose:                                                             *
*   This header provides abstraction layer for accessing HAL pins and  *
*   parameters. In Phase 1, these are thin wrappers over direct access.*
*   In Phase 3, they will enable thread-local data copies for          *
*   improved thread safety and determinism.                            *
*                                                                      *
* Design Documentation:                                                *
*   See THREAD_LOCAL_HAL.md in repository root for complete            *
*   architecture details and migration strategy.                       *
*                                                                      *
* Migration Patterns:                                                  *
*                                                                      *
*   Pin conversion (double pointer pattern):                           *
*     **pin           →  hal_pin_get_TYPE(&pin)                        *
*     **pin = value   →  hal_pin_set_TYPE(&pin, value)                 *
*                                                                      *
*   Parameter conversion (single pointer pattern):                     *
*     *param          →  hal_param_get_TYPE(&param)                    *
*     *param = value  →  hal_param_set_TYPE(&param, value)             *
*                                                                      *
*   Thread sync (add to component update functions):                   *
*     void update(void *arg, long period) {                            *
*         hal_thread_sync_read();                                      *
*         // ... component logic ...                                   *
*         hal_thread_sync_write();                                     *
*     }                                                                *
*                                                                      *
* Notes:                                                               *
*   - All functions are static inline for zero overhead                *
*   - Compatible with both ULAPI and RTAPI components                  *
*   - Existing code continues to work unchanged                        *
*   - New code should use these accessors for future compatibility     *
*                                                                      *
************************************************************************/

#include "hal.h"

/***********************************************************************
*                      PIN ACCESSOR FUNCTIONS                          *
*                                                                      *
* Pins use double pointers - the pin variable points to a pointer      *
* that points to the actual data (signal or dummy signal).             *
************************************************************************/

// Getters
static inline hal_bit_t hal_pin_get_bit(hal_bit_t **pin) {
    return **pin;
}

static inline hal_float_t hal_pin_get_float(hal_float_t **pin) {
    return **pin;
}

static inline hal_s32_t hal_pin_get_s32(hal_s32_t **pin) {
    return **pin;
}

static inline hal_u32_t hal_pin_get_u32(hal_u32_t **pin) {
    return **pin;
}

// Setters
static inline void hal_pin_set_bit(hal_bit_t **pin, hal_bit_t value) {
    **pin = value;
}

static inline void hal_pin_set_float(hal_float_t **pin, hal_float_t value) {
    **pin = value;
}

static inline void hal_pin_set_s32(hal_s32_t **pin, hal_s32_t value) {
    **pin = value;
}

static inline void hal_pin_set_u32(hal_u32_t **pin, hal_u32_t value) {
    **pin = value;
}

/***********************************************************************
*                   PARAMETER ACCESSOR FUNCTIONS                       *
*                                                                      *
* Parameters use single pointers - they point directly to the data     *
* and are not linked to signals.                                       *
************************************************************************/

// Getters
static inline hal_bit_t hal_param_get_bit(hal_bit_t *param) {
    return *param;
}

static inline hal_float_t hal_param_get_float(hal_float_t *param) {
    return *param;
}

static inline hal_s32_t hal_param_get_s32(hal_s32_t *param) {
    return *param;
}

static inline hal_u32_t hal_param_get_u32(hal_u32_t *param) {
    return *param;
}

// Setters
static inline void hal_param_set_bit(hal_bit_t *param, hal_bit_t value) {
    *param = value;
}

static inline void hal_param_set_float(hal_float_t *param, hal_float_t value) {
    *param = value;
}

static inline void hal_param_set_s32(hal_s32_t *param, hal_s32_t value) {
    *param = value;
}

static inline void hal_param_set_u32(hal_u32_t *param, hal_u32_t value) {
    *param = value;
}

/***********************************************************************
*                    THREAD SYNC FUNCTIONS                             *
*                                                                      *
* In Phase 1, these are no-ops. In Phase 3, they will sync data        *
* between thread-local copies and the master HAL data.                 *
*                                                                      *
* Userspace components must call these in their main loop.             *
* Realtime components have sync handled by the thread infrastructure.  *
************************************************************************/

static inline void hal_thread_sync_read(void) {
    // Phase 1: No-op
    // Phase 3: Will sync from master to thread-local copy
}

static inline void hal_thread_sync_write(void) {
    // Phase 1: No-op
    // Phase 3: Will sync dirty data from thread-local copy to master
}

#endif /* HAL_API_H */
