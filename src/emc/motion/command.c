/********************************************************************
* Description: command.c
*   emcmotCommandhandler() takes commands passed from user space and
*   performs various functions based on the value in inst->command->command.
*   For the full list, see the EMCMOT_COMMAND enum in motion.h
*
* pc says:
*
*   Most of the configs would be better off being passed via an ioctl
*   implementation leaving pure realtime data to be handled by
*   emcmotCommandHandler() - This would provide a small performance
*   increase on slower systems.
*
* jmk says:
*
*   Using commands to set config parameters is "undesirable", because
*   of the large amount of code needed for each parameter.  Today you
*   need to do the following to add a single new parameter called foo:
*
*   1)  Add a member 'foo' to the config or joint structure in motion.h
*   2)  Add a command 'EMCMOT_SET_FOO" to the cmd_code_t enum in motion.h
*   3)  Add a field to the command_t struct for the value used by
*       the set command (if there isn't already one that can be used.)
*   4)  Add a case to the giant switch statement in command.c to
*       handle the 'EMCMOT_SET_FOO' command.
*   5)  Write a function emcSetFoo() in taskintf.cc to issue the command.
*   6)  Add a prototype for emcSetFoo() to emc.hh
*   7)  Add code to iniaxis.cc (or one of the other inixxx.cc files) to
*       get the value from the INI file and call emcSetFoo().  (Note
*       that each parameter has about 16 lines of code, but the code
*       is identical except for variable/parameter names.)
*   8)  Add more code to iniaxis.cc to write the new value back out
*       to the INI file.
*   After all that, you have the abililty to get a number from the
*   INI file to a structure in shared memory where the motion controller
*   can actually use it.  However, if you want to manipulate that number
*   using NML, you have to do more:
*   9)  Add a #define EMC_SET_FOO_TYPE to emc.hh
*   10) Add a class definition for EMC_SET_FOO to emc.hh
*   11) Add a case to a giant switch statement in emctaskmain.cc to
*       call emcSetFoo() when the NML command is received.  (Actually
*       there are about 6 switch statements that need at least a
*       case label added.
*   12) Add cases to two giant switch statements in emc.cc, associated
*       with looking up and formatting the command.
*
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2004 All rights reserved.
********************************************************************/

#include <float.h>
#include "posemath.h"
#include "rtapi.h"
#include "rtapi_mutex.h"
#include "hal.h"
#include "motion.h"

#include "mot_priv.h"
#include "motion_struct.h"
#include "rtapi_math.h"
#include "motion_types.h"
#include "tp_api.h"
#include "home_api.h"
#include "axis.h"


#define ABS(x) (((x) < 0) ? -(x) : (x))

// Mark strings for translation, but defer translation to userspace
#define _(s) (s)

#define rehomeAll (inst->rehomeAll)

/* Override mot_priv.h macros to use local inst parameter */
#undef emcmot_hal_data
#undef emcmotStruct
#undef emcmotCommand
#undef emcmotStatus
#undef emcmotConfig
#undef emcmotInternal
#undef motmod_tp_api
#undef motmod_home_api
#undef motion_num_spindles
#define emcmot_hal_data  (inst->hal_data)
#define emcmotStruct     (inst->mot_struct)
#define emcmotCommand    (inst->command)
#define emcmotStatus     (inst->status)
#define emcmotConfig     (inst->config)
#define emcmotInternal   (inst->internal)
#define motmod_tp_api    ((const tp_callbacks_t *)inst->tp_api)
#define motmod_home_api  ((const home_callbacks_t *)inst->home_api)
#define motion_num_spindles (inst->num_spindles)
#define ai ((axis_inst_t *)inst->axis_inst)

/* limits_ok() returns 1 if none of the hard limits are set,
   0 if any are set. Called on a linear and circular move. */
static int limits_ok(motmod_inst_t *inst)
{
    int joint_num;
    emcmot_joint_t *joint;

    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
	/* point to joint data */
	joint = &inst->joints[joint_num];
	if (!GET_JOINT_ACTIVE_FLAG(joint)) {
	    /* if joint is not active, don't even look at its limits */
	    continue;
	}

	if (GET_JOINT_PHL_FLAG(joint) || GET_JOINT_NHL_FLAG(joint)) {
	    return 0;
	}
    }

    return 1;
}

/* check the value of the joint and velocity against current position,
   returning 1 (okay) if the request is to jog off the limit, 0 (bad)
   if the request is to jog further past a limit. */
static int joint_jog_ok(motmod_inst_t *inst, int joint_num, double vel)
{
    emcmot_joint_t *joint;
    int neg_limit_override, pos_limit_override;

    /* point to joint data */
    joint = &inst->joints[joint_num];
    /* are any limits for this joint overridden? */
    neg_limit_override = inst->status->overrideLimitMask & ( 1 << (joint_num*2));
    pos_limit_override = inst->status->overrideLimitMask & ( 2 << (joint_num*2));
    if ( neg_limit_override && pos_limit_override ) {
	/* both limits have been overridden at the same time.  This
	   happens only when they both share an input, but means it
	   is impossible to know which direction is safe to move.  So
	   we skip the following tests... */
	return 1;
    }
    if (joint_num < 0 || joint_num >= ALL_JOINTS) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog invalid joint number %d."), joint_num);
	return 0;
    }
    if (vel > 0.0 && GET_JOINT_PHL_FLAG(joint)) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint %d further past max hard limit."),
	    joint_num);
	return 0;
    }
    if (vel < 0.0 && GET_JOINT_NHL_FLAG(joint)) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint %d further past min hard limit."),
	    joint_num);
	return 0;
    }
    refresh_jog_limits(inst, joint, joint_num);
    if ( vel > 0.0 && (joint->pos_cmd > joint->max_jog_limit) ) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint %d further past max soft limit."),
	    joint_num);
	return 0;
    }
    if ( vel < 0.0 && (joint->pos_cmd < joint->min_jog_limit) ) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint %d further past min soft limit."),
	    joint_num);
	return 0;
    }
    /* okay to jog */
    return 1;
}

/* Jogs limits change, based on whether the machine is homed or
   or not.  If not homed, the limits are relative to the current
   position by +/- the full range of travel.  Once homed, they
   are absolute.

   homing api requires joint_num
*/
void refresh_jog_limits(motmod_inst_t *inst, emcmot_joint_t *joint, int joint_num)
{
    double range;

    if (motmod_home_api->get_homed(motmod_home_api->ctx, joint_num) ) {
	/* if homed, set jog limits using soft limits */
	joint->max_jog_limit = joint->max_pos_limit;
	joint->min_jog_limit = joint->min_pos_limit;
    } else {
	/* not homed, set limits based on current position */
	range = joint->max_pos_limit - joint->min_pos_limit;
	joint->max_jog_limit = joint->pos_fb + range;
	joint->min_jog_limit = joint->pos_fb - range;
    }
}

void apply_spindle_limits(spindle_status_t *s){
    if (s->speed > 0) {
        if (s->speed > s->max_pos_speed) s->speed = s->max_pos_speed;
        if (s->speed < s->min_pos_speed) s->speed = s->min_pos_speed;
    } else if (s->speed < 0) {
        if (s->speed < s->min_neg_speed) s->speed = s->min_neg_speed;
        if (s->speed > s->max_neg_speed) s->speed = s->max_neg_speed;
    }
}


/* inRange() returns non-zero if the position lies within the joint
   limits, or 0 if not.  It also reports an error for each joint limit
   violation.  It's possible to get more than one violation per move. */
static int inRange(motmod_inst_t *inst, EmcPose pos, int id, char *move_type)
{
    double joint_pos[EMCMOT_MAX_JOINTS];
    int joint_num, axis_num;
    emcmot_joint_t *joint;
    int in_range = 1;
    int failing_axes[EMCMOT_MAX_AXIS];
    double targets[EMCMOT_MAX_AXIS];
    const char axis_letters[] = "XYZABCUVW";

    if (EMCMOT_MAX_AXIS != 9) {
        rtapi_print_msg(RTAPI_MSG_ERR, "BUG: %s(): invalid number of axes defined", __func__);
    } else {
        targets[0] = pos.tran.x;
        targets[1] = pos.tran.y;
        targets[2] = pos.tran.z;
        targets[3] = pos.a;
        targets[4] = pos.b;
        targets[5] = pos.c;
        targets[6] = pos.u;
        targets[7] = pos.v;
        targets[8] = pos.w;
        axis_check_constraints(ai, targets, failing_axes);
        for (axis_num = 0; axis_num < EMCMOT_MAX_AXIS; axis_num += 1) {
            if (failing_axes[axis_num] == -1) {
                rtapi_print_msg(RTAPI_MSG_ERR, _("%s move on line %d would exceed %c's %s limit"),
                                move_type, id, axis_letters[axis_num], _("negative"));
                in_range = 0;
            }
            if (failing_axes[axis_num] == 1) {
                rtapi_print_msg(RTAPI_MSG_ERR, _("%s move on line %d would exceed %c's %s limit"),
                                move_type, id, axis_letters[axis_num], _("positive"));
                in_range = 0;
            }
        }
    }

    /* Now, check that the endpoint puts the joints within their limits too */

    /* fill in all joints with 0 */
    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
        joint = &inst->joints[joint_num];
        joint_pos[joint_num] = joint->pos_cmd;
    }

    /* now fill in with real values, for joints that are used */
    if (kinematicsInverse(&pos, joint_pos, &inst->iflags, &inst->fflags) != 0)
    {
	rtapi_print_msg(RTAPI_MSG_ERR, _("%s move on line %d fails kinematicsInverse"),
		    move_type, id);
	return 0;
    }

    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
	/* point to joint data */
	joint = &inst->joints[joint_num];

	if (!GET_JOINT_ACTIVE_FLAG(joint)) {
	    /* if joint is not active, don't even look at its limits */
	    continue;
	}
	if(!isfinite(joint_pos[joint_num]))
	{
	    rtapi_print_msg(RTAPI_MSG_ERR, _("%s move on line %d gave non-finite joint location on joint %d"),
		    move_type, id, joint_num);
	    in_range = 0;
	    continue;
	}
	if (joint_pos[joint_num] > joint->max_pos_limit) {
            in_range = 0;
	    rtapi_print_msg(RTAPI_MSG_ERR, _("%s move on line %d would exceed joint %d's positive limit"),
			move_type, id, joint_num);
        }

        if (joint_pos[joint_num] < joint->min_pos_limit) {
	    in_range = 0;
	    rtapi_print_msg(RTAPI_MSG_ERR, _("%s move on line %d would exceed joint %d's negative limit"),
			move_type, id, joint_num);
	}
    }
    return in_range;
}

/* legacy note:
   clearHomes() will clear the homed flags for joints that have moved
   since homing, outside coordinated control, for machines with no
   forward kinematics. This is used in conjunction with the rehomeAll
   flag, which is set for any coordinated move that in general will
   result in all joints moving. The flag is consulted whenever a joint
   is jogged in joint mode, so that either its flag can be cleared if
   no other joints have moved, or all have to be cleared.

   NOTE: dubious usefulness (inverse-only kins etc.)
*/
void clearHomes(motmod_inst_t *inst, int joint_num)
{
    int n;
    if (inst->config->kinType == KINEMATICS_INVERSE_ONLY) {
	if (rehomeAll) {
	    for (n = 0; n < ALL_JOINTS; n++) {
                motmod_home_api->set_unhomed(motmod_home_api->ctx, n, (home_motion_state_t)inst->status->motion_state);
	    }
	} else {
            motmod_home_api->set_unhomed(motmod_home_api->ctx, joint_num, (home_motion_state_t)inst->status->motion_state);
	}
    }
}

void emcmotSetRotaryUnlock(motmod_inst_t *inst, int jnum, int unlock) {
    if (NULL == inst->hal_data->joint[jnum].unlock) {
        rtapi_print_msg(RTAPI_MSG_ERR,
        "emcmotSetRotaryUnlock(): No unlock pin configured for joint %d\n"
        "   Use motmod parameter: unlock_joints_mask=%X",
        jnum,1<<jnum);
        return;
    }
    *(inst->hal_data->joint[jnum].unlock) = unlock;
}

int emcmotGetRotaryIsUnlocked(motmod_inst_t *inst, int jnum) {
    static int gave_message = 0;
    if (NULL == inst->hal_data->joint[jnum].unlock) {
        if (!gave_message) {
            rtapi_print_msg(RTAPI_MSG_ERR,
            "emcmotGetRotaryUnlocked(): No unlock pin configured for joint %d\n"
            "   Use motmod parameter: unlock_joints_mask=%X'",
            jnum,1<<jnum);
        }
        gave_message = 1;
        return 0;
    }
    return *(inst->hal_data->joint[jnum].is_unlocked);
}

/*! \function emcmotDioWrite()

  sets or clears a HAL DIO pin,
  pins get exported at runtime

  index is valid from 0 to inst->config->num_dio <= EMCMOT_MAX_DIO, defined in emcmotcfg.h

*/
void emcmotDioWrite(motmod_inst_t *inst, int index, char value)
{
    if ((index >= inst->config->numDIO) || (index < 0)) {
	rtapi_print_msg(RTAPI_MSG_ERR, "ERROR: index out of range, %d not in [0..%d] (increase num_dio/EMCMOT_MAX_DIO=%d)\n", index, inst->config->numDIO, EMCMOT_MAX_DIO);
    } else {
	if (value != 0) {
	    *(inst->hal_data->synch_do[index])=1;
	} else {
	    *(inst->hal_data->synch_do[index])=0;
	}
    }
}

/*! \function emcmotAioWrite()

  sets or clears a HAL AIO pin,
  pins get exported at runtime

  index is valid from 0 to inst->config->num_aio <= EMCMOT_MAX_AIO, defined in emcmotcfg.h

*/
void emcmotAioWrite(motmod_inst_t *inst, int index, double value)
{
    if ((index >= inst->config->numAIO) || (index < 0)) {
	rtapi_print_msg(RTAPI_MSG_ERR, "ERROR: index out of range, %d not in [0..%d] (increase num_aio/EMCMOT_MAX_AIO=%d)\n", index, inst->config->numAIO, EMCMOT_MAX_AIO);
    } else {
        *(inst->hal_data->analog_output[index]) = value;
    }
}

static int is_feed_type(int motion_type)
{
    switch(motion_type) {
    case EMC_MOTION_TYPE_ARC:
    case EMC_MOTION_TYPE_FEED:
    case EMC_MOTION_TYPE_PROBING:
        return 1;
    default:
        rtapi_print_msg(RTAPI_MSG_ERR, "Internal error: unhandled motion type %d\n", motion_type);
        /* fall through */
    case EMC_MOTION_TYPE_TOOLCHANGE:
    case EMC_MOTION_TYPE_TRAVERSE:
    case EMC_MOTION_TYPE_INDEXROTARY:
        return 0;
    }
}


/*
  emcmotCommandHandler() is called each main cycle to read the
  shared memory buffer

  This function runs with the inst->command struct locked.
  */
void emcmotCommandHandler_locked(void *arg, long servo_period)
{
    motmod_inst_t *inst = (motmod_inst_t *)arg;
    motmod_set_active_inst(inst);

    int joint_num, spindle_num;
    int n,s0,s1;
    emcmot_joint_t *joint;
    double tmp1;
    emcmot_comp_entry_t *comp_entry;
    char issue_atspeed = 0;
    int abort = 0;
    char* emsg = "";

    if (inst->command->commandNum != inst->status->commandNumEcho) {
	/* increment head count-- we'll be modifying inst->status */
	inst->status->head++;
	inst->internal->head++;

	/* got a new command-- echo command and number... */
	inst->status->commandEcho = inst->command->command;
	inst->status->commandNumEcho = inst->command->commandNum;

	/* clear status value by default */
	inst->status->commandStatus = EMCMOT_COMMAND_OK;

	/* ...and process command */

        joint = 0;
        joint_num = inst->command->joint;

//-----------------------------------------------------------------------------
// joints_axes test for unexpected conditions
// example: non-cooperating guis
// example: attempt to jog locking indexer axis letter
        if (   inst->command->command == EMCMOT_JOG_CONT
            || inst->command->command == EMCMOT_JOG_INCR
            || inst->command->command == EMCMOT_JOG_ABS
           ) {
           if (GET_MOTION_TELEOP_FLAG() && (inst->command->axis < 0)) {
               emsg = "command.com teleop: unexpected negative axis_num";
               if (joint_num >= 0) {
                   emsg = "Mode is TELEOP, cannot jog joint";
               }
               abort = 1;
           }
           if (!GET_MOTION_TELEOP_FLAG() && joint_num < 0) {
               emsg = "command.com !teleop: unexpected negative joint_num";
               if (inst->command->axis >= 0) {
                   emsg = "Mode is NOT TELEOP, cannot jog axis coordinate";
               }
               abort = 1;
           }
           if (   !GET_MOTION_TELEOP_FLAG()
               && (joint_num >= ALL_JOINTS || joint_num <  0)
              ) {
               rtapi_print_msg(RTAPI_MSG_ERR,
                    "Joint jog requested for undefined joint number=%d (min=0,max=%d)",
                    joint_num,ALL_JOINTS-1);
               return;
           }
           if (GET_MOTION_TELEOP_FLAG()) {
                if ( (inst->command->axis >= 0) && (axis_get_locking_joint(ai, inst->command->axis) >= 0) ) {
                    rtapi_print_msg(RTAPI_MSG_ERR,
                    "Cannot jog a locking indexer AXIS_%c,joint_num=%d\n",
                    "XYZABCUVW"[inst->command->axis], axis_get_locking_joint(ai, inst->command->axis));
                    return;
                }
           }
        }
        if (abort) {
          switch (inst->command->command) {
          case EMCMOT_JOG_CONT:
               rtapi_print_msg(RTAPI_MSG_ERR,"JOG_CONT %s\n",emsg);
               break;
          case EMCMOT_JOG_INCR:
               rtapi_print_msg(RTAPI_MSG_ERR,"JOG_INCR %s\n",emsg);
               break;
          case EMCMOT_JOG_ABS:
               rtapi_print_msg(RTAPI_MSG_ERR,"JOG_ABS %s\n",emsg);
               break;
          default: break;
          }
          return;
        }

        if (joint_num >= 0 && joint_num < ALL_JOINTS) {
            joint = &inst->joints[joint_num];
            if (   (   inst->command->command == EMCMOT_JOG_CONT
                    || inst->command->command == EMCMOT_JOG_INCR
                    || inst->command->command == EMCMOT_JOG_ABS
                   )
                && !(GET_MOTION_TELEOP_FLAG())
                && motmod_home_api->get_is_synchronized(motmod_home_api->ctx, joint_num)
                && !motmod_home_api->get_is_active(motmod_home_api->ctx)
               ) {
                  if (inst->config->kinType == KINEMATICS_IDENTITY) {
                      rtapi_print_msg(RTAPI_MSG_ERR,
                      "Homing is REQUIRED to jog requested coordinate\n"
                      "because joint (%d) home_sequence is synchronized (%d)\n"
                      ,joint_num,motmod_home_api->get_sequence(motmod_home_api->ctx, joint_num));
                  } else {
                      rtapi_print_msg(RTAPI_MSG_ERR,
                      "Cannot jog joint %d because home_sequence is synchronized (%d)\n"
                      ,joint_num,motmod_home_api->get_sequence(motmod_home_api->ctx, joint_num));
                  }
                  return;
            }
        }
	switch (inst->command->command) {
	case EMCMOT_ABORT:
	    /* abort motion */
	    /* can happen at any time */
	    /* this command attempts to stop all machine motion. it looks at
	       the current mode and acts accordingly, if in teleop mode, it
	       sets the desired velocities to zero, if in coordinated mode,
	       it calls the traj planner abort function (don't know what that
	       does yet), and if in free mode, it disables the free mode traj
	       planners which stops joint motion */
	    rtapi_print_msg(RTAPI_MSG_DBG, "ABORT");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    /* check for coord or free space motion active */
	    if (GET_MOTION_TELEOP_FLAG()) {
                axis_jog_abort_all(ai, 0);
	    } else if (GET_MOTION_COORD_FLAG()) {
		motmod_tp_api->abort(motmod_tp_api->ctx);
	    } else {
		for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
		    /* point to joint struct */
		    joint = &inst->joints[joint_num];
		    /* tell joint planner to stop */
		    joint->free_tp.enable = 0;
		    /* stop homing if in progress */
		    if ( ! motmod_home_api->get_is_idle(motmod_home_api->ctx, joint_num)) {
			motmod_home_api->do_cancel(motmod_home_api->ctx, joint_num);
		    }
		}
	    }
            SET_MOTION_ERROR_FLAG(0);
	    /* clear joint errors (regardless of mode) */
	    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
		/* point to joint struct */
		joint = &inst->joints[joint_num];
		/* update status flags */
		SET_JOINT_ERROR_FLAG(joint, 0);
		SET_JOINT_FAULT_FLAG(joint, 0);
	    }
	    inst->status->paused = 0;
	    break;

	case EMCMOT_JOG_ABORT:
	    /* abort one joint number or axis number */
	    /* can happen at any time */
	    if (GET_MOTION_TELEOP_FLAG()) {
	        /* tell teleop planner to stop */
	        if ((inst->command->axis >= 0) && (inst->command->axis < EMCMOT_MAX_AXIS)) {
	            axis_jog_abort(ai, inst->command->axis, 0);
	        }
	    } else {
	        if (joint == 0) { break; }
	        /* tell joint planner to stop */
	        joint->free_tp.enable    = 0;
	        joint->kb_jjog_active    = 0;
	        joint->wheel_jjog_active = 0;
	        /* stop homing if in progress */
	        if ( !motmod_home_api->get_is_idle(motmod_home_api->ctx, joint_num) ) {
	            motmod_home_api->do_cancel(motmod_home_api->ctx, joint_num);
	        }
	        /* update status flags */
	        SET_JOINT_ERROR_FLAG(joint, 0);
	    }
	    break;

	case EMCMOT_FREE:
            axis_jog_abort_all(ai, 0);
	    /* change the mode to free mode motion (joint mode) */
	    /* can be done at any time */
	    /* this code doesn't actually make the transition, it merely
	       requests the transition by clearing a couple of flags */
	    /* reset the inst->internal->coordinating flag to defer transition
	       to controller cycle */
	    rtapi_print_msg(RTAPI_MSG_DBG, "FREE");
	    inst->internal->coordinating = 0;
	    inst->internal->teleoperating = 0;
	    break;

	case EMCMOT_COORD:
	    /* change the mode to coordinated axis motion */
	    /* can be done at any time */
	    /* this code doesn't actually make the transition, it merely
	       tests a condition and then sets a flag requesting the
	       transition */
	    /* set the inst->internal->coordinating flag to defer transition to
	       controller cycle */

	    rtapi_print_msg(RTAPI_MSG_DBG, "COORD");
	    inst->internal->coordinating = 1;
	    inst->internal->teleoperating = 0;
	    if (inst->config->kinType != KINEMATICS_IDENTITY) {
		if (!motmod_home_api->get_allhomed(motmod_home_api->ctx)) {
		    rtapi_print_msg(RTAPI_MSG_ERR,
			_("all joints must be homed before going into coordinated mode"));
		    inst->internal->coordinating = 0;
		    break;
		}
	    }
	    break;

	case EMCMOT_TELEOP:
	    rtapi_print_msg(RTAPI_MSG_DBG, "TELEOP");
            switch_to_teleop_mode(inst);
	    break;

	case EMCMOT_SET_NUM_JOINTS:
	    /* set the global NUM_JOINTS, which must be between 1 and
	       EMCMOT_MAX_JOINTS, inclusive.
	       Called  by task using [KINS]JOINTS= which is typically
	       the same value as the motmod num_joints= parameter
	    */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_NUM_JOINTS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", inst->command->joint);
	    if (( inst->command->joint <= 0 ) ||
		( inst->command->joint > EMCMOT_MAX_JOINTS )) {
		break;
	    }
	    ALL_JOINTS = inst->command->joint;
	    break;

	case EMCMOT_SET_NUM_SPINDLES:
	    /* set the global NUM_SPINDLES, which must be between 1 and
	       EMCMOT_MAX_SPINDLES, inclusive and less than or equal to
	       the number of spindles configured for the motion module
	       (motion_num_spindles)
	    */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_NUM_SPINDLES");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", inst->command->spindle);
	    if (   inst->command->spindle > motion_num_spindles
	        || inst->command->spindle <= 0
	        || inst->command->spindle > EMCMOT_MAX_SPINDLES
	       ) {
	        rtapi_print_msg(RTAPI_MSG_ERR, "Problem:\n"
	                    "  motmod configured for %d spindles\n"
	                    "  but command requests %d spindles\n"
	                    "  Using: %d spindles",
	                    motion_num_spindles,
	                    inst->command->spindle,
	                    motion_num_spindles
	                   );
	        inst->config->numSpindles = motion_num_spindles;
	    } else {
	        inst->config->numSpindles = inst->command->spindle;
	    }
	    break;

	case EMCMOT_SET_WORLD_HOME:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_WORLD_HOME");
	    inst->status->world_home = inst->command->pos;
	    break;

	case EMCMOT_SET_JOINT_HOMING_PARAMS:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_HOMING_PARAMS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0) {
		break;
	    }
	    motmod_home_api->set_joint_params(motmod_home_api->ctx, joint_num,
	                            inst->command->offset,
	                            inst->command->home,
	                            inst->command->home_final_vel,
	                            inst->command->search_vel,
	                            inst->command->latch_vel,
	                            inst->command->flags,
	                            inst->command->home_sequence,
	                            (int32_t)inst->command->volatile_home
	                           );
	    break;

	case EMCMOT_UPDATE_JOINT_HOMING_PARAMS:
	    rtapi_print_msg(RTAPI_MSG_DBG, "UPDATE_JOINT_HOMING_PARAMS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0) {
		break;
	    }
	    motmod_home_api->update_joint_params(motmod_home_api->ctx, joint_num,
	                               inst->command->offset,
	                               inst->command->home,
	                               inst->command->home_sequence
	                               );
	    break;

	case EMCMOT_OVERRIDE_LIMITS:
	    /* this command can be issued with joint < 0 to re-enable
	       limits, but they are automatically re-enabled at the
	       end of the next jog */
	    rtapi_print_msg(RTAPI_MSG_DBG, "OVERRIDE_LIMITS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (joint_num < 0) {
		/* don't override limits */
		rtapi_print_msg(RTAPI_MSG_DBG, "override off");
		inst->status->overrideLimitMask = 0;
	    } else {
		rtapi_print_msg(RTAPI_MSG_DBG, "override on");
		inst->status->overrideLimitMask = 0;
		for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
		    /* point at joint data */
		    joint = &inst->joints[joint_num];
		    /* only override limits that are currently tripped */
		    if ( GET_JOINT_NHL_FLAG(joint) ) {
			inst->status->overrideLimitMask |= (1 << (joint_num*2));
		    }
		    if ( GET_JOINT_PHL_FLAG(joint) ) {
			inst->status->overrideLimitMask |= (2 << (joint_num*2));
		    }
		}
	    }
	    inst->internal->overriding = 0;
	    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
		/* point at joint data */
		joint = &inst->joints[joint_num];
		/* clear joint errors */
		SET_JOINT_ERROR_FLAG(joint, 0);
	    }
	    break;

	case EMCMOT_SET_JOINT_MOTOR_OFFSET:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_MOTOR_OFFSET");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if(joint == 0) {
		break;
	    }
	    joint->motor_offset = inst->command->motor_offset;
	    break;

	case EMCMOT_SET_JOINT_POSITION_LIMITS:
	    /* set the position limits for the joint */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_POSITION_LIMITS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0) {
		break;
	    }
	    joint->min_pos_limit = inst->command->minLimit;
	    joint->max_pos_limit = inst->command->maxLimit;
	    break;

	case EMCMOT_SET_JOINT_BACKLASH:
	    /* set the backlash for the joint */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_BACKLASH");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0) {
		break;
	    }
	    joint->backlash = inst->command->backlash;
	    break;

	    /*
	       Max and min ferror work like this: limiting ferror is
	       determined by slope of ferror line, = maxFerror/limitVel ->
	       limiting ferror = maxFerror/limitVel * vel. If ferror <
	       minFerror then OK else if ferror < limiting ferror then OK
	       else ERROR */
	case EMCMOT_SET_JOINT_MAX_FERROR:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_MAX_FERROR");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0 || inst->command->maxFerror < 0.0) {
		break;
	    }
	    joint->max_ferror = inst->command->maxFerror;
	    break;

	case EMCMOT_SET_JOINT_MIN_FERROR:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_MIN_FERROR");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0 || inst->command->minFerror < 0.0) {
		break;
	    }
	    joint->min_ferror = inst->command->minFerror;
	    break;

	case EMCMOT_JOG_CONT:
	    /* do a continuous jog, implemented as an incremental jog to the
	       limit.  When the user lets go of the button an abort will
	       stop the jog. */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOG_CONT");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (!GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint when not enabled."));
		SET_JOINT_ERROR_FLAG(joint, 1);
		break;
	    }
            // cannot jog if jog-inhibit is TRUE
            if (*(inst->hal_data->jog_inhibit)){
                    rtapi_print_msg(RTAPI_MSG_ERR, _("Cannot jog while jog-inhibit is active."));
                break;
            }
	    if ( motmod_home_api->get_is_active(motmod_home_api->ctx) ) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog any joints while homing."));
		SET_JOINT_ERROR_FLAG(joint, 1);
		break;
	    }
            if (!GET_MOTION_TELEOP_FLAG()) {
	        if (joint->wheel_jjog_active) {
		    /* can't do two kinds of jog at once */
		    break;
	        }
                if (motmod_home_api->get_needs_unlock_first(motmod_home_api->ctx, joint_num) ) {
                    rtapi_print_msg(RTAPI_MSG_ERR, "Can't jog locking joint_num=%d",joint_num);
                    SET_JOINT_ERROR_FLAG(joint, 1);
                    break;
                }
	        /* don't jog further onto limits */
	        if (!joint_jog_ok(inst, joint_num, inst->command->vel)) {
		    SET_JOINT_ERROR_FLAG(joint, 1);
		    break;
	        }
	        /* set destination of jog */
	        refresh_jog_limits(inst, joint, joint_num);
	        if (inst->command->vel > 0.0) {
		    joint->free_tp.pos_cmd = joint->max_jog_limit;
	        } else {
		    joint->free_tp.pos_cmd = joint->min_jog_limit;
	        }
	        /* set velocity of jog */
	        joint->free_tp.max_vel = fabs(inst->command->vel);
	        /* use max joint accel */
	        joint->free_tp.max_acc = joint->acc_limit;
	        /* lock out other jog sources */
	        joint->kb_jjog_active = 1;
	        /* and let it go */
	        joint->free_tp.enable = 1;
                axis_jog_abort_all(ai, 0);
	        /*! \todo FIXME - should we really be clearing errors here? */
	        SET_JOINT_ERROR_FLAG(joint, 0);
	        /* clear joints homed flag(s) if we don't have forward kins.
	           Otherwise, a transition into coordinated mode will incorrectly
	           assume the homed position. Do all if they've all been moved
	           since homing, otherwise just do this one */
	        clearHomes(inst, joint_num);
            } else {
                // TELEOP  JOG_CONT
                if (GET_MOTION_ERROR_FLAG()) { break; }
                for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
                    joint = &inst->joints[joint_num];
                    if (joint != 0) { joint->free_tp.enable = 0; }
                }
                axis_jog_cont(ai, inst->command->axis, inst->command->vel, servo_period);
            }
	    break;

	case EMCMOT_JOG_INCR:
	    /* do an incremental jog */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOG_INCR");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (!GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint when not enabled."));
		SET_JOINT_ERROR_FLAG(joint, 1);
		break;
	    }
            // cannot jog if jog-inhibit is TRUE
            if (*(inst->hal_data->jog_inhibit)){
                    rtapi_print_msg(RTAPI_MSG_ERR, _("Cannot jog while jog-inhibit is active."));
                break;
            }
	    if ( motmod_home_api->get_is_active(motmod_home_api->ctx) ) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog any joint while homing."));
		SET_JOINT_ERROR_FLAG(joint, 1);
		break;
	    }
            if (!GET_MOTION_TELEOP_FLAG()) {
	        if (joint->wheel_jjog_active) {
		    /* can't do two kinds of jog at once */
		    break;
	        }
                if (motmod_home_api->get_needs_unlock_first(motmod_home_api->ctx, joint_num) ) {
                    rtapi_print_msg(RTAPI_MSG_ERR, "Can't jog locking joint_num=%d",joint_num);
                    SET_JOINT_ERROR_FLAG(joint, 1);
                    break;
                }
	        /* don't jog further onto limits */
	        if (!joint_jog_ok(inst, joint_num, inst->command->vel)) {
		    SET_JOINT_ERROR_FLAG(joint, 1);
		    break;
	        }
	        /* set target position for jog */
	        if (inst->command->vel > 0.0) {
		    tmp1 = joint->free_tp.pos_cmd + inst->command->offset;
	        } else {
		    tmp1 = joint->free_tp.pos_cmd - inst->command->offset;
	        }
	        /* don't jog past limits */
	        refresh_jog_limits(inst, joint, joint_num);
	        if (tmp1 > joint->max_jog_limit) {
		    break;
	        }
	        if (tmp1 < joint->min_jog_limit) {
		    break;
	        }
	        /* set target position */
	        joint->free_tp.pos_cmd = tmp1;
	        /* set velocity of jog */
	        joint->free_tp.max_vel = fabs(inst->command->vel);
	        /* use max joint accel */
	        joint->free_tp.max_acc = joint->acc_limit;
	        /* lock out other jog sources */
	        joint->kb_jjog_active = 1;
	        /* and let it go */
	        joint->free_tp.enable = 1;
                axis_jog_abort_all(ai, 0);
	        SET_JOINT_ERROR_FLAG(joint, 0);
	        /* clear joint homed flag(s) if we don't have forward kins.
	           Otherwise, a transition into coordinated mode will incorrectly
	           assume the homed position. Do all if they've all been moved
	           since homing, otherwise just do this one */
	        clearHomes(inst, joint_num);
            } else {
                // TELEOP JOG_INCR
                if (GET_MOTION_ERROR_FLAG()) { break; }
                axis_jog_incr(ai, inst->command->axis, inst->command->offset, inst->command->vel, servo_period);
                for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
                    joint = &inst->joints[joint_num];
                    if (joint != 0) { joint->free_tp.enable = 0; }
                }
            }
	    break;

	case EMCMOT_JOG_ABS:
	    /* do an absolute jog */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOG_ABS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (joint == 0) {
		break;
	    }
	    if (!GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog joint when not enabled."));
		SET_JOINT_ERROR_FLAG(joint, 1);
		break;
	    }
            // cannot jog if jog-inhibit is TRUE
            if (*(inst->hal_data->jog_inhibit)){
                    rtapi_print_msg(RTAPI_MSG_ERR, _("Cannot jog while jog-inhibit is active."));
                break;
            }
	    if ( motmod_home_api->get_is_active(motmod_home_api->ctx) ) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("Can't jog any joints while homing."));
		SET_JOINT_ERROR_FLAG(joint, 1);
		break;
	    }
            if (!GET_MOTION_TELEOP_FLAG()) {
                // FREE JOG_ABS
                if (joint->wheel_jjog_active) {
                    /* can't do two kinds of jog at once */
                    break;
                }
                /* don't jog further onto limits */
                if (!joint_jog_ok(inst, joint_num, inst->command->vel)) {
                    SET_JOINT_ERROR_FLAG(joint, 1);
                    break;
                }
                /*! \todo FIXME-- use 'goal' instead */
                joint->free_tp.pos_cmd = inst->command->offset;
                /* don't jog past limits */
                refresh_jog_limits(inst, joint, joint_num);
                if (joint->free_tp.pos_cmd > joint->max_jog_limit) {
                    joint->free_tp.pos_cmd = joint->max_jog_limit;
                }
                if (joint->free_tp.pos_cmd < joint->min_jog_limit) {
                    joint->free_tp.pos_cmd = joint->min_jog_limit;
                }
                /* set velocity of jog */
                joint->free_tp.max_vel = fabs(inst->command->vel);
                /* use max joint accel */
                joint->free_tp.max_acc = joint->acc_limit;
                /* lock out other jog sources */
                joint->kb_jjog_active = 1;
                /* and let it go */
                joint->free_tp.enable = 1;
                SET_JOINT_ERROR_FLAG(joint, 0);
                /* clear joint homed flag(s) if we don't have forward kins.
                   Otherwise, a transition into coordinated mode will incorrectly
                   assume the homed position. Do all if they've all been moved
                   since homing, otherwise just do this one */
                clearHomes(inst, joint_num);
            } else {
                // TELEOP JOG_ABS
                axis_jog_abs(ai, inst->command->axis, inst->command->offset, inst->command->vel);
                for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
                   joint = &inst->joints[joint_num];
                   if (joint != 0) { joint->free_tp.enable = 0; }
                }
                return;
            }
            break;

	case EMCMOT_SET_TERM_COND:
	    /* sets termination condition for motion inst->internal->coord_tp */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_TERM_COND");
	    motmod_tp_api->set_term_cond(motmod_tp_api->ctx, inst->command->termCond, inst->command->tolerance);
	    break;

	case EMCMOT_SET_SPINDLESYNC:
		motmod_tp_api->set_spindle_sync(motmod_tp_api->ctx, inst->command->spindle, inst->command->spindlesync, inst->command->flags);
		break;

	case EMCMOT_SET_LINE:
	    /* inst->internal->coord_tp up a linear move */
	    /* requires motion enabled, coordinated mode, not on limits */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_LINE");
	    if (!GET_MOTION_COORD_FLAG() || !GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("need to be enabled, in coord mode for linear move"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!inRange(inst, inst->command->pos, inst->command->id, "Linear")) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("invalid params in linear command"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!limits_ok(inst)) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("can't do linear move with limits exceeded"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    }

		if(inst->status->atspeed_next_feed && is_feed_type(inst->command->motion_type) ) {
			issue_atspeed = 1;
			inst->status->atspeed_next_feed = 0;
		}
		if(!is_feed_type(inst->command->motion_type) &&
				inst->status->spindle_status[inst->command->spindle].css_factor) {
			inst->status->atspeed_next_feed = 1;
		}

	    /* append it to the inst->internal->coord_tp */
	    motmod_tp_api->set_id(motmod_tp_api->ctx, inst->command->id);
	    int res_addline = motmod_tp_api->add_line(motmod_tp_api->ctx, 
					(const tp_pose_t *)&inst->command->pos,
					inst->command->motion_type,
					inst->command->vel,
					inst->command->ini_maxvel,
					inst->command->acc,
					inst->status->enables_new,
					(int8_t)issue_atspeed,
					inst->command->turn,
					(const tp_state_tag_t *)&inst->command->tag);
        //KLUDGE ignore zero length line
        if (res_addline < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, _("can't add linear move at line %d, error code %d"),
                    inst->command->id, res_addline);
            inst->status->commandStatus = EMCMOT_COMMAND_BAD_EXEC;
            motmod_tp_api->abort(motmod_tp_api->ctx);
            SET_MOTION_ERROR_FLAG(1);
            break;
        } else if (res_addline != 0) {
            //TODO make this hand-shake more explicit
            //KLUDGE Non fatal error, need to restore state so that the next
            //line properly handles at_speed
            if (issue_atspeed) {
                inst->status->atspeed_next_feed = 1;
            }
        } else {
		SET_MOTION_ERROR_FLAG(0);
		/* set flag that indicates all joints need rehoming, if any
		   joint is moved in joint mode, for machines with no forward
		   kins */
		rehomeAll = 1;
	    }
	    break;

	case EMCMOT_SET_CIRCLE:
	    /* inst->internal->coord_tp up a circular move */
	    /* requires coordinated mode, enable on, not on limits */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_CIRCLE");
	    if (!GET_MOTION_COORD_FLAG() || !GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("need to be enabled, in coord mode for circular move"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!inRange(inst, inst->command->pos, inst->command->id, "Circular")) {
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!limits_ok(inst)) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("can't do circular move with limits exceeded"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    }
            if(inst->status->atspeed_next_feed) {
                issue_atspeed = 1;
                inst->status->atspeed_next_feed = 0;
            }
	    /* append it to the inst->internal->coord_tp */
	    motmod_tp_api->set_id(motmod_tp_api->ctx, inst->command->id);
	    int res_addcircle = motmod_tp_api->add_circle(motmod_tp_api->ctx, 
                            (const tp_pose_t *)&inst->command->pos,
                            (const tp_cartesian_t *)&inst->command->center,
                            (const tp_cartesian_t *)&inst->command->normal,
                            inst->command->turn, inst->command->motion_type,
                            inst->command->vel, inst->command->ini_maxvel,
                            inst->command->acc, inst->status->enables_new,
			    (int8_t)issue_atspeed,
                            (const tp_state_tag_t *)&inst->command->tag);
        if (res_addcircle < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, _("can't add circular move at line %d, error code %d"),
                    inst->command->id, res_addcircle);
		inst->status->commandStatus = EMCMOT_COMMAND_BAD_EXEC;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
        } else if (res_addcircle != 0) {
            //FIXME! This is a band-aid for a single issue, but there may be
            //other consequences of non-fatal errors from AddXXX functions. We
            //either need to fix the root cause (subtle position error after
            //homing), or have a full restore here.
            if (issue_atspeed) {
                inst->status->atspeed_next_feed = 1;
            }
        } else {
		SET_MOTION_ERROR_FLAG(0);
		/* set flag that indicates all joints need rehoming, if any
		   joint is moved in joint mode, for machines with no forward
		   kins */
		rehomeAll = 1;
	    }
	    break;

	case EMCMOT_SET_VEL:
	    /* set the velocity for subsequent moves */
	    /* can do it at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_VEL");
	    inst->status->vel = inst->command->vel;
	    motmod_tp_api->set_vmax(motmod_tp_api->ctx, inst->status->vel, inst->command->ini_maxvel);
	    break;

	case EMCMOT_SET_VEL_LIMIT:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_VEL_LIMIT");
	    emcmot_config_change();
	    /* set the absolute max velocity for all subsequent moves */
	    /* can do it at any time */
	    inst->config->limitVel = inst->command->vel;
	    motmod_tp_api->set_vlimit(motmod_tp_api->ctx, inst->config->limitVel);
	    break;

	case EMCMOT_SET_JOINT_VEL_LIMIT:
	    /* set joint max velocity */
	    /* can do it at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_VEL_LIMIT");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    /* check joint range */
	    if (joint == 0) {
		break;
	    }
	    joint->vel_limit = inst->command->vel;
	    break;

	case EMCMOT_SET_JOINT_ACC_LIMIT:
	    /* set joint max acceleration */
	    /* can do it at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_ACC_LIMIT");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    /* check joint range */
	    if (joint == 0) {
		break;
	    }
	    joint->acc_limit = inst->command->acc;
	    break;

	case EMCMOT_SET_JOINT_JERK_LIMIT:
	    /* set joint max jerk (0 = disabled) */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_JERK_LIMIT");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    emcmot_config_change();
	    if (joint == 0) {
		break;
	    }
	    joint->jerk_limit = inst->command->jerk;
	    jerk_filter_recompute_window(inst);
	    break;

	case EMCMOT_SET_ACC:
	    /* set the max acceleration */
	    /* can do it at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_ACCEL");
	    inst->status->acc = inst->command->acc;
	    motmod_tp_api->set_amax(motmod_tp_api->ctx, inst->status->acc);
	    break;

	case EMCMOT_PAUSE:
	    /* pause the motion */
	    /* can happen at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "PAUSE");
	    motmod_tp_api->pause(motmod_tp_api->ctx);
	    inst->status->paused = 1;
	    break;

	case EMCMOT_REVERSE:
	    /* run motion in reverse*/
	    /* only allowed during a pause */
	    rtapi_print_msg(RTAPI_MSG_DBG, "REVERSE");
	    motmod_tp_api->set_run_dir(motmod_tp_api->ctx, TP_REVERSE);
	    break;

	case EMCMOT_FORWARD:
	    /* run motion in reverse*/
	    /* only allowed during a pause */
	    rtapi_print_msg(RTAPI_MSG_DBG, "FORWARD");
	    motmod_tp_api->set_run_dir(motmod_tp_api->ctx, TP_FORWARD);
	    break;

	case EMCMOT_RESUME:
	    /* resume paused motion */
	    /* can happen at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "RESUME");
	    inst->status->stepping = 0;
	    motmod_tp_api->resume(motmod_tp_api->ctx);
	    inst->status->paused = 0;
	    break;

	case EMCMOT_STEP:
	    /* resume paused motion until id changes */
	    /* can happen at any time */
            rtapi_print_msg(RTAPI_MSG_DBG, "STEP");
            if(inst->status->paused) {
                inst->internal->idForStep = inst->status->id;
                inst->status->stepping = 1;
                motmod_tp_api->resume(motmod_tp_api->ctx);
                inst->status->paused = 1;
            } else {
		rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: can't STEP while already executing"));
	    }
	    break;

	case EMCMOT_FEED_SCALE:
	    /* override speed */
	    /* can happen at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "FEED SCALE");
	    if (inst->command->scale < 0.0) {
		inst->command->scale = 0.0;	/* clamp it */
	    }
	    inst->status->feed_scale = inst->command->scale;
	    break;

	case EMCMOT_RAPID_SCALE:
	    /* override rapids */
	    /* can happen at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "RAPID SCALE");
	    if (inst->command->scale < 0.0) {
		inst->command->scale = 0.0;	/* clamp it */
	    }
	    inst->status->rapid_scale = inst->command->scale;
	    break;

	case EMCMOT_FS_ENABLE:
	    /* enable/disable overriding speed */
	    /* can happen at any time */
	    if ( inst->command->mode != 0 ) {
		rtapi_print_msg(RTAPI_MSG_DBG, "FEED SCALE: ON");
		inst->status->enables_new |= FS_ENABLED;
            } else {
		rtapi_print_msg(RTAPI_MSG_DBG, "FEED SCALE: OFF");
		inst->status->enables_new &= ~FS_ENABLED;
	    }
	    break;

	case EMCMOT_FH_ENABLE:
	    /* enable/disable feed hold */
	    /* can happen at any time */
	    if ( inst->command->mode != 0 ) {
		rtapi_print_msg(RTAPI_MSG_DBG, "FEED HOLD: ENABLED");
		inst->status->enables_new |= FH_ENABLED;
            } else {
		rtapi_print_msg(RTAPI_MSG_DBG, "FEED HOLD: DISABLED");
		inst->status->enables_new &= ~FH_ENABLED;
	    }
	    break;

	case EMCMOT_SPINDLE_SCALE:
	    /* override spindle speed */
	    /* can happen at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE SCALE");
	    if (inst->command->scale < 0.0) {
		inst->command->scale = 0.0;	/* clamp it */
	    }
	    inst->status->spindle_status[inst->command->spindle].scale = inst->command->scale;
	    break;

	case EMCMOT_SS_ENABLE:
	    /* enable/disable overriding spindle speed */
	    /* can happen at any time */
	    if ( inst->command->mode != 0 ) {
		rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE SCALE: ON");
		inst->status->enables_new |= SS_ENABLED;
            } else {
		rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE SCALE: OFF");
		inst->status->enables_new &= ~SS_ENABLED;
	    }
	    break;

	case EMCMOT_AF_ENABLE:
	    /* enable/disable adaptive feedrate override from HAL pin */
	    /* can happen at any time */
	    if ( inst->command->flags != 0 ) {
		rtapi_print_msg(RTAPI_MSG_DBG, "ADAPTIVE FEED: ON");
		inst->status->enables_new |= AF_ENABLED;
            } else {
		rtapi_print_msg(RTAPI_MSG_DBG, "ADAPTIVE FEED: OFF");
		inst->status->enables_new &= ~AF_ENABLED;
	    }
	    break;

	case EMCMOT_DISABLE:
	    /* go into disable */
	    /* can happen at any time */
	    /* reset the inst->internal->enabling flag to defer disable until
	       controller cycle (it *will* be honored) */
	    rtapi_print_msg(RTAPI_MSG_DBG, "DISABLE");
	    inst->internal->enabling = 0;
	    if (inst->config->kinType == KINEMATICS_INVERSE_ONLY) {
		inst->internal->teleoperating = 0;
		inst->internal->coordinating = 0;
	    }
	    break;

	case EMCMOT_ENABLE:
	    /* come out of disable */
	    /* can happen at any time */
	    /* set the inst->internal->enabling flag to defer enable until
	       controller cycle */
	    rtapi_print_msg(RTAPI_MSG_DBG, "ENABLE");
	    if ( *(inst->hal_data->enable) == 0 ) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("can't enable motion, enable input is false"));
	    } else {
		inst->internal->enabling = 1;
		if (inst->config->kinType == KINEMATICS_INVERSE_ONLY) {
		    inst->internal->teleoperating = 0;
		    inst->internal->coordinating = 0;
		}
	    }
	    break;

	case EMCMOT_JOINT_ACTIVATE:
	    /* make joint active, so that amps will be enabled when system is
	       enabled or disabled */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOINT_ACTIVATE");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (joint == 0) {
		break;
	    }
	    SET_JOINT_ACTIVE_FLAG(joint, 1);
	    break;

	case EMCMOT_JOINT_DEACTIVATE:
	    /* make joint inactive, so that amps won't be affected when system
	       is enabled or disabled */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOINT_DEACTIVATE");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (joint == 0) {
		break;
	    }
	    SET_JOINT_ACTIVE_FLAG(joint, 0);
	    break;
	case EMCMOT_JOINT_ENABLE_AMPLIFIER:
	    /* enable the amplifier directly, but don't enable calculations */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOINT_ENABLE_AMP");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (joint == 0) {
		break;
	    }
	    break;

	case EMCMOT_JOINT_DISABLE_AMPLIFIER:
	    /* disable the joint calculations and amplifier, but don't disable
	       calculations */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOINT_DISABLE_AMP");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);
	    if (joint == 0) {
		break;
	    }
	    break;

	case EMCMOT_JOINT_HOME:
	    /* home the specified joint */
	    /* need to be in free mode, enable on */
	    /* this just sets the initial state, then the state machine in
	       homing.c does the rest */
	    rtapi_print_msg(RTAPI_MSG_DBG, "JOINT_HOME");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);

	    if (inst->status->motion_state != EMCMOT_MOTION_FREE) {
		/* can't home unless in free mode */
		rtapi_print_msg(RTAPI_MSG_ERR, _("must be in joint mode to home"));
		return;
	    }
	    if (*(inst->hal_data->homing_inhibit)) {
	        rtapi_print_msg(RTAPI_MSG_ERR, _("Homing denied by motion.homing-inhibit joint=%d\n"),
	                   joint_num);
                return;
	    }

	    if (!GET_MOTION_ENABLE_FLAG()) {
		break;
	    }

	    // Negative joint_num specifies homeall
	    motmod_home_api->do_home_joint(motmod_home_api->ctx, joint_num);
	    break;

	case EMCMOT_JOINT_UNHOME:
            /* unhome the specified joint, or all joints if -1 */
            rtapi_print_msg(RTAPI_MSG_DBG, "JOINT_UNHOME");
            rtapi_print_msg(RTAPI_MSG_DBG, " %d", joint_num);

            if (   (inst->status->motion_state != EMCMOT_MOTION_FREE)
                && (inst->status->motion_state != EMCMOT_MOTION_DISABLED)) {
                rtapi_print_msg(RTAPI_MSG_ERR, _("must be in joint mode or disabled to unhome"));
                return;
            }

            //Negative joint_num specifies unhome_method (-1,-2)
            motmod_home_api->set_unhomed(motmod_home_api->ctx, joint_num, (home_motion_state_t)inst->status->motion_state);
            break;

	case EMCMOT_CLEAR_PROBE_FLAGS:
	    rtapi_print_msg(RTAPI_MSG_DBG, "CLEAR_PROBE_FLAGS");
	    inst->status->probing = 0;
            inst->status->probeTripped = 0;
	    break;

	case EMCMOT_PROBE:
	    /* most of this is taken from EMCMOT_SET_LINE */
	    /* inst->internal->coord_tp up a linear move */
	    /* requires coordinated mode, enable off, not on limits */
	    rtapi_print_msg(RTAPI_MSG_DBG, "PROBE");
	    if (!GET_MOTION_COORD_FLAG() || !GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("need to be enabled, in coord mode for probe move"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!inRange(inst, inst->command->pos, inst->command->id, "Probe")) {
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!limits_ok(inst)) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("can't do probe move with limits exceeded"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!(inst->command->probe_type & 1)) {
                // if suppress errors = off...

                int probeval = !!*(inst->hal_data->probe_input);
                int probe_whenclears = !!(inst->command->probe_type & 2);

                if (probeval != probe_whenclears) {
                    // the probe is already in the state we're seeking.
                    if(probe_whenclears)
                        rtapi_print_msg(RTAPI_MSG_ERR, _("Probe is already clear when starting G38.4 or G38.5 move"));
                    else
                        rtapi_print_msg(RTAPI_MSG_ERR, _("Probe is already tripped when starting G38.2 or G38.3 move"));

                    inst->status->commandStatus = EMCMOT_COMMAND_BAD_EXEC;
                    motmod_tp_api->abort(motmod_tp_api->ctx);
                    SET_MOTION_ERROR_FLAG(1);
                    break;
                }
            }

	    /* append it to the inst->internal->coord_tp */
	    motmod_tp_api->set_id(motmod_tp_api->ctx, inst->command->id);
	    if (-1 == motmod_tp_api->add_line(motmod_tp_api->ctx, 
				(const tp_pose_t *)&inst->command->pos,
				inst->command->motion_type,
				inst->command->vel,
				inst->command->ini_maxvel,
				inst->command->acc,
				inst->status->enables_new,
				0,
				-1,
				(const tp_state_tag_t *)&inst->command->tag)) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("can't add probe move"));
		inst->status->commandStatus = EMCMOT_COMMAND_BAD_EXEC;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else {
		inst->status->probing = 1;
                inst->status->probe_type = inst->command->probe_type;
		SET_MOTION_ERROR_FLAG(0);
		/* set flag that indicates all joints need rehoming, if any
		   joint is moved in joint mode, for machines with no forward
		   kins */
		rehomeAll = 1;
	    }
	    break;

	case EMCMOT_RIGID_TAP:
	    /* most of this is taken from EMCMOT_SET_LINE */
	    /* inst->internal->coord_tp up a linear move */
	    /* requires coordinated mode, enable off, not on limits */
	    rtapi_print_msg(RTAPI_MSG_DBG, "RIGID_TAP");
	    if (!GET_MOTION_COORD_FLAG() || !GET_MOTION_ENABLE_FLAG()) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("need to be enabled, in coord mode for rigid tap move"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!inRange(inst, inst->command->pos, inst->command->id, "Rigid tap")) {
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else if (!limits_ok(inst)) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("can't do rigid tap move with limits exceeded"));
		inst->status->commandStatus = EMCMOT_COMMAND_INVALID_PARAMS;
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    }

	    /* append it to the inst->internal->tp */
	    motmod_tp_api->set_id(motmod_tp_api->ctx, inst->command->id);
        int res_addtap = motmod_tp_api->add_rigid_tap(motmod_tp_api->ctx, 
                                    (const tp_pose_t *)&inst->command->pos,
                                    inst->command->vel,
                                    inst->command->ini_maxvel,
                                    inst->command->acc,
                                    inst->status->enables_new,
                                    inst->command->scale,
                                    (const tp_state_tag_t *)&inst->command->tag);
        if (res_addtap < 0) {
            inst->status->atspeed_next_feed = 0; /* rigid tap always waits for spindle to be at-speed */
            rtapi_print_msg(RTAPI_MSG_ERR, _("can't add rigid tap move at line %d, error code %d"),
                    inst->command->id, res_addtap);
		motmod_tp_api->abort(motmod_tp_api->ctx);
		SET_MOTION_ERROR_FLAG(1);
		break;
	    } else {
		SET_MOTION_ERROR_FLAG(0);
	    }
	    break;

	case EMCMOT_SET_DEBUG:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_DEBUG");
	    inst->config->debug = inst->command->debug;
	    emcmot_config_change();
	    break;

	/* needed for synchronous I/O */
	case EMCMOT_SET_AOUT:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_AOUT");
	    if (inst->command->now) { //we set it right away
		emcmotAioWrite(inst, inst->command->out, inst->command->minLimit);
	    } else { // we put it on the TP queue, warning: only room for one in there, any new ones will overwrite
		motmod_tp_api->set_aout(motmod_tp_api->ctx, inst->command->out,
		    inst->command->minLimit, inst->command->maxLimit);
	    }
	    break;

	case EMCMOT_SET_DOUT:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_DOUT");
	    if (inst->command->now) { //we set it right away
		emcmotDioWrite(inst, inst->command->out, inst->command->start);
	    } else { // we put it on the TP queue, warning: only room for one in there, any new ones will overwrite
		motmod_tp_api->set_dout(motmod_tp_api->ctx, inst->command->out,
		    inst->command->start, inst->command->end);
	    }
	    break;

    case EMCMOT_SET_SPINDLE_PARAMS:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_SETUP: spindle %d/%d max_pos %f min_pos %f"
                "max_neg %f min_neg %f, home: %f, %f, %d\n",
                        inst->command->spindle, inst->config->numSpindles, inst->command->maxLimit,
                        inst->command->min_pos_speed, inst->command->max_neg_speed, inst->command->minLimit,
                        inst->command->search_vel, inst->command->home, inst->command->home_sequence);
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to configure non-existent spindle"));
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        inst->status->spindle_status[spindle_num].max_pos_speed = inst->command->maxLimit;
        inst->status->spindle_status[spindle_num].min_neg_speed = inst->command->minLimit;
        inst->status->spindle_status[spindle_num].max_neg_speed = inst->command->max_neg_speed;
        inst->status->spindle_status[spindle_num].min_pos_speed = inst->command->min_pos_speed;
        inst->status->spindle_status[spindle_num].home_search_vel = inst->command->search_vel;
        inst->status->spindle_status[spindle_num].home_sequence = inst->command->home_sequence;
        inst->status->spindle_status[spindle_num].increment = inst->command->offset;

        break;
	case EMCMOT_SPINDLE_ON:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_ON: spindle %d/%d speed %d\n",
                        inst->command->spindle, inst->config->numSpindles, (int) inst->command->vel);
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to start non-existent spindle"));
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){

	        if (*(inst->hal_data->spindle[n].spindle_orient))
	    	rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_ORIENT cancelled by SPINDLE_ON\n");
	        if (*(inst->hal_data->spindle[n].spindle_locked))
		    rtapi_print_msg(RTAPI_MSG_DBG, "spindle-locked cleared by SPINDLE_ON\n");
	        *(inst->hal_data->spindle[n].spindle_locked) = 0;
	        *(inst->hal_data->spindle[n].spindle_orient) = 0;
	        inst->status->spindle_status[n].orient_state = EMCMOT_ORIENT_NONE;

	        /* if (inst->status->spindle.orient) { */
	        /* 	rtapi_print_msg(RTAPI_MSG_ERR, _("can\'t turn on spindle during orient in progress")); */
	        /* 	inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND; */
	        /* 	motmod_tp_api->abort(&inst->internal->tp); */
	        /* 	SET_MOTION_ERROR_FLAG(1); */
	        /* } else {...} */
	        rtapi_print_msg(RTAPI_MSG_DBG, "command state %d\n", inst->command->state);
	        inst->status->spindle_status[n].state = inst->command->state;
	        inst->status->spindle_status[n].speed = inst->command->vel;
	        inst->status->spindle_status[n].css_factor = inst->command->ini_maxvel;
	        inst->status->spindle_status[n].xoffset = inst->command->acc;
            if (inst->command->state) {
	            if (inst->command->vel >= 0) {
		        inst->status->spindle_status[n].direction = 1;
	            } else {
		        inst->status->spindle_status[n].direction = -1;
	            }
	            inst->status->spindle_status[n].brake = 0; //disengage brake
            }
            apply_spindle_limits(&inst->status->spindle_status[n]);
        }
        inst->status->atspeed_next_feed = inst->command->wait_for_spindle_at_speed;

       // check whether it's passed correctly
       if (!inst->status->atspeed_next_feed){
           rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_ON without wait-for-atspeed");
       }
	   break;

	case EMCMOT_SPINDLE_OFF:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_OFF");
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to stop non-existent spindle <%d>"),spindle_num);
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){

	        inst->status->spindle_status[n].state = 0;
	        inst->status->spindle_status[n].speed = 0;
	        inst->status->spindle_status[n].direction = 0;
	        inst->status->spindle_status[n].brake = 1; // engage brake
	        if (*(inst->hal_data->spindle[n].spindle_orient))
		    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_ORIENT cancelled by SPINDLE_OFF");
	        if (*(inst->hal_data->spindle[n].spindle_locked)){
		    rtapi_print_msg(RTAPI_MSG_DBG, "spindle-locked cleared by SPINDLE_OFF");
	            *(inst->hal_data->spindle[n].spindle_locked) = 0;
            }
	        *(inst->hal_data->spindle[n].spindle_orient) = 0;
	        inst->status->spindle_status[n].orient_state = EMCMOT_ORIENT_NONE;
        }
	    break;

	case EMCMOT_SPINDLE_ORIENT:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_ORIENT");
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to orient non-existent spindle <%d>"),spindle_num);
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){

	        if (n > inst->config->numSpindles){
                rtapi_print_msg(RTAPI_MSG_ERR, "spindle number <%d> too high in M19",n);
                break;
	        }
	        if (*(inst->hal_data->spindle[n].spindle_orient)) {
		    rtapi_print_msg(RTAPI_MSG_DBG, "orient already in progress");

		    // mah:FIXME unsure whether this is ok or an error
		    /* rtapi_print_msg(RTAPI_MSG_ERR, _("orient already in progress")); */
		    /* inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND; */
		    /* motmod_tp_api->abort(&inst->internal->tp); */
		    /* SET_MOTION_ERROR_FLAG(1); */
	        }
	        inst->status->spindle_status[n].orient_state = EMCMOT_ORIENT_IN_PROGRESS;
	        inst->status->spindle_status[n].speed = 0;
	        inst->status->spindle_status[n].direction = 0;
	        // so far like spindle stop, except opening brake
	        inst->status->spindle_status[n].brake = 0; // open brake

            // https://github.com/LinuxCNC/linuxcnc/issues/3389
            inst->status->spindle_status[n].state = 0;
            *(inst->hal_data->spindle[n].spindle_on) = 0;
            // https://github.com/LinuxCNC/linuxcnc/issues/3389

	        *(inst->hal_data->spindle[n].spindle_orient_angle) = inst->command->orientation;
	        *(inst->hal_data->spindle[n].spindle_orient_mode) = inst->command->mode;
	        *(inst->hal_data->spindle[n].spindle_locked) = 0;
	        *(inst->hal_data->spindle[n].spindle_orient) = 1;

	        // mirror in spindle status
	        inst->status->spindle_status[n].orient_fault = 0; // this pin read during spindle-orient == 1
	        inst->status->spindle_status[n].locked = 0;
        }
	    break;

	case EMCMOT_SPINDLE_INCREASE:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_INCREASE");
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to increase non-existent spindle <%d>"),spindle_num);
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){
	        if (inst->status->spindle_status[n].speed > 0) {
		    inst->status->spindle_status[n].speed += inst->status->spindle_status[n].increment;
	        } else if (inst->status->spindle_status[n].speed < 0) {
		    inst->status->spindle_status[n].speed -= inst->status->spindle_status[n].increment;
	        }
            apply_spindle_limits(&inst->status->spindle_status[n]);
        }
	    break;

	case EMCMOT_SPINDLE_DECREASE:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_DECREASE");
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to decrease non-existent spindle <%d>."),spindle_num);
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){
            if (inst->status->spindle_status[n].speed > inst->status->spindle_status[n].increment) {
                inst->status->spindle_status[n].speed -= inst->status->spindle_status[n].increment;
            } else if (inst->status->spindle_status[n].speed < -1.0 * inst->status->spindle_status[n].increment) {
                inst->status->spindle_status[n].speed += inst->status->spindle_status[n].increment;
            }
            apply_spindle_limits(&inst->status->spindle_status[n]);
        }
        break;

	case EMCMOT_SPINDLE_BRAKE_ENGAGE:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_BRAKE_ENGAGE");
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to engage brake of non-existent spindle <%d>"),spindle_num);
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){

	        inst->status->spindle_status[n].speed = 0;
	        inst->status->spindle_status[n].direction = 0;
	        inst->status->spindle_status[n].brake = 1;
        }
	    break;

	case EMCMOT_SPINDLE_BRAKE_RELEASE:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SPINDLE_BRAKE_RELEASE");
	    spindle_num = inst->command->spindle;
        if (spindle_num >= inst->config->numSpindles){
            rtapi_print_msg(RTAPI_MSG_ERR, _("Attempt to release brake of non-existent spindle <%d>"),spindle_num);
            inst->status->commandStatus = EMCMOT_COMMAND_INVALID_COMMAND;
            break;
        }
        s0 = spindle_num;
        s1 = spindle_num;
        if (spindle_num ==-1){
            s0 = 0;
            s1 = motion_num_spindles - 1;
        }
        for (n = s0; n<=s1; n++){

	        inst->status->spindle_status[n].brake = 0;
        }
	    break;

	case EMCMOT_SET_JOINT_COMP:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_JOINT_COMP for joint %d", joint_num);
	    if (joint == 0) {
		break;
	    }
	    if (joint->comp.entries >= EMCMOT_COMP_SIZE) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("joint %d: too many compensation entries"), joint_num);
		break;
	    }
	    /* point to last entry */
	    comp_entry = &(joint->comp.array[joint->comp.entries]);
	    if (inst->command->comp_nominal <= comp_entry[0].nominal) {
		rtapi_print_msg(RTAPI_MSG_ERR, _("joint %d: compensation values must increase"), joint_num);
		break;
	    }
	    /* store data to new entry */
	    comp_entry[1].nominal = inst->command->comp_nominal;
	    comp_entry[1].fwd_trim = inst->command->comp_forward;
	    comp_entry[1].rev_trim = inst->command->comp_reverse;
	    /* calculate slopes from previous entry to the new one */
	    if ( comp_entry[0].nominal != -DBL_MAX ) {
		/* but only if the previous entry is "real" */
		tmp1 = comp_entry[1].nominal - comp_entry[0].nominal;
		comp_entry[0].fwd_slope =
		    (comp_entry[1].fwd_trim - comp_entry[0].fwd_trim) / tmp1;
		comp_entry[0].rev_slope =
		    (comp_entry[1].rev_trim - comp_entry[0].rev_trim) / tmp1;
	    } else {
		/* previous entry is at minus infinity, slopes are zero */
		comp_entry[0].fwd_trim = comp_entry[1].fwd_trim;
		comp_entry[0].rev_trim = comp_entry[1].rev_trim;
	    }
	    joint->comp.entries++;
	    break;

        case EMCMOT_SET_OFFSET:
            inst->status->tool_offset = inst->command->tool_offset;
            break;

	case EMCMOT_SET_AXIS_POSITION_LIMITS:
	    /* set the position limits for axis */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_AXIS_POSITION_LIMITS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", inst->command->axis);
	    emcmot_config_change();
            if ((inst->command->axis < 0) || (inst->command->axis >= EMCMOT_MAX_AXIS)) {
                break;
            }
            axis_set_min_pos_limit(ai, inst->command->axis, inst->command->minLimit);
            axis_set_max_pos_limit(ai, inst->command->axis, inst->command->maxLimit);
	    break;

        case EMCMOT_SET_AXIS_VEL_LIMIT:
	    /* set the max axis vel */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_AXIS_VEL_LIMITS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", inst->command->axis);
	    emcmot_config_change();
            if ((inst->command->axis < 0) || (inst->command->axis >= EMCMOT_MAX_AXIS)) {
                break;
            }
            axis_set_vel_limit(ai, inst->command->axis, inst->command->vel);
            axis_set_ext_offset_vel_limit(ai, inst->command->axis, inst->command->ext_offset_vel);
            break;

        case EMCMOT_SET_AXIS_ACC_LIMIT:
 	    /* set the max axis acc */
	    /* can be done at any time */
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_AXIS_ACC_LIMITS");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", inst->command->axis);
	    emcmot_config_change();
            if ((inst->command->axis < 0) || (inst->command->axis >= EMCMOT_MAX_AXIS)) {
                break;
            }
            axis_set_acc_limit(ai, inst->command->axis, inst->command->acc);
            axis_set_ext_offset_acc_limit(ai, inst->command->axis, inst->command->ext_offset_acc);
            break;

        case EMCMOT_SET_AXIS_LOCKING_JOINT:
	    rtapi_print_msg(RTAPI_MSG_DBG, "SET_AXIS_ACC_LOCKING_JOINT");
	    rtapi_print_msg(RTAPI_MSG_DBG, " %d", inst->command->axis);
	    emcmot_config_change();
            if ((inst->command->axis < 0) || (inst->command->axis >= EMCMOT_MAX_AXIS)) {
                break;
            }
            axis_set_locking_joint(ai, inst->command->axis, joint_num);
            break;

	default:
	    rtapi_print_msg(RTAPI_MSG_DBG, "UNKNOWN");
	    rtapi_print_msg(RTAPI_MSG_ERR, _("unrecognized command %d"), inst->command->command);
	    inst->status->commandStatus = EMCMOT_COMMAND_UNKNOWN_COMMAND;
	    break;
        case EMCMOT_SET_MAX_FEED_OVERRIDE:
            inst->config->maxFeedScale = inst->command->maxFeedScale;
            break;
        case EMCMOT_SETUP_ARC_BLENDS:
            inst->config->arcBlendEnable = inst->command->arcBlendEnable;
            inst->config->arcBlendFallbackEnable = inst->command->arcBlendFallbackEnable;
            inst->config->arcBlendOptDepth = inst->command->arcBlendOptDepth;
            inst->config->arcBlendGapCycles = inst->command->arcBlendGapCycles;
            inst->config->arcBlendRampFreq = inst->command->arcBlendRampFreq;
            inst->config->arcBlendTangentKinkRatio = inst->command->arcBlendTangentKinkRatio;
            break;
        case EMCMOT_SET_PROBE_ERR_INHIBIT:
            inst->config->inhibit_probe_jog_error = inst->command->probe_jog_err_inhibit;
            inst->config->inhibit_probe_home_error = inst->command->probe_home_err_inhibit;
            break;

	}			/* end of: command switch */
	if (inst->status->commandStatus != EMCMOT_COMMAND_OK) {
	    rtapi_print_msg(RTAPI_MSG_DBG, "ERROR: %d",
		inst->status->commandStatus);
	}
	rtapi_print_msg(RTAPI_MSG_DBG, "\n");
	/* synch tail count */
	inst->status->tail = inst->status->head;
	inst->config->tail = inst->config->head;
	inst->internal->tail = inst->internal->head;

    }
    /* end of: if-new-command */

    return;
}


void emcmotCommandHandler(void *arg, long servo_period) {
    motmod_inst_t *inst = (motmod_inst_t *)arg;
    if (rtapi_mutex_try(&inst->mot_struct->command_mutex) != 0) {
        // Failed to take the mutex, because it is held by Task.
        // This means Task is in the process of updating the command.
        // Give up for now, and try again on the next invocation.
        return;
    }
    emcmotCommandHandler_locked(arg, servo_period);
    rtapi_mutex_give(&inst->mot_struct->command_mutex);
}
