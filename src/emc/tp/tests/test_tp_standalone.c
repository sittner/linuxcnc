/********************************************************************
* Description: test_tp_standalone.c
*   Simple test program for standalone TP compilation
*
*   This program exercises basic TP functionality to verify that the
*   trajectory planner can be compiled and linked independently from
*   the full LinuxCNC build system.
*
*   The goal is to validate abstractions and identify remaining
*   dependencies, not to comprehensively test TP behavior.
*
* Author: LinuxCNC Developers
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2025 All rights reserved.
********************************************************************/

#define TP_STANDALONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Need motion.h for types before including TP headers */
#include "motion.h"
#include "motion_types.h"

/* Include TP headers */
#include "../tp.h"
#include "../tc.h"
#include "../tcq.h"
#include "../tp_types.h"
#include "../blendmath.h"
#include "../spherical_arc.h"
#include "../tp_motion_interface.h"

/* Motion interface stubs for standalone mode */
static int stub_get_planner_type(void) {
    return 0;  /* Default planner */
}

static double stub_get_jerk_limit(void) {
    return 10000.0;  /* Default jerk limit */
}

static double stub_get_cycle_time(void) {
    return 0.001;  /* 1ms cycle time */
}

static void stub_set_distance_to_go(double distance) {
    (void)distance;
    /* Stub: do nothing */
}

static void stub_set_current_vel(double vel) {
    (void)vel;
    /* Stub: do nothing */
}

static void stub_set_current_acc(double acc) {
    (void)acc;
    /* Stub: do nothing */
}

static void stub_set_current_jerk(double jerk) {
    (void)jerk;
    /* Stub: do nothing */
}

static void stub_set_requested_vel(double vel) {
    (void)vel;
    /* Stub: do nothing */
}

static void stub_set_dtg(const EmcPose *dtg) {
    (void)dtg;
    /* Stub: do nothing */
}

static void stub_set_enables_queued(unsigned int enables) {
    (void)enables;
    /* Stub: do nothing */
}

static void stub_set_spindle_sync(int sync) {
    (void)sync;
    /* Stub: do nothing */
}

static void stub_set_current_dir(double x, double y, double z) {
    (void)x; (void)y; (void)z;
    /* Stub: do nothing */
}

static unsigned int stub_get_enables_new(void) {
    return 0xFF;  /* All axes enabled */
}

/* Motion functions stubs */
static void stub_dioWrite(int index, char value) {
    (void)index; (void)value;
}

static void stub_aioWrite(int index, double value) {
    (void)index; (void)value;
}

static void stub_setRotaryUnlock(int axis, int unlock) {
    (void)axis; (void)unlock;
}

static int stub_getRotaryUnlock(int axis) {
    (void)axis;
    return 0;
}

static double stub_axis_get_vel_limit(int axis) {
    (void)axis;
    return 100.0;
}

static double stub_axis_get_acc_limit(int axis) {
    (void)axis;
    return 1000.0;
}

/*
 * Initialize the TP motion interface with stub functions
 */
void init_motion_interface(void) {
    extern tp_motion_interface_t tp_motion_interface;
    
    tp_motion_interface.get_planner_type = stub_get_planner_type;
    tp_motion_interface.get_jerk_limit = stub_get_jerk_limit;
    tp_motion_interface.get_cycle_time = stub_get_cycle_time;
    tp_motion_interface.set_distance_to_go = stub_set_distance_to_go;
    tp_motion_interface.set_current_vel = stub_set_current_vel;
    tp_motion_interface.set_current_acc = stub_set_current_acc;
    tp_motion_interface.set_current_jerk = stub_set_current_jerk;
    tp_motion_interface.set_requested_vel = stub_set_requested_vel;
    tp_motion_interface.set_dtg = stub_set_dtg;
    tp_motion_interface.set_enables_queued = stub_set_enables_queued;
    tp_motion_interface.set_spindle_sync = stub_set_spindle_sync;
    tp_motion_interface.set_current_dir = stub_set_current_dir;
    tp_motion_interface.get_enables_new = stub_get_enables_new;
}

/*
 * Test basic TP initialization and simple operations
 */
int test_tp_basic(void) {
    TP_STRUCT tp;
    EmcPose start_pos, end_pos;
    struct state_tag_t tag = {0};
    int result;
    
    printf("Test: Basic TP initialization\n");
    
    /* Create TP with queue size */
    result = tpCreate(&tp, TP_DEFAULT_QUEUE_SIZE, 1);
    if (result != 0) {
        printf("FAIL: tpCreate returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpCreate\n");
    
    /* Initialize TP */
    result = tpInit(&tp);
    if (result != 0) {
        printf("FAIL: tpInit returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpInit\n");
    
    /* Set cycle time */
    result = tpSetCycleTime(&tp, 0.001);
    if (result != 0) {
        printf("FAIL: tpSetCycleTime returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpSetCycleTime\n");
    
    /* Set velocity and acceleration limits */
    result = tpSetVmax(&tp, 100.0, 200.0);
    if (result != 0) {
        printf("FAIL: tpSetVmax returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpSetVmax\n");
    
    result = tpSetAmax(&tp, 1000.0);
    if (result != 0) {
        printf("FAIL: tpSetAmax returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpSetAmax\n");
    
    /* Set initial position */
    ZERO_EMC_POSE(start_pos);
    result = tpSetPos(&tp, &start_pos);
    if (result != 0) {
        printf("FAIL: tpSetPos returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpSetPos\n");
    
    /* Add a simple line move */
    ZERO_EMC_POSE(end_pos);
    end_pos.tran.x = 10.0;
    end_pos.tran.y = 5.0;
    end_pos.tran.z = 2.0;
    
    result = tpAddLine(&tp, end_pos, EMC_MOTION_TYPE_FEED,
                      50.0,    /* vel */
                      100.0,   /* ini_maxvel */
                      500.0,   /* acc */
                      5000.0,  /* ini_maxjerk */
                      0xFF,    /* enables - all axes */
                      0,       /* atspeed */
                      -1,      /* indexrotary */
                      tag);
    if (result != 0) {
        printf("FAIL: tpAddLine returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpAddLine\n");
    
    /* Check queue depth */
    int depth = tpQueueDepth(&tp);
    printf("  Queue depth: %d\n", depth);
    
    /* Check if TP is done (should not be, we just added a move) */
    int done = tpIsDone(&tp);
    printf("  TP done: %s\n", done ? "yes" : "no");
    
    /* Clear the TP */
    result = tpClear(&tp);
    if (result != 0) {
        printf("FAIL: tpClear returned %d\n", result);
        return -1;
    }
    printf("  PASS: tpClear\n");
    
    printf("Test: Basic TP operations - PASSED\n\n");
    return 0;
}

/*
 * Main test program
 */
int main(int argc, char *argv[]) {
    struct emcmot_status_t status;
    struct emcmot_config_t config;
    
    (void)argc;
    (void)argv;
    
    printf("==========================================\n");
    printf("TP Standalone Test Program\n");
    printf("==========================================\n\n");
    
    /* Initialize motion interface stubs */
    printf("Initializing motion interface stubs...\n");
    init_motion_interface();
    
    /* Register motion functions */
    printf("Registering motion functions...\n");
    tpMotFunctions(stub_dioWrite, stub_aioWrite, 
                   stub_setRotaryUnlock, stub_getRotaryUnlock,
                   stub_axis_get_vel_limit, stub_axis_get_acc_limit);
    
    /* Set up motion data pointers with allocated structures */
    memset(&status, 0, sizeof(status));
    memset(&config, 0, sizeof(config));
    tpMotData(&status, &config);
    
    printf("\n");
    
    /* Run basic tests */
    if (test_tp_basic() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    printf("==========================================\n");
    printf("All tests PASSED\n");
    printf("==========================================\n");
    
    return 0;
}
