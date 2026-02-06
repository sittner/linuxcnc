/********************************************************************
* Description: tp_hal_interface.h
*   HAL Interface for Trajectory Planner
*
*   This interface abstracts HAL (Hardware Abstraction Layer) functions,
*   allowing the TP to be compiled and tested in standalone mode.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef TP_HAL_INTERFACE_H
#define TP_HAL_INTERFACE_H

/*
 * HAL Interface for Trajectory Planner
 * 
 * This interface abstracts HAL functions to enable:
 * 1. Standalone builds without HAL dependencies
 * 2. Unit testing without kernel modules
 * 3. Future alternative HAL implementations
 *
 * When TP_STANDALONE is defined, HAL functions become stubs/no-ops.
 * When TP_STANDALONE is not defined, they map to real HAL functions.
 */

#ifdef TP_STANDALONE
    /* Standalone mode - use stub types and no-op functions */
    #include <stdint.h>
    
    /* Stub HAL types */
    typedef int hal_bit_t;
    typedef double hal_float_t;
    typedef int32_t hal_s32_t;
    typedef uint32_t hal_u32_t;
    typedef int hal_comp_id;
    
    /* Direction/access constants - stub values */
    #define TP_HAL_IN     0
    #define TP_HAL_OUT    1
    #define TP_HAL_IO     2
    #define TP_HAL_RO     0
    #define TP_HAL_RW     1
    
    /* Stub HAL functions - return success codes */
    #define tp_hal_init(name) (1)  /* Return fake component ID */
    #define tp_hal_ready(comp_id) (0)
    #define tp_hal_exit(comp_id) do {} while(0)
    #define tp_hal_pin_bit_new(name, dir, ptr, comp_id) (0)
    #define tp_hal_pin_float_new(name, dir, ptr, comp_id) (0)
    #define tp_hal_pin_s32_new(name, dir, ptr, comp_id) (0)
    #define tp_hal_pin_u32_new(name, dir, ptr, comp_id) (0)
    #define tp_hal_param_bit_new(name, dir, ptr, comp_id) (0)
    #define tp_hal_param_float_new(name, dir, ptr, comp_id) (0)
    #define tp_hal_export_funct(name, funct, arg, uses_fp, reentrant, comp_id) (0)

#else
    /* Normal mode - use real HAL functions */
    #include "hal.h"
    
    /* Map direction/access constants to real HAL constants */
    #define TP_HAL_IN     HAL_IN
    #define TP_HAL_OUT    HAL_OUT
    #define TP_HAL_IO     HAL_IO
    #define TP_HAL_RO     HAL_RO
    #define TP_HAL_RW     HAL_RW
    
    /* Map to real HAL functions */
    #define tp_hal_init(name) hal_init(name)
    #define tp_hal_ready(comp_id) hal_ready(comp_id)
    #define tp_hal_exit(comp_id) hal_exit(comp_id)
    #define tp_hal_pin_bit_new(name, dir, ptr, comp_id) hal_pin_bit_new(name, dir, ptr, comp_id)
    #define tp_hal_pin_float_new(name, dir, ptr, comp_id) hal_pin_float_new(name, dir, ptr, comp_id)
    #define tp_hal_pin_s32_new(name, dir, ptr, comp_id) hal_pin_s32_new(name, dir, ptr, comp_id)
    #define tp_hal_pin_u32_new(name, dir, ptr, comp_id) hal_pin_u32_new(name, dir, ptr, comp_id)
    #define tp_hal_param_bit_new(name, dir, ptr, comp_id) hal_param_bit_new(name, dir, ptr, comp_id)
    #define tp_hal_param_float_new(name, dir, ptr, comp_id) hal_param_float_new(name, dir, ptr, comp_id)
    #define tp_hal_export_funct(name, funct, arg, uses_fp, reentrant, comp_id) hal_export_funct(name, funct, arg, uses_fp, reentrant, comp_id)

#endif /* TP_STANDALONE */

#endif /* TP_HAL_INTERFACE_H */
