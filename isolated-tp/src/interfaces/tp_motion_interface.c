/********************************************************************
* Description: tp_motion_interface.c
*   Implementation of motion interface for trajectory planner
*
*   Default implementations that access emcmotStatus directly.
*   Can be replaced with custom implementations for unit testing.
*
* Author: Generated for LinuxCNC
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2024 All rights reserved.
********************************************************************/

#include "tp_motion_interface.h"
#include "motion.h"

/* External reference to motion status (set by motion module) */
extern emcmot_status_t *emcmotStatus;

/* Global interface instance */
tp_motion_interface_t tp_motion_interface = {0};

/* Default implementations that access emcmotStatus directly */
static int default_get_planner_type(void) {
    return emcmotStatus ? emcmotStatus->planner_type : 0;
}

static double default_get_jerk_limit(void) {
    return emcmotStatus ? emcmotStatus->jerk : 0.0;
}

static void default_set_distance_to_go(double distance) {
    if (emcmotStatus) emcmotStatus->distance_to_go = distance;
}

static void default_set_current_vel(double vel) {
    if (emcmotStatus) emcmotStatus->current_vel = vel;
}

static void default_set_current_acc(double acc) {
    if (emcmotStatus) emcmotStatus->current_acc = acc;
}

static void default_set_current_jerk(double jerk) {
    if (emcmotStatus) emcmotStatus->current_jerk = jerk;
}

static void default_set_requested_vel(double vel) {
    if (emcmotStatus) emcmotStatus->requested_vel = vel;
}

static void default_set_dtg(const EmcPose *dtg) {
    if (emcmotStatus && dtg) emcmotStatus->dtg = *dtg;
}

static void default_set_enables_queued(unsigned int enables) {
    if (emcmotStatus) emcmotStatus->enables_queued = enables;
}

static void default_set_spindle_sync(int sync) {
    if (emcmotStatus) emcmotStatus->spindleSync = sync;
}

static void default_set_current_dir(double x, double y, double z) {
    if (emcmotStatus) {
        emcmotStatus->current_dir.x = x;
        emcmotStatus->current_dir.y = y;
        emcmotStatus->current_dir.z = z;
    }
}

static unsigned int default_get_enables_new(void) {
    return emcmotStatus ? emcmotStatus->enables_new : 0;
}

void tpMotionInterfaceInit(void) {
    tp_motion_interface.get_planner_type = default_get_planner_type;
    tp_motion_interface.get_jerk_limit = default_get_jerk_limit;
    tp_motion_interface.set_distance_to_go = default_set_distance_to_go;
    tp_motion_interface.set_current_vel = default_set_current_vel;
    tp_motion_interface.set_current_acc = default_set_current_acc;
    tp_motion_interface.set_current_jerk = default_set_current_jerk;
    tp_motion_interface.set_requested_vel = default_set_requested_vel;
    tp_motion_interface.set_dtg = default_set_dtg;
    tp_motion_interface.set_enables_queued = default_set_enables_queued;
    tp_motion_interface.set_spindle_sync = default_set_spindle_sync;
    tp_motion_interface.set_current_dir = default_set_current_dir;
    tp_motion_interface.get_enables_new = default_get_enables_new;
}

void tpMotionInterfaceSet(const tp_motion_interface_t *interface) {
    if (interface) {
        tp_motion_interface = *interface;
    }
}
