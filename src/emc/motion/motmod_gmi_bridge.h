// motmod_gmi_bridge.h — Bridge layer for motmod GMI API consumption
//
// Provides inline wrappers that dispatch through GMI API callback pointers.
// control.c and command.c call these tp/home functions.

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
// tpmod owns the TP_STRUCT internally.  These wrappers match the original
// tp.h signatures for minimal change in control.c / command.c, except
// the TP_STRUCT pointer is ignored (kept only for call-site compatibility).

static inline int tpCreate(int _queueSize, int id)
{
    return motmod_tp_api->create(_queueSize, id);
}

static inline int tpClear(void)
{
    return motmod_tp_api->clear();
}

static inline int tpSetCycleTime(double secs)
{
    return motmod_tp_api->set_cycle_time(secs);
}

static inline int tpSetVmax(double vmax, double ini_maxvel)
{
    return motmod_tp_api->set_vmax(vmax, ini_maxvel);
}

static inline int tpSetVlimit(double limit)
{
    return motmod_tp_api->set_vlimit(limit);
}

static inline int tpSetAmax(double amax)
{
    return motmod_tp_api->set_amax(amax);
}

static inline int tpSetId(int id)
{
    return motmod_tp_api->set_id(id);
}

static inline int tpGetExecId(void)
{
    return motmod_tp_api->get_exec_id();
}

static inline struct state_tag_t tpGetExecTag(void)
{
    struct state_tag_t result;
    tp_state_tag_t gmi_tag;
    motmod_tp_api->get_exec_tag(&gmi_tag);
    memcpy(&result, &gmi_tag, sizeof(result));
    return result;
}

static inline int tpSetTermCond(int cond, double tolerance)
{
    return motmod_tp_api->set_term_cond(cond, tolerance);
}

static inline int tpSetPos(EmcPose const * const pos)
{
    return motmod_tp_api->set_pos((tp_pose_t *)pos);
}

static inline int tpRunCycle(long period)
{
    return motmod_tp_api->run_cycle((int64_t)period);
}

static inline int tpPause(void)
{
    return motmod_tp_api->pause();
}

static inline int tpResume(void)
{
    return motmod_tp_api->resume();
}

static inline int tpAbort(void)
{
    return motmod_tp_api->abort();
}

static inline int tpAddLine(EmcPose end,
    int canon_motion_type, double vel, double ini_maxvel, double acc,
    unsigned char enables, char atspeed, int indexrotary,
    struct state_tag_t tag)
{
    return motmod_tp_api->add_line(
        (const tp_pose_t *)&end,
        canon_motion_type, vel, ini_maxvel, acc,
        enables, (int8_t)atspeed, indexrotary,
        (const tp_state_tag_t *)&tag);
}

static inline int tpAddCircle(EmcPose end,
    PmCartesian center, PmCartesian normal, int turn,
    int canon_motion_type, double vel, double ini_maxvel, double acc,
    unsigned char enables, char atspeed, struct state_tag_t tag)
{
    return motmod_tp_api->add_circle(
        (const tp_pose_t *)&end,
        (const tp_cartesian_t *)&center,
        (const tp_cartesian_t *)&normal,
        turn, canon_motion_type, vel, ini_maxvel, acc,
        enables, (int8_t)atspeed,
        (const tp_state_tag_t *)&tag);
}

static inline int tpAddRigidTap(EmcPose end,
    double vel, double ini_maxvel, double acc,
    unsigned char enables, double scale, struct state_tag_t tag)
{
    return motmod_tp_api->add_rigid_tap(
        (const tp_pose_t *)&end,
        vel, ini_maxvel, acc,
        enables, scale,
        (const tp_state_tag_t *)&tag);
}

static inline int tpSetAout(unsigned char index,
    double start, double end)
{
    return motmod_tp_api->set_aout(index, start, end);
}

static inline int tpSetDout(int index,
    unsigned char start, unsigned char end)
{
    return motmod_tp_api->set_dout(index, start, end);
}

static inline int tpGetPos(EmcPose * const pos)
{
    return motmod_tp_api->get_pos((tp_pose_t *)pos);
}

static inline int tpIsDone(void)
{
    return motmod_tp_api->is_done();
}

static inline int tpQueueDepth(void)
{
    return motmod_tp_api->queue_depth();
}

static inline int tpActiveDepth(void)
{
    return motmod_tp_api->active_depth();
}

static inline int tpGetMotionType(void)
{
    return motmod_tp_api->get_motion_type();
}

static inline int tpSetSpindleSync(int spindle,
    double sync, int wait)
{
    return motmod_tp_api->set_spindle_sync(spindle, sync, wait);
}

static inline int tpSetRunDir(tc_direction_t dir)
{
    tp_direction_t gmi_dir = (tp_direction_t)dir;
    return motmod_tp_api->set_run_dir(&gmi_dir);
}

static inline int tpGetRunDir(void)
{
    return motmod_tp_api->get_run_dir();
}

static inline int tpQueueFull(void)
{
    return motmod_tp_api->queue_full();
}

// ─── Home bridge functions ──────────────────────────────────────────────

static inline void read_homing_in_pins(int njoints)
{
    motmod_home_api->read_in_pins(njoints);
}

static inline bool do_homing(void)
{
    return (bool)motmod_home_api->do_homing();
}

static inline void write_homing_out_pins(int njoints)
{
    motmod_home_api->write_out_pins(njoints);
}

static inline void do_home_joint(int jno)
{
    motmod_home_api->do_home_joint(jno);
}

static inline void do_cancel_homing(int jno)
{
    motmod_home_api->do_cancel(jno);
}

static inline void set_unhomed(int jno, motion_state_t motstate)
{
    home_motion_state_t gmi_state = (home_motion_state_t)motstate;
    motmod_home_api->set_unhomed(jno, &gmi_state);
}

static inline void set_joint_homing_params(int jno,
    double offset, double home, double home_final_vel,
    double home_search_vel, double home_latch_vel,
    int home_flags, int home_sequence, bool volatile_home)
{
    motmod_home_api->set_joint_params(jno, offset, home,
        home_final_vel, home_search_vel, home_latch_vel,
        home_flags, home_sequence, (int32_t)volatile_home);
}

static inline void update_joint_homing_params(int jno,
    double home_offset, double home_home, int home_sequence)
{
    motmod_home_api->update_joint_params(jno, home_offset,
        home_home, home_sequence);
}

static inline bool get_allhomed(void)
{
    return (bool)motmod_home_api->get_allhomed();
}

static inline bool get_homing_is_active(void)
{
    return (bool)motmod_home_api->get_is_active();
}

static inline int get_home_sequence(int jno)
{
    return motmod_home_api->get_sequence(jno);
}

static inline bool get_homing(int jno)
{
    return (bool)motmod_home_api->get_homing(jno);
}

static inline bool get_homed(int jno)
{
    return (bool)motmod_home_api->get_homed(jno);
}

static inline bool get_index_enable(int jno)
{
    return (bool)motmod_home_api->get_index_enable(jno);
}

static inline bool get_home_needs_unlock_first(int jno)
{
    return (bool)motmod_home_api->get_needs_unlock_first(jno);
}

static inline bool get_home_is_idle(int jno)
{
    return (bool)motmod_home_api->get_is_idle(jno);
}

static inline bool get_home_is_synchronized(int jno)
{
    return (bool)motmod_home_api->get_is_synchronized(jno);
}

static inline bool get_homing_at_index_search_wait(int jno)
{
    return (bool)motmod_home_api->get_at_index_search_wait(jno);
}

#endif // MOTMOD_GMI_BRIDGE_H
