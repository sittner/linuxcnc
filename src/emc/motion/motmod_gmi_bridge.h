// motmod_gmi_bridge.h — Bridge layer for motmod GMI API consumption
//
// Provides inline wrappers that match the original tp.h/homing.h function
// signatures but dispatch through GMI API callback pointers.  This lets
// control.c and command.c call tp/home functions without code changes.
//
// Usage: replace #include "tp.h" / #include "homing.h" with this header.
// Include "tp_types.h" / "tc_types.h" / "tcq.h" separately for types.

#ifndef MOTMOD_GMI_BRIDGE_H
#define MOTMOD_GMI_BRIDGE_H

#include <stdint.h>
#include <string.h>
#include "tp_api.h"
#include "home_api.h"

// ─── API pointers (defined in motion.c, set during New()) ───────────────

extern const tp_callbacks_t   *motmod_tp_api;
extern const home_callbacks_t *motmod_home_api;

// ─── TP bridge functions ────────────────────────────────────────────────
// These match the original tp.h signatures exactly.

static inline int tpCreate(TP_STRUCT * const tp, int _queueSize, int id)
{
    int32_t out;
    motmod_tp_api->create((uintptr_t)tp, _queueSize, id, &out);
    return out;
}

static inline int tpClear(TP_STRUCT * const tp)
{
    int32_t out;
    motmod_tp_api->clear((uintptr_t)tp, &out);
    return out;
}

static inline int tpSetCycleTime(TP_STRUCT *tp, double secs)
{
    int32_t out;
    motmod_tp_api->set_cycle_time((uintptr_t)tp, secs, &out);
    return out;
}

static inline int tpSetVmax(TP_STRUCT *tp, double vmax, double ini_maxvel)
{
    int32_t out;
    motmod_tp_api->set_vmax((uintptr_t)tp, vmax, ini_maxvel, &out);
    return out;
}

static inline int tpSetVlimit(TP_STRUCT *tp, double limit)
{
    int32_t out;
    motmod_tp_api->set_vlimit((uintptr_t)tp, limit, &out);
    return out;
}

static inline int tpSetAmax(TP_STRUCT *tp, double amax)
{
    int32_t out;
    motmod_tp_api->set_amax((uintptr_t)tp, amax, &out);
    return out;
}

static inline int tpSetId(TP_STRUCT *tp, int id)
{
    int32_t out;
    motmod_tp_api->set_id((uintptr_t)tp, id, &out);
    return out;
}

static inline int tpGetExecId(TP_STRUCT *tp)
{
    int32_t out;
    motmod_tp_api->get_exec_id((uintptr_t)tp, &out);
    return out;
}

static inline struct state_tag_t tpGetExecTag(TP_STRUCT * const tp)
{
    struct state_tag_t result;
    int32_t out;
    tp_state_tag_t gmi_tag;
    motmod_tp_api->get_exec_tag((uintptr_t)tp, &gmi_tag, &out);
    memcpy(&result, &gmi_tag, sizeof(result));
    return result;
}

static inline int tpSetTermCond(TP_STRUCT *tp, int cond, double tolerance)
{
    int32_t out;
    motmod_tp_api->set_term_cond((uintptr_t)tp, cond, tolerance, &out);
    return out;
}

static inline int tpSetPos(TP_STRUCT *tp, EmcPose const * const pos)
{
    int32_t out;
    motmod_tp_api->set_pos((uintptr_t)tp, (tp_pose_t *)pos, &out);
    return out;
}

static inline int tpRunCycle(TP_STRUCT *tp, long period)
{
    int32_t out;
    motmod_tp_api->run_cycle((uintptr_t)tp, (int64_t)period, &out);
    return out;
}

static inline int tpPause(TP_STRUCT *tp)
{
    int32_t out;
    motmod_tp_api->pause((uintptr_t)tp, &out);
    return out;
}

static inline int tpResume(TP_STRUCT *tp)
{
    int32_t out;
    motmod_tp_api->resume((uintptr_t)tp, &out);
    return out;
}

static inline int tpAbort(TP_STRUCT *tp)
{
    int32_t out;
    motmod_tp_api->abort((uintptr_t)tp, &out);
    return out;
}

static inline int tpAddLine(TP_STRUCT * const tp, EmcPose end,
    int canon_motion_type, double vel, double ini_maxvel, double acc,
    unsigned char enables, char atspeed, int indexrotary,
    struct state_tag_t tag)
{
    int32_t out;
    motmod_tp_api->add_line((uintptr_t)tp,
        (const tp_pose_t *)&end,
        canon_motion_type, vel, ini_maxvel, acc,
        enables, (int8_t)atspeed, indexrotary,
        (const tp_state_tag_t *)&tag, &out);
    return out;
}

static inline int tpAddCircle(TP_STRUCT * const tp, EmcPose end,
    PmCartesian center, PmCartesian normal, int turn,
    int canon_motion_type, double vel, double ini_maxvel, double acc,
    unsigned char enables, char atspeed, struct state_tag_t tag)
{
    int32_t out;
    motmod_tp_api->add_circle((uintptr_t)tp,
        (const tp_pose_t *)&end,
        (const tp_cartesian_t *)&center,
        (const tp_cartesian_t *)&normal,
        turn, canon_motion_type, vel, ini_maxvel, acc,
        enables, (int8_t)atspeed,
        (const tp_state_tag_t *)&tag, &out);
    return out;
}

static inline int tpAddRigidTap(TP_STRUCT * const tp, EmcPose end,
    double vel, double ini_maxvel, double acc,
    unsigned char enables, double scale, struct state_tag_t tag)
{
    int32_t out;
    motmod_tp_api->add_rigid_tap((uintptr_t)tp,
        (const tp_pose_t *)&end,
        vel, ini_maxvel, acc,
        enables, scale,
        (const tp_state_tag_t *)&tag, &out);
    return out;
}

static inline int tpSetAout(TP_STRUCT * const tp, unsigned char index,
    double start, double end)
{
    int32_t out;
    motmod_tp_api->set_aout((uintptr_t)tp, index, start, end, &out);
    return out;
}

static inline int tpSetDout(TP_STRUCT * const tp, int index,
    unsigned char start, unsigned char end)
{
    int32_t out;
    motmod_tp_api->set_dout((uintptr_t)tp, index, start, end, &out);
    return out;
}

static inline int tpGetPos(TP_STRUCT const * const tp, EmcPose * const pos)
{
    int32_t out;
    motmod_tp_api->get_pos((uintptr_t)tp, (tp_pose_t *)pos, &out);
    return out;
}

static inline int tpIsDone(TP_STRUCT * const tp)
{
    int32_t out;
    motmod_tp_api->is_done((uintptr_t)tp, &out);
    return out;
}

static inline int tpQueueDepth(TP_STRUCT * const tp)
{
    int32_t out;
    motmod_tp_api->queue_depth((uintptr_t)tp, &out);
    return out;
}

static inline int tpActiveDepth(TP_STRUCT * const tp)
{
    int32_t out;
    motmod_tp_api->active_depth((uintptr_t)tp, &out);
    return out;
}

static inline int tpGetMotionType(TP_STRUCT * const tp)
{
    int32_t out;
    motmod_tp_api->get_motion_type((uintptr_t)tp, &out);
    return out;
}

static inline int tpSetSpindleSync(TP_STRUCT * const tp, int spindle,
    double sync, int wait)
{
    int32_t out;
    motmod_tp_api->set_spindle_sync((uintptr_t)tp, spindle, sync, wait, &out);
    return out;
}

static inline int tpSetRunDir(TP_STRUCT * const tp, tc_direction_t dir)
{
    int32_t out;
    tp_direction_t gmi_dir = (tp_direction_t)dir;
    motmod_tp_api->set_run_dir((uintptr_t)tp, &gmi_dir, &out);
    return out;
}

// tcqFull — use motmod_tcqFull() instead, since tcqFull is declared in tcq.h
// and we can't redefine it here.  The single call site in control.c must
// be changed to use this helper.
static inline int motmod_tcqFull(TP_STRUCT const * const tp)
{
    int32_t out;
    motmod_tp_api->queue_full((uintptr_t)tp, &out);
    return out;
}

// ─── Home bridge functions ──────────────────────────────────────────────

static inline void read_homing_in_pins(int njoints)
{
    int32_t out;
    motmod_home_api->read_in_pins(njoints, &out);
}

static inline bool do_homing(void)
{
    int32_t out;
    motmod_home_api->do_homing(&out);
    return (bool)out;
}

static inline void write_homing_out_pins(int njoints)
{
    int32_t out;
    motmod_home_api->write_out_pins(njoints, &out);
}

static inline void do_home_joint(int jno)
{
    int32_t out;
    motmod_home_api->do_home_joint(jno, &out);
}

static inline void do_cancel_homing(int jno)
{
    int32_t out;
    motmod_home_api->do_cancel(jno, &out);
}

static inline void set_unhomed(int jno, motion_state_t motstate)
{
    int32_t out;
    home_motion_state_t gmi_state = (home_motion_state_t)motstate;
    motmod_home_api->set_unhomed(jno, &gmi_state, &out);
}

static inline void set_joint_homing_params(int jno,
    double offset, double home, double home_final_vel,
    double home_search_vel, double home_latch_vel,
    int home_flags, int home_sequence, bool volatile_home)
{
    int32_t out;
    motmod_home_api->set_joint_params(jno, offset, home,
        home_final_vel, home_search_vel, home_latch_vel,
        home_flags, home_sequence, (int32_t)volatile_home, &out);
}

static inline void update_joint_homing_params(int jno,
    double home_offset, double home_home, int home_sequence)
{
    int32_t out;
    motmod_home_api->update_joint_params(jno, home_offset,
        home_home, home_sequence, &out);
}

static inline bool get_allhomed(void)
{
    int32_t out;
    motmod_home_api->get_allhomed(&out);
    return (bool)out;
}

static inline bool get_homing_is_active(void)
{
    int32_t out;
    motmod_home_api->get_is_active(&out);
    return (bool)out;
}

static inline int get_home_sequence(int jno)
{
    int32_t out;
    motmod_home_api->get_sequence(jno, &out);
    return out;
}

static inline bool get_homing(int jno)
{
    int32_t out;
    motmod_home_api->get_homing(jno, &out);
    return (bool)out;
}

static inline bool get_homed(int jno)
{
    int32_t out;
    motmod_home_api->get_homed(jno, &out);
    return (bool)out;
}

static inline bool get_index_enable(int jno)
{
    int32_t out;
    motmod_home_api->get_index_enable(jno, &out);
    return (bool)out;
}

static inline bool get_home_needs_unlock_first(int jno)
{
    int32_t out;
    motmod_home_api->get_needs_unlock_first(jno, &out);
    return (bool)out;
}

static inline bool get_home_is_idle(int jno)
{
    int32_t out;
    motmod_home_api->get_is_idle(jno, &out);
    return (bool)out;
}

static inline bool get_home_is_synchronized(int jno)
{
    int32_t out;
    motmod_home_api->get_is_synchronized(jno, &out);
    return (bool)out;
}

static inline bool get_homing_at_index_search_wait(int jno)
{
    int32_t out;
    motmod_home_api->get_at_index_search_wait(jno, &out);
    return (bool)out;
}

#endif // MOTMOD_GMI_BRIDGE_H
