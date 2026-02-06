/********************************************************************
* Description: tp_rtapi_interface.h
*   RTAPI Interface for Trajectory Planner
*
*   This interface abstracts RTAPI print/logging functions,
*   allowing the TP to be compiled and tested in standalone mode.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef TP_RTAPI_INTERFACE_H
#define TP_RTAPI_INTERFACE_H

/*
 * RTAPI Print Interface for Trajectory Planner
 * 
 * This interface abstracts RTAPI print/logging functions to enable:
 * 1. Standalone builds without RTAPI dependencies
 * 2. Unit testing with standard C I/O
 * 3. Future alternative logging implementations
 *
 * When TP_STANDALONE is defined, print functions map to standard printf.
 * When TP_STANDALONE is not defined, they map to RTAPI print functions.
 */

#ifdef TP_STANDALONE
    /* Standalone mode - use standard C printf */
    #include <stdio.h>
    
    /* Message level macros - map to simple identifiers for standalone */
    #define TP_MSG_NONE 0
    #define TP_MSG_ERR  1
    #define TP_MSG_WARN 2
    #define TP_MSG_INFO 3
    #define TP_MSG_DBG  4
    #define TP_MSG_ALL  5
    
    /* Print functions - map to printf in standalone mode */
    #define TP_PRINT_MSG(level, fmt, ...) \
        do { \
            const char* level_str = ""; \
            switch(level) { \
                case TP_MSG_ERR:  level_str = "ERR: "; break; \
                case TP_MSG_WARN: level_str = "WARN: "; break; \
                case TP_MSG_INFO: level_str = "INFO: "; break; \
                case TP_MSG_DBG:  level_str = "DBG: "; break; \
                default: break; \
            } \
            printf("%s" fmt, level_str, ##__VA_ARGS__); \
        } while(0)
    
    #define TP_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)

#else
    /* Normal mode - use RTAPI functions */
    #include "rtapi.h"
    
    /* Message level macros - map to RTAPI message levels */
    #define TP_MSG_NONE RTAPI_MSG_NONE
    #define TP_MSG_ERR  RTAPI_MSG_ERR
    #define TP_MSG_WARN RTAPI_MSG_WARN
    #define TP_MSG_INFO RTAPI_MSG_INFO
    #define TP_MSG_DBG  RTAPI_MSG_DBG
    #define TP_MSG_ALL  RTAPI_MSG_ALL
    
    /* Print functions - map to RTAPI functions */
    #define TP_PRINT_MSG(level, fmt, ...) rtapi_print_msg(level, fmt, ##__VA_ARGS__)
    #define TP_PRINT(fmt, ...) rtapi_print(fmt, ##__VA_ARGS__)

#endif /* TP_STANDALONE */

#endif /* TP_RTAPI_INTERFACE_H */
