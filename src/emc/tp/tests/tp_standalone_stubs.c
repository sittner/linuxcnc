/********************************************************************
* Description: tp_standalone_stubs.c
*   Stub implementations for standalone TP testing
*
*   This file provides minimal stub implementations of LinuxCNC
*   dependencies to enable standalone compilation of the trajectory
*   planner module with TP_STANDALONE defined.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#include <stddef.h>
#include "tp_standalone_stubs.h"
#include "../../../emc/motion/emcmotcfg.h"

/*
 * Note: emcmotStatus and emcmotConfig are defined in tp.c,
 * so we don't define them here to avoid duplicate symbols.
 */

/*
 * Axis property stubs
 * Return safe default values for axis velocity and acceleration limits
 */

double axis_get_vel_limit(int axis) {
    /* Return a reasonable default velocity limit */
    (void)axis;  /* Unused in stub */
    return 100.0;  /* arbitrary default */
}

double axis_get_acc_limit(int axis) {
    /* Return a reasonable default acceleration limit */
    (void)axis;  /* Unused in stub */
    return 1000.0;  /* arbitrary default */
}

/*
 * DIO/AIO write stubs
 * These do nothing in standalone mode but prevent link errors
 */

void dioWrite(int index, char value) {
    /* Stub: do nothing */
    (void)index;
    (void)value;
}

void aioWrite(int index, double value) {
    /* Stub: do nothing */
    (void)index;
    (void)value;
}

/*
 * Rotary unlock stubs
 * Manage state for rotary axis unlocking (unused in standalone)
 */

static int rotary_unlock_state[EMCMOT_MAX_AXIS] = {0};

void setRotaryUnlock(int axis, int unlock) {
    if (axis >= 0 && axis < EMCMOT_MAX_AXIS) {
        rotary_unlock_state[axis] = unlock;
    }
}

int getRotaryUnlock(int axis) {
    if (axis >= 0 && axis < EMCMOT_MAX_AXIS) {
        return rotary_unlock_state[axis];
    }
    return 0;
}
