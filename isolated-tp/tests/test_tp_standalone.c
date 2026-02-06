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
#include "tp.h"
#include "tc.h"
#include "tcq.h"
#include "tp_types.h"
#include "blendmath.h"
#include "spherical_arc.h"
#include "tp_motion_interface.h"
#include "sp_scurve.h"

/* Test helper macros */
#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s\n", msg); \
            printf("    Condition failed: %s\n", #cond); \
            return -1; \
        } \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("  FAIL: %s\n", msg); \
            printf("    Expected: %d, Got: %d\n", (int)(b), (int)(a)); \
            return -1; \
        } \
    } while(0)

#define ASSERT_NEAR(a, b, tol, msg) \
    do { \
        double _diff = fabs((double)(a) - (double)(b)); \
        if (_diff > (tol)) { \
            printf("  FAIL: %s\n", msg); \
            printf("    Expected: %.10f, Got: %.10f, Diff: %.10f, Tol: %.10f\n", \
                   (double)(b), (double)(a), _diff, (double)(tol)); \
            return -1; \
        } \
    } while(0)

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
 * Test S-curve velocity calculation (without testing solve_cubic directly)
 */
int test_scurve_velocity(void) {
    double req_v;
    int result;
    
    printf("Test: S-curve velocity calculations\n");
    
    /* Test findSCurveVSpeed with typical values */
    result = findSCurveVSpeed(100.0, /* distance in mm */
                             1000.0, /* max accel mm/s^2 */
                             10000.0, /* max jerk mm/s^3 */
                             &req_v);
    ASSERT_EQ(result, 1, "findSCurveVSpeed should succeed");
    ASSERT_TRUE(req_v > 0.0, "findSCurveVSpeed velocity should be positive");
    ASSERT_TRUE(req_v < 500.0, "findSCurveVSpeed velocity should be reasonable");
    printf("  PASS: findSCurveVSpeed basic case (distance=100mm, v=%.2f mm/s)\n", req_v);
    
    /* Test with very short distance */
    result = findSCurveVSpeed(1.0, 1000.0, 10000.0, &req_v);
    ASSERT_EQ(result, 1, "findSCurveVSpeed should handle short distance");
    ASSERT_TRUE(req_v > 0.0 && req_v < 100.0, "findSCurveVSpeed short distance velocity reasonable");
    printf("  PASS: findSCurveVSpeed short distance (d=1mm, v=%.2f mm/s)\n", req_v);
    
    /* Test with high jerk */
    result = findSCurveVSpeed(100.0, 1000.0, 100000.0, &req_v);
    ASSERT_EQ(result, 1, "findSCurveVSpeed should handle high jerk");
    printf("  PASS: findSCurveVSpeed high jerk (v=%.2f mm/s)\n", req_v);
    
    /* Test findSCurveVSpeedWithEndSpeed */
    result = findSCurveVSpeedWithEndSpeed(100.0, /* distance */
                                          50.0,  /* end velocity */
                                          1000.0, /* max accel */
                                          10000.0, /* max jerk */
                                          &req_v);
    ASSERT_EQ(result, 1, "findSCurveVSpeedWithEndSpeed should succeed");
    ASSERT_TRUE(req_v >= 50.0, "findSCurveVSpeedWithEndSpeed velocity >= end velocity");
    printf("  PASS: findSCurveVSpeedWithEndSpeed (Ve=50, v=%.2f mm/s)\n", req_v);
    
    printf("Test: S-curve velocity - PASSED\n\n");
    return 0;
}

/*
 * Test S-curve distance calculations
 */
int test_scurve_distance(void) {
    double dist;
    
    printf("Test: S-curve distance calculations\n");
    
    /* Test stoppingDist with typical values */
    dist = stoppingDist(100.0,  /* velocity mm/s */
                       0.0,     /* initial accel */
                       1000.0,  /* max accel */
                       10000.0); /* max jerk */
    ASSERT_TRUE(dist > 0.0, "stoppingDist should be positive");
    ASSERT_TRUE(dist < 50.0, "stoppingDist should be reasonable");
    printf("  PASS: stoppingDist from v=100 mm/s: %.2f mm\n", dist);
    
    /* Test finishWithSpeedDist */
    dist = finishWithSpeedDist(100.0,  /* initial velocity */
                              50.0,   /* end velocity */
                              0.0,    /* initial accel */
                              1000.0, /* max accel */
                              10000.0); /* max jerk */
    ASSERT_TRUE(dist >= 0.0, "finishWithSpeedDist should be non-negative");
    printf("  PASS: finishWithSpeedDist v=100->50 mm/s: %.2f mm\n", dist);
    
    /* Test zero velocity case */
    dist = stoppingDist(0.0, 0.0, 1000.0, 10000.0);
    ASSERT_NEAR(dist, 0.0, 1e-6, "stoppingDist from zero velocity should be zero");
    printf("  PASS: stoppingDist from zero velocity\n");
    
    printf("Test: S-curve distance - PASSED\n\n");
    return 0;
}

/*
 * Test blend math utility functions
 */
int test_blendmath_utils(void) {
    PmCartesian u1, u2;
    double theta;
    int result;
    
    printf("Test: Blend math utilities\n");
    
    /* Test intersection angle calculation - 90 degree angle */
    u1.x = 1.0; u1.y = 0.0; u1.z = 0.0;
    u2.x = 0.0; u2.y = 1.0; u2.z = 0.0;
    result = findIntersectionAngle(&u1, &u2, &theta);
    ASSERT_EQ(result, 0, "findIntersectionAngle should succeed");
    ASSERT_NEAR(theta, M_PI/4.0, 1e-6, "90 degree angle should give theta=pi/4");
    printf("  PASS: findIntersectionAngle 90 degrees (theta=%.4f rad)\n", theta);
    
    /* Test parallel vectors */
    u1.x = 1.0; u1.y = 0.0; u1.z = 0.0;
    u2.x = 1.0; u2.y = 0.0; u2.z = 0.0;
    result = pmCartCartParallel(&u1, &u2, 1e-6);
    ASSERT_TRUE(result != 0, "Parallel vectors should be detected");
    printf("  PASS: pmCartCartParallel detects parallel vectors\n");
    
    /* Test anti-parallel vectors */
    u1.x = 1.0; u1.y = 0.0; u1.z = 0.0;
    u2.x = -1.0; u2.y = 0.0; u2.z = 0.0;
    result = pmCartCartAntiParallel(&u1, &u2, 1e-6);
    ASSERT_TRUE(result != 0, "Anti-parallel vectors should be detected");
    printf("  PASS: pmCartCartAntiParallel detects anti-parallel vectors\n");
    
    /* Test saturate function */
    double val = saturate(150.0, 100.0);
    ASSERT_NEAR(val, 100.0, 1e-6, "saturate should clip to max");
    val = saturate(50.0, 100.0);
    ASSERT_NEAR(val, 50.0, 1e-6, "saturate should not clip below max");
    printf("  PASS: saturate function\n");
    
    printf("Test: Blend math utilities - PASSED\n\n");
    return 0;
}

/*
 * Test trajectory segment (TC) functions
 */
int test_tc_basic(void) {
    TC_STRUCT tc;
    EmcPose end_pos;
    int result;
    
    printf("Test: Trajectory segment (TC) basic operations\n");
    
    /* Initialize a line segment */
    result = tcInit(&tc, 
                   EMC_MOTION_TYPE_FEED,
                   0, /* canon_motion_type */
                   0.001, /* cycle_time */
                   0xFF, /* enables */
                   0); /* atspeed */
    ASSERT_EQ(result, 0, "tcInit should succeed");
    printf("  PASS: tcInit\n");
    
    /* Clear flags */
    result = tcClearFlags(&tc);
    ASSERT_EQ(result, 0, "tcClearFlags should succeed");
    printf("  PASS: tcClearFlags\n");
    
    /* Test endpoint/startpoint functions with zeroed TC */
    result = tcGetEndpoint(&tc, &end_pos);
    ASSERT_EQ(result, 0, "tcGetEndpoint should succeed even with empty TC");
    printf("  PASS: tcGetEndpoint\n");
    
    result = tcGetStartpoint(&tc, &end_pos);
    ASSERT_EQ(result, 0, "tcGetStartpoint should succeed even with empty TC");
    printf("  PASS: tcGetStartpoint\n");
    
    /* Test initial kink properties */
    result = tcInitKinkProperties(&tc);
    ASSERT_EQ(result, 0, "tcInitKinkProperties should succeed");
    printf("  PASS: tcInitKinkProperties\n");
    
    printf("Test: TC basic operations - PASSED\n\n");
    return 0;
}

/*
 * Test queue operations
 */
int test_tcq_operations(void) {
    TC_QUEUE_STRUCT tcq;
    TC_STRUCT tc_space[10];
    TC_STRUCT tc;
    int result;
    
    printf("Test: Queue (TCQ) operations\n");
    
    /* Create queue */
    result = tcqCreate(&tcq, 10, tc_space);
    ASSERT_EQ(result, 0, "tcqCreate should succeed");
    printf("  PASS: tcqCreate\n");
    
    /* Initialize queue */
    result = tcqInit(&tcq);
    ASSERT_EQ(result, 0, "tcqInit should succeed");
    printf("  PASS: tcqInit\n");
    
    /* Check empty queue */
    ASSERT_EQ(tcqLen(&tcq), 0, "Queue should start empty");
    printf("  PASS: Queue starts empty\n");
    
    /* Add items to queue */
    memset(&tc, 0, sizeof(tc));
    tc.id = 1;
    result = tcqPut(&tcq, &tc);
    ASSERT_EQ(result, 0, "tcqPut should succeed");
    ASSERT_EQ(tcqLen(&tcq), 1, "Queue length should be 1 after first put");
    printf("  PASS: tcqPut first item\n");
    
    /* Add more items */
    tc.id = 2;
    tcqPut(&tcq, &tc);
    tc.id = 3;
    tcqPut(&tcq, &tc);
    ASSERT_EQ(tcqLen(&tcq), 3, "Queue length should be 3");
    printf("  PASS: tcqPut multiple items (length=%d)\n", tcqLen(&tcq));
    
    /* Test queue item access */
    TC_STRUCT *item = tcqItem(&tcq, 0);
    ASSERT_TRUE(item != NULL, "tcqItem should return valid pointer");
    ASSERT_EQ(item->id, 1, "First item should have id=1");
    printf("  PASS: tcqItem access\n");
    
    /* Test last item access */
    TC_STRUCT *last = tcqLast(&tcq);
    ASSERT_TRUE(last != NULL, "tcqLast should return valid pointer");
    ASSERT_EQ(last->id, 3, "Last item should have id=3");
    printf("  PASS: tcqLast access\n");
    
    /* Test pop back */
    result = tcqPopBack(&tcq);
    ASSERT_EQ(result, 0, "tcqPopBack should succeed");
    ASSERT_EQ(tcqLen(&tcq), 2, "Queue length should be 2 after pop");
    printf("  PASS: tcqPopBack\n");
    
    /* Test queue full condition */
    for (int i = 0; i < 10; i++) {
        tc.id = i + 10;
        tcqPut(&tcq, &tc);
    }
    ASSERT_TRUE(tcqFull(&tcq), "Queue should be full");
    printf("  PASS: tcqFull detection\n");
    
    printf("Test: Queue operations - PASSED\n\n");
    return 0;
}

/*
 * Test integration: multi-segment motion
 */
int test_integration_multisegment(void) {
    TP_STRUCT tp;
    EmcPose pos, end_pos;
    struct state_tag_t tag = {0};
    int result;
    
    printf("Test: Integration - Multi-segment motion\n");
    
    /* Create and initialize TP */
    result = tpCreate(&tp, TP_DEFAULT_QUEUE_SIZE, 1);
    ASSERT_EQ(result, 0, "tpCreate should succeed");
    
    result = tpInit(&tp);
    ASSERT_EQ(result, 0, "tpInit should succeed");
    
    result = tpSetCycleTime(&tp, 0.001);
    ASSERT_EQ(result, 0, "tpSetCycleTime should succeed");
    
    result = tpSetVmax(&tp, 100.0, 200.0);
    ASSERT_EQ(result, 0, "tpSetVmax should succeed");
    
    result = tpSetAmax(&tp, 1000.0);
    ASSERT_EQ(result, 0, "tpSetAmax should succeed");
    
    /* Set starting position */
    ZERO_EMC_POSE(pos);
    result = tpSetPos(&tp, &pos);
    ASSERT_EQ(result, 0, "tpSetPos should succeed");
    printf("  PASS: TP initialization\n");
    
    /* Add first line segment */
    ZERO_EMC_POSE(end_pos);
    end_pos.tran.x = 10.0;
    result = tpAddLine(&tp, end_pos, EMC_MOTION_TYPE_FEED,
                      50.0, 100.0, 500.0, 5000.0, 0xFF, 0, -1, tag);
    ASSERT_EQ(result, 0, "First tpAddLine should succeed");
    
    /* Add second line segment */
    end_pos.tran.x = 10.0;
    end_pos.tran.y = 10.0;
    result = tpAddLine(&tp, end_pos, EMC_MOTION_TYPE_FEED,
                      50.0, 100.0, 500.0, 5000.0, 0xFF, 0, -1, tag);
    ASSERT_EQ(result, 0, "Second tpAddLine should succeed");
    
    /* Add third line segment */
    end_pos.tran.x = 0.0;
    end_pos.tran.y = 10.0;
    result = tpAddLine(&tp, end_pos, EMC_MOTION_TYPE_FEED,
                      50.0, 100.0, 500.0, 5000.0, 0xFF, 0, -1, tag);
    ASSERT_EQ(result, 0, "Third tpAddLine should succeed");
    
    ASSERT_EQ(tpQueueDepth(&tp), 3, "Queue should have 3 segments");
    printf("  PASS: Added 3 line segments to queue\n");
    
    /* Run a few cycles */
    for (int i = 0; i < 10; i++) {
        result = tpRunCycle(&tp, 0);
        ASSERT_EQ(result, 0, "tpRunCycle should succeed");
    }
    printf("  PASS: Executed 10 TP cycles\n");
    
    /* Clear queue */
    result = tpClear(&tp);
    ASSERT_EQ(result, 0, "tpClear should succeed");
    ASSERT_EQ(tpQueueDepth(&tp), 0, "Queue should be empty after clear");
    printf("  PASS: tpClear\n");
    
    printf("Test: Integration - Multi-segment motion - PASSED\n\n");
    return 0;
}

/*
 * Test circular arc motion
 */
int test_circular_arc(void) {
    TP_STRUCT tp;
    EmcPose start_pos, end_pos;
    PmCartesian center, normal;
    struct state_tag_t tag = {0};
    int result;
    
    printf("Test: Circular arc motion\n");
    
    /* Create and initialize TP */
    result = tpCreate(&tp, TP_DEFAULT_QUEUE_SIZE, 1);
    ASSERT_EQ(result, 0, "tpCreate should succeed");
    
    result = tpInit(&tp);
    ASSERT_EQ(result, 0, "tpInit should succeed");
    
    result = tpSetCycleTime(&tp, 0.001);
    ASSERT_EQ(result, 0, "tpSetCycleTime should succeed");
    
    result = tpSetVmax(&tp, 100.0, 200.0);
    ASSERT_EQ(result, 0, "tpSetVmax should succeed");
    
    result = tpSetAmax(&tp, 1000.0);
    ASSERT_EQ(result, 0, "tpSetAmax should succeed");
    
    /* Set starting position */
    ZERO_EMC_POSE(start_pos);
    start_pos.tran.x = 10.0;
    start_pos.tran.y = 0.0;
    result = tpSetPos(&tp, &start_pos);
    ASSERT_EQ(result, 0, "tpSetPos should succeed");
    printf("  PASS: TP initialization for arc\n");
    
    /* Add a circular arc - quarter circle in XY plane */
    ZERO_EMC_POSE(end_pos);
    end_pos.tran.x = 0.0;
    end_pos.tran.y = 10.0;
    
    /* Center of circle at origin */
    center.x = 0.0;
    center.y = 0.0;
    center.z = 0.0;
    
    /* Normal pointing up (Z axis) */
    normal.x = 0.0;
    normal.y = 0.0;
    normal.z = 1.0;
    
    result = tpAddCircle(&tp, end_pos, center, normal, 1, /* turn */
                        EMC_MOTION_TYPE_ARC,
                        50.0,    /* vel */
                        100.0,   /* ini_maxvel */
                        500.0,   /* acc */
                        5000.0,  /* ini_maxjerk */
                        0xFF,    /* enables */
                        0,       /* atspeed */
                        tag);
    ASSERT_EQ(result, 0, "tpAddCircle should succeed");
    printf("  PASS: Added circular arc to queue\n");
    
    /* Run a few cycles */
    for (int i = 0; i < 5; i++) {
        result = tpRunCycle(&tp, 0);
        ASSERT_EQ(result, 0, "tpRunCycle should succeed for arc");
    }
    printf("  PASS: Executed TP cycles with arc\n");
    
    /* Clear queue */
    result = tpClear(&tp);
    ASSERT_EQ(result, 0, "tpClear should succeed");
    
    printf("Test: Circular arc motion - PASSED\n\n");
    return 0;
}

/*
 * Test edge cases and error conditions
 */
int test_edge_cases(void) {
    TP_STRUCT tp;
    EmcPose pos;
    struct state_tag_t tag = {0};
    int result;
    double dist;
    
    printf("Test: Edge cases and error handling\n");
    
    /* Test zero distance stopping */
    dist = stoppingDist(0.0, 0.0, 1000.0, 10000.0);
    ASSERT_NEAR(dist, 0.0, 1e-6, "Zero velocity stopping distance should be zero");
    printf("  PASS: Zero velocity stopping distance\n");
    
    /* Test very small distance S-curve */
    double req_v;
    result = findSCurveVSpeed(0.001, 1000.0, 10000.0, &req_v);
    ASSERT_EQ(result, 1, "findSCurveVSpeed should handle tiny distance");
    ASSERT_TRUE(req_v >= 0.0, "Velocity should be non-negative");
    printf("  PASS: S-curve with very small distance (d=0.001mm, v=%.4f mm/s)\n", req_v);
    
    /* Test TP with zero queue size edge case - create TP first */
    result = tpCreate(&tp, 10, 1);
    ASSERT_EQ(result, 0, "tpCreate with small queue should succeed");
    
    result = tpInit(&tp);
    ASSERT_EQ(result, 0, "tpInit should succeed");
    
    /* Test tpIsDone on empty queue */
    ASSERT_TRUE(tpIsDone(&tp), "Empty TP should be done");
    printf("  PASS: tpIsDone on empty queue\n");
    
    /* Test adding move with zero length (degenerate case) */
    ZERO_EMC_POSE(pos);
    result = tpSetPos(&tp, &pos);
    ASSERT_EQ(result, 0, "tpSetPos should succeed");
    
    /* Try to add line with same start and end (zero length) */
    result = tpAddLine(&tp, pos, EMC_MOTION_TYPE_FEED,
                      50.0, 100.0, 500.0, 5000.0, 0xFF, 0, -1, tag);
    /* This might fail or succeed depending on implementation - just ensure it doesn't crash */
    printf("  PASS: Zero-length move handling (result=%d)\n", result);
    
    /* Test queue depth after abort */
    tpAbort(&tp);
    ASSERT_TRUE(tpIsDone(&tp), "TP should be done after abort");
    printf("  PASS: tpAbort\n");
    
    printf("Test: Edge cases - PASSED\n\n");
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
    
    /* Run S-curve tests */
    if (test_scurve_velocity() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    if (test_scurve_distance() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    /* Run blend math tests */
    if (test_blendmath_utils() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    /* Run TC tests */
    if (test_tc_basic() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    /* Run queue tests */
    if (test_tcq_operations() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    /* Run integration tests */
    if (test_integration_multisegment() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    /* Run circular arc tests */
    if (test_circular_arc() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    /* Run edge case tests */
    if (test_edge_cases() != 0) {
        printf("\nTEST FAILED\n");
        return 1;
    }
    
    printf("==========================================\n");
    printf("All tests PASSED\n");
    printf("==========================================\n");
    
    return 0;
}
