/*
 * nml_shim.cc — extern "C" wrappers around the NML C++ API.
 *
 * Provides a flat C interface for the emcgateway gomod to access
 * NML stat/command/error channels via cgo.
 */

#include "nml_shim.h"

#include <cstring>
#include <cstdio>

#include "rcs.hh"
#include "emc_nml.hh"
#include "emc.hh"
#include "nml_oi.hh"
#include "emcglb.h"
#include "timer.hh"
#include "linuxcnc.h"

/* NML channels */
static RCS_STAT_CHANNEL *stat_channel = nullptr;
static RCS_CMD_CHANNEL  *cmd_channel  = nullptr;
static NML              *err_channel  = nullptr;

/* Command serial tracking for wait_complete */
static int cmd_serial = 0;

/* Forward declarations for NML format function */
extern int emcFormat(NMLTYPE type, void *buf, CMS *cms);

/* ─── Helper: EmcPose → nml_position_t ─── */

static void pose_to_pos(const EmcPose *src, nml_position_t *dst)
{
    dst->x = src->tran.x;
    dst->y = src->tran.y;
    dst->z = src->tran.z;
    dst->a = src->a;
    dst->b = src->b;
    dst->c = src->c;
    dst->u = src->u;
    dst->v = src->v;
    dst->w = src->w;
}

/* ─── Helper: send command and track serial ─── */

static int send_cmd(RCS_CMD_MSG *msg)
{
    if (!cmd_channel || !cmd_channel->valid())
        return -1;
    if (cmd_channel->write(msg))
        return -1;
    cmd_serial = msg->serial_number;
    return 0;
}

/* ─── Lifecycle ─── */

extern "C" int nml_shim_init(const char *nml_file)
{
    /* Set the global NML file path used by NML channel constructors */
    if (nml_file && nml_file[0]) {
        strncpy(emc_nmlfile, nml_file, sizeof(emc_nmlfile) - 1);
        emc_nmlfile[sizeof(emc_nmlfile) - 1] = '\0';
    }

    stat_channel = new RCS_STAT_CHANNEL(emcFormat, "emcStatus", "xemc", emc_nmlfile);
    if (!stat_channel || !stat_channel->valid()) {
        fprintf(stderr, "nml_shim: failed to open stat channel\n");
        return -1;
    }

    cmd_channel = new RCS_CMD_CHANNEL(emcFormat, "emcCommand", "xemc", emc_nmlfile);
    if (!cmd_channel || !cmd_channel->valid()) {
        fprintf(stderr, "nml_shim: failed to open command channel\n");
        return -1;
    }

    err_channel = new NML(emcFormat, "emcError", "xemc", emc_nmlfile);
    if (!err_channel || !err_channel->valid()) {
        fprintf(stderr, "nml_shim: failed to open error channel\n");
        return -1;
    }

    return 0;
}

extern "C" void nml_shim_shutdown(void)
{
    delete stat_channel; stat_channel = nullptr;
    delete cmd_channel;  cmd_channel  = nullptr;
    delete err_channel;  err_channel  = nullptr;
}

/* ─── Stat ─── */

extern "C" int nml_shim_poll_stat(nml_stat_t *out)
{
    if (!stat_channel || !stat_channel->valid())
        return -1;

    NMLTYPE type = stat_channel->peek();
    if (type != EMC_STAT_TYPE)
        return -1;

    EMC_STAT *st = static_cast<EMC_STAT *>(stat_channel->get_address());
    if (!st)
        return -1;

    memset(out, 0, sizeof(*out));

    /* Task */
    out->task_mode           = static_cast<int>(st->task.mode);
    out->task_state          = static_cast<int>(st->task.state);
    out->interp_state        = st->task.interpState;
    out->exec_state          = st->task.execState;
    strncpy(out->file, st->task.file, NML_SHIM_LINELEN - 1);
    strncpy(out->command, st->task.command, NML_SHIM_LINELEN - 1);
    out->motion_line         = st->task.motionLine;
    out->current_line        = st->task.currentLine;
    out->read_line           = st->task.readLine;
    out->queued_mdi_commands = st->task.queuedMDIcommands;
    out->optional_stop       = st->task.optional_stop_state;
    out->block_delete        = st->task.block_delete_state;
    out->task_paused         = st->task.task_paused;
    out->g5x_index           = st->task.g5x_index;

    /* Motion / trajectory */
    out->motion_mode    = static_cast<int>(st->motion.traj.mode);
    out->motion_enabled = st->motion.traj.enabled;
    out->in_position    = st->motion.traj.inpos;
    out->motion_paused  = st->motion.traj.paused;
    out->feedrate       = st->motion.traj.scale;
    out->rapidrate      = st->motion.traj.rapid_scale;
    out->max_velocity   = st->motion.traj.maxVelocity;
    out->velocity       = st->motion.traj.velocity;
    out->distance_to_go = st->motion.traj.distance_to_go;
    pose_to_pos(&st->motion.traj.dtg, &out->dtg);
    out->current_vel    = st->motion.traj.current_vel;
    out->motion_id      = st->motion.traj.id;
    out->motion_type    = st->motion.traj.motion_type;

    /* Positions */
    pose_to_pos(&st->motion.traj.position, &out->position);
    pose_to_pos(&st->motion.traj.actualPosition, &out->actual_position);
    pose_to_pos(&st->motion.traj.probedPosition, &out->probed_position);
    pose_to_pos(&st->task.g5x_offset, &out->g5x_offset);
    pose_to_pos(&st->task.g92_offset, &out->g92_offset);
    pose_to_pos(&st->task.toolOffset, &out->tool_offset);
    out->rotation_xy = st->task.rotation_xy;

    /* Joint data */
    out->joints_count    = st->motion.traj.joints;
    out->num_extrajoints = st->motion.numExtraJoints;
    for (int i = 0; i < NML_SHIM_MAX_JOINTS; i++) {
        const auto &jt = st->motion.joint[i];
        out->joints[i].homed           = jt.homed;
        out->joints[i].homing          = jt.homing;
        out->joints[i].enabled         = jt.enabled;
        out->joints[i].fault           = jt.fault;
        out->joints[i].min_soft_limit  = jt.minPositionLimit;
        out->joints[i].max_soft_limit  = jt.maxPositionLimit;
        out->joints[i].min_hard_limit  = jt.minHardLimit;
        out->joints[i].max_hard_limit  = jt.maxHardLimit;
        out->joints[i].override_limits = jt.overrideLimits;
        out->joints[i].velocity        = jt.velocity;
        out->joints[i].input           = jt.input;
        out->joints[i].output          = jt.output;

        /* Compute limit bitmask (same as emcmodule.cc) */
        int lim = 0;
        if (jt.minHardLimit) lim |= 1;
        if (jt.maxHardLimit) lim |= 2;
        if (jt.minSoftLimit) lim |= 4;
        if (jt.maxSoftLimit) lim |= 8;
        out->joints[i].limit = lim;

        out->homed[i]               = jt.homed;
        out->limit[i]               = lim;
        out->joint_actual_position[i] = jt.input;
    }

    /* Spindle data */
    for (int i = 0; i < NML_SHIM_MAX_SPINDLES; i++) {
        const auto &sp = st->motion.spindle[i];
        out->spindle[i].speed            = sp.speed;
        out->spindle[i].direction        = sp.direction;
        out->spindle[i].brake            = sp.brake;
        out->spindle[i].enabled          = sp.enabled;
        out->spindle[i].override         = sp.spindle_scale;
        out->spindle[i].override_enabled = sp.spindle_override_enabled;
        out->spindle[i].homed            = sp.homed;
        out->spindle[i].orient_state     = sp.orient_state;
        out->spindle[i].orient_fault     = sp.orient_fault;
    }

    /* Axis data */
    for (int i = 0; i < NML_SHIM_MAX_AXIS; i++) {
        const auto &ax = st->motion.axis[i];
        out->axis[i].velocity           = ax.velocity;
        out->axis[i].min_position_limit = ax.minPositionLimit;
        out->axis[i].max_position_limit = ax.maxPositionLimit;
    }

    /* G-codes, M-codes, settings */
    for (int i = 0; i < NML_SHIM_ACTIVE_G_CODES; i++)
        out->active_gcodes[i] = st->task.activeGCodes[i];
    for (int i = 0; i < NML_SHIM_ACTIVE_M_CODES; i++)
        out->active_mcodes[i] = st->task.activeMCodes[i];
    for (int i = 0; i < NML_SHIM_ACTIVE_SETTINGS; i++)
        out->active_settings[i] = st->task.activeSettings[i];

    /* Scalars */
    out->kinematics_type = st->motion.traj.kinematics_type;
    out->axis_mask       = st->motion.traj.axis_mask;
    out->flood           = st->io.coolant.flood;
    out->mist            = st->io.coolant.mist;
    out->tool_in_spindle = st->io.tool.toolInSpindle;
    out->pocket_prepped  = st->io.tool.pocketPrepped;
    out->linear_units    = st->motion.traj.linearUnits;

    /* RCS state */
    out->state               = st->status;
    out->echo_serial_number  = st->echo_serial_number;
    out->debug               = st->debug;

    return 0;
}

/* ─── Commands ─── */

extern "C" int nml_shim_set_state(int state)
{
    EMC_TASK_SET_STATE msg;
    msg.state = static_cast<enum EMC_TASK_STATE_ENUM>(state);
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_mode(int mode)
{
    EMC_TASK_SET_MODE msg;
    msg.mode = static_cast<enum EMC_TASK_MODE_ENUM>(mode);
    return send_cmd(&msg);
}

extern "C" int nml_shim_auto_cmd(int cmd, int line)
{
    switch (cmd) {
    case 0: { /* RUN */
        EMC_TASK_PLAN_RUN msg;
        msg.line = line;
        return send_cmd(&msg);
    }
    case 1: { /* PAUSE */
        EMC_TASK_PLAN_PAUSE msg;
        return send_cmd(&msg);
    }
    case 2: { /* RESUME */
        EMC_TASK_PLAN_RESUME msg;
        return send_cmd(&msg);
    }
    case 3: { /* STEP */
        EMC_TASK_PLAN_STEP msg;
        return send_cmd(&msg);
    }
    case 4: { /* REVERSE */
        EMC_TASK_PLAN_REVERSE msg;
        return send_cmd(&msg);
    }
    case 5: { /* FORWARD */
        EMC_TASK_PLAN_FORWARD msg;
        return send_cmd(&msg);
    }
    default:
        return -1;
    }
}

extern "C" int nml_shim_mdi(const char *command)
{
    EMC_TASK_PLAN_EXECUTE msg;
    strncpy(msg.command, command, sizeof(msg.command) - 1);
    msg.command[sizeof(msg.command) - 1] = '\0';
    return send_cmd(&msg);
}

extern "C" int nml_shim_jog(int jog_type, int jjogmode, int axis_or_joint,
                             double velocity, double distance)
{
    switch (jog_type) {
    case 0: /* STOP */
        return nml_shim_jog_stop(jjogmode, axis_or_joint);
    case 1: { /* CONTINUOUS */
        EMC_JOG_CONT msg;
        msg.joint_or_axis = axis_or_joint;
        msg.vel = velocity;
        msg.jjogmode = jjogmode;
        return send_cmd(&msg);
    }
    case 2: { /* INCREMENT */
        EMC_JOG_INCR msg;
        msg.joint_or_axis = axis_or_joint;
        msg.vel = velocity;
        msg.incr = distance;
        msg.jjogmode = jjogmode;
        return send_cmd(&msg);
    }
    default:
        return -1;
    }
}

extern "C" int nml_shim_jog_stop(int jjogmode, int axis_or_joint)
{
    EMC_JOG_STOP msg;
    msg.joint_or_axis = axis_or_joint;
    msg.jjogmode = jjogmode;
    return send_cmd(&msg);
}

extern "C" int nml_shim_spindle(int cmd, double speed, int spindle_num, int wait)
{
    switch (cmd) {
    case 1: { /* FORWARD */
        EMC_SPINDLE_ON msg;
        msg.speed = speed;
        msg.spindle = spindle_num;
        msg.wait_for_spindle_at_speed = wait;
        return send_cmd(&msg);
    }
    case -1: { /* REVERSE */
        EMC_SPINDLE_ON msg;
        msg.speed = -speed;
        msg.spindle = spindle_num;
        msg.wait_for_spindle_at_speed = wait;
        return send_cmd(&msg);
    }
    case 0: { /* OFF */
        EMC_SPINDLE_OFF msg;
        msg.spindle = spindle_num;
        return send_cmd(&msg);
    }
    case 10: { /* INCREASE */
        EMC_SPINDLE_INCREASE msg;
        msg.spindle = spindle_num;
        return send_cmd(&msg);
    }
    case 11: { /* DECREASE */
        EMC_SPINDLE_DECREASE msg;
        msg.spindle = spindle_num;
        return send_cmd(&msg);
    }
    case 12: { /* CONSTANT */
        EMC_SPINDLE_CONSTANT msg;
        msg.spindle = spindle_num;
        return send_cmd(&msg);
    }
    default:
        return -1;
    }
}

extern "C" int nml_shim_home(int joint)
{
    EMC_JOINT_HOME msg;
    msg.joint = joint;
    return send_cmd(&msg);
}

extern "C" int nml_shim_unhome(int joint)
{
    EMC_JOINT_UNHOME msg;
    msg.joint = joint;
    return send_cmd(&msg);
}

extern "C" int nml_shim_override_limits(void)
{
    EMC_JOINT_OVERRIDE_LIMITS msg;
    msg.joint = 0;
    return send_cmd(&msg);
}

extern "C" int nml_shim_teleop_enable(int enable)
{
    EMC_TRAJ_SET_TELEOP_ENABLE msg;
    msg.enable = enable;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_feed_override(double rate)
{
    EMC_TRAJ_SET_SCALE msg;
    msg.scale = rate;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_spindle_override(double rate, int spindle_num)
{
    EMC_TRAJ_SET_SPINDLE_SCALE msg;
    msg.scale = rate;
    msg.spindle = spindle_num;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_rapid_override(double rate)
{
    EMC_TRAJ_SET_RAPID_SCALE msg;
    msg.scale = rate;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_max_velocity(double velocity)
{
    EMC_TRAJ_SET_MAX_VELOCITY msg;
    msg.velocity = velocity;
    return send_cmd(&msg);
}

extern "C" int nml_shim_flood(int on)
{
    if (on) {
        EMC_COOLANT_FLOOD_ON msg;
        return send_cmd(&msg);
    } else {
        EMC_COOLANT_FLOOD_OFF msg;
        return send_cmd(&msg);
    }
}

extern "C" int nml_shim_mist(int on)
{
    if (on) {
        EMC_COOLANT_MIST_ON msg;
        return send_cmd(&msg);
    } else {
        EMC_COOLANT_MIST_OFF msg;
        return send_cmd(&msg);
    }
}

extern "C" int nml_shim_brake(int on, int spindle_num)
{
    if (on) {
        EMC_SPINDLE_BRAKE_ENGAGE msg;
        msg.spindle = spindle_num;
        return send_cmd(&msg);
    } else {
        EMC_SPINDLE_BRAKE_RELEASE msg;
        msg.spindle = spindle_num;
        return send_cmd(&msg);
    }
}

extern "C" int nml_shim_abort(void)
{
    EMC_TASK_ABORT msg;
    return send_cmd(&msg);
}

extern "C" int nml_shim_task_plan_synch(void)
{
    EMC_TASK_PLAN_SYNCH msg;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_debug(int debug)
{
    EMC_SET_DEBUG msg;
    msg.debug = debug;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_optional_stop(int on)
{
    EMC_TASK_PLAN_SET_OPTIONAL_STOP msg;
    msg.state = on;
    return send_cmd(&msg);
}

extern "C" int nml_shim_set_block_delete(int on)
{
    EMC_TASK_PLAN_SET_BLOCK_DELETE msg;
    msg.state = on;
    return send_cmd(&msg);
}

extern "C" int nml_shim_load_tool_table(void)
{
    EMC_TOOL_LOAD_TOOL_TABLE msg;
    msg.file[0] = '\0';
    return send_cmd(&msg);
}

extern "C" int nml_shim_program_open(const char *file)
{
    /* Close first, then open (matches emcmodule.cc pattern) */
    EMC_TASK_PLAN_CLOSE close_msg;
    int rc = send_cmd(&close_msg);
    if (rc)
        return rc;

    EMC_TASK_PLAN_OPEN open_msg;
    strncpy(open_msg.file, file, sizeof(open_msg.file) - 1);
    open_msg.file[sizeof(open_msg.file) - 1] = '\0';
    return send_cmd(&open_msg);
}

extern "C" int nml_shim_wait_complete(double timeout)
{
    if (!stat_channel || !stat_channel->valid())
        return -1;

    double elapsed = 0.0;
    const double poll_interval = 0.01; /* 10ms */

    while (timeout < 0 || elapsed < timeout) {
        NMLTYPE type = stat_channel->peek();
        if (type == EMC_STAT_TYPE) {
            EMC_STAT *st = static_cast<EMC_STAT *>(stat_channel->get_address());
            if (st) {
                int serial_diff = st->echo_serial_number - cmd_serial;
                if (serial_diff > 0)
                    return NML_SHIM_RCS_DONE;
                if (serial_diff == 0 &&
                    (st->status == RCS_DONE || st->status == RCS_ERROR))
                    return st->status;
            }
        }
        esleep(poll_interval);
        elapsed += poll_interval;
    }

    return -1; /* timeout */
}

/* ─── Errors ─── */

extern "C" int nml_shim_poll_errors(nml_error_t *errors, int max_errors)
{
    if (!err_channel || !err_channel->valid())
        return 0;

    int count = 0;
    while (count < max_errors) {
        NMLTYPE type = err_channel->read();
        if (type == 0)
            break;

        errors[count].kind = static_cast<int>(type);
        const char *text = "";

        switch (type) {
        case EMC_OPERATOR_ERROR_TYPE: {
            auto *m = static_cast<EMC_OPERATOR_ERROR *>(err_channel->get_address());
            text = m->error;
            break;
        }
        case EMC_OPERATOR_TEXT_TYPE: {
            auto *m = static_cast<EMC_OPERATOR_TEXT *>(err_channel->get_address());
            text = m->text;
            break;
        }
        case EMC_OPERATOR_DISPLAY_TYPE: {
            auto *m = static_cast<EMC_OPERATOR_DISPLAY *>(err_channel->get_address());
            text = m->display;
            break;
        }
        case NML_ERROR_TYPE: {
            auto *m = static_cast<NML_ERROR *>(err_channel->get_address());
            text = m->error;
            break;
        }
        case NML_TEXT_TYPE: {
            auto *m = static_cast<NML_TEXT *>(err_channel->get_address());
            text = m->text;
            break;
        }
        case NML_DISPLAY_TYPE: {
            auto *m = static_cast<NML_DISPLAY *>(err_channel->get_address());
            text = m->display;
            break;
        }
        default:
            text = "unrecognized error";
            break;
        }

        snprintf(errors[count].text, NML_SHIM_LINELEN, "%s", text);
        count++;
    }

    return count;
}
