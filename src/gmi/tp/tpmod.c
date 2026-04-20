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
// tp.c now accepts the mot API directly via tpSetMotAPI().

static const mot_callbacks_t *mot;

// ─── GMI callback wrappers ──────────────────────────────────────────────────

static int32_t gmi_tp_init(void)
{
    return 0;
}

static int32_t gmi_tp_create(uint64_t tp_ptr, int32_t queue_size,
                          int32_t comp_id)
{
    return tpCreate(TP(tp_ptr), queue_size, comp_id);
}

static int32_t gmi_tp_clear(uint64_t tp_ptr)
{
    return tpClear(TP(tp_ptr));
}

static int32_t gmi_tp_set_cycle_time(uint64_t tp_ptr, double secs)
{
    return tpSetCycleTime(TP(tp_ptr), secs);
}

static int32_t gmi_tp_set_vmax(uint64_t tp_ptr, double vmax,
                            double ini_maxvel)
{
    return tpSetVmax(TP(tp_ptr), vmax, ini_maxvel);
}

static int32_t gmi_tp_set_vlimit(uint64_t tp_ptr, double limit)
{
    return tpSetVlimit(TP(tp_ptr), limit);
}

static int32_t gmi_tp_set_amax(uint64_t tp_ptr, double amax)
{
    return tpSetAmax(TP(tp_ptr), amax);
}

static int32_t gmi_tp_set_id(uint64_t tp_ptr, int32_t id)
{
    return tpSetId(TP(tp_ptr), id);
}

static int32_t gmi_tp_set_pos(uint64_t tp_ptr, tp_pose_t *pos)
{
    return tpSetPos(TP(tp_ptr), (EmcPose const *)pos);
}

static int32_t gmi_tp_set_term_cond(uint64_t tp_ptr, int32_t cond,
                                 double tolerance)
{
    return tpSetTermCond(TP(tp_ptr), cond, tolerance);
}

static int32_t gmi_tp_set_spindle_sync(uint64_t tp_ptr, int32_t spindle,
                                    double sync, int32_t wait)
{
    return tpSetSpindleSync(TP(tp_ptr), spindle, sync, wait);
}

static int32_t gmi_tp_set_run_dir(uint64_t tp_ptr,
                               const tp_direction_t *dir)
{
    return tpSetRunDir(TP(tp_ptr), (tc_direction_t)*dir);
}

// --- Motion segment addition ---

static int32_t gmi_tp_add_line(uint64_t tp_ptr, const tp_pose_t *end,
    int32_t canon_motion_type, double vel, double ini_maxvel,
    double acc, uint8_t enables, int8_t atspeed,
    int32_t indexrotary, const tp_state_tag_t *tag)
{
    return tpAddLine(TP(tp_ptr),
                     *(EmcPose *)end,
                     canon_motion_type, vel, ini_maxvel, acc,
                     enables, (char)atspeed, indexrotary,
                     *(struct state_tag_t *)tag);
}

static int32_t gmi_tp_add_circle(uint64_t tp_ptr, const tp_pose_t *end,
    const tp_cartesian_t *center, const tp_cartesian_t *normal,
    int32_t turn, int32_t canon_motion_type,
    double vel, double ini_maxvel, double acc,
    uint8_t enables, int8_t atspeed, const tp_state_tag_t *tag)
{
    return tpAddCircle(TP(tp_ptr),
                       *(EmcPose *)end,
                       *(PmCartesian *)center,
                       *(PmCartesian *)normal,
                       turn, canon_motion_type,
                       vel, ini_maxvel, acc,
                       enables, (char)atspeed,
                       *(struct state_tag_t *)tag);
}

static int32_t gmi_tp_add_rigid_tap(uint64_t tp_ptr, const tp_pose_t *end,
    double vel, double ini_maxvel, double acc,
    uint8_t enables, double scale, const tp_state_tag_t *tag)
{
    return tpAddRigidTap(TP(tp_ptr),
                         *(EmcPose *)end,
                         vel, ini_maxvel, acc,
                         enables, scale,
                         *(struct state_tag_t *)tag);
}

// --- Synchronized IO ---

static int32_t gmi_tp_set_aout(uint64_t tp_ptr, uint8_t index,
                            double start_val, double end_val)
{
    return tpSetAout(TP(tp_ptr), index, start_val, end_val);
}

static int32_t gmi_tp_set_dout(uint64_t tp_ptr, int32_t index,
                            uint8_t start_val, uint8_t end_val)
{
    return tpSetDout(TP(tp_ptr), index, start_val, end_val);
}

// --- Execution control ---

static int32_t gmi_tp_run_cycle(uint64_t tp_ptr, int64_t period)
{
    return tpRunCycle(TP(tp_ptr), (long)period);
}

static int32_t gmi_tp_pause(uint64_t tp_ptr)
{
    return tpPause(TP(tp_ptr));
}

static int32_t gmi_tp_resume(uint64_t tp_ptr)
{
    return tpResume(TP(tp_ptr));
}

static int32_t gmi_tp_abort(uint64_t tp_ptr)
{
    return tpAbort(TP(tp_ptr));
}

// --- Queries ---

static int32_t gmi_tp_get_exec_id(uint64_t tp_ptr)
{
    return tpGetExecId(TP(tp_ptr));
}

static int32_t gmi_tp_get_exec_tag(uint64_t tp_ptr,
                                tp_state_tag_t *tag)
{
    struct state_tag_t t = tpGetExecTag(TP(tp_ptr));
    memcpy(tag, &t, sizeof(t));
    return 0;
}

static int32_t gmi_tp_get_pos(uint64_t tp_ptr, tp_pose_t *pos)
{
    return tpGetPos(TP(tp_ptr), (EmcPose *)pos);
}

static int32_t gmi_tp_is_done(uint64_t tp_ptr)
{
    return tpIsDone(TP(tp_ptr));
}

static int32_t gmi_tp_queue_depth(uint64_t tp_ptr)
{
    return tpQueueDepth(TP(tp_ptr));
}

static int32_t gmi_tp_active_depth(uint64_t tp_ptr)
{
    return tpActiveDepth(TP(tp_ptr));
}

static int32_t gmi_tp_get_motion_type(uint64_t tp_ptr)
{
    return tpGetMotionType(TP(tp_ptr));
}

static int32_t gmi_tp_queue_full(uint64_t tp_ptr)
{
    return tcqFull(&TP(tp_ptr)->queue);
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

    /* Pass the mot API directly to tp.c */
    tpSetMotAPI(mot);
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
