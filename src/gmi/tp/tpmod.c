// tpmod.c — GMI cmod wrapper for the trajectory planner.
//
// Provides the "tp" API by wrapping existing tp.c functions behind
// GMI callback pointers.  motmod consumes this API via tp_api_get().
//
// License: GPL Version 2

#include <stdint.h>
#include <string.h>
#include "gomc_env.h"
#include "tp_api.h"
#include "mot_api.h"
#include "motion.h"
#include "tp.h"
#include "tcq.h"

// Static assert to verify type layout compatibility.
// tp_pose_t and EmcPose must be identical in memory (9 contiguous doubles).
_Static_assert(sizeof(tp_pose_t) == sizeof(EmcPose),
    "tp_pose_t and EmcPose must have the same size");
_Static_assert(sizeof(tp_cartesian_t) == sizeof(PmCartesian),
    "tp_cartesian_t and PmCartesian must have the same size");
_Static_assert(sizeof(tp_state_tag_t) == sizeof(struct state_tag_t),
    "tp_state_tag_t and state_tag_t must have the same size");

// ─── Stored API/env pointers ────────────────────────────────────────────────

static const gomc_api_t *tpmod_api;

// ─── Helper macros ──────────────────────────────────────────────────────────

#define TP(ptr) ((TP_STRUCT *)(uintptr_t)(ptr))

// ─── Mot API adapter functions ──────────────────────────────────────────────
// tp.c expects legacy function-pointer signatures via tpMotFunctions().
// These adapters bridge to the mot API callbacks looked up at Start() time.

static const mot_callbacks_t *mot;

static void adapt_dio_write(int index, char value)
{
    mot->dio_write(index, (int8_t)value);
}

static void adapt_aio_write(int index, double value)
{
    mot->aio_write(index, value);
}

static void adapt_set_rotary_unlock(int jnum, int unlock)
{
    mot->set_rotary_unlock(jnum, unlock);
}

static int adapt_get_rotary_unlock(int jnum)
{
    int32_t out;
    mot->get_rotary_unlock(jnum, &out);
    return out;
}

static double adapt_axis_get_vel_limit(int axis)
{
    double out;
    mot->axis_get_vel_limit(axis, &out);
    return out;
}

static double adapt_axis_get_acc_limit(int axis)
{
    double out;
    mot->axis_get_acc_limit(axis, &out);
    return out;
}

// ─── GMI callback wrappers ──────────────────────────────────────────────────

static int gmi_tp_init(uint64_t status_ptr, uint64_t config_ptr, int32_t *out)
{
    tpMotData((emcmot_status_t *)(uintptr_t)status_ptr,
              (emcmot_config_t *)(uintptr_t)config_ptr);
    *out = 0;
    return 0;
}

static int gmi_tp_create(uint64_t tp_ptr, int32_t queue_size,
                          int32_t comp_id, int32_t *out)
{
    *out = tpCreate(TP(tp_ptr), queue_size, comp_id);
    return 0;
}

static int gmi_tp_clear(uint64_t tp_ptr, int32_t *out)
{
    *out = tpClear(TP(tp_ptr));
    return 0;
}

static int gmi_tp_set_cycle_time(uint64_t tp_ptr, double secs, int32_t *out)
{
    *out = tpSetCycleTime(TP(tp_ptr), secs);
    return 0;
}

static int gmi_tp_set_vmax(uint64_t tp_ptr, double vmax,
                            double ini_maxvel, int32_t *out)
{
    *out = tpSetVmax(TP(tp_ptr), vmax, ini_maxvel);
    return 0;
}

static int gmi_tp_set_vlimit(uint64_t tp_ptr, double limit, int32_t *out)
{
    *out = tpSetVlimit(TP(tp_ptr), limit);
    return 0;
}

static int gmi_tp_set_amax(uint64_t tp_ptr, double amax, int32_t *out)
{
    *out = tpSetAmax(TP(tp_ptr), amax);
    return 0;
}

static int gmi_tp_set_id(uint64_t tp_ptr, int32_t id, int32_t *out)
{
    *out = tpSetId(TP(tp_ptr), id);
    return 0;
}

static int gmi_tp_set_pos(uint64_t tp_ptr, tp_pose_t *pos, int32_t *out)
{
    *out = tpSetPos(TP(tp_ptr), (EmcPose const *)pos);
    return 0;
}

static int gmi_tp_set_term_cond(uint64_t tp_ptr, int32_t cond,
                                 double tolerance, int32_t *out)
{
    *out = tpSetTermCond(TP(tp_ptr), cond, tolerance);
    return 0;
}

static int gmi_tp_set_spindle_sync(uint64_t tp_ptr, int32_t spindle,
                                    double sync, int32_t wait, int32_t *out)
{
    *out = tpSetSpindleSync(TP(tp_ptr), spindle, sync, wait);
    return 0;
}

static int gmi_tp_set_run_dir(uint64_t tp_ptr,
                               const tp_direction_t *dir, int32_t *out)
{
    *out = tpSetRunDir(TP(tp_ptr), (tc_direction_t)*dir);
    return 0;
}

// --- Motion segment addition ---

static int gmi_tp_add_line(
    uint64_t tp_ptr, const tp_pose_t *end,
    int32_t canon_motion_type, double vel, double ini_maxvel,
    double acc, uint8_t enables, int8_t atspeed,
    int32_t indexrotary, const tp_state_tag_t *tag,
    int32_t *out)
{
    *out = tpAddLine(TP(tp_ptr),
                     *(EmcPose *)end,
                     canon_motion_type, vel, ini_maxvel, acc,
                     enables, (char)atspeed, indexrotary,
                     *(struct state_tag_t *)tag);
    return 0;
}

static int gmi_tp_add_circle(
    uint64_t tp_ptr, const tp_pose_t *end,
    const tp_cartesian_t *center, const tp_cartesian_t *normal,
    int32_t turn, int32_t canon_motion_type,
    double vel, double ini_maxvel, double acc,
    uint8_t enables, int8_t atspeed, const tp_state_tag_t *tag,
    int32_t *out)
{
    *out = tpAddCircle(TP(tp_ptr),
                       *(EmcPose *)end,
                       *(PmCartesian *)center,
                       *(PmCartesian *)normal,
                       turn, canon_motion_type,
                       vel, ini_maxvel, acc,
                       enables, (char)atspeed,
                       *(struct state_tag_t *)tag);
    return 0;
}

static int gmi_tp_add_rigid_tap(
    uint64_t tp_ptr, const tp_pose_t *end,
    double vel, double ini_maxvel, double acc,
    uint8_t enables, double scale, const tp_state_tag_t *tag,
    int32_t *out)
{
    *out = tpAddRigidTap(TP(tp_ptr),
                         *(EmcPose *)end,
                         vel, ini_maxvel, acc,
                         enables, scale,
                         *(struct state_tag_t *)tag);
    return 0;
}

// --- Synchronized IO ---

static int gmi_tp_set_aout(uint64_t tp_ptr, uint8_t index,
                            double start_val, double end_val, int32_t *out)
{
    *out = tpSetAout(TP(tp_ptr), index, start_val, end_val);
    return 0;
}

static int gmi_tp_set_dout(uint64_t tp_ptr, int32_t index,
                            uint8_t start_val, uint8_t end_val, int32_t *out)
{
    *out = tpSetDout(TP(tp_ptr), index, start_val, end_val);
    return 0;
}

// --- Execution control ---

static int gmi_tp_run_cycle(uint64_t tp_ptr, int64_t period, int32_t *out)
{
    *out = tpRunCycle(TP(tp_ptr), (long)period);
    return 0;
}

static int gmi_tp_pause(uint64_t tp_ptr, int32_t *out)
{
    *out = tpPause(TP(tp_ptr));
    return 0;
}

static int gmi_tp_resume(uint64_t tp_ptr, int32_t *out)
{
    *out = tpResume(TP(tp_ptr));
    return 0;
}

static int gmi_tp_abort(uint64_t tp_ptr, int32_t *out)
{
    *out = tpAbort(TP(tp_ptr));
    return 0;
}

// --- Queries ---

static int gmi_tp_get_exec_id(uint64_t tp_ptr, int32_t *out)
{
    *out = tpGetExecId(TP(tp_ptr));
    return 0;
}

static int gmi_tp_get_exec_tag(uint64_t tp_ptr,
                                tp_state_tag_t *tag, int32_t *out)
{
    struct state_tag_t t = tpGetExecTag(TP(tp_ptr));
    memcpy(tag, &t, sizeof(t));
    *out = 0;
    return 0;
}

static int gmi_tp_get_pos(uint64_t tp_ptr, tp_pose_t *pos, int32_t *out)
{
    *out = tpGetPos(TP(tp_ptr), (EmcPose *)pos);
    return 0;
}

static int gmi_tp_is_done(uint64_t tp_ptr, int32_t *out)
{
    *out = tpIsDone(TP(tp_ptr));
    return 0;
}

static int gmi_tp_queue_depth(uint64_t tp_ptr, int32_t *out)
{
    *out = tpQueueDepth(TP(tp_ptr));
    return 0;
}

static int gmi_tp_active_depth(uint64_t tp_ptr, int32_t *out)
{
    *out = tpActiveDepth(TP(tp_ptr));
    return 0;
}

static int gmi_tp_get_motion_type(uint64_t tp_ptr, int32_t *out)
{
    *out = tpGetMotionType(TP(tp_ptr));
    return 0;
}

static int gmi_tp_queue_full(uint64_t tp_ptr, int32_t *out)
{
    *out = tcqFull(&TP(tp_ptr)->queue);
    return 0;
}

// ─── Callbacks table ────────────────────────────────────────────────────────

static const tp_callbacks_t tpmod_callbacks = GMI_TP_CALLBACKS;

// ─── cmod lifecycle ─────────────────────────────────────────────────────────

static cmod_t tpmod_cmod;

static void tpmod_destroy(cmod_t *self) { (void)self; }

static int tpmod_init(cmod_t *self)
{
    (void)self;

    /* Look up the mot reverse-callback API registered by motmod. */
    mot = mot_api_get(tpmod_api, "default");
    if (!mot)
        return -1;

    /* Wire legacy tp.c function-pointer statics through mot API adapters. */
    tpMotFunctions(adapt_dio_write,
                   adapt_aio_write,
                   adapt_set_rotary_unlock,
                   adapt_get_rotary_unlock,
                   adapt_axis_get_vel_limit,
                   adapt_axis_get_acc_limit);
    return 0;
}

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out)
{
    (void)argc; (void)argv;

    tpmod_api = env->api;

    int rc = tp_api_register(env->api, "default", &tpmod_callbacks);
    if (rc != 0) {
        gomc_log_errorf(env->log, name,
            "failed to register tp API: %d", rc);
        return rc;
    }

    tpmod_cmod.Init    = tpmod_init;
    tpmod_cmod.Start   = NULL;
    tpmod_cmod.Destroy = tpmod_destroy;
    *out = &tpmod_cmod;
    return 0;
}
