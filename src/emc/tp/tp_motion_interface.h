/********************************************************************
* Description: tp_motion_interface.h
*   Motion Interface for Trajectory Planner
*
*   This interface abstracts the motion module dependencies,
*   allowing the TP to be unit tested independently.
*
* Author: Generated for LinuxCNC
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2024 All rights reserved.
********************************************************************/

#ifndef TP_MOTION_INTERFACE_H
#define TP_MOTION_INTERFACE_H

#include "posemath.h"
#include "emcpose.h"

/*
 * Motion Interface for Trajectory Planner
 * 
 * This interface abstracts the motion module dependencies,
 * allowing the TP to be unit tested independently.
 */

/* Callback typedefs for reading motion parameters */
typedef int (*tp_get_planner_type_fn)(void);
typedef double (*tp_get_jerk_limit_fn)(void);
typedef double (*tp_get_cycle_time_fn)(void);

/* Callback typedefs for writing motion status */
typedef void (*tp_set_distance_to_go_fn)(double distance);
typedef void (*tp_set_current_vel_fn)(double vel);
typedef void (*tp_set_current_acc_fn)(double acc);
typedef void (*tp_set_current_jerk_fn)(double jerk);
typedef void (*tp_set_requested_vel_fn)(double vel);
typedef void (*tp_set_dtg_fn)(const EmcPose *dtg);
typedef void (*tp_set_enables_queued_fn)(unsigned int enables);
typedef void (*tp_set_spindle_sync_fn)(int sync);
typedef void (*tp_set_current_dir_fn)(double x, double y, double z);

/* Motion interface structure */
typedef struct {
    /* Getters - read motion parameters */
    tp_get_planner_type_fn get_planner_type;
    tp_get_jerk_limit_fn get_jerk_limit;
    tp_get_cycle_time_fn get_cycle_time;
    
    /* Setters - write motion status */
    tp_set_distance_to_go_fn set_distance_to_go;
    tp_set_current_vel_fn set_current_vel;
    tp_set_current_acc_fn set_current_acc;
    tp_set_current_jerk_fn set_current_jerk;
    tp_set_requested_vel_fn set_requested_vel;
    tp_set_dtg_fn set_dtg;
    tp_set_enables_queued_fn set_enables_queued;
    tp_set_spindle_sync_fn set_spindle_sync;
    tp_set_current_dir_fn set_current_dir;
    
    /* For getting enables_new to copy to enables_queued when idle */
    unsigned int (*get_enables_new)(void);
} tp_motion_interface_t;

/* Global interface instance (set by motion module) */
extern tp_motion_interface_t tp_motion_interface;

/* Initialize the interface with default (direct access) implementations */
void tpMotionInterfaceInit(void);

/* Set custom interface (for unit testing) */
void tpMotionInterfaceSet(const tp_motion_interface_t *interface);

/* Convenience macros for accessing the interface */
#define TP_GET_PLANNER_TYPE() \
    (tp_motion_interface.get_planner_type ? tp_motion_interface.get_planner_type() : 0)

#define TP_GET_JERK_LIMIT() \
    (tp_motion_interface.get_jerk_limit ? tp_motion_interface.get_jerk_limit() : 0.0)

#define TP_SET_DISTANCE_TO_GO(d) \
    do { if (tp_motion_interface.set_distance_to_go) tp_motion_interface.set_distance_to_go(d); } while(0)

#define TP_SET_CURRENT_VEL(v) \
    do { if (tp_motion_interface.set_current_vel) tp_motion_interface.set_current_vel(v); } while(0)

#define TP_SET_CURRENT_ACC(a) \
    do { if (tp_motion_interface.set_current_acc) tp_motion_interface.set_current_acc(a); } while(0)

#define TP_SET_CURRENT_JERK(j) \
    do { if (tp_motion_interface.set_current_jerk) tp_motion_interface.set_current_jerk(j); } while(0)

#define TP_SET_REQUESTED_VEL(v) \
    do { if (tp_motion_interface.set_requested_vel) tp_motion_interface.set_requested_vel(v); } while(0)

#define TP_SET_DTG(pose) \
    do { if (tp_motion_interface.set_dtg) tp_motion_interface.set_dtg(pose); } while(0)

#define TP_SET_ENABLES_QUEUED(e) \
    do { if (tp_motion_interface.set_enables_queued) tp_motion_interface.set_enables_queued(e); } while(0)

#define TP_SET_SPINDLE_SYNC(s) \
    do { if (tp_motion_interface.set_spindle_sync) tp_motion_interface.set_spindle_sync(s); } while(0)

#define TP_SET_CURRENT_DIR(x, y, z) \
    do { if (tp_motion_interface.set_current_dir) tp_motion_interface.set_current_dir(x, y, z); } while(0)

#define TP_GET_ENABLES_NEW() \
    (tp_motion_interface.get_enables_new ? tp_motion_interface.get_enables_new() : 0)

#endif /* TP_MOTION_INTERFACE_H */
