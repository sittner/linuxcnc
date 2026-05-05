/*
 * joint_ctrl_cmod.c — cmod HAL component: standalone single-joint control.
 *
 * Provides: homing, keyboard jogging, jogwheel jogging, trapezoidal
 * trajectory planning, following-error monitoring, limit-switch handling,
 * and backlash compensation.
 *
 * Usage:
 *   load joint_ctrl
 *
 * Original author: LinuxCNC developers
 * Converted to cmod API: 2026
 * License: GPL Version 2
 */

#include "gomc_env.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

/* ============================================================
 * SIMPLE TRAJECTORY PLANNER (inlined from joint_simple_tp.c)
 * ============================================================ */

#define TINY_DP(max_acc, period) ((max_acc) * (period) * (period) * 0.001)

typedef struct {
    double pos_cmd;
    double max_vel;
    double max_acc;
    int    enable;
    double curr_pos;
    double curr_vel;
    int    active;
} simple_tp_t;

static void simple_tp_update(simple_tp_t *tp, double period) {
    double max_dv, tiny_dp, pos_err, vel_req;
    tp->active = 0;
    max_dv = tp->max_acc * period;
    tiny_dp = TINY_DP(tp->max_acc, period);
    if (tp->enable) {
        pos_err = tp->pos_cmd - tp->curr_pos;
        if (pos_err > tiny_dp) {
            vel_req = -max_dv + sqrt(2.0 * tp->max_acc * pos_err + max_dv * max_dv);
            tp->active = 1;
        } else if (pos_err < -tiny_dp) {
            vel_req = max_dv - sqrt(-2.0 * tp->max_acc * pos_err + max_dv * max_dv);
            tp->active = 1;
        } else {
            vel_req = 0.0;
        }
    } else {
        vel_req = 0.0;
        tp->pos_cmd = tp->curr_pos;
    }
    if (vel_req > tp->max_vel) vel_req = tp->max_vel;
    else if (vel_req < -tp->max_vel) vel_req = -tp->max_vel;
    if (vel_req > tp->curr_vel + max_dv) tp->curr_vel += max_dv;
    else if (vel_req < tp->curr_vel - max_dv) tp->curr_vel -= max_dv;
    else tp->curr_vel = vel_req;
    if (tp->curr_vel != 0.0) tp->active = 1;
    tp->curr_pos += tp->curr_vel * period;
}

/* ============================================================
 * HOMING STATE MACHINE (inlined from joint_homing.c)
 * ============================================================ */

#define HOME_IGNORE_LIMITS            1
#define HOME_USE_INDEX                2
#define HOME_IS_SHARED                4
#define HOME_UNLOCK_FIRST             8
#define HOME_ABSOLUTE_ENCODER        16
#define HOME_NO_REHOME               32
#define HOME_NO_FINAL_MOVE           64
#define HOME_INDEX_NO_ENCODER_RESET 128

#define HOME_DELAY 0.100

typedef enum {
    HOME_IDLE = 0, HOME_START, HOME_UNLOCK, HOME_UNLOCK_WAIT,
    HOME_INITIAL_BACKOFF_START, HOME_INITIAL_BACKOFF_WAIT,
    HOME_INITIAL_SEARCH_START, HOME_INITIAL_SEARCH_WAIT,
    HOME_SET_COARSE_POSITION,
    HOME_FINAL_BACKOFF_START, HOME_FINAL_BACKOFF_WAIT,
    HOME_RISE_SEARCH_START, HOME_RISE_SEARCH_WAIT,
    HOME_FALL_SEARCH_START, HOME_FALL_SEARCH_WAIT,
    HOME_SET_SWITCH_POSITION,
    HOME_INDEX_ONLY_START, HOME_INDEX_SEARCH_START, HOME_INDEX_SEARCH_WAIT,
    HOME_SET_INDEX_POSITION,
    HOME_FINAL_MOVE_START, HOME_FINAL_MOVE_WAIT,
    HOME_LOCK, HOME_LOCK_WAIT, HOME_FINISHED, HOME_ABORT
} home_state_t;

typedef struct {
    double pos_cmd, pos_fb, motor_pos_fb, motor_offset, backlash_filt;
    double max_pos_limit, min_pos_limit, vel_limit;
    int on_pos_limit, on_neg_limit;
    simple_tp_t free_tp;
} joint_state_t;

typedef struct {
    home_state_t home_state;
    int homing, homed;
    int home_sw, index_enable;
    int pause_timer;
    double home_offset, home, home_final_vel, home_search_vel, home_latch_vel;
    int home_flags;
    int volatile_home;
    double servo_freq;
} home_data_t;

static void home_data_init(home_data_t *h, double servo_period) {
    memset(h, 0, sizeof(*h));
    h->servo_freq = (servo_period > 1e-9) ? (1.0 / servo_period) : 1000.0;
}

static void home_start_move(joint_state_t *j, double vel) {
    double range = j->max_pos_limit - j->min_pos_limit;
    j->free_tp.pos_cmd = j->pos_cmd + ((vel > 0.0) ? 2.0 : -2.0) * range;
    j->free_tp.max_vel = (fabs(vel) < j->vel_limit) ? fabs(vel) : j->vel_limit;
    j->free_tp.enable = 1;
}

static int home_run(home_data_t *h, joint_state_t *j) {
    double offset, tmp;
    int home_sw_active = h->home_sw;
    int immediate_state;

    if (h->home_state == HOME_IDLE) return 0;

    do {
        immediate_state = 0;
        switch (h->home_state) {
        case HOME_IDLE: break;
        case HOME_START:
            if ((h->home_flags & HOME_IS_SHARED) && home_sw_active) { h->home_state = HOME_IDLE; break; }
            if ((h->home_flags & HOME_NO_REHOME) && h->homed) { h->home_state = HOME_IDLE; break; }
            h->homing = 1; h->homed = 0;
            j->free_tp.enable = 0; h->pause_timer = 0;
            if (h->home_flags & HOME_ABSOLUTE_ENCODER) {
                h->home_state = HOME_SET_SWITCH_POSITION; immediate_state = 1; break;
            }
            h->home_state = HOME_UNLOCK_WAIT; immediate_state = 1;
            break;
        case HOME_UNLOCK: h->home_state = HOME_UNLOCK_WAIT; break;
        case HOME_UNLOCK_WAIT:
            if (h->home_search_vel == 0.0) {
                if (h->home_latch_vel == 0.0) { h->home_state = HOME_SET_SWITCH_POSITION; immediate_state = 1; }
                else if (h->home_flags & HOME_USE_INDEX) { h->home_state = HOME_INDEX_ONLY_START; immediate_state = 1; }
                else h->home_state = HOME_IDLE;
            } else {
                if (h->home_latch_vel != 0.0) { h->home_state = HOME_INITIAL_SEARCH_START; immediate_state = 1; }
                else h->home_state = HOME_IDLE;
            }
            break;
        case HOME_INITIAL_BACKOFF_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            home_start_move(j, -h->home_search_vel);
            h->home_state = HOME_INITIAL_BACKOFF_WAIT;
            break;
        case HOME_INITIAL_BACKOFF_WAIT:
            if (!home_sw_active) { j->free_tp.enable = 0; h->home_state = HOME_INITIAL_SEARCH_START; immediate_state = 1; break; }
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_ABORT; immediate_state = 1; }
            break;
        case HOME_INITIAL_SEARCH_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            if (home_sw_active) { h->home_state = HOME_INITIAL_BACKOFF_START; immediate_state = 1; break; }
            home_start_move(j, h->home_search_vel);
            h->home_state = HOME_INITIAL_SEARCH_WAIT;
            break;
        case HOME_INITIAL_SEARCH_WAIT:
            if (home_sw_active) { j->free_tp.enable = 0; h->home_state = HOME_SET_COARSE_POSITION; immediate_state = 1; break; }
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_ABORT; immediate_state = 1; }
            break;
        case HOME_SET_COARSE_POSITION:
            offset = h->home_offset - j->pos_fb;
            j->pos_cmd += offset; j->pos_fb += offset;
            j->free_tp.curr_pos += offset; j->motor_offset -= offset;
            tmp = h->home_search_vel * h->home_latch_vel;
            h->home_state = (tmp > 0.0) ? HOME_FINAL_BACKOFF_START : HOME_FALL_SEARCH_START;
            immediate_state = 1;
            break;
        case HOME_FINAL_BACKOFF_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            if (!home_sw_active) { h->home_state = HOME_IDLE; break; }
            home_start_move(j, -h->home_search_vel);
            h->home_state = HOME_FINAL_BACKOFF_WAIT;
            break;
        case HOME_FINAL_BACKOFF_WAIT:
            if (!home_sw_active) { j->free_tp.enable = 0; h->home_state = HOME_RISE_SEARCH_START; immediate_state = 1; break; }
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_ABORT; immediate_state = 1; }
            break;
        case HOME_RISE_SEARCH_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            if (home_sw_active) { h->home_state = HOME_IDLE; break; }
            home_start_move(j, h->home_latch_vel);
            h->home_state = HOME_RISE_SEARCH_WAIT;
            break;
        case HOME_RISE_SEARCH_WAIT:
            if (home_sw_active) {
                if (h->home_flags & HOME_USE_INDEX) { h->home_state = HOME_INDEX_SEARCH_START; immediate_state = 1; }
                else { j->free_tp.enable = 0; h->home_state = HOME_SET_SWITCH_POSITION; immediate_state = 1; }
                break;
            }
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_ABORT; immediate_state = 1; }
            break;
        case HOME_FALL_SEARCH_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            if (!home_sw_active) { h->home_state = HOME_IDLE; break; }
            home_start_move(j, h->home_latch_vel);
            h->home_state = HOME_FALL_SEARCH_WAIT;
            break;
        case HOME_FALL_SEARCH_WAIT:
            if (!home_sw_active) {
                if (h->home_flags & HOME_USE_INDEX) { h->home_state = HOME_INDEX_SEARCH_START; immediate_state = 1; }
                else { j->free_tp.enable = 0; h->home_state = HOME_SET_SWITCH_POSITION; immediate_state = 1; }
                break;
            }
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_ABORT; immediate_state = 1; }
            break;
        case HOME_SET_SWITCH_POSITION:
            if (h->home_flags & HOME_ABSOLUTE_ENCODER) offset = h->home_offset;
            else offset = h->home_offset - j->pos_fb;
            j->pos_cmd += offset; j->pos_fb += offset;
            j->free_tp.curr_pos += offset; j->motor_offset -= offset;
            if ((h->home_flags & HOME_ABSOLUTE_ENCODER) && (h->home_flags & HOME_NO_FINAL_MOVE)) {
                h->homed = 1; h->home_state = HOME_FINISHED; immediate_state = 1; break;
            }
            h->home_state = HOME_FINAL_MOVE_START; immediate_state = 1;
            break;
        case HOME_INDEX_ONLY_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            offset = h->home_offset - j->pos_fb;
            j->pos_cmd += offset; j->pos_fb += offset;
            j->free_tp.curr_pos += offset; j->motor_offset -= offset;
            h->index_enable = 1;
            home_start_move(j, h->home_latch_vel);
            h->home_state = HOME_INDEX_SEARCH_WAIT;
            break;
        case HOME_INDEX_SEARCH_START:
            h->index_enable = 1;
            h->home_state = HOME_INDEX_SEARCH_WAIT;
            break;
        case HOME_INDEX_SEARCH_WAIT:
            if (!h->index_enable) { j->free_tp.enable = 0; h->home_state = HOME_SET_INDEX_POSITION; immediate_state = 1; break; }
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_ABORT; immediate_state = 1; }
            break;
        case HOME_SET_INDEX_POSITION:
            j->motor_offset = -h->home_offset;
            j->pos_fb = j->motor_pos_fb - (j->backlash_filt + j->motor_offset);
            j->pos_cmd = j->pos_fb; j->free_tp.curr_pos = j->pos_fb;
            if (h->home_flags & HOME_INDEX_NO_ENCODER_RESET) {
                offset = h->home_offset - j->pos_fb;
                j->pos_cmd += offset; j->pos_fb += offset;
                j->free_tp.curr_pos += offset; j->motor_offset -= offset;
            }
            h->home_state = HOME_FINAL_MOVE_START; immediate_state = 1;
            break;
        case HOME_FINAL_MOVE_START:
            if (j->free_tp.active) { h->pause_timer = 0; break; }
            if (h->pause_timer < (int)(HOME_DELAY * h->servo_freq)) { h->pause_timer++; break; }
            h->pause_timer = 0;
            j->free_tp.pos_cmd = h->home;
            j->free_tp.max_vel = (h->home_final_vel > 0.0) ? fabs(h->home_final_vel) : j->vel_limit;
            if (j->free_tp.max_vel > j->vel_limit) j->free_tp.max_vel = j->vel_limit;
            j->free_tp.enable = 1;
            h->home_state = HOME_FINAL_MOVE_WAIT;
            break;
        case HOME_FINAL_MOVE_WAIT:
            if (!j->free_tp.active) { j->free_tp.enable = 0; h->home_state = HOME_LOCK; immediate_state = 1; break; }
            if ((j->on_pos_limit || j->on_neg_limit) && !(h->home_flags & HOME_IGNORE_LIMITS)) {
                h->home_state = HOME_ABORT; immediate_state = 1;
            }
            break;
        case HOME_LOCK:
            h->home_state = HOME_LOCK_WAIT; immediate_state = 1;
            break;
        case HOME_LOCK_WAIT:
            h->home_state = HOME_FINISHED; immediate_state = 1;
            break;
        case HOME_FINISHED:
            h->homing = 0; h->homed = 1; h->home_state = HOME_IDLE;
            if (!(h->home_flags & HOME_ABSOLUTE_ENCODER))
                j->free_tp.curr_pos = h->home;
            immediate_state = 1;
            break;
        case HOME_ABORT:
            h->homing = 0; h->homed = 0; h->index_enable = 0;
            j->free_tp.enable = 0; h->home_state = HOME_IDLE;
            immediate_state = 1;
            break;
        default:
            h->home_state = HOME_ABORT; immediate_state = 1;
            break;
        }
    } while (immediate_state);
    return 1;
}

/* ============================================================
 * JOINT CONTROL COMPONENT
 * ============================================================ */

typedef struct {
    /* Motor interface */
    gomc_hal_float_t *motor_pos_cmd;
    gomc_hal_float_t *motor_pos_fb;
    gomc_hal_bit_t   *amp_enable;
    gomc_hal_bit_t   *amp_fault;
    /* Limits */
    gomc_hal_bit_t   *pos_lim_sw;
    gomc_hal_bit_t   *neg_lim_sw;
    /* Homing */
    gomc_hal_bit_t   *home_sw;
    gomc_hal_bit_t   *index_enable;
    gomc_hal_bit_t   *do_home;
    gomc_hal_bit_t   *do_cancel_home;
    gomc_hal_bit_t   *homing_out;
    gomc_hal_bit_t   *homed_out;
    gomc_hal_s32_t   *home_state_out;
    /* Keyboard jog */
    gomc_hal_bit_t   *jog_enable;
    gomc_hal_float_t *jog_vel;
    gomc_hal_float_t *jog_incr;
    gomc_hal_bit_t   *jog_active;
    /* Wheel jog */
    gomc_hal_s32_t   *jjog_counts;
    gomc_hal_bit_t   *jjog_enable;
    gomc_hal_float_t *jjog_scale;
    gomc_hal_float_t *jjog_accel_fraction;
    gomc_hal_bit_t   *jjog_vel_mode;
    /* Coord mode */
    gomc_hal_bit_t   *coord_mode;
    gomc_hal_float_t *coord_pos_cmd;
    gomc_hal_float_t *coord_vel_cmd;
    gomc_hal_float_t *pos_fb_out;
    gomc_hal_float_t *motor_pos_fb_out;
    gomc_hal_bit_t   *kb_jog_active_out;
    gomc_hal_bit_t   *wheel_jog_active_out;
    /* Control/status */
    gomc_hal_bit_t   *enable;
    gomc_hal_bit_t   *in_position;
    gomc_hal_bit_t   *error_out;
    gomc_hal_float_t *f_error;
    gomc_hal_float_t *f_error_lim;
    gomc_hal_bit_t   *f_errored;
    gomc_hal_bit_t   *faulted;
    gomc_hal_bit_t   *pos_hard_limit;
    gomc_hal_bit_t   *neg_hard_limit;
    gomc_hal_float_t *joint_pos_cmd;
    gomc_hal_float_t *joint_pos_fb;
    gomc_hal_float_t *vel_cmd_out;
    gomc_hal_bit_t   *override_limits;
    gomc_hal_bit_t   *unlock;
    gomc_hal_bit_t   *is_unlocked;
} jc_pins_t;

typedef struct {
    cmod_t base;
    const cmod_env_t *env;
    int comp_id;
    char name[GOMC_HAL_NAME_LEN + 1];
    jc_pins_t *pins;
    /* HAL params (stored directly) */
    int32_t type;
    double max_vel, max_acc, min_limit, max_limit;
    double max_ferror, min_ferror, backlash;
    double home_offset, home_pos, home_search_vel, home_latch_vel, home_final_vel;
    int32_t home_flags;
    gomc_hal_bit_t volatile_home_param;
    /* Internal state */
    joint_state_t jstate;
    home_data_t home;
    double vel_cmd, motor_pos_cmd, backlash_corr, backlash_vel;
    double ferror, ferror_limit, max_jog_limit, min_jog_limit;
    int kb_jjog_active, wheel_jjog_active, old_jjog_counts;
    int enabled, was_enabled, in_error, ferr_flag;
    int prev_do_home, prev_jog_enable;
} inst_t;

static void refresh_jog_limits(inst_t *c) {
    if (c->home.homed) {
        c->max_jog_limit = c->max_limit;
        c->min_jog_limit = c->min_limit;
    } else {
        double range = c->max_limit - c->min_limit;
        c->max_jog_limit = c->jstate.pos_fb + range;
        c->min_jog_limit = c->jstate.pos_fb - range;
    }
}

static void joint_ctrl_update(void *arg, long period) {
    inst_t *c = (inst_t *)arg;
    double fperiod = period * 1e-9;
    jc_pins_t *p = c->pins;

    if (fperiod > 1e-9) c->home.servo_freq = 1.0 / fperiod;

    /* Read inputs */
    c->jstate.motor_pos_fb = *(p->motor_pos_fb);
    c->jstate.on_pos_limit = *(p->pos_lim_sw) ? 1 : 0;
    c->jstate.on_neg_limit = *(p->neg_lim_sw) ? 1 : 0;
    c->home.home_sw = *(p->home_sw) ? 1 : 0;
    c->home.index_enable = *(p->index_enable) ? 1 : 0;
    int enable = *(p->enable) ? 1 : 0;
    int override_limits = *(p->override_limits) ? 1 : 0;
    int amp_fault = *(p->amp_fault) ? 1 : 0;

    /* Homing trigger */
    int do_home_cur = *(p->do_home) ? 1 : 0;
    if (do_home_cur && !c->prev_do_home && enable) {
        c->home.home_state = HOME_START;
    }
    c->prev_do_home = do_home_cur;
    if (*(p->do_cancel_home) && (c->home.homing || c->home.home_state != HOME_IDLE))
        c->home.home_state = HOME_ABORT;

    int homing_active = c->home.homing || (c->home.home_state != HOME_IDLE);

    /* Sync params into homing */
    c->home.home_offset = c->home_offset;
    c->home.home = c->home_pos;
    c->home.home_search_vel = c->home_search_vel;
    c->home.home_latch_vel = c->home_latch_vel;
    c->home.home_final_vel = c->home_final_vel;
    c->home.home_flags = c->home_flags;
    c->home.volatile_home = c->volatile_home_param;
    c->jstate.max_pos_limit = c->max_limit;
    c->jstate.min_pos_limit = c->min_limit;
    c->jstate.vel_limit = c->max_vel;

    /* Position feedback */
    if ((c->home.home_state == HOME_INDEX_SEARCH_WAIT) && !c->home.index_enable)
        c->jstate.pos_fb = c->jstate.pos_cmd;
    else
        c->jstate.pos_fb = c->jstate.motor_pos_fb - (c->jstate.backlash_filt + c->jstate.motor_offset);

    /* Following error */
    c->ferror = c->jstate.pos_cmd - c->jstate.pos_fb;
    double abs_ferror = fabs(c->ferror);
    c->ferror_limit = (c->max_vel > 0.0) ? c->max_ferror * fabs(c->vel_cmd) / c->max_vel : 0.0;
    if (c->ferror_limit < c->min_ferror) c->ferror_limit = c->min_ferror;
    c->ferr_flag = (abs_ferror > c->ferror_limit) ? 1 : 0;

    /* Fault check */
    c->in_error = 0;
    if (!override_limits && !homing_active) {
        if (c->jstate.on_pos_limit || c->jstate.on_neg_limit) c->in_error = 1;
    }
    if (amp_fault) c->in_error = 1;
    if (c->ferr_flag) c->in_error = 1;

    /* Enable/disable */
    if (!enable || c->in_error) {
        if (c->was_enabled) {
            c->jstate.free_tp.enable = 0;
            c->jstate.free_tp.curr_vel = 0.0;
            c->kb_jjog_active = 0;
            c->wheel_jjog_active = 0;
            if (c->home.volatile_home) { c->home.homed = 0; }
        }
        c->enabled = 0;
    } else {
        c->enabled = 1;
    }
    c->was_enabled = c->enabled;

    int coord_active = *(p->coord_mode) && !homing_active && c->enabled;

    if (!coord_active) {
        /* Homing */
        if (homing_active) home_run(&c->home, &c->jstate);

        /* Keyboard jog */
        if (c->enabled && !homing_active) {
            int jog_enable_cur = *(p->jog_enable) ? 1 : 0;
            if (jog_enable_cur && !c->prev_jog_enable) {
                double vel = *(p->jog_vel);
                double incr = *(p->jog_incr);
                if (vel != 0.0 && !c->wheel_jjog_active) {
                    refresh_jog_limits(c);
                    if (incr == 0.0) {
                        if (vel > 0.0 && !c->jstate.on_pos_limit)
                            c->jstate.free_tp.pos_cmd = c->max_jog_limit;
                        else if (vel < 0.0 && !c->jstate.on_neg_limit)
                            c->jstate.free_tp.pos_cmd = c->min_jog_limit;
                    } else {
                        double target = c->jstate.free_tp.pos_cmd + (vel > 0.0 ? 1.0 : -1.0) * fabs(incr);
                        if (target > c->max_jog_limit) target = c->max_jog_limit;
                        if (target < c->min_jog_limit) target = c->min_jog_limit;
                        c->jstate.free_tp.pos_cmd = target;
                    }
                    c->jstate.free_tp.max_vel = fabs(vel);
                    c->jstate.free_tp.max_acc = c->max_acc;
                    c->jstate.free_tp.enable = 1;
                    c->kb_jjog_active = 1;
                    c->wheel_jjog_active = 0;
                }
            }
            if (!jog_enable_cur && c->prev_jog_enable && c->kb_jjog_active) {
                c->jstate.free_tp.enable = 0;
                c->kb_jjog_active = 0;
            }
            if (c->kb_jjog_active && !c->jstate.free_tp.active && !c->jstate.free_tp.enable)
                c->kb_jjog_active = 0;
            c->prev_jog_enable = jog_enable_cur;
        }

        /* Wheel jog */
        if (c->enabled && !homing_active) {
            double jaccel_limit = c->max_acc;
            if (*(p->jjog_accel_fraction) > 0.0 && *(p->jjog_accel_fraction) <= 1.0)
                jaccel_limit = *(p->jjog_accel_fraction) * c->max_acc;

            int new_counts = *(p->jjog_counts);
            int delta = new_counts - c->old_jjog_counts;
            c->old_jjog_counts = new_counts;

            if (delta != 0 && *(p->jjog_enable) && !c->kb_jjog_active) {
                double distance = (double)delta * *(p->jjog_scale);
                int do_jog = 1;
                if (distance > 0.0 && c->jstate.on_pos_limit) do_jog = 0;
                if (distance < 0.0 && c->jstate.on_neg_limit) do_jog = 0;
                double pos = c->jstate.free_tp.pos_cmd + distance;
                if (do_jog) {
                    refresh_jog_limits(c);
                    if (pos > c->max_jog_limit || pos < c->min_jog_limit) do_jog = 0;
                }
                if (do_jog && *(p->jjog_vel_mode) && jaccel_limit > 0.0) {
                    double v2 = c->max_vel;
                    double stop_dist = v2 * v2 / (2.0 * jaccel_limit);
                    double cur_pos = c->jstate.pos_cmd;
                    if (pos > cur_pos + stop_dist) pos = cur_pos + stop_dist;
                    if (pos < cur_pos - stop_dist) pos = cur_pos - stop_dist;
                }
                if (do_jog) {
                    c->jstate.free_tp.pos_cmd = pos;
                    c->jstate.free_tp.max_vel = c->max_vel;
                    c->jstate.free_tp.max_acc = jaccel_limit;
                    c->jstate.free_tp.enable = 1;
                    c->wheel_jjog_active = 1;
                    c->kb_jjog_active = 0;
                }
            }
            if (c->wheel_jjog_active && !c->jstate.free_tp.active)
                c->wheel_jjog_active = 0;
        }

        /* Trajectory planner */
        if (c->jstate.free_tp.max_vel > c->max_vel) c->jstate.free_tp.max_vel = c->max_vel;
        if (c->jstate.free_tp.max_acc > c->max_acc) c->jstate.free_tp.max_acc = c->max_acc;
        simple_tp_update(&c->jstate.free_tp, fperiod);
        c->jstate.pos_cmd = c->jstate.free_tp.curr_pos;
        c->vel_cmd = c->jstate.free_tp.curr_vel;
    } else {
        /* Coord mode */
        c->jstate.pos_cmd = *(p->coord_pos_cmd);
        c->vel_cmd = *(p->coord_vel_cmd);
        c->jstate.free_tp.curr_pos = c->jstate.pos_cmd;
        c->jstate.free_tp.pos_cmd = c->jstate.pos_cmd;
        c->jstate.free_tp.curr_vel = 0.0;
        c->jstate.free_tp.enable = 0;
        c->jstate.free_tp.active = 0;
        c->kb_jjog_active = 0;
        c->wheel_jjog_active = 0;
        c->old_jjog_counts = *(p->jjog_counts);
        c->prev_jog_enable = *(p->jog_enable) ? 1 : 0;
    }

    /* Backlash compensation */
    if (c->vel_cmd > 0.0) c->backlash_corr = 0.5 * c->backlash;
    else if (c->vel_cmd < 0.0) c->backlash_corr = -0.5 * c->backlash;

    double bv_max = 0.5 * c->max_vel;
    double ba_max = 0.5 * c->max_acc;
    double bv = c->backlash_vel;
    double bs_to_go, bds_vel, bds_stop, bds_acc, bdv_acc;

    if (c->backlash_corr >= c->jstate.backlash_filt) {
        bs_to_go = c->backlash_corr - c->jstate.backlash_filt;
        if (bs_to_go > 0.0) {
            bds_vel = bv * fperiod; bdv_acc = ba_max * fperiod;
            bds_stop = 0.5 * (bv + bdv_acc) * (bv + bdv_acc) / ba_max;
            if (bs_to_go <= bds_stop + bds_vel) {
                if (bv > bdv_acc) { c->backlash_vel -= bdv_acc; c->jstate.backlash_filt += bds_vel - 0.5*bdv_acc*fperiod; }
                else { c->backlash_vel = 0; c->jstate.backlash_filt = c->backlash_corr; }
            } else {
                if (bv + bdv_acc > bv_max) bdv_acc = bv_max - bv;
                bds_acc = 0.5 * bdv_acc * fperiod;
                bds_stop = 0.5 * (bv + bdv_acc) * (bv + bdv_acc) / ba_max;
                if (bs_to_go > bds_stop + bds_vel + bds_acc) { c->backlash_vel += bdv_acc; c->jstate.backlash_filt += bds_vel + bds_acc; }
                else c->jstate.backlash_filt += bds_vel;
            }
        } else if (bs_to_go < 0.0) { c->backlash_vel = 0; c->jstate.backlash_filt = c->backlash_corr; }
    } else {
        bs_to_go = c->jstate.backlash_filt - c->backlash_corr;
        if (bs_to_go > 0.0) {
            bds_vel = fabs(bv) * fperiod; bdv_acc = ba_max * fperiod;
            bds_stop = 0.5 * (bv - bdv_acc) * (bv - bdv_acc) / ba_max;
            if (bs_to_go <= bds_stop + bds_vel) {
                if (-bv > bdv_acc) { c->backlash_vel += bdv_acc; c->jstate.backlash_filt -= bds_vel - 0.5*bdv_acc*fperiod; }
                else { c->backlash_vel = 0; c->jstate.backlash_filt = c->backlash_corr; }
            } else {
                if (-bv + bdv_acc > bv_max) bdv_acc = bv_max + bv;
                bds_acc = 0.5 * bdv_acc * fperiod;
                bds_stop = 0.5 * (bv - bdv_acc) * (bv - bdv_acc) / ba_max;
                if (bs_to_go > bds_stop + bds_vel + bds_acc) { c->backlash_vel -= bdv_acc; c->jstate.backlash_filt -= bds_vel + bds_acc; }
                else c->jstate.backlash_filt -= bds_vel;
            }
        } else if (bs_to_go < 0.0) { c->backlash_vel = 0; c->jstate.backlash_filt = c->backlash_corr; }
    }

    c->motor_pos_cmd = c->jstate.pos_cmd + c->jstate.backlash_filt + c->jstate.motor_offset;

    /* Write outputs */
    *(p->motor_pos_cmd) = c->motor_pos_cmd;
    *(p->joint_pos_cmd) = c->jstate.pos_cmd;
    *(p->joint_pos_fb) = c->jstate.pos_fb;
    *(p->vel_cmd_out) = c->vel_cmd;
    *(p->amp_enable) = c->enabled;
    *(p->index_enable) = c->home.index_enable ? 1 : 0;
    *(p->homing_out) = c->home.homing ? 1 : 0;
    *(p->homed_out) = c->home.homed ? 1 : 0;
    *(p->home_state_out) = (int32_t)c->home.home_state;
    *(p->jog_active) = (c->kb_jjog_active || c->wheel_jjog_active) ? 1 : 0;
    *(p->in_position) = !c->jstate.free_tp.active;
    *(p->error_out) = c->in_error;
    *(p->f_error) = c->ferror;
    *(p->f_error_lim) = c->ferror_limit;
    *(p->f_errored) = c->ferr_flag;
    *(p->faulted) = amp_fault;
    *(p->pos_hard_limit) = c->jstate.on_pos_limit;
    *(p->neg_hard_limit) = c->jstate.on_neg_limit;
    if (!c->enabled) *(p->unlock) = 0;
    *(p->pos_fb_out) = c->jstate.pos_fb;
    *(p->motor_pos_fb_out) = c->jstate.motor_pos_fb;
    *(p->kb_jog_active_out) = c->kb_jjog_active ? 1 : 0;
    *(p->wheel_jog_active_out) = c->wheel_jjog_active ? 1 : 0;
}

static void inst_destroy(cmod_t *self) {
    inst_t *inst = (inst_t *)self;
    if (inst->comp_id > 0)
        inst->env->hal->exit(inst->env->hal->ctx, inst->comp_id);
    inst->env->rtapi->free(inst->env->rtapi->ctx, inst);
}

#define P_BIT_IN(ptr, pn)  do { r = gomc_hal_pin_bit_newf(env->hal, GOMC_HAL_IN, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)
#define P_BIT_OUT(ptr, pn) do { r = gomc_hal_pin_bit_newf(env->hal, GOMC_HAL_OUT, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)
#define P_BIT_IO(ptr, pn)  do { r = gomc_hal_pin_bit_newf(env->hal, GOMC_HAL_IO, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)
#define P_FLT_IN(ptr, pn)  do { r = gomc_hal_pin_float_newf(env->hal, GOMC_HAL_IN, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)
#define P_FLT_OUT(ptr, pn) do { r = gomc_hal_pin_float_newf(env->hal, GOMC_HAL_OUT, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)
#define P_S32_IN(ptr, pn)  do { r = gomc_hal_pin_s32_newf(env->hal, GOMC_HAL_IN, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)
#define P_S32_OUT(ptr, pn) do { r = gomc_hal_pin_s32_newf(env->hal, GOMC_HAL_OUT, &pins->ptr, cid, "%s." pn, name); if(r) goto err; } while(0)

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out) {
    inst_t *inst;
    jc_pins_t *pins;
    int r, cid;
    char buf[GOMC_HAL_NAME_LEN + 1];

    (void)argc; (void)argv;

    inst = env->rtapi->calloc(env->rtapi->ctx, sizeof(*inst));
    if (!inst) return -ENOMEM;

    inst->base.Destroy = inst_destroy;
    inst->env = env;
    strncpy(inst->name, name, sizeof(inst->name) - 1);

    cid = env->hal->init(env->hal->ctx, name, env->dl_handle, GOMC_HAL_COMP_REALTIME);
    if (cid < 0) goto err;
    inst->comp_id = cid;

    inst->pins = env->hal->malloc(env->hal->ctx, sizeof(jc_pins_t));
    if (!inst->pins) goto err;
    memset(inst->pins, 0, sizeof(jc_pins_t));
    pins = inst->pins;

    /* Motor interface */
    P_FLT_OUT(motor_pos_cmd, "motor-pos-cmd");
    P_FLT_IN(motor_pos_fb, "motor-pos-fb");
    P_BIT_OUT(amp_enable, "amp-enable");
    P_BIT_IN(amp_fault, "amp-fault");
    /* Limits */
    P_BIT_IN(pos_lim_sw, "pos-lim-sw-in");
    P_BIT_IN(neg_lim_sw, "neg-lim-sw-in");
    /* Homing */
    P_BIT_IN(home_sw, "home-sw-in");
    P_BIT_IO(index_enable, "index-enable");
    P_BIT_IN(do_home, "do-home");
    P_BIT_IN(do_cancel_home, "do-cancel-home");
    P_BIT_OUT(homing_out, "homing");
    P_BIT_OUT(homed_out, "homed");
    P_S32_OUT(home_state_out, "home-state");
    /* Keyboard jog */
    P_BIT_IN(jog_enable, "jog-enable");
    P_FLT_IN(jog_vel, "jog-vel");
    P_FLT_IN(jog_incr, "jog-incr");
    P_BIT_OUT(jog_active, "jog-active");
    /* Wheel jog */
    P_S32_IN(jjog_counts, "jjog-counts");
    P_BIT_IN(jjog_enable, "jjog-enable");
    P_FLT_IN(jjog_scale, "jjog-scale");
    P_FLT_IN(jjog_accel_fraction, "jjog-accel-fraction");
    P_BIT_IN(jjog_vel_mode, "jjog-vel-mode");
    /* Coord mode */
    P_BIT_IN(coord_mode, "coord-mode");
    P_FLT_IN(coord_pos_cmd, "coord-pos-cmd");
    P_FLT_IN(coord_vel_cmd, "coord-vel-cmd");
    P_FLT_OUT(pos_fb_out, "pos-fb-out");
    P_FLT_OUT(motor_pos_fb_out, "motor-pos-fb-out");
    P_BIT_OUT(kb_jog_active_out, "kb-jog-active");
    P_BIT_OUT(wheel_jog_active_out, "wheel-jog-active");
    /* Control/status */
    P_BIT_IN(enable, "enable");
    P_BIT_OUT(in_position, "in-position");
    P_BIT_OUT(error_out, "error");
    P_FLT_OUT(f_error, "f-error");
    P_FLT_OUT(f_error_lim, "f-error-lim");
    P_BIT_OUT(f_errored, "f-errored");
    P_BIT_OUT(faulted, "faulted");
    P_BIT_OUT(pos_hard_limit, "pos-hard-limit");
    P_BIT_OUT(neg_hard_limit, "neg-hard-limit");
    P_FLT_OUT(joint_pos_cmd, "joint-pos-cmd");
    P_FLT_OUT(joint_pos_fb, "joint-pos-fb");
    P_FLT_OUT(vel_cmd_out, "vel-cmd");
    P_BIT_IN(override_limits, "override-limits");
    P_BIT_OUT(unlock, "unlock");
    P_BIT_IN(is_unlocked, "is-unlocked");

    /* Parameters */
    r = gomc_hal_param_s32_newf(env->hal, GOMC_HAL_RW, &inst->type, cid, "%s.type", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->max_vel, cid, "%s.max-vel", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->max_acc, cid, "%s.max-acc", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->min_limit, cid, "%s.min-limit", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->max_limit, cid, "%s.max-limit", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->max_ferror, cid, "%s.max-ferror", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->min_ferror, cid, "%s.min-ferror", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->backlash, cid, "%s.backlash", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->home_offset, cid, "%s.home-offset", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->home_pos, cid, "%s.home-pos", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->home_search_vel, cid, "%s.home-search-vel", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->home_latch_vel, cid, "%s.home-latch-vel", name); if(r) goto err;
    r = gomc_hal_param_float_newf(env->hal, GOMC_HAL_RW, &inst->home_final_vel, cid, "%s.home-final-vel", name); if(r) goto err;
    r = gomc_hal_param_s32_newf(env->hal, GOMC_HAL_RW, &inst->home_flags, cid, "%s.home-flags", name); if(r) goto err;
    r = gomc_hal_param_bit_newf(env->hal, GOMC_HAL_RW, &inst->volatile_home_param, cid, "%s.volatile-home", name); if(r) goto err;

    /* Defaults */
    inst->max_vel = 1.0;
    inst->max_acc = 10.0;
    inst->min_limit = -1e20;
    inst->max_limit = 1e20;
    inst->max_ferror = 1.0;
    inst->min_ferror = 0.01;
    inst->max_jog_limit = 1e20;
    inst->min_jog_limit = -1e20;
    inst->jstate.free_tp.max_vel = 1.0;
    inst->jstate.free_tp.max_acc = 10.0;
    home_data_init(&inst->home, 0.001);

    /* Export function */
    snprintf(buf, sizeof(buf), "%s.update", name);
    r = env->hal->export_funct(env->hal->ctx, buf, joint_ctrl_update, inst, 1, 0, cid);
    if (r) goto err;

    r = env->hal->ready(env->hal->ctx, cid);
    if (r) goto err;

    *out = &inst->base;
    return 0;

err:
    if (inst->comp_id > 0)
        env->hal->exit(env->hal->ctx, inst->comp_id);
    env->rtapi->free(env->rtapi->ctx, inst);
    return -1;
}
