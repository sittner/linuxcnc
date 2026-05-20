/********************************************************************
* Description: taskintf.cc
*   Interface functions for motion.
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
********************************************************************/

#include <cmath>
#include <float.h>		// DBL_MAX
#include <string.h>		// memcpy() strncpy()
#include <unistd.h>             // unlink()

#include "motion.h"		// emcmot_command_t,STATUS, etc.
#include "homing.h"
#include "emc.hh"
#include "emccfg.h"		// EMC_INIFILE
#include "emcglb.h"		// EMC_INIFILE
#include "emc_nml.hh"
#include "rcs_print.hh"
#include "timer.hh"
#include "gomc/pkg/cmodule/gomc_ini.h"
#include "gomc/pkg/cmodule/gomc_hal.h"
#include "gomc/pkg/cmodule/gomc_log.h"
#include "gomc/pkg/cmodule/gomc_api.h"
#include "motctl_api.h"
#include "motstat_api.h"
#include "iniaxis_gomc.hh"
#include "inijoint_gomc.hh"
#include "inispindle_gomc.hh"
#include "initraj_gomc.hh"
#include "inihal_gomc.hh"

value_inihal_data old_inihal_data;

// gomc API pointers — set by taskintf_gomc_init(), used by the
// iniXxx() and ini_hal_init() calls throughout this file.
static const gomc_ini_t *the_ini;
static const gomc_hal_t *the_hal;
static const gomc_log_t *the_log;

// Motion controller APIs — looked up from the GMI registry during init.
static const motctl_callbacks_t *motctl;
static const motstat_callbacks_t *motstat;

// Instance name for motion controller lookup (default: "motmod").
const char *taskintf_motion_instance = "motmod";

// Log subscription for forwarding RTAPI_MSG_ERR to OPERATOR_ERROR.
static gomc_log_sub_t *log_error_sub;

void taskintf_gomc_init(const gomc_ini_t *ini,
                       const gomc_hal_t *hal,
                       const gomc_log_t *log)
{
    the_ini = ini;
    the_hal = hal;
    the_log = log;
}

// Called once the gomc_api_t is available (after New(), before Init()).
extern const gomc_api_t *gomc_api_ptr;

static int taskintf_lookup_apis(void)
{
    if (!gomc_api_ptr) {
        rcs_print_error("taskintf: gomc_api_ptr is NULL\n");
        return -1;
    }
    motctl = motctl_api_get(gomc_api_ptr, taskintf_motion_instance);
    if (!motctl) {
        rcs_print_error("taskintf: motctl API not registered (instance '%s', is motmod loaded?)\n", taskintf_motion_instance);
        return -1;
    }
    motstat = motstat_api_get(gomc_api_ptr, taskintf_motion_instance);
    if (!motstat) {
        rcs_print_error("taskintf: motstat API not registered (instance '%s', is motmod loaded?)\n", taskintf_motion_instance);
        return -1;
    }
    return 0;
}

/* define this to catch isnan errors, for rtlinux FPU register 
   problem testing */
#define ISNAN_TRAP

#ifdef ISNAN_TRAP
#define CATCH_NAN(cond) do {                           \
    if (cond) {                                        \
        printf("isnan error in %s()\n", __FUNCTION__); \
        return -1;                                     \
    }                                                  \
} while(0)
#else
#define CATCH_NAN(cond) do {} while(0)
#endif


// MOTION INTERFACE

static emcmot_status_t emcmotStatus;

/*
  Implementation notes:

  Initing:  the emcmot interface needs to be inited once, but nml_traj_init()
  and nml_servo_init() can be called in any order. Similarly, the emcmot
  interface needs to be exited once, but nml_traj_exit() and nml_servo_exit()
  can be called in any order. They can also be called multiple times. Flags
  are used to signify if initing has been done, or if the final exit has
  been called.
  */

static struct TrajConfig_t TrajConfig;
static struct JointConfig_t JointConfig[EMCMOT_MAX_JOINTS];
static struct AxisConfig_t AxisConfig[EMCMOT_MAX_AXIS];
static struct SpindleConfig_t SpindleConfig[EMCMOT_MAX_SPINDLES];

__attribute__ ((unused))
static int emcmotIoInited = 0;	// non-zero means io called init
static int emcmotion_initialized = 0;	// non-zero means both
						// emcMotionInit called.

// local status data, not provided by emcmot
static unsigned long localMotionHeartbeat = 0;
static int localMotionCommandType = 0;
static int localMotionEchoSerialNumber = 0;

// --- Conversion helpers ---

static inline motctl_pose_t to_motctl_pose(const EmcPose &p)
{
    motctl_pose_t mp;
    mp.x = p.tran.x; mp.y = p.tran.y; mp.z = p.tran.z;
    mp.a = p.a; mp.b = p.b; mp.c = p.c;
    mp.u = p.u; mp.v = p.v; mp.w = p.w;
    return mp;
}

static inline motctl_state_tag_t to_motctl_tag(const state_tag_t &t)
{
    motctl_state_tag_t mt;
    static_assert(sizeof(mt.fields_float) == sizeof(t.fields_float), "tag float size mismatch");
    static_assert(sizeof(mt.fields) == sizeof(t.fields), "tag int size mismatch");
    memcpy(mt.fields_float, t.fields_float, sizeof(mt.fields_float));
    memcpy(mt.fields, t.fields, sizeof(mt.fields));
    mt.packed_flags = t.packed_flags;
    return mt;
}

//FIXME-AJ: see if needed
//static double localEmcAxisUnits[EMCMOT_MAX_AXIS];

// axes and joints are numbered 0..NUM-1

/*
  In emcmot, we need to set the cycle time for traj, and the interpolation
  rate, in any order, but both need to be done. 
 */

/*! functions involving joints */

int emcJointSetType(int joint, unsigned char jointType)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    JointConfig[joint].Type = jointType;

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %d)\n", __FUNCTION__, joint, jointType);
    }
    return 0;
}

int emcJointSetUnits(int joint, double units)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    JointConfig[joint].Units = units;

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f)\n", __FUNCTION__, joint, units);
    }
    return 0;
}

int emcJointSetBacklash(int joint, double backlash)
{
#ifdef ISNAN_TRAP
    if (std::isnan(backlash)) {
	printf("std::isnan error in emcJointSetBacklash()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    int retval = motctl->set_joint_backlash(motctl->ctx, joint, backlash);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, joint, backlash, retval);
    }
    return retval;
}

int emcJointSetMinPositionLimit(int joint, double limit)
{
#ifdef ISNAN_TRAP
    if (std::isnan(limit)) {
	printf("isnan error in emcJointSetMinPosition()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    JointConfig[joint].MinLimit = limit;

    int retval = motctl->set_joint_position_limits(motctl->ctx, joint,
        JointConfig[joint].MinLimit, JointConfig[joint].MaxLimit);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4g) returned %d\n", __FUNCTION__, joint, limit, retval);
    }
    return retval;
}

int emcJointSetMaxPositionLimit(int joint, double limit)
{
#ifdef ISNAN_TRAP
    if (std::isnan(limit)) {
	printf("std::isnan error in emcJointSetMaxPosition()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    JointConfig[joint].MaxLimit = limit;

    int retval = motctl->set_joint_position_limits(motctl->ctx, joint,
        JointConfig[joint].MinLimit, JointConfig[joint].MaxLimit);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4g) returned %d\n", __FUNCTION__, joint, limit, retval);
    }
    return retval;
}

int emcJointSetMotorOffset(int joint, double offset) 
{
#ifdef ISNAN_TRAP
    if (std::isnan(offset)) {
	printf("isnan error in emcJointSetMotorOffset()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }
    int retval = motctl->set_joint_motor_offset(motctl->ctx, joint, offset);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, joint, offset, retval);
    }
    return retval;
}

int emcJointSetFerror(int joint, double ferror)
{
#ifdef ISNAN_TRAP
    if (std::isnan(ferror)) {
	printf("isnan error in emcJointSetFerror()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    int retval = motctl->set_joint_max_ferror(motctl->ctx, joint, ferror);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, joint, ferror, retval);
    }
    return retval;
}

int emcJointSetMinFerror(int joint, double ferror)
{
#ifdef ISNAN_TRAP
    if (std::isnan(ferror)) {
	printf("isnan error in emcJointSetMinFerror()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }
    int retval = motctl->set_joint_min_ferror(motctl->ctx, joint, ferror);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, joint, ferror, retval);
    }
    return retval;
}

int emcJointSetHomingParams(int joint, double home, double offset, double home_final_vel,
			   double search_vel, double latch_vel,
			   int use_index, int encoder_does_not_reset,
			   int ignore_limits, int is_shared,
			   int sequence,int volatile_home, int locking_indexer,int absolute_encoder)
{
#ifdef ISNAN_TRAP
    if (std::isnan(home) || std::isnan(offset) || std::isnan(home_final_vel) ||
	std::isnan(search_vel) || std::isnan(latch_vel)) {
	printf("isnan error in emcJointSetHomingParams()\n");
	return -1;
    }
#endif

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    int flags = 0;
    if (use_index) flags |= HOME_USE_INDEX;
    if (encoder_does_not_reset) flags |= HOME_INDEX_NO_ENCODER_RESET;
    if (ignore_limits) flags |= HOME_IGNORE_LIMITS;
    if (is_shared) flags |= HOME_IS_SHARED;
    if (locking_indexer) flags |= HOME_UNLOCK_FIRST;
    if (absolute_encoder) {
        switch (absolute_encoder) {
          case 0: break;
          case 1: flags |= HOME_ABSOLUTE_ENCODER | HOME_NO_REHOME; break;
          case 2: flags |= HOME_ABSOLUTE_ENCODER | HOME_NO_REHOME | HOME_NO_FINAL_MOVE; break;
          default: fprintf(stderr, "Unknown option for absolute_encoder <%d>", absolute_encoder); break;
        }
    }

    int retval = motctl->set_joint_homing_params(motctl->ctx, joint,
        offset, home, home_final_vel, search_vel, latch_vel,
        flags, sequence, volatile_home);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f, %.4f, %.4f, %.4f, %.4f, %d, %d, %d, %d, %d) returned %d\n",
          __FUNCTION__, joint, home, offset, home_final_vel, search_vel, latch_vel,
          use_index, ignore_limits, is_shared, sequence, volatile_home, retval);
    }
    return retval;
}

int emcJointUpdateHomingParams(int joint, double home, double offset, int sequence)
{
    CATCH_NAN(std::isnan(home) || std::isnan(offset) );

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    int retval = motctl->update_joint_homing_params(motctl->ctx, joint, offset, home, sequence);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f, %.4f) returned %d\n",
          __FUNCTION__, joint, home, offset,retval);
    }
    return retval;
}

int emcJointSetMaxVelocity(int joint, double vel)
{
    CATCH_NAN(std::isnan(vel));

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    if (vel < 0.0) {
	vel = 0.0;
    }

    JointConfig[joint].MaxVel = vel;

    int retval = motctl->set_joint_vel_limit(motctl->ctx, joint, vel);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, joint, vel, retval);
    }
    return retval;
}

int emcJointSetMaxAcceleration(int joint, double acc)
{
    CATCH_NAN(std::isnan(acc));

    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }
    if (acc < 0.0) {
	acc = 0.0;
    }
    JointConfig[joint].MaxAccel = acc;

    int retval = motctl->set_joint_acc_limit(motctl->ctx, joint, acc);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4g) returned %d\n", __FUNCTION__, joint, acc, retval);
    }
    return retval;
}

/*! functions involving cartesian Axes (X,Y,Z,A,B,C,U,V,W) */
    
int emcAxisSetMinPositionLimit(int axis, double limit)
{
    CATCH_NAN(std::isnan(limit));

    if (axis < 0 || axis >= EMCMOT_MAX_AXIS || !(TrajConfig.AxisMask & (1 << axis))) {
	return 0;
    }

    AxisConfig[axis].MinLimit = limit;

    int retval = motctl->set_axis_position_limits(motctl->ctx, axis,
        AxisConfig[axis].MinLimit, AxisConfig[axis].MaxLimit);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, axis, limit, retval);
    }
    return retval;
}

int emcAxisSetMaxPositionLimit(int axis, double limit)
{
    CATCH_NAN(std::isnan(limit));

    if (axis < 0 || axis >= EMCMOT_MAX_AXIS || !(TrajConfig.AxisMask & (1 << axis))) {
	return 0;
    }

    AxisConfig[axis].MaxLimit = limit;

    int retval = motctl->set_axis_position_limits(motctl->ctx, axis,
        AxisConfig[axis].MinLimit, AxisConfig[axis].MaxLimit);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, axis, limit, retval);
    }
    return retval;
}

int emcAxisSetMaxVelocity(int axis, double vel,double ext_offset_vel)
{
    CATCH_NAN(std::isnan(vel));

    if (axis < 0 || axis >= EMCMOT_MAX_AXIS || !(TrajConfig.AxisMask & (1 << axis))) {
	return 0;
    }

    if (vel < 0.0) {
	vel = 0.0;
    }

    AxisConfig[axis].MaxVel = vel;

    int retval = motctl->set_axis_vel_limit(motctl->ctx, axis, vel, ext_offset_vel);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, axis, vel, retval);
    }
    return retval;
}

int emcAxisSetMaxAcceleration(int axis, double acc,double ext_offset_acc)
{
    CATCH_NAN(std::isnan(acc));

    if (axis < 0 || axis >= EMCMOT_MAX_AXIS || !(TrajConfig.AxisMask & (1 << axis))) {
	return 0;
    }

    if (acc < 0.0) {
	acc = 0.0;
    }
    
    AxisConfig[axis].MaxAccel = acc;

    int retval = motctl->set_axis_acc_limit(motctl->ctx, axis, acc, ext_offset_acc);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %.4f) returned %d\n", __FUNCTION__, axis, acc, retval);
    }
    return retval;
}

int emcAxisSetLockingJoint(int axis, int joint)
{

    if (axis < 0 || axis >= EMCMOT_MAX_AXIS || !(TrajConfig.AxisMask & (1 << axis))) {
	return 0;
    }

    if (joint < 0) {
	joint = -1;
    }

    int retval = motctl->set_axis_locking_joint(motctl->ctx, axis, joint);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %d) returned %d\n", __FUNCTION__, axis, joint, retval);
    }
    return retval;
}

double emcAxisGetMaxVelocity(int axis)
{
    if (axis < 0 || axis >= EMCMOT_MAX_AXIS) {
        return 0;
    }

    return AxisConfig[axis].MaxVel;
}

double emcAxisGetMaxAcceleration(int axis)
{
    if (axis < 0 || axis >= EMCMOT_MAX_AXIS) {
        return 0;
    }

    return AxisConfig[axis].MaxAccel;
}

int emcAxisUpdate(EMC_AXIS_STAT stat[], int axis_mask)
{
    int axis_num;
    emcmot_axis_status_t *axis;
    
    for (axis_num = 0; axis_num < EMCMOT_MAX_AXIS; axis_num++) {
        if(!(axis_mask & (1 << axis_num))) continue;
        axis = &(emcmotStatus.axis_status[axis_num]);

        stat[axis_num].velocity = axis->teleop_vel_cmd;
        stat[axis_num].minPositionLimit = axis->min_pos_limit;
        stat[axis_num].maxPositionLimit = axis->max_pos_limit;
    }
    return 0;
}

/* This function checks to see if any joint or the traj has
   been inited already.  At startup, if none have been inited,
   the motctl/motstat APIs must be looked up first.
*/

static int JointOrTrajInited(void)
{
    int joint, spindle;

    for (joint = 0; joint < EMCMOT_MAX_JOINTS; joint++) {
	if (JointConfig[joint].Inited) {
	    return 1;
	}
    }
    for (spindle = 0; spindle < EMCMOT_MAX_SPINDLES; spindle++) {
        if (SpindleConfig[spindle].Inited) {
            return 1;
        }
    }
    if (TrajConfig.Inited) {
	return 1;
    }
    return 0;
}

int emcJointInit(int joint)
{
    int retval = 0;
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }
    // init motctl/motstat APIs on first init
    if (!JointOrTrajInited()) {
	if (0 != taskintf_lookup_apis()) {
	    return -1;
	}
    }
    JointConfig[joint].Inited = 1;
    if (0 != iniJoint(joint, the_ini)) {
	retval = -1;
    }
    return retval;
}

int emcAxisInit(int axis)
{
    int retval = 0;

    if (axis < 0 || axis >= EMCMOT_MAX_AXIS) {
	return 0;
    }
    if (!JointOrTrajInited()) {
	if (0 != taskintf_lookup_apis()) {
	    return -1;
	}
    }
    AxisConfig[axis].Inited = 1;
    if (0 != iniAxis(axis, the_ini)) {
	retval = -1;
    }
    return retval;
}

int emcSpindleInit(int spindle)
{
    int retval = 0;

    if (spindle < 0 || spindle >= EMCMOT_MAX_SPINDLES) {
	return 0;
    }
    if (!JointOrTrajInited()) {
	if (0 != taskintf_lookup_apis()) {
	    return -1;
	}
    }
    SpindleConfig[spindle].Inited = 1;
    if (0 != iniSpindle(spindle, the_ini)) {
	retval = -1;
    }
    return retval;
}

int emcJointHalt(int joint)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }
    /*! \todo FIXME-- refs global emcStatus; should make EMC_JOINT_STAT an arg here */
    if (NULL != emcStatus && emcmotion_initialized
	&& JointConfig[joint].Inited) {
	//dumpJoint(joint, emc_inifile, &emcStatus->motion.joint[joint]);
    }
    JointConfig[joint].Inited = 0;
    return 0;
}

int emcJogAbort(int joint)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }
    return motctl->jog_abort(motctl->ctx, joint, 1/*is_teleop=joint mode*/);
}

int emcJointActivate(int joint)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    int retval = motctl->joint_activate(motctl->ctx, joint);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d) returned %d\n", __FUNCTION__, joint, retval);
    }
    return retval;
}

int emcJointDeactivate(int joint)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    return 0; // joint deactivate is a no-op in the motctl API
}

int emcJointOverrideLimits(int joint)
{
    // can have joint < 0, for resuming normal limit checking
    if (joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    return motctl->override_limits(motctl->ctx, joint);
}

int emcJointEnable(int joint)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    return 0; // joint enable amplifier is a no-op in the motctl API
}

int emcJointDisable(int joint)
{
    if (joint < 0 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    return 0; // joint disable amplifier is a no-op in the motctl API
}

int emcJointHome(int joint)
{
    if (joint < -1 || joint >= EMCMOT_MAX_JOINTS) {
	return 0;
    }

    return motctl->joint_home(motctl->ctx, joint);
}

int emcJointUnhome(int joint)
{
	if (joint < -2 || joint >= EMCMOT_MAX_JOINTS) {
		return 0;
	}

	return motctl->joint_unhome(motctl->ctx, joint);
}

int emcJogCont(int nr, double vel, int jjogmode)
{
    if (jjogmode) {
        if (nr < 0 || nr >= EMCMOT_MAX_JOINTS) { return 0; }
        if (vel > JointConfig[nr].MaxVel) {
            vel = JointConfig[nr].MaxVel;
        } else if (vel < -JointConfig[nr].MaxVel) {
            vel = -JointConfig[nr].MaxVel;
        }
    } else {
        if (nr < 0 || nr >= EMCMOT_MAX_AXIS) { return 0; }
        if (vel > AxisConfig[nr].MaxVel) {
            vel = AxisConfig[nr].MaxVel;
        } else if (vel < -AxisConfig[nr].MaxVel) {
            vel = -AxisConfig[nr].MaxVel;
        }
    }
    return motctl->jog_cont(motctl->ctx, nr, vel, jjogmode ? 0 : 1);
}

int emcJogIncr(int nr, double incr, double vel, int jjogmode)
{
    if (jjogmode) {
        if (nr < 0 || nr >= EMCMOT_MAX_JOINTS) { return 0; }
        if (vel > JointConfig[nr].MaxVel) {
            vel = JointConfig[nr].MaxVel;
        } else if (vel < -JointConfig[nr].MaxVel) {
            vel = -JointConfig[nr].MaxVel;
        }
    } else {
        if (nr < 0 || nr >= EMCMOT_MAX_AXIS) { return 0; }
        if (vel > AxisConfig[nr].MaxVel) {
            vel = AxisConfig[nr].MaxVel;
        } else if (vel < -AxisConfig[nr].MaxVel) {
            vel = -AxisConfig[nr].MaxVel;
        }
    }
    return motctl->jog_incr(motctl->ctx, nr, vel, incr, jjogmode ? 0 : 1);
}

int emcJogAbs(int nr, double pos, double vel, int jjogmode)
{
    if (jjogmode) {
        if (nr < 0 || nr >= EMCMOT_MAX_JOINTS) { return 0; }
        if (vel > JointConfig[nr].MaxVel) {
            vel = JointConfig[nr].MaxVel;
        } else if (vel < -JointConfig[nr].MaxVel) {
            vel = -JointConfig[nr].MaxVel;
        }
    } else {
        if (nr < 0 || nr >= EMCMOT_MAX_AXIS) { return 0; }
        if (vel > AxisConfig[nr].MaxVel) {
            vel = AxisConfig[nr].MaxVel;
        } else if (vel < -AxisConfig[nr].MaxVel) {
            vel = -AxisConfig[nr].MaxVel;
        }
    }
    return motctl->jog_abs(motctl->ctx, nr, vel, pos, jjogmode ? 0 : 1);
}

int emcJogStop(int nr, int jjogmode)
{
    if (jjogmode) {
        if (nr < 0 || nr >= EMCMOT_MAX_JOINTS) { return 0; }
    } else {
        if (nr < 0 || nr >= EMCMOT_MAX_AXIS) { return 0; }
    }
    return motctl->jog_abort(motctl->ctx, nr, jjogmode ? 0 : 1);
}


int emcJointLoadComp(int joint, const char *file, int type)
{
    FILE *f = fopen(file, "r");
    if (!f) {
        rcs_print_error("can't open compensation file %s\n", file);
        return -1;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        double nom, fwd, rev;
        int n;
        if (type == 0) {
            n = sscanf(buf, "%lf %lf %lf", &nom, &fwd, &rev);
        } else {
            n = sscanf(buf, "%lf %lf", &nom, &fwd);
            rev = fwd;
        }
        if (n < 2) continue;
        if (motctl->set_joint_comp(motctl->ctx, joint, nom, fwd, rev) != 0) {
            rcs_print_error("error sending comp data for joint %d\n", joint);
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static int new_config = 0;

// Cached config fields from motstat (populated on config_num change).
static struct {
    int32_t kin_type;
    double  traj_cycle_time;
    double  limit_vel;
    int32_t debug;
} cached_config;

/*! \todo FIXME - debugging - uncomment the following line to log changes in
   JOINT_FLAG */
// #define WATCH_FLAGS 1

int emcJointUpdate(EMC_JOINT_STAT stat[], int numJoints)
{
/*! \todo FIXME - this function accesses data that has been
   moved.  Once I know what it is used for I'll fix it */

    int joint_num;
    emcmot_joint_status_t *joint;
#ifdef WATCH_FLAGS
    static int old_joint_flag[8];
#endif

    // check for valid range
    if (numJoints <= 0 || numJoints > EMCMOT_MAX_JOINTS) {
	return -1;
    }

    for (joint_num = 0; joint_num < numJoints; joint_num++) {
	/* point to joint data */

	joint = &(emcmotStatus.joint_status[joint_num]);

	stat[joint_num].jointType = JointConfig[joint_num].Type;
	stat[joint_num].units = JointConfig[joint_num].Units;
	if (new_config) {
	    stat[joint_num].backlash = joint->backlash;
	    stat[joint_num].minPositionLimit = joint->min_pos_limit;
	    stat[joint_num].maxPositionLimit = joint->max_pos_limit;
	    stat[joint_num].minFerror = joint->min_ferror;
	    stat[joint_num].maxFerror = joint->max_ferror;
/*! \todo FIXME - should all homing config params be included here? */
//	    stat[joint_num].homeOffset = joint->home_offset;
	}
	stat[joint_num].output = joint->pos_cmd;
	stat[joint_num].input = joint->pos_fb;
	stat[joint_num].velocity = joint->vel_cmd;
	stat[joint_num].ferrorCurrent = joint->ferror;
	stat[joint_num].ferrorHighMark = joint->ferror_high_mark;

	stat[joint_num].homing = joint->homing;
	stat[joint_num].homed  = joint->homed;

	stat[joint_num].fault = (joint->flag & EMCMOT_JOINT_FAULT_BIT ? 1 : 0);
	stat[joint_num].enabled = (joint->flag & EMCMOT_JOINT_ENABLE_BIT ? 1 : 0);
	stat[joint_num].inpos = (joint->flag & EMCMOT_JOINT_INPOS_BIT ? 1 : 0);

/* FIXME - soft limits are now applied to the command, and should never
   happen */
	stat[joint_num].minSoftLimit = 0;
	stat[joint_num].maxSoftLimit = 0;
	stat[joint_num].minHardLimit =
	    (joint->flag & EMCMOT_JOINT_MIN_HARD_LIMIT_BIT ? 1 : 0);
	stat[joint_num].maxHardLimit =
	    (joint->flag & EMCMOT_JOINT_MAX_HARD_LIMIT_BIT ? 1 : 0);
	stat[joint_num].overrideLimits = !!(emcmotStatus.overrideLimitMask);	// one
	// for
	// all

#ifdef WATCH_FLAGS
	if (old_joint_flag[joint_num] != joint->flag) {
	    printf("joint %d flag: %04X -> %04X\n", joint_num,
		   old_joint_flag[joint_num], joint->flag);
	    old_joint_flag[joint_num] = joint->flag;
	}
#endif
	if (joint->flag & EMCMOT_JOINT_ERROR_BIT) {
	    if (stat[joint_num].status != RCS_ERROR) {
		rcs_print_error("Error on joint %d, command number %d\n",
				joint_num, emcmotStatus.commandNumEcho);
		stat[joint_num].status = RCS_ERROR;
	    }
	} else if (joint->flag & EMCMOT_JOINT_INPOS_BIT) {
	    stat[joint_num].status = RCS_DONE;
	} else {
	    stat[joint_num].status = RCS_EXEC;
	}
    }
    return 0;
}

// EMC_TRAJ functions

int emcTrajSetJoints(int joints)
{
    if (joints <= 0 || joints > EMCMOT_MAX_JOINTS) {
	rcs_print("emcTrajSetJoints failing: joints=%d\n",
		joints);
	return -1;
    }

    TrajConfig.Joints = joints;
    // num_joints is now a module param — just store locally
    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d)\n", __FUNCTION__, joints);
    }
    return 0;
}

// FIXME CJR move this to TrajConfig?
static struct state_tag_t localEmcTrajTag;

int emcTrajUpdateTag(StateTag const &tag) {
    localEmcTrajTag = tag.get_state_tag();
    return 0;
}

int emcTrajSetAxes(int axismask)
{
    int axes = 0;
    for(int i=0; i<EMCMOT_MAX_AXIS; i++)
        if(axismask & (1<<i)) axes = i+1;

    TrajConfig.AxisMask = axismask;
    
    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %d)\n", __FUNCTION__, axes, axismask);
    }
    return 0;
}

int emcTrajSetSpindles(int spindles)
{
    if (spindles <= 0 || spindles > EMCMOT_MAX_SPINDLES) {
	rcs_print("emcTrajSetSpindles failing: spindles=%d\n",
		spindles);
	return -1;
    }

    TrajConfig.Spindles = spindles;
    // num_spindles is now a module param — just store locally
    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d)\n", __FUNCTION__, spindles);
    }
    return 0;
}

int emcTrajSetUnits(double linearUnits, double angularUnits)
{
    if (linearUnits <= 0.0 || angularUnits <= 0.0) {
	return -1;
    }

    TrajConfig.LinearUnits = linearUnits;
    TrajConfig.AngularUnits = angularUnits;

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%.4f, %.4f)\n", __FUNCTION__, linearUnits, angularUnits);
    }
    return 0;
}

int emcTrajSetMode(int mode)
{
    switch (mode) {
    case EMC_TRAJ_MODE_FREE:
	return motctl->set_free(motctl->ctx);
    case EMC_TRAJ_MODE_COORD:
	return motctl->set_coord(motctl->ctx);
    case EMC_TRAJ_MODE_TELEOP:
	return motctl->set_teleop(motctl->ctx);
    default:
	return -1;
    }
}

int emcTrajSetVelocity(double vel, double ini_maxvel)
{
    if (vel < 0.0) {
	vel = 0.0;
    } else if (vel > TrajConfig.MaxVel) {
	vel = TrajConfig.MaxVel;
    }

    if (ini_maxvel < 0.0) {
	    ini_maxvel = 0.0;
    } else if (vel > TrajConfig.MaxVel) {
	    ini_maxvel = TrajConfig.MaxVel;
    }

    int retval = motctl->set_vel(motctl->ctx, vel);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%.4f, %.4f) returned %d\n", __FUNCTION__, vel, ini_maxvel, retval);
    }
    return retval;
}

int emcTrajSetAcceleration(double acc)
{
    if (acc < 0.0) {
	acc = 0.0;
    } else if (acc > TrajConfig.MaxAccel) {
	acc = TrajConfig.MaxAccel;
    }

    int retval = motctl->set_acc(motctl->ctx, acc);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%.4g) returned %d\n", __FUNCTION__, acc, retval);
    }
    return retval;
}

/*
  emcmot has no limits on max velocity, acceleration so we'll save them
  here and apply them in the functions above
  */
int emcTrajSetMaxVelocity(double vel)
{
    if (vel < 0.0) {
	vel = 0.0;
    }

    TrajConfig.MaxVel = vel;

    int retval = motctl->set_vel_limit(motctl->ctx, vel);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%.4f) returned %d\n", __FUNCTION__, vel, retval);
    }
    return retval;
}

int emcTrajSetMaxAcceleration(double acc)
{
    if (acc < 0.0) {
	acc = 0.0;
    }

    TrajConfig.MaxAccel = acc;

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%.4g)\n", __FUNCTION__, acc);
    }
    return 0;
}

int emcTrajSetHome(EmcPose home)
{
#ifdef ISNAN_TRAP
    if (std::isnan(home.tran.x) || std::isnan(home.tran.y) || std::isnan(home.tran.z) ||
	std::isnan(home.a) || std::isnan(home.b) || std::isnan(home.c) ||
	std::isnan(home.u) || std::isnan(home.v) || std::isnan(home.w)) {
	printf("std::isnan error in emcTrajSetHome()\n");
	return 0;		// ignore it for now, just don't send it
    }
#endif

    motctl_pose_t mp = to_motctl_pose(home);
    int retval = motctl->set_world_home(motctl->ctx, &mp);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f) returned %d\n", 
          __FUNCTION__, home.tran.x, home.tran.y, home.tran.z, home.a, home.b, home.c, 
          home.u, home.v, home.w, retval);
    }
    return retval;
}

int emcTrajSetScale(double scale)
{
    if (scale < 0.0) {
	scale = 0.0;
    }

    return motctl->set_feed_scale(motctl->ctx, scale);
}

int emcTrajSetRapidScale(double scale)
{
    if (scale < 0.0) {
	scale = 0.0;
    }

    return motctl->set_rapid_scale(motctl->ctx, scale);
}

int emcTrajSetSpindleScale(int spindle, double scale)
{
    if (scale < 0.0) {
	scale = 0.0;
    }

    return motctl->set_spindle_scale(motctl->ctx, spindle, scale);
}

int emcTrajSetFOEnable(unsigned char mode)
{
    return motctl->feed_scale_enable(motctl->ctx, mode);
}

int emcTrajSetFHEnable(unsigned char mode)
{
    return motctl->feed_hold_enable(motctl->ctx, mode);
}

int emcTrajSetSOEnable(unsigned char mode)
{
    return motctl->spindle_scale_enable(motctl->ctx, -1, mode);
}

int emcTrajSetAFEnable(unsigned char enable)
{
    return motctl->adaptive_feed_enable(motctl->ctx, enable ? 1 : 0);
}

int emcTrajSetMotionId(int id)
{

    if (EMC_DEBUG_MOTION_TIME & emc_debug) {
	if (id != TrajConfig.MotionId) {
	    rcs_print("Outgoing motion id is %d.\n", id);
	}
    }

    TrajConfig.MotionId = id;

    return 0;
}

int emcTrajInit()
{
    int retval = 0;

    TrajConfig.Inited = 0;
    TrajConfig.Joints = 0;
    TrajConfig.MaxAccel = DBL_MAX;
    TrajConfig.AxisMask = 0;
    TrajConfig.LinearUnits = 1.0;
    TrajConfig.AngularUnits = 1.0;
    TrajConfig.MotionId = 0;
    TrajConfig.MaxVel = DEFAULT_TRAJ_MAX_VELOCITY;

    // init motctl/motstat APIs on first init
    if (!JointOrTrajInited()) {
	if (0 != taskintf_lookup_apis()) {
	    return -1;
	}
    }
    TrajConfig.Inited = 1;
    // initialize parameters from INI file
    if (0 != iniTraj(the_ini)) {
	retval = -1;
    }
    return retval;
}

int emcTrajHalt()
{
    TrajConfig.Inited = 0;
    return 0;
}

int emcTrajEnable()
{
    return motctl->enable(motctl->ctx);
}

int emcTrajDisable()
{
    return motctl->disable(motctl->ctx);
}

int emcTrajAbort()
{
    return motctl->abort(motctl->ctx);
}

int emcTrajPause()
{
    return motctl->pause(motctl->ctx);
}

int emcTrajReverse()
{
    return motctl->reverse(motctl->ctx);
}

int emcTrajForward()
{
    return motctl->forward(motctl->ctx);
}

int emcTrajStep()
{
    return motctl->step(motctl->ctx, TrajConfig.MotionId);
}

int emcTrajResume()
{
    return motctl->resume(motctl->ctx);
}

int emcTrajDelay(double delay)
{
    /* nothing need be done here - it's done in task controller */

    return 0;
}

double emcTrajGetLinearUnits()
{
    return TrajConfig.LinearUnits;
}

double emcTrajGetAngularUnits()
{
    return TrajConfig.AngularUnits;
}

int emcTrajSetOffset(EmcPose tool_offset)
{
    motctl_pose_t mp = to_motctl_pose(tool_offset);
    return motctl->set_offset(motctl->ctx, &mp);
}

int emcTrajSetSpindleSync(int spindle, double fpr, bool wait_for_index)
{
    // motion_type is passed as wait_for_index flag
    return motctl->set_spindlesync(motctl->ctx, fpr, wait_for_index);
}

int emcTrajSetTermCond(int cond, double tolerance)
{
    return motctl->set_term_cond(motctl->ctx, cond, tolerance);
}

int emcTrajLinearMove(EmcPose end, int type, double vel, double ini_maxvel, double acc,
                      int indexer_jnum)
{
#ifdef ISNAN_TRAP
    if (std::isnan(end.tran.x) || std::isnan(end.tran.y) || std::isnan(end.tran.z) ||
        std::isnan(end.a) || std::isnan(end.b) || std::isnan(end.c) ||
        std::isnan(end.u) || std::isnan(end.v) || std::isnan(end.w)) {
	printf("std::isnan error in emcTrajLinearMove()\n");
	return 0;		// ignore it for now, just don't send it
    }
#endif

    motctl_pose_t mp = to_motctl_pose(end);
    motctl_state_tag_t mt = to_motctl_tag(localEmcTrajTag);
    return motctl->set_line(motctl->ctx, &mp, vel, ini_maxvel, acc,
        type, TrajConfig.MotionId, &mt, indexer_jnum);
}

int emcTrajCircularMove(EmcPose end, PM_CARTESIAN center,
			PM_CARTESIAN normal, int turn, int type, double vel, double ini_maxvel, double acc)
{
#ifdef ISNAN_TRAP
    if (std::isnan(end.tran.x) || std::isnan(end.tran.y) || std::isnan(end.tran.z) ||
	std::isnan(end.a) || std::isnan(end.b) || std::isnan(end.c) ||
	std::isnan(end.u) || std::isnan(end.v) || std::isnan(end.w) ||
	std::isnan(center.x) || std::isnan(center.y) || std::isnan(center.z) ||
	std::isnan(normal.x) || std::isnan(normal.y) || std::isnan(normal.z)) {
	printf("std::isnan error in emcTrajCircularMove()\n");
	return 0;		// ignore it for now, just don't send it
    }
#endif

    motctl_pose_t mp = to_motctl_pose(end);
    motctl_cartesian_t mc = {center.x, center.y, center.z};
    motctl_cartesian_t mn = {normal.x, normal.y, normal.z};
    motctl_state_tag_t mt = to_motctl_tag(localEmcTrajTag);
    return motctl->set_circle(motctl->ctx, &mp, &mc, &mn, turn,
        vel, ini_maxvel, acc, type, TrajConfig.MotionId, &mt);
}

int emcTrajClearProbeTrippedFlag()
{
    return motctl->clear_probe_flags(motctl->ctx);
}

int emcTrajProbe(EmcPose pos, int type, double vel, double ini_maxvel, double acc, unsigned char probe_type)
{
#ifdef ISNAN_TRAP
    if (std::isnan(pos.tran.x) || std::isnan(pos.tran.y) || std::isnan(pos.tran.z) ||
        std::isnan(pos.a) || std::isnan(pos.b) || std::isnan(pos.c) ||
        std::isnan(pos.u) || std::isnan(pos.v) || std::isnan(pos.w)) {
	printf("std::isnan error in emcTrajProbe()\n");
	return 0;		// ignore it for now, just don't send it
    }
#endif

    motctl_pose_t mp = to_motctl_pose(pos);
    motctl_state_tag_t mt = to_motctl_tag(localEmcTrajTag);
    return motctl->probe(motctl->ctx, &mp, vel, ini_maxvel, acc,
        type, probe_type, TrajConfig.MotionId, &mt);
}

int emcTrajRigidTap(EmcPose pos, double vel, double ini_maxvel, double acc, double scale)
{
#ifdef ISNAN_TRAP
    if (std::isnan(pos.tran.x) || std::isnan(pos.tran.y) || std::isnan(pos.tran.z)) {
	printf("std::isnan error in emcTrajRigidTap()\n");
	return 0;		// ignore it for now, just don't send it
    }
#endif

    motctl_pose_t mp = to_motctl_pose(pos);
    motctl_state_tag_t mt = to_motctl_tag(localEmcTrajTag);
    return motctl->rigid_tap(motctl->ctx, &mp, vel, ini_maxvel, acc,
        scale, TrajConfig.MotionId, &mt);
}


static int last_id = 0;
static int last_id_printed = 0;
static int last_status = 0;
static double last_id_time;

int emcTrajUpdate(EMC_TRAJ_STAT * stat)
{
    int joint, enables;

    stat->joints = TrajConfig.Joints;
    stat->spindles = TrajConfig.Spindles;
    stat->axis_mask = TrajConfig.AxisMask;
    stat->linearUnits = TrajConfig.LinearUnits;
    stat->angularUnits = TrajConfig.AngularUnits;

    stat->mode =
	emcmotStatus.
	motionFlag & EMCMOT_MOTION_TELEOP_BIT ? EMC_TRAJ_MODE_TELEOP
	: (emcmotStatus.
	   motionFlag & EMCMOT_MOTION_COORD_BIT ? EMC_TRAJ_MODE_COORD :
	   EMC_TRAJ_MODE_FREE);

    /* enabled if motion enabled and all joints enabled */
    stat->enabled = 0;		/* start at disabled */
    if (emcmotStatus.motionFlag & EMCMOT_MOTION_ENABLE_BIT) {
	for (joint = 0; joint < TrajConfig.Joints; joint++) {
/*! \todo Another #if 0 */
#if 0				/*! \todo FIXME - the axis flag has been moved to the joint struct */
	    if (!emcmotStatus.axisFlag[axis] & EMCMOT_JOINT_ENABLE_BIT) {
		break;
	    }
#endif
	    /* got here, then all are enabled */
	    stat->enabled = 1;
	}
    }

    stat->inpos = emcmotStatus.motionFlag & EMCMOT_MOTION_INPOS_BIT;
    stat->queue = emcmotStatus.depth;
    stat->activeQueue = emcmotStatus.activeDepth;
    stat->queueFull = emcmotStatus.queueFull;
    stat->id = emcmotStatus.id;
    StateTag newtag(emcmotStatus.tag);
    //TODO assignment operator
    stat->tag = newtag;
    stat->motion_type = emcmotStatus.motionType;
    stat->distance_to_go = emcmotStatus.distance_to_go;
    stat->dtg = emcmotStatus.dtg;
    stat->current_vel = emcmotStatus.current_vel;
    if (EMC_DEBUG_MOTION_TIME & emc_debug) {
	if (stat->id != last_id) {
	    if (last_id != last_id_printed) {
		rcs_print("Motion id %d took %f seconds.\n", last_id,
			  etime() - last_id_time);
		last_id_printed = last_id;
	    }
	    last_id = stat->id;
	    last_id_time = etime();
	}
    }

    stat->paused = emcmotStatus.paused;
    stat->scale = emcmotStatus.feed_scale;
    stat->rapid_scale = emcmotStatus.rapid_scale;

    stat->position = emcmotStatus.carte_pos_cmd;

    stat->actualPosition = emcmotStatus.carte_pos_fb;

    stat->velocity = emcmotStatus.vel;
    stat->acceleration = emcmotStatus.acc;
    stat->maxAcceleration = TrajConfig.MaxAccel;

    if (emcmotStatus.motionFlag & EMCMOT_MOTION_ERROR_BIT) {
	stat->status = RCS_ERROR;
    } else if (stat->inpos && (stat->queue == 0)) {
	stat->status = RCS_DONE;
    } else {
	stat->status = RCS_EXEC;
    }

    if (EMC_DEBUG_MOTION_TIME & emc_debug) {
	if (stat->status == RCS_DONE && last_status != RCS_DONE
	    && stat->id != last_id_printed) {
	    rcs_print("Motion id %d took %f seconds.\n", last_id,
		      etime() - last_id_time);
	    last_id_printed = last_id = stat->id;
	    last_id_time = etime();
	}
    }

    stat->probedPosition = emcmotStatus.probedPos;

    stat->probeval = emcmotStatus.probeVal;
    stat->probing = emcmotStatus.probing;
    stat->probe_tripped = emcmotStatus.probeTripped;
    
    if (emcmotStatus.motionFlag & EMCMOT_MOTION_COORD_BIT)
        enables = emcmotStatus.enables_queued;
    else
        enables = emcmotStatus.enables_new;
    
    stat->feed_override_enabled = enables & FS_ENABLED;
    stat->adaptive_feed_enabled = enables & AF_ENABLED;
    stat->feed_hold_enabled = enables & FH_ENABLED;

    if (new_config) {
	stat->cycleTime = cached_config.traj_cycle_time;
	stat->kinematics_type = cached_config.kin_type;
	stat->maxVelocity = cached_config.limit_vel;
    }

    return 0;
}


int setup_inihal(void) {
    // Must be called after emcTrajInit(), which loads the number of
    // joints from the INI file.
    if (emcmotion_initialized != 1) {
        rcs_print_error("%s: emcMotionInit() has not completed, can't setup inihal\n", __FUNCTION__);
        return -1;
    }

    if (ini_hal_init(the_hal, the_log, TrajConfig.Joints)) {
        rcs_print_error("%s: ini_hal_init(%d) failed\n", __FUNCTION__, TrajConfig.Joints);
        return -1;
    }

    if (ini_hal_init_pins(TrajConfig.Joints)) {
        rcs_print_error("%s: ini_hal_init_pins(%d) failed\n", __FUNCTION__, TrajConfig.Joints);
        return -1;
    }

    return 0;
}


int emcPositionLoad() {
    double positions[EMCMOT_MAX_JOINTS];
    const char *posfile = the_ini->get(the_ini->ctx, "TRAJ", "POSITION_FILE");
    if(!posfile || !posfile[0]) return 0;
    FILE *f = fopen(posfile, "r");
    if(!f) return 0;
    for(int i=0; i<EMCMOT_MAX_JOINTS; i++) {
	int r = fscanf(f, "%lf", &positions[i]);
	if(r != 1) {
            fclose(f);
            rcs_print("%s: failed to load joint %d position from %s, ignoring\n", __FUNCTION__, i, posfile);
            return -1;
        }
    }
    fclose(f);
    int result = 0;
    for(int i=0; i<EMCMOT_MAX_JOINTS; i++) {
	if(emcJointSetMotorOffset(i, -positions[i]) != 0) {
            rcs_print("%s: failed to set joint %d position (%.6f) from %s, ignoring\n", __FUNCTION__, i, positions[i], posfile);
            result = -1;
        }
    }
    return result;
}


int emcPositionSave() {
    const char *posfile = the_ini->get(the_ini->ctx, "TRAJ", "POSITION_FILE");

    if(!posfile || !posfile[0]) return 0;
    // like the var file, make sure the posfile is recreated according to umask
    unlink(posfile);
    FILE *f = fopen(posfile, "w");
    if(!f) return -1;
    for(int i=0; i<EMCMOT_MAX_JOINTS; i++) {
	int r = fprintf(f, "%.17f\n", emcmotStatus.joint_status[i].pos_fb);
	if(r < 0) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

// EMC_MOTION functions

// This function gets called by Task from emctask_startup().
// emctask_startup() calls this function in a loop, retrying it until
// it succeeds or until the retries time out.
int emcMotionInit()
{
    int r;
    int joint, axis, spindle;
    
    r = emcTrajInit(); // we want to check Traj first, the sane defaults for units are there
    // it also determines the number of existing joints, and axes
    if (r != 0) {
        rcs_print("%s: emcTrajInit failed\n", __FUNCTION__);
        return -1;
    }

    for (joint = 0; joint < TrajConfig.Joints; joint++) {
	if (0 != emcJointInit(joint)) {
            rcs_print("%s: emcJointInit(%d) failed\n", __FUNCTION__, joint);
            return -1;
	}
    }

    for (axis = 0; axis < EMCMOT_MAX_AXIS; axis++) {
        if (TrajConfig.AxisMask & (1<<axis)) {
	    if (0 != emcAxisInit(axis)) {
                rcs_print("%s: emcAxisInit(%d) failed\n", __FUNCTION__, axis);
                return -1;
	    }
	}
	}

    for (spindle = 0; spindle < TrajConfig.Spindles; spindle++) {
	    if (0 != emcSpindleInit(spindle)) {
                rcs_print("%s: emcSpindleInit(%d) failed\n", __FUNCTION__, spindle);
                return -1;
	    }
	}


    // Ignore errors from emcPositionLoad(), because what are you going to do?
    (void)emcPositionLoad();

    // Subscribe to ERROR-level log messages for forwarding to OPERATOR_ERROR.
    if (the_log && the_log->subscribe) {
        log_error_sub = the_log->subscribe(the_log->ctx, GOMC_LOG_ERROR);
    }

    emcmotion_initialized = 1;

    return 0;
}

int emcMotionHalt()
{
    int r1, r2, r3, r4, r5;
    int t;

    // Unsubscribe from log messages.
    if (log_error_sub && the_log && the_log->unsubscribe) {
        the_log->unsubscribe(the_log->ctx, log_error_sub);
        log_error_sub = NULL;
    }

    r1 = -1;
    for (t = 0; t < EMCMOT_MAX_JOINTS; t++) {
	if (0 == emcJointHalt(t)) {
	    r1 = 0;		// at least one is okay
	}
    }

    r2 = emcTrajDisable();
    r3 = emcTrajHalt();
    r4 = emcPositionSave();
    r5 = ini_hal_exit();
    emcmotion_initialized = 0;

    return (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0 && r5 == 0) ? 0 : -1;
}

int emcMotionAbort()
{
    int r1;
    int r2;
    int r3 = 0;
    int t;

    r1 = -1;
    for (t = 0; t < EMCMOT_MAX_JOINTS; t++) {
	if (0 == emcJogAbort(t)) {
	    r1 = 0;		// at least one is okay
	}
    }

    r2 = emcTrajAbort();

    return (r1 == 0 && r2 == 0 && r3 == 0) ? 0 : -1;
}

int emcMotionSetDebug(int debug)
{
    return motctl->set_debug(motctl->ctx, debug);
}

/*! \function emcMotionSetAout()

    This function sends a EMCMOT_SET_AOUT message to the motion controller.
    That one plans a AOUT command when motion starts or right now.

    @parameter index   which output gets modified
    @parameter now     whether change is immediate or synched with motion
    @parameter start   value set at start of motion
    @parameter end     value set at end of motion
*/
int emcMotionSetAout(unsigned char index, double start, double end, unsigned char now)
{
    if (now) {
        return motctl->set_aout(motctl->ctx, index, start);
    } else {
        return motctl->set_aout_synched(motctl->ctx, index, start, end);
    }
}

/*! \function emcMotionSetDout()

    This function sends a EMCMOT_SET_DOUT message to the motion controller.
    That one plans a DOUT command when motion starts or right now.

    @parameter index   which output gets modified
    @parameter now     whether change is immediate or synched with motion
    @parameter start   value set at start of motion
    @parameter end     value set at end of motion
*/
int emcMotionSetDout(unsigned char index, unsigned char start,
		     unsigned char end, unsigned char now)
{
    if (now) {
        return motctl->set_dout(motctl->ctx, index, start);
    } else {
        return motctl->set_dout_synched(motctl->ctx, index, start, end);
    }
}

int emcSpindleSetParams(int spindle, double max_pos, double min_pos, double max_neg,
			   double min_neg, double search_vel, double home_angle, int sequence, double increment)
{

    if (spindle < 0 || spindle >= EMCMOT_MAX_SPINDLES) {
	return 0;
    }

    int retval = motctl->set_spindle_params(motctl->ctx, spindle,
        max_pos, min_pos, max_neg, min_neg,
        search_vel, sequence, increment);

    if (emc_debug & EMC_DEBUG_CONFIG) {
        rcs_print("%s(%d, %e, %e, %e, %e, %f, %f, %i, %f) returned %d\n",
          __FUNCTION__, spindle, max_pos, min_pos, max_neg, min_neg, search_vel, home_angle,
          sequence, increment, retval);
    }
    return retval;
}

int emcSpindleAbort(int spindle)
{
    return emcSpindleOff(spindle);
}

int emcSpindleSpeed(int spindle, double speed, double css_factor, double offset)
{
    return motctl->spindle_on(motctl->ctx, spindle, speed, css_factor, offset, 0);
}

int emcSpindleOrient(int spindle, double orientation, int mode)
{
    return motctl->spindle_orient(motctl->ctx, spindle, orientation, mode);
}


int emcSpindleOn(int spindle, double speed, double css_factor, double offset, int wait_for_at_speed)
{
    return motctl->spindle_on(motctl->ctx, spindle, speed, css_factor, offset, wait_for_at_speed);
}

int emcSpindleOff(int spindle)
{
    return motctl->spindle_off(motctl->ctx, spindle);
}

int emcSpindleBrakeRelease(int spindle)
{
    return motctl->spindle_brake_release(motctl->ctx, spindle);
}

int emcSpindleBrakeEngage(int spindle)
{
    return motctl->spindle_brake_engage(motctl->ctx, spindle);
}

int emcSpindleIncrease(int spindle)
{
    return motctl->spindle_increase(motctl->ctx, spindle);
}

int emcSpindleDecrease(int spindle)
{
    return motctl->spindle_decrease(motctl->ctx, spindle);
}

int emcSpindleConstant(int spindle)
{
    return 0; // nothing to do
}

int emcSpindleUpdate(EMC_SPINDLE_STAT stat[], int num_spindles){
	int s;
	int enables;
    if (emcmotStatus.motionFlag & EMCMOT_MOTION_COORD_BIT)
        enables = emcmotStatus.enables_queued;
    else
        enables = emcmotStatus.enables_new;

    for (s = 0; s < num_spindles; s++){
		stat[s].spindle_override_enabled = enables & SS_ENABLED;
		stat[s].enabled = emcmotStatus.spindle_status[s].speed != 0;
		stat[s].speed = emcmotStatus.spindle_status[s].speed;
		stat[s].brake = emcmotStatus.spindle_status[s].brake;
		stat[s].direction = emcmotStatus.spindle_status[s].direction;
		stat[s].orient_state = emcmotStatus.spindle_status[s].orient_state;
		stat[s].orient_fault = emcmotStatus.spindle_status[s].orient_fault;
		stat[s].spindle_scale = emcmotStatus.spindle_status[s].scale;
    }
    return 0;
}

int emcMotionUpdate(EMC_MOTION_STAT * stat)
{
    int r1, r2, r3, r4;
    int joint;
    int error;
    int exec;
    int dio, aio, num_error;

    // read the emcmot status via motstat API
    motstat_motion_status_t ms;
    if (0 != motstat->get_status(motstat->ctx, &ms)) {
	return -1;
    }
    // Copy motstat fields into the legacy emcmotStatus struct.
    // This allows emcJointUpdate/emcTrajUpdate/etc. to continue
    // reading from emcmotStatus without rewriting all their field access.
    emcmotStatus.heartbeat = ms.heartbeat;
    emcmotStatus.commandEcho = (cmd_code_t)ms.command_echo;
    emcmotStatus.commandNumEcho = ms.command_num_echo;
    emcmotStatus.commandStatus = (cmd_status_t)ms.command_status;
    // Reconstruct motionFlag from individual booleans
    emcmotStatus.motionFlag = 0;
    if (ms.enabled)  emcmotStatus.motionFlag |= EMCMOT_MOTION_ENABLE_BIT;
    if (ms.inpos)    emcmotStatus.motionFlag |= EMCMOT_MOTION_INPOS_BIT;
    if (ms.coord)    emcmotStatus.motionFlag |= EMCMOT_MOTION_COORD_BIT;
    if (ms.error)    emcmotStatus.motionFlag |= EMCMOT_MOTION_ERROR_BIT;
    if (ms.teleop)   emcmotStatus.motionFlag |= EMCMOT_MOTION_TELEOP_BIT;
    emcmotStatus.depth = ms.queue_depth;
    emcmotStatus.activeDepth = ms.active_depth;
    emcmotStatus.queueFull = ms.queue_full;
    emcmotStatus.id = ms.id;
    emcmotStatus.motionType = ms.motion_type;
    emcmotStatus.distance_to_go = ms.distance_to_go;
    emcmotStatus.current_vel = ms.current_vel;
    emcmotStatus.feed_scale = ms.feed_scale;
    emcmotStatus.rapid_scale = ms.rapid_scale;
    emcmotStatus.paused = ms.paused;
    emcmotStatus.vel = ms.vel;
    emcmotStatus.acc = ms.acc;
    emcmotStatus.overrideLimitMask = ms.override_limit_mask;
    // Reconstruct enables flags from individual booleans
    {
        unsigned char en = 0;
        if (ms.feed_scale_enabled) en |= FS_ENABLED;
        if (ms.adaptive_feed_enabled) en |= AF_ENABLED;
        if (ms.feed_hold_enabled) en |= FH_ENABLED;
        if (ms.spindle_scale_enabled) en |= SS_ENABLED;
        emcmotStatus.enables_queued = en;
        emcmotStatus.enables_new = en;
    }
    emcmotStatus.probeVal = ms.probe.val;
    emcmotStatus.probing = ms.probe.probing;
    emcmotStatus.probeTripped = ms.probe.tripped;
    emcmotStatus.jogging_active = ms.jogging_active;
    // Positions
    emcmotStatus.carte_pos_cmd.tran.x = ms.carte_pos_cmd.x;
    emcmotStatus.carte_pos_cmd.tran.y = ms.carte_pos_cmd.y;
    emcmotStatus.carte_pos_cmd.tran.z = ms.carte_pos_cmd.z;
    emcmotStatus.carte_pos_cmd.a = ms.carte_pos_cmd.a;
    emcmotStatus.carte_pos_cmd.b = ms.carte_pos_cmd.b;
    emcmotStatus.carte_pos_cmd.c = ms.carte_pos_cmd.c;
    emcmotStatus.carte_pos_cmd.u = ms.carte_pos_cmd.u;
    emcmotStatus.carte_pos_cmd.v = ms.carte_pos_cmd.v;
    emcmotStatus.carte_pos_cmd.w = ms.carte_pos_cmd.w;
    emcmotStatus.carte_pos_fb.tran.x = ms.carte_pos_fb.x;
    emcmotStatus.carte_pos_fb.tran.y = ms.carte_pos_fb.y;
    emcmotStatus.carte_pos_fb.tran.z = ms.carte_pos_fb.z;
    emcmotStatus.carte_pos_fb.a = ms.carte_pos_fb.a;
    emcmotStatus.carte_pos_fb.b = ms.carte_pos_fb.b;
    emcmotStatus.carte_pos_fb.c = ms.carte_pos_fb.c;
    emcmotStatus.carte_pos_fb.u = ms.carte_pos_fb.u;
    emcmotStatus.carte_pos_fb.v = ms.carte_pos_fb.v;
    emcmotStatus.carte_pos_fb.w = ms.carte_pos_fb.w;
    // DTG
    emcmotStatus.dtg.tran.x = ms.dtg.x;
    emcmotStatus.dtg.tran.y = ms.dtg.y;
    emcmotStatus.dtg.tran.z = ms.dtg.z;
    emcmotStatus.dtg.a = ms.dtg.a;
    emcmotStatus.dtg.b = ms.dtg.b;
    emcmotStatus.dtg.c = ms.dtg.c;
    emcmotStatus.dtg.u = ms.dtg.u;
    emcmotStatus.dtg.v = ms.dtg.v;
    emcmotStatus.dtg.w = ms.dtg.w;
    // Probed position
    emcmotStatus.probedPos.tran.x = ms.probe.pos.x;
    emcmotStatus.probedPos.tran.y = ms.probe.pos.y;
    emcmotStatus.probedPos.tran.z = ms.probe.pos.z;
    emcmotStatus.probedPos.a = ms.probe.pos.a;
    emcmotStatus.probedPos.b = ms.probe.pos.b;
    emcmotStatus.probedPos.c = ms.probe.pos.c;
    emcmotStatus.probedPos.u = ms.probe.pos.u;
    emcmotStatus.probedPos.v = ms.probe.pos.v;
    emcmotStatus.probedPos.w = ms.probe.pos.w;
    // State tag
    memcpy(emcmotStatus.tag.fields_float, ms.tag.fields_float, sizeof(ms.tag.fields_float));
    memcpy(emcmotStatus.tag.fields, ms.tag.fields, sizeof(ms.tag.fields));
    emcmotStatus.tag.packed_flags = ms.tag.packed_flags;
    // DIO/AIO
    for (int i = 0; i < EMCMOT_MAX_DIO && i < MOTSTAT_MAX_DIO; i++) {
        emcmotStatus.synch_di[i] = ms.synch_di[i];
        emcmotStatus.synch_do[i] = ms.synch_do[i];
    }
    for (int i = 0; i < EMCMOT_MAX_AIO && i < MOTSTAT_MAX_AIO; i++) {
        emcmotStatus.analog_input[i] = ms.analog_input[i];
        emcmotStatus.analog_output[i] = ms.analog_output[i];
    }
    // Joint status
    for (int j = 0; j < MOTSTAT_MAX_JOINTS && j < EMCMOT_MAX_JOINTS; j++) {
        emcmot_joint_status_t *dst = &emcmotStatus.joint_status[j];
        const motstat_joint_status_t *src = &ms.joints[j];
        dst->pos_cmd = src->pos_cmd;
        dst->pos_fb = src->pos_fb;
        dst->vel_cmd = src->vel_cmd;
        dst->ferror = src->ferror;
        dst->ferror_high_mark = src->ferror_high_mark;
        dst->min_pos_limit = src->min_pos_limit;
        dst->max_pos_limit = src->max_pos_limit;
        dst->min_ferror = src->min_ferror;
        dst->max_ferror = src->max_ferror;
        dst->homing = src->homing;
        dst->homed = src->homed;
        // Rebuild flag from individual booleans
        dst->flag = 0;
        if (src->fault) dst->flag |= EMCMOT_JOINT_FAULT_BIT;
        if (src->enabled) dst->flag |= EMCMOT_JOINT_ENABLE_BIT;
        if (src->inpos) dst->flag |= EMCMOT_JOINT_INPOS_BIT;
        if (src->on_neg_limit) dst->flag |= EMCMOT_JOINT_MIN_HARD_LIMIT_BIT;
        if (src->on_pos_limit) dst->flag |= EMCMOT_JOINT_MAX_HARD_LIMIT_BIT;
        if (src->error) dst->flag |= EMCMOT_JOINT_ERROR_BIT;
    }
    // Axis status
    for (int a = 0; a < EMCMOT_MAX_AXIS && a < MOTSTAT_MAX_AXIS; a++) {
        emcmotStatus.axis_status[a].min_pos_limit = ms.axes[a].min_pos_limit;
        emcmotStatus.axis_status[a].max_pos_limit = ms.axes[a].max_pos_limit;
    }
    // Spindle status
    for (int s = 0; s < EMCMOT_MAX_SPINDLES && s < MOTSTAT_MAX_SPINDLES; s++) {
        emcmotStatus.spindle_status[s].speed = ms.spindles[s].speed;
        emcmotStatus.spindle_status[s].scale = ms.spindles[s].scale;
        emcmotStatus.spindle_status[s].direction = ms.spindles[s].direction;
        emcmotStatus.spindle_status[s].brake = ms.spindles[s].brake;
        emcmotStatus.spindle_status[s].orient_state = ms.spindles[s].orient_state;
        emcmotStatus.spindle_status[s].orient_fault = ms.spindles[s].orient_fault;
    }

    new_config = 0;
    static uint32_t last_config_num = 0;
    if (ms.config_num != (int32_t)last_config_num) {
	last_config_num = ms.config_num;
	new_config = 1;
	cached_config.kin_type        = ms.kin_type;
	cached_config.traj_cycle_time = ms.traj_cycle_time;
	cached_config.limit_vel       = ms.limit_vel;
	cached_config.debug           = ms.debug;
    }
    // read the emcmot error
    if (log_error_sub) {
	uint32_t err_level;
	char err_component[GOMC_LOG_COMPONENT_LEN];
	char err_msg[GOMC_LOG_MSG_LEN];
	while (gomc_log_sub_poll(log_error_sub, &err_level,
				 err_component, err_msg)) {
	    emcOperatorError(0, "%s", err_msg);
	}
    }

    // save the heartbeat and command number locally,
    // for use with emcMotionUpdate
    localMotionHeartbeat = emcmotStatus.heartbeat;
    localMotionCommandType = emcmotStatus.commandEcho;	/*! \todo FIXME-- not NML one! */
    localMotionEchoSerialNumber = emcmotStatus.commandNumEcho;

    r3 = emcTrajUpdate(&stat->traj);
    r1 = emcJointUpdate(&stat->joint[0], stat->traj.joints);
    r2 = emcAxisUpdate(&stat->axis[0], stat->traj.axis_mask);
    r3 = emcTrajUpdate(&stat->traj);
    r4 = emcSpindleUpdate(&stat->spindle[0], stat->traj.spindles);
    stat->heartbeat = localMotionHeartbeat;
    stat->command_type = localMotionCommandType;
    stat->echo_serial_number = localMotionEchoSerialNumber;
    stat->debug = cached_config.debug;

    for (dio = 0; dio < EMCMOT_MAX_DIO; dio++) {
	stat->synch_di[dio] = emcmotStatus.synch_di[dio];
	stat->synch_do[dio] = emcmotStatus.synch_do[dio];
    }

    for (aio = 0; aio < EMCMOT_MAX_AIO; aio++) {
	stat->analog_input[aio] = emcmotStatus.analog_input[aio];
	stat->analog_output[aio] = emcmotStatus.analog_output[aio];
    }

    for (num_error = 0; num_error < EMCMOT_MAX_MISC_ERROR; num_error++){
      stat->misc_error[num_error] = emcmotStatus.misc_error[num_error];
    }

    stat->jogging_active = emcmotStatus.jogging_active;
    stat->numExtraJoints = emcmotStatus.numExtraJoints;

    // set the status flag
    error = 0;
    exec = 0;

    // FIXME-AJ: joints not axes
    for (joint = 0; joint < stat->traj.joints; joint++) {
	if (stat->joint[joint].status == RCS_ERROR) {
	    error = 1;
	    break;
	}
	if (stat->joint[joint].status == RCS_EXEC) {
	    exec = 1;
	    break;
	}
    }
    if (stat->traj.status == RCS_ERROR) {
	error = 1;
    } else if (stat->traj.status == RCS_EXEC) {
	exec = 1;
    }

    if (error) {
	stat->status = RCS_ERROR;
    } else if (exec) {
	stat->status = RCS_EXEC;
    } else {
	stat->status = RCS_DONE;
    }
    return (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0) ? 0 : -1;
}

int emcSetupArcBlends(int arcBlendEnable,
        int arcBlendFallbackEnable,
        int arcBlendOptDepth,
        int arcBlendGapCycles,
        double arcBlendRampFreq,
        double arcBlendTangentKinkRatio) {

    // Arc blend config is now set via module params at loadrt time.
    (void)arcBlendEnable;
    (void)arcBlendFallbackEnable;
    (void)arcBlendOptDepth;
    (void)arcBlendGapCycles;
    (void)arcBlendRampFreq;
    (void)arcBlendTangentKinkRatio;
    return 0;
}

int emcSetMaxFeedOverride(double maxFeedScale) {
    return motctl->set_max_feed_override(motctl->ctx, maxFeedScale);
}

int emcSetProbeErrorInhibit(int j_inhibit, int h_inhibit) {
    return motctl->set_probe_err_inhibit(motctl->ctx, j_inhibit, h_inhibit);
}

int emcGetExternalOffsetApplied(void) {
    return emcmotStatus.external_offsets_applied;
}

EmcPose emcGetExternalOffsets(void) {
    return emcmotStatus.eoffset_pose;
}
