/********************************************************************
* Description: axis.h (Stub for Standalone Mode)
*   Minimal stub for axis.h to support standalone compilation
*
*   This provides stub declarations for axis functions.
*   The actual implementations are in tp_standalone_stubs.c
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef AXIS_H
#define AXIS_H

#ifdef TP_STANDALONE
    /* Standalone mode - declare stub functions */
    /* Implementations are in tp_standalone_stubs.c */
    
    double axis_get_vel_limit(int axis);
    double axis_get_acc_limit(int axis);
    
    void dioWrite(int index, char value);
    void aioWrite(int index, double value);
    
    void setRotaryUnlock(int axis, int unlock);
    int getRotaryUnlock(int axis);
#else
    /* Include real axis.h from system */
    #include_next <axis.h>
#endif

#endif /* AXIS_H */
