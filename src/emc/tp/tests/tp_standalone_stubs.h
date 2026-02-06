/********************************************************************
* Description: tp_standalone_stubs.h
*   Stub definitions for standalone TP testing
*
*   This file provides minimal stub implementations of LinuxCNC
*   dependencies to enable standalone compilation of the trajectory
*   planner module with TP_STANDALONE defined.
*
*   This header documents external dependencies that still need to be
*   addressed for full decoupling of the TP module.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#ifndef TP_STANDALONE_STUBS_H
#define TP_STANDALONE_STUBS_H

#include <stdint.h>

/*
 * Axis interface stubs
 * Functions that would normally access individual axis properties
 */
double axis_get_vel_limit(int axis);
double axis_get_acc_limit(int axis);

/*
 * DIO/AIO write function stubs
 * For digital and analog output during motion
 */
void dioWrite(int index, char value);
void aioWrite(int index, double value);

/*
 * Rotary unlock functions
 * For managing rotary axis unlocking
 */
void setRotaryUnlock(int axis, int unlock);
int getRotaryUnlock(int axis);

#endif /* TP_STANDALONE_STUBS_H */
