// emccmd_handlers.cc — GMI emccmd callback implementations for milltask.
//
// Each handler constructs the corresponding NML message struct and submits
// it synchronously.  emccmd_submit() blocks until milltask processes the
// command and returns the RCS_STATUS result.

#include "emc.hh"
#include "emc_nml.hh"
#include "emccmd_slot.hh"
#include "emcglb.h"
#include <rtapi_string.h>

#include "gomc/generated/gmi/emccmd/emccmd_api.h"

// --- Command handlers ---

static int32_t cmd_set_state(void *ctx, int32_t state)
{
    EMC_TASK_SET_STATE msg;
    msg.state = (enum EMC_TASK_STATE_ENUM)state;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_mode(void *ctx, int32_t mode)
{
    EMC_TASK_SET_MODE msg;
    msg.mode = (enum EMC_TASK_MODE_ENUM)mode;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_auto_cmd(void *ctx, emccmd_auto_cmd_t cmd, int32_t line)
{
    switch (cmd) {
    case EMCCMD_AUTO_RUN: {
        EMC_TASK_PLAN_RUN msg;
        msg.line = line;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_AUTO_PAUSE: {
        EMC_TASK_PLAN_PAUSE msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_AUTO_RESUME: {
        EMC_TASK_PLAN_RESUME msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_AUTO_STEP: {
        EMC_TASK_PLAN_STEP msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_AUTO_REVERSE: {
        EMC_TASK_PLAN_REVERSE msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_AUTO_FORWARD: {
        EMC_TASK_PLAN_FORWARD msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
    default:
        return -1;
    }
}

static int32_t cmd_mdi(void *ctx, const char *command)
{
    EMC_TASK_PLAN_EXECUTE msg;
    rtapi_strxcpy(msg.command, command);
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_jog(void *ctx, emccmd_jog_type_t jog_type, bool jjogmode,
                        int32_t axis_or_joint, double velocity, double distance)
{
    switch (jog_type) {
    case EMCCMD_JOG_STOP: {
        EMC_JOG_STOP msg;
        msg.joint_or_axis = axis_or_joint;
        msg.jjogmode = jjogmode ? 1 : 0;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_JOG_CONTINUOUS: {
        EMC_JOG_CONT msg;
        msg.joint_or_axis = axis_or_joint;
        msg.vel = velocity;
        msg.jjogmode = jjogmode ? 1 : 0;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_JOG_INCREMENT: {
        EMC_JOG_INCR msg;
        msg.joint_or_axis = axis_or_joint;
        msg.vel = velocity;
        msg.incr = distance;
        msg.jjogmode = jjogmode ? 1 : 0;
        return emccmd_submit(&msg, sizeof(msg));
    }
    default:
        return -1;
    }
}

static int32_t cmd_jog_stop(void *ctx, bool jjogmode, int32_t axis_or_joint)
{
    EMC_JOG_STOP msg;
    msg.joint_or_axis = axis_or_joint;
    msg.jjogmode = jjogmode ? 1 : 0;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_spindle(void *ctx, emccmd_spindle_cmd_t cmd, double speed,
                            int32_t spindle_num, int32_t wait)
{
    switch (cmd) {
    case EMCCMD_SPINDLE_FORWARD: {
        EMC_SPINDLE_ON msg;
        msg.speed = speed;
        msg.spindle = spindle_num;
        msg.wait_for_spindle_at_speed = wait;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_SPINDLE_REVERSE: {
        EMC_SPINDLE_ON msg;
        msg.speed = -speed;
        msg.spindle = spindle_num;
        msg.wait_for_spindle_at_speed = wait;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_SPINDLE_OFF: {
        EMC_SPINDLE_OFF msg;
        msg.spindle = spindle_num;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_SPINDLE_INCREASE: {
        EMC_SPINDLE_INCREASE msg;
        msg.spindle = spindle_num;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_SPINDLE_DECREASE: {
        EMC_SPINDLE_DECREASE msg;
        msg.spindle = spindle_num;
        return emccmd_submit(&msg, sizeof(msg));
    }
    case EMCCMD_SPINDLE_CONSTANT: {
        EMC_SPINDLE_CONSTANT msg;
        msg.spindle = spindle_num;
        return emccmd_submit(&msg, sizeof(msg));
    }
    default:
        return -1;
    }
}

static int32_t cmd_home(void *ctx, int32_t joint)
{
    EMC_JOINT_HOME msg;
    msg.joint = joint;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_unhome(void *ctx, int32_t joint)
{
    EMC_JOINT_UNHOME msg;
    msg.joint = joint;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_override_limits(void *ctx)
{
    EMC_JOINT_OVERRIDE_LIMITS msg;
    msg.joint = 0;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_teleop_enable(void *ctx, bool enable)
{
    EMC_TRAJ_SET_TELEOP_ENABLE msg;
    msg.enable = enable;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_feed_override(void *ctx, double rate)
{
    EMC_TRAJ_SET_SCALE msg;
    msg.scale = rate;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_spindle_override(void *ctx, double rate,
                                         int32_t spindle_num)
{
    EMC_TRAJ_SET_SPINDLE_SCALE msg;
    msg.spindle = spindle_num;
    msg.scale = rate;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_rapid_override(void *ctx, double rate)
{
    EMC_TRAJ_SET_RAPID_SCALE msg;
    msg.scale = rate;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_max_velocity(void *ctx, double velocity)
{
    EMC_TRAJ_SET_MAX_VELOCITY msg;
    msg.velocity = velocity;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_flood(void *ctx, bool on)
{
    if (on) {
        EMC_COOLANT_FLOOD_ON msg;
        return emccmd_submit(&msg, sizeof(msg));
    } else {
        EMC_COOLANT_FLOOD_OFF msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
}

static int32_t cmd_mist(void *ctx, bool on)
{
    if (on) {
        EMC_COOLANT_MIST_ON msg;
        return emccmd_submit(&msg, sizeof(msg));
    } else {
        EMC_COOLANT_MIST_OFF msg;
        return emccmd_submit(&msg, sizeof(msg));
    }
}

static int32_t cmd_brake(void *ctx, bool on, int32_t spindle_num)
{
    if (on) {
        EMC_SPINDLE_BRAKE_ENGAGE msg;
        msg.spindle = spindle_num;
        return emccmd_submit(&msg, sizeof(msg));
    } else {
        EMC_SPINDLE_BRAKE_RELEASE msg;
        msg.spindle = spindle_num;
        return emccmd_submit(&msg, sizeof(msg));
    }
}

static int32_t cmd_abort(void *ctx)
{
    EMC_TASK_ABORT msg;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_task_plan_synch(void *ctx)
{
    EMC_TASK_PLAN_SYNCH msg;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_optional_stop(void *ctx, bool on)
{
    EMC_TASK_PLAN_SET_OPTIONAL_STOP msg;
    msg.state = on ? 1 : 0;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_set_block_delete(void *ctx, bool on)
{
    EMC_TASK_PLAN_SET_BLOCK_DELETE msg;
    msg.state = on ? 1 : 0;
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_load_tool_table(void *ctx)
{
    EMC_TOOL_LOAD_TOOL_TABLE msg;
    msg.file[0] = '\0';
    return emccmd_submit(&msg, sizeof(msg));
}

static int32_t cmd_program_open(void *ctx, const char *file)
{
    // Close current program first, then open new one.
    {
        EMC_TASK_PLAN_CLOSE msg;
        int rc = emccmd_submit(&msg, sizeof(msg));
        if (rc == RCS_ERROR) return rc;
    }
    {
        EMC_TASK_PLAN_OPEN msg;
        rtapi_strxcpy(msg.file, file);
        return emccmd_submit(&msg, sizeof(msg));
    }
}

static int32_t cmd_wait_complete(void *ctx, double timeout)
{
    // With synchronous command dispatch, wait_complete is a no-op:
    // each command already blocks until milltask processes it.
    (void)ctx;
    (void)timeout;
    return RCS_DONE;
}

static int32_t cmd_set_debug(void *ctx, int32_t debug)
{
    EMC_SET_DEBUG msg;
    msg.debug = debug;
    return emccmd_submit(&msg, sizeof(msg));
}

// --- Callbacks table ---

extern const emccmd_callbacks_t emccmd_handler_table = {
    .ctx = nullptr,
    .set_state = cmd_set_state,
    .set_mode = cmd_set_mode,
    .auto_cmd = cmd_auto_cmd,
    .mdi = cmd_mdi,
    .jog = cmd_jog,
    .jog_stop = cmd_jog_stop,
    .spindle = cmd_spindle,
    .home = cmd_home,
    .unhome = cmd_unhome,
    .override_limits = cmd_override_limits,
    .teleop_enable = cmd_teleop_enable,
    .set_feed_override = cmd_set_feed_override,
    .set_spindle_override = cmd_set_spindle_override,
    .set_rapid_override = cmd_set_rapid_override,
    .set_max_velocity = cmd_set_max_velocity,
    .flood = cmd_flood,
    .mist = cmd_mist,
    .brake = cmd_brake,
    .abort = cmd_abort,
    .task_plan_synch = cmd_task_plan_synch,
    .set_optional_stop = cmd_set_optional_stop,
    .set_block_delete = cmd_set_block_delete,
    .load_tool_table = cmd_load_tool_table,
    .program_open = cmd_program_open,
    .wait_complete = cmd_wait_complete,
    .set_debug = cmd_set_debug,
};
