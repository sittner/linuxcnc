/********************************************************************
* Description: tp_motion_interface.h
*   Explicit interface between TP and motion controller
*   Defines exactly what TP reads from motion module
********************************************************************/
#ifndef TP_MOTION_INTERFACE_H
#define TP_MOTION_INTERFACE_H

#include "posemath.h"
#include "emcpose.h"
#include "emcmotcfg.h"

/**
 * Motion controller status (what TP reads each cycle)
 */
typedef struct tp_motion_status {
    int stepping;
    EmcPose carte_pos_cmd;
    
    // Spindle status (per spindle)
    struct {
        double speed;
        double revs;
        int at_speed;
        int index_enable;
        int direction;
    } spindle[EMCMOT_MAX_SPINDLES];
    
    int on_soft_limit;
} tp_motion_status_t;

/**
 * Motion controller configuration (what TP needs to know)
 */
typedef struct tp_motion_config {
    double trajCycleTime;
    int numJoints;
    int kinematics_type;
    
    double max_velocity;
    double max_acceleration;
    double maxFeedScale;
    
    // Arc blend configuration
    int arcBlendOptDepth;
    int arcBlendEnable;
    int arcBlendFallbackEnable;
    int arcBlendGapCycles;
    double arcBlendRampFreq;
    double arcBlendTangentKinkRatio;
    
    // DIO/AIO configuration
    int numDIO;
    int numAIO;
} tp_motion_config_t;

/**
 * Populate TP motion status from motion controller
 * (Adapter function - implementation in tp.c or motion module)
 */
void tp_motion_populate_status(void *tp, void *emcmot_status);

/**
 * Set TP motion configuration from motion controller
 * (Adapter function - implementation in tp.c or motion module)
 */
void tp_motion_set_config(void *tp, void *emcmot_config);

#endif // TP_MOTION_INTERFACE_H
