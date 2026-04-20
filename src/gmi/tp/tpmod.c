// tpmod.c — GMI cmod wrapper for the trajectory planner.
//
// Provides the "tp" API by wrapping existing tp.c functions behind
// GMI callback pointers.  motmod consumes this API via tp_api_get().
//
// License: GPL Version 2

#include <stdint.h>
#include <stdlib.h>
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

// ─── TP instance (owned by tpmod, calloc'd at create time) ──────────────────

static TP_STRUCT *g_tp;

// ─── Mot API adapter functions ──────────────────────────────────────────────
// tp.c now accepts the mot API directly via tpSetMotAPI().

static const mot_callbacks_t *mot;

// ─── GMI callback wrappers ──────────────────────────────────────────────────

static int32_t gmi_tp_init(void)
{
    return 0;
}

static int32_t gmi_tp_create(int32_t queue_size, int32_t comp_id)
{
    g_tp = calloc(1, sizeof(TP_STRUCT));
    if (!g_tp) return -1;
    return tpCreate(g_tp, queue_size, comp_id);
}

static int32_t gmi_tp_clear(void)
{
    return tpClear(g_tp);
}

static int32_t gmi_tp_set_cycle_time(double secs)
{
    return tpSetCycleTime(g_tp, secs);
}

static int32_t gmi_tp_set_vmax(double vmax, double ini_maxvel)
{
    return tpSetVmax(g_tp, vmax, ini_maxvel);
}

static int32_t gmi_tp_set_vlimit(double limit)
{
    return tpSetVlimit(g_tp, limit);
}

static int32_t gmi_tp_set_amax(double amax)
{
    return tpSetAmax(g_tp, amax);
}

static int32_t gmi_tp_set_id(int32_t id)
{
    return tpSetId(g_tp, id);
}

static int32_t gmi_tp_set_pos(tp_pose_t *pos)
{
    return tpSetPos(g_tp, (EmcPose const *)pos);
}

static int32_t gmi_tp_set_term_cond(int32_t cond, double tolerance)
{
    return tpSetTermCond(g_tp, cond, tolerance);
}

static int32_t gmi_tp_set_spindle_sync(int32_t spindle,
                                    double sync, int32_t wait)
{
    return tpSetSpindleSync(g_tp, spindle, sync, wait);
}

static int32_t gmi_tp_set_run_dir(tp_direction_t dir)
{
    return tpSetRunDir(g_tp, (tc_direction_t)dir);
}

// --- Motion segment addition ---

static int32_t gmi_tp_add_line(const tp_pose_t *end,
    int32_t canon_motion_type, double vel, double ini_maxvel,
    double acc, uint8_t enables, int8_t atspeed,
    int32_t indexrotary, const tp_state_tag_t *tag)
{
    return tpAddLine(g_tp,
                     *(EmcPose *)end,
                     canon_motion_type, vel, ini_maxvel, acc,
                     enables, (char)atspeed, indexrotary,
                     *(struct state_tag_t *)tag);
}

static int32_t gmi_tp_add_circle(const tp_pose_t *end,
    const tp_cartesian_t *center, const tp_cartesian_t *normal,
    int32_t turn, int32_t canon_motion_type,
    double vel, double ini_maxvel, double acc,
    uint8_t enables, int8_t atspeed, const tp_state_tag_t *tag)
{
    return tpAddCircle(g_tp,
                       *(EmcPose *)end,
                       *(PmCartesian *)center,
                       *(PmCartesian *)normal,
                       turn, canon_motion_type,
                       vel, ini_maxvel, acc,
                       enables, (char)atspeed,
                       *(struct state_tag_t *)tag);
}

static int32_t gmi_tp_add_rigid_tap(const tp_pose_t *end,
    double vel, double ini_maxvel, double acc,
    uint8_t enables, double scale, const tp_state_tag_t *tag)
{
    return tpAddRigidTap(g_tp,
                         *(EmcPose *)end,
                         vel, ini_maxvel, acc,
                         enables, scale,
                         *(struct state_tag_t *)tag);
}

// --- Synchronized IO ---

static int32_t gmi_tp_set_aout(uint8_t index,
                            double start_val, double end_val)
{
    return tpSetAout(g_tp, index, start_val, end_val);
}

static int32_t gmi_tp_set_dout(int32_t index,
                            uint8_t start_val, uint8_t end_val)
{
    return tpSetDout(g_tp, index, start_val, end_val);
}

// --- Execution control ---

static int32_t gmi_tp_run_cycle(int64_t period)
{
    return tpRunCycle(g_tp, (long)period);
}

static int32_t gmi_tp_pause(void)
{
    return tpPause(g_tp);
}

static int32_t gmi_tp_resume(void)
{
    return tpResume(g_tp);
}

static int32_t gmi_tp_abort(void)
{
    return tpAbort(g_tp);
}

// --- Queries ---

static int32_t gmi_tp_get_exec_id(void)
{
    return tpGetExecId(g_tp);
}

static int32_t gmi_tp_get_exec_tag(tp_state_tag_t *tag)
{
    struct state_tag_t t = tpGetExecTag(g_tp);
    memcpy(tag, &t, sizeof(t));
    return 0;
}

static int32_t gmi_tp_get_pos(tp_pose_t *pos)
{
    return tpGetPos(g_tp, (EmcPose *)pos);
}

static int32_t gmi_tp_is_done(void)
{
    return tpIsDone(g_tp);
}

static int32_t gmi_tp_queue_depth(void)
{
    return tpQueueDepth(g_tp);
}

static int32_t gmi_tp_active_depth(void)
{
    return tpActiveDepth(g_tp);
}

static int32_t gmi_tp_get_motion_type(void)
{
    return tpGetMotionType(g_tp);
}

static int32_t gmi_tp_queue_full(void)
{
    return tcqFull(&g_tp->queue);
}

static int32_t gmi_tp_get_run_dir(void)
{
    return g_tp->reverse_run;
}

// ─── Callbacks table ────────────────────────────────────────────────────────

static const tp_callbacks_t tpmod_callbacks = GMI_TP_CALLBACKS;

// ─── cmod lifecycle ─────────────────────────────────────────────────────────

static cmod_t tpmod_cmod;

static void tpmod_destroy(cmod_t *self) {
    (void)self;
    free(g_tp);
    g_tp = NULL;
}

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
