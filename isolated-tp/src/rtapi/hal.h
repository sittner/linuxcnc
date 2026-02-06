/********************************************************************
* Description: hal.h (Stub for Standalone Mode)
*   Minimal stub for hal.h to support standalone compilation
*
*   This provides stub definitions for HAL types and functions.
*   The actual HAL interface is abstracted via tp_hal_interface.h,
*   but some files may still include hal.h directly.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef HAL_H
#define HAL_H

#ifdef TP_STANDALONE
    #include <stdint.h>
    
    /* Stub HAL types */
    typedef int hal_bit_t;
    typedef double hal_float_t;
    typedef int32_t hal_s32_t;
    typedef uint32_t hal_u32_t;
    
    /* Stub HAL constants */
    #define HAL_IN  0
    #define HAL_OUT 1
    #define HAL_IO  2
    #define HAL_RO  0
    #define HAL_RW  1
    
    /* Stub HAL functions */
    static inline int hal_init(const char *name) { (void)name; return 1; }
    static inline int hal_exit(int comp_id) { (void)comp_id; return 0; }
    static inline int hal_ready(int comp_id) { (void)comp_id; return 0; }
#else
    /* Include real hal.h from system */
    #include_next <hal.h>
#endif

#endif /* HAL_H */
