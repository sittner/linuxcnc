/********************************************************************
* Description: rtapi_app.h (Stub for Standalone Mode)
*   Minimal stub for rtapi_app.h to support standalone compilation
*
*   This provides stub definitions for RTAPI module macros used in
*   LinuxCNC kernel modules. In standalone mode, these are no-ops.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef RTAPI_APP_H
#define RTAPI_APP_H

#ifdef TP_STANDALONE
    /* Standalone mode - stub out module-related macros */
    #define EXPORT_SYMBOL(sym)
    #define MODULE_LICENSE(lic)
    #define MODULE_AUTHOR(auth)
    #define MODULE_DESCRIPTION(desc)
    #define RTAPI_MP_INT(var, desc)
    #define RTAPI_MP_LONG(var, desc)
    #define RTAPI_MP_STRING(var, desc)
    #define RTAPI_MP_ARRAY_INT(var, num, desc)
    #define RTAPI_MP_ARRAY_LONG(var, num, desc)
    #define RTAPI_MP_ARRAY_STRING(var, num, desc)
#else
    /* Include real rtapi_app.h from system */
    #include_next <rtapi_app.h>
#endif

#endif /* RTAPI_APP_H */
