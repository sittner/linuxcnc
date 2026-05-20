/********************************************************************
* Description: homing.c
*   code to handle homing - originally in control.c, but moved out
*   to improve modularity and keep control.c from bloating
*
* Author: jmkasunich
* License: GPL Version 2
* Created on:
* System: Linux
*
* Copyright (c) 2004 All rights reserved.
********************************************************************/

#include "rtapi.h"
#include "rtapi_math.h"
#include "motion.h"
#include "homing.h"
#include "hal.h"

#include <stdint.h>
#include <string.h>
#include "gomc_env.h"
#include "home_api.h"
#include "mot_api.h"

static double servo_freq;
static const mot_callbacks_t *_mot;
static int all_joints;     // motmod num_joints (typ ini file: [KINS]JOINTS)
static int extra_joints;   // motmod num_extrajoints


#define ABS(x) (((x) < 0) ? -(x) : (x))

// Mark strings for translation, but defer translation to userspace
#define _(s) (s)

// Forward declarations for base_* functions used before definition
static bool base_get_allhomed(void);
static bool base_get_homing_is_active(void);
static bool base_get_homing(int jno);
static bool base_get_homed(int jno);



/***********************************************************************
*                         LOCAL CONSTANTS                              *
************************************************************************/

/* Length of delay between homing motions - this is intended to
   ensure that all motion has ceased and switch bouncing has
   ended.  We might want to make this user adjustable, but for
   now it's a constant.  It is in seconds */
#define HOME_DELAY 0.100

#define MAX_HOME_SEQUENCES EMCMOT_MAX_JOINTS

/***********************************************************************
*                  LOCAL VARIABLE DECLARATIONS                         *
************************************************************************/

// internal SEQUENCE states
typedef enum {
  HOME_SEQUENCE_IDLE = 0,        // valid start state
  HOME_SEQUENCE_START,           // valid start state
  HOME_SEQUENCE_DO_ONE_JOINT,    // valid start state
  HOME_SEQUENCE_DO_ONE_SEQUENCE, // valid start state
  HOME_SEQUENCE_START_JOINTS,    // internal usage
  HOME_SEQUENCE_WAIT_JOINTS,     // internal usage
} home_sequence_state_t;

static home_sequence_state_t sequence_state;
static int  current_sequence = 0;
static bool homing_active;

/* internal states for homing */
typedef enum {
  HOME_IDLE = 0,
  HOME_START,// 1
  HOME_UNLOCK,// 2
  HOME_UNLOCK_WAIT,// 3
  HOME_INITIAL_BACKOFF_START,// 4
  HOME_INITIAL_BACKOFF_WAIT,// 5
  HOME_INITIAL_SEARCH_START,// 6
  HOME_INITIAL_SEARCH_WAIT,// 7
  HOME_SET_COARSE_POSITION,// 8
  HOME_FINAL_BACKOFF_START,// 9
  HOME_FINAL_BACKOFF_WAIT,// 10
  HOME_RISE_SEARCH_START,// 11
  HOME_RISE_SEARCH_WAIT,// 12
  HOME_FALL_SEARCH_START,// 13
  HOME_FALL_SEARCH_WAIT,// 14
  HOME_SET_SWITCH_POSITION,// 15
  HOME_INDEX_ONLY_START,// 16
  HOME_INDEX_SEARCH_START,// 17
  HOME_INDEX_SEARCH_WAIT,// 18
  HOME_SET_INDEX_POSITION,// 19
  HOME_FINAL_MOVE_START,// 20
  HOME_FINAL_MOVE_WAIT,// 21
  HOME_LOCK,// 22
  HOME_LOCK_WAIT,// 23
  HOME_FINISHED,// 24
  HOME_ABORT// 25
} home_state_t;

// local per-joint data (includes hal pin data)
typedef struct {
  home_state_t home_state;           // OUT pin
  bool         homing;               // OUT pin
  bool         homed;                // OUT pin
  bool         home_sw;              // IN  pin
  bool         index_enable;         // IO  pin
  bool         joint_in_sequence;
  int          pause_timer;
  double       home_offset;          // intfc, updateable
  double       home;                 // intfc, updateable
  double       home_final_vel;       // intfc
  double       home_search_vel;      // intfc
  double       home_latch_vel;       // intfc
  int          home_flags;           // intfc
  int          home_sequence;        // intfc, updateable
  bool         volatile_home;        // intfc
  bool         home_is_synchronized;
} home_local_data;

static  home_local_data H[EMCMOT_MAX_JOINTS];

// data for per-joint homing-specific hal pins:
typedef struct {
    hal_bit_t *home_sw;      // home switch input
    hal_bit_t *homing;       // joint is homing
    hal_bit_t *homed;        // joint was homed
    hal_bit_t *index_enable; // motmod sets: request reset on index
                             //        encoder clears: index arrived
    hal_s32_t *home_state;   // homing state machine state
} one_joint_home_data_t;

typedef struct {
    one_joint_home_data_t jhd[EMCMOT_MAX_JOINTS];
} all_joints_home_data_t;

static all_joints_home_data_t *joint_home_data = 0;

/***********************************************************************
*                      LOCAL FUNCTIONS                                 *
************************************************************************/

/* a couple of helper functions with code that would otherwise be
   repeated in several different states of the homing state machine */

/* 'home_start_move()' starts a move at the specified velocity.  The
   length of the move is equal to twice the overall range of the joint,
   but the intent is that something (like a home switch or index pulse)
   will stop it before that point. */
static void home_start_move(int jno, double vel)
{
    double joint_range;

    /* set up a long move */
    joint_range = _mot->joint_get_max_pos_limit(_mot->ctx, jno) - _mot->joint_get_min_pos_limit(_mot->ctx, jno);
    if (vel > 0.0) {
        _mot->joint_set_free_tp_pos_cmd(_mot->ctx, jno, _mot->joint_get_pos_cmd(_mot->ctx, jno) + 2.0 * joint_range);
    } else {
        _mot->joint_set_free_tp_pos_cmd(_mot->ctx, jno, _mot->joint_get_pos_cmd(_mot->ctx, jno) - 2.0 * joint_range);
    }
    if (fabs(vel) < _mot->joint_get_vel_limit(_mot->ctx, jno)) {
        _mot->joint_set_free_tp_max_vel(_mot->ctx, jno, fabs(vel));
    } else {
        /* clamp on max vel for this joint */
        _mot->joint_set_free_tp_max_vel(_mot->ctx, jno, _mot->joint_get_vel_limit(_mot->ctx, jno));
    }
    /* start the move */
    _mot->joint_set_free_tp_enable(_mot->ctx, jno, 1);
} // home_start_move()

/* 'home_do_moving_checks()' is called from states where the machine
   is supposed to be moving.  It checks to see if the machine has
   hit a limit, or if the move has stopped.  (Normally such moves
   will be terminated by the home switch or an index pulse or some
   other event, if the move goes to completion, something is wrong.) */
static bool home_do_moving_checks(int jno)
{
    /* check for limit switches */
    if ( _mot->joint_get_on_pos_limit(_mot->ctx, jno) ||  _mot->joint_get_on_neg_limit(_mot->ctx, jno)) {
        /* on limit, check to see if we should trip */
        if (!(H[jno].home_flags & HOME_IGNORE_LIMITS)) {
            /* not ignoring limits, time to quit */
            rtapi_print_msg(RTAPI_MSG_ERR, _("j%d hit limit in home state %d"),jno, H[jno].home_state);
            H[jno].home_state = HOME_ABORT;
            return 1; // abort reqd
        }
    }
    /* check for reached end of move */
    if (! _mot->joint_get_free_tp_active(_mot->ctx, jno)) {
        /* reached end of move without hitting switch */
         _mot->joint_set_free_tp_enable(_mot->ctx, jno, 0);
        rtapi_print_msg(RTAPI_MSG_ERR,_("j%d end of move in home state %d"),jno, H[jno].home_state);
        H[jno].home_state = HOME_ABORT;
        return 1; // abort reqd
    }
    return 0;
} // home_do_moving_checks()

#define ABORT_CHECK(joint_num) do { \
    if (home_do_moving_checks(joint_num)) { \
        H[joint_num].home_state = HOME_ABORT; \
        immediate_state = 1; \
    } \
} while(0);

static void update_home_is_synchronized(void) {
    // invoke anytime H[*].home_sequence is altered
    int jno,jj;

    for (jno = 0; jno < all_joints; jno++) {
        H[jno].home_is_synchronized = 0;
        if (H[jno].home_sequence < 0) {
            // neg: sync all joints with same ABS(H[jno].home_sequence):
            for (jj = 0; jj < all_joints; jj++) {
                if (ABS(H[jj].home_sequence) == ABS(H[jno].home_sequence)) {
                    H[jj].home_is_synchronized = 1;
                }
            }
        }
    }
}

static int base_make_joint_home_pins(int id,int njoints)
{
//NOTE: motmod supplies the component id
    int jno,retval;
    one_joint_home_data_t *addr;

    joint_home_data = hal_malloc(sizeof(all_joints_home_data_t));
    if (joint_home_data == 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, _("HOMING: all_joints_home_data_t malloc failed\n"));
        return -1;
    }

    retval = 0;
    for (jno = 0; jno < njoints; jno++) {
        addr = &(joint_home_data->jhd[jno]);

        retval += hal_pin_bit_newf(HAL_IN, &(addr->home_sw), id,
                                  "joint.%d.home-sw-in", jno);
        retval += hal_pin_bit_newf(HAL_OUT, &(addr->homing), id,
                                  "joint.%d.homing", jno);
        retval += hal_pin_bit_newf(HAL_OUT, &(addr->homed), id,
                                  "joint.%d.homed", jno);
        retval += hal_pin_s32_newf(HAL_OUT, &(addr->home_state), id,
                                  "joint.%d.home-state", jno);
        retval += hal_pin_bit_newf(HAL_IO, &(addr->index_enable), id,
                                  "joint.%d.index-enable", jno);
    }
    return retval;
} // base_make_joint_home_pins()

static void do_home_all(void)
{
    if (!base_get_homing_is_active() ) {
        sequence_state = HOME_SEQUENCE_START;
    }
} // do_home_all()

static void do_home_one_joint(int jno)
{
    //NOTE: if H[jno].home_sequence neg, home all joints in sequence
    int jj;
    if (H[jno].home_sequence < 0) {  //neg: home all joints in sequence
        sequence_state = HOME_SEQUENCE_DO_ONE_SEQUENCE;
        for (jj = 0; jj < all_joints; jj++) {
            if (ABS(H[jj].home_sequence) == ABS(H[jno].home_sequence)) {
                // set home_state for all joints with same sequence
                H[jj].home_state = HOME_START;
            }
        }
    } else {
        sequence_state = HOME_SEQUENCE_DO_ONE_JOINT;
    }
    H[jno].home_state = HOME_START;
} // do_home_one_joint()

static void set_all_unhomed(int unhome_method, motion_state_t motstate)
{
    /*
    ** unhome_method == -1: unhome all joints
    ** unhome_method == -2: unhome joints marked as VOLATILE_HOME
    */
    int jno;
    /* we want all or none, so these checks need to all be done first.
     * but, let's only report the first error.  There might be several,
     * for instance if a homing sequence is running. */
    for (jno = 0; jno < all_joints; jno++) {
        if(_mot->joint_get_active_flag(_mot->ctx, jno)) {
            if (base_get_homing(jno)) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                     _("Cannot unhome while homing, joint %d"), jno);
                return;
            }
            if (!_mot->joint_get_inpos_flag(_mot->ctx, jno)) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                     _("Cannot unhome while moving, joint %d"), jno);
                return;
            }
        }
        if (   (jno >= (all_joints - extra_joints))  // jno is extrajoint
            && (motstate != EMCMOT_MOTION_DISABLED)) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                 _("Cannot unhome extrajoint <%d> with motion enabled"), jno);
            return;
        }
    }
    /* we made it through the checks, so unhome them all per unhome_method */
    for (jno = 0; jno < all_joints; jno++) {
        if(_mot->joint_get_active_flag(_mot->ctx, jno)) {
            if (     (unhome_method == -1)
                || ( (unhome_method == -2) && (H[jno].volatile_home) )
               ) {
                H[jno].homed = 0;
            }
        }
    }
} // set_all_unhomed()

// SEQUENCE management
static void do_homing_sequence(void)
{
    int i,ii;
    int seen;
    int sequence_is_set = 0;

    switch( sequence_state ) {
    case HOME_SEQUENCE_IDLE:
        current_sequence = 0;
        /* nothing to do */
        break;

    case HOME_SEQUENCE_DO_ONE_JOINT:
        // Expect one joint with home_state==HOME_START
        for (i=0; i < all_joints; i++) {
            if (H[i].home_state == HOME_START) {
               H[i].joint_in_sequence = 1;
               current_sequence = ABS(H[i].home_sequence);
            } else {
               H[i].joint_in_sequence = 0;
            }
        }
        sequence_is_set = 1;
        /* fallthrough */
    case HOME_SEQUENCE_DO_ONE_SEQUENCE:
        // Expect multiple joints with home_state==HOME_START
        // specified by a negative sequence
        // Determine current_sequence and set H[i].joint_in_sequence
        // based on home_state[i] == HOME_START
        if (!sequence_is_set) {
            for (i=0; i < all_joints; i++) {
                if (H[i].home_state == HOME_START) {
                    if (   sequence_is_set
                        && (ABS(H[i].home_sequence) != current_sequence)) {
                        rtapi_print_msg(RTAPI_MSG_ERR,
                           _("homing.c Unexpected joint=%d jseq=%d current_seq=%d\n")
                           ,i,H[i].home_sequence,current_sequence);
                    }
                    current_sequence = ABS(H[i].home_sequence);
                    sequence_is_set = 1;
                }
                H[i].joint_in_sequence = 1; //disprove
                if  (   (H[i].home_state  != HOME_START)
                     || (current_sequence != ABS(H[i].home_sequence))
                    ) {
                    H[i].joint_in_sequence = 0;
                }
            }
        }
        sequence_state = HOME_SEQUENCE_START;
        /* fallthrough */
    case HOME_SEQUENCE_START:
        // Request to home all joints or a single sequence
        // A negative H[i].home_sequence means sync final move
        if (!sequence_is_set) {
            // sequence_is_set not otherwise established: home-all
            for (i=0; i < EMCMOT_MAX_JOINTS; i++) {
                H[i].joint_in_sequence = 1;
                // unspecified joints have an unrealizable H[i].home_sequence:
                if (H[i].home_sequence >100) {
                   // docs: 'If HOME_SEQUENCE is not specified then this joint
                   //        will not be homed by the HOME ALL sequence'
                   H[i].joint_in_sequence = 0;  // per docs
                }
            }
            sequence_is_set  = 1;
            current_sequence = 0;
        }
        /* Initializations */
        for(i=0; i < all_joints; i++) {
            if (!H[i].joint_in_sequence) continue;
            if (   (H[i].home_flags & HOME_NO_REHOME)
                &&  H[i].homed
               ) {
                continue;
            } else {
                H[i].homed = 0;
            }
            if (H[i].home_sequence < 0) {
                // If a H[i].home_sequence is neg, find all joints that
                // have the same ABS sequence value and make them the same.
                // Note: overrides inconsistent mixed positive/negative settings
                //       for H[ii].home_sequence
                for(ii=0; ii < all_joints; ii++) {
                    if (H[ii].home_sequence == ABS(H[i].home_sequence)) {
                        H[ii].home_sequence =      H[i].home_sequence;
                    }
                }
            }
        }
        for(i=0; i < all_joints; i++) {
            if (!H[i].joint_in_sequence) continue;
            if  ( H[i].home_state != HOME_IDLE && H[i].home_state != HOME_START) {
                /* a home is already in progress, abort the home-all */
                sequence_state = HOME_SEQUENCE_IDLE;
                return;
            }
        }
        /* tell the world we're on the job */
        homing_active = 1;
        /* fallthrough */
    case HOME_SEQUENCE_START_JOINTS:
        seen = 0;
        /* start all joints whose sequence number matches H[i].home_sequence */
        for(i=0; i < all_joints; i++) {
            if(ABS(H[i].home_sequence) == current_sequence) {
                if (!H[i].joint_in_sequence) continue;
                /* start this joint */
                _mot->joint_set_free_tp_enable(_mot->ctx, i, 0);
                H[i].home_state = HOME_START;
                seen++;
            }
        }
        if (seen || current_sequence == 0) {
            sequence_state = HOME_SEQUENCE_WAIT_JOINTS;
        } else {
            /* no joints have this sequence number, we're done */
            sequence_state = HOME_SEQUENCE_IDLE;
            /* tell the world */
            homing_active = 0;
        }
        break;

    case HOME_SEQUENCE_WAIT_JOINTS:
        seen = 0;
        for(i=0; i < all_joints; i++) {
            if (!H[i].joint_in_sequence) continue;
            // negative H[i].home_sequence means sync final move
            if(ABS(H[i].home_sequence) != current_sequence) {
                /* this joint is not at the current sequence number, ignore it */
                continue;
            }
            if(H[i].home_state != HOME_IDLE) {
                /* still busy homing, keep waiting */
                seen = 1;
                continue;
            }
        }
        if(!seen) {
            /* all joints at this step have finished, move on to next step */
            current_sequence++;
            sequence_state = HOME_SEQUENCE_START_JOINTS;
        }
        break;

    default:
        /* should never get here */
        rtapi_print_msg(RTAPI_MSG_ERR, _("unknown state '%d' during homing sequence"),
                        sequence_state);
        sequence_state = HOME_SEQUENCE_IDLE;
        homing_active = 0;
        break;
    }
} // do_homing_sequence()

static int base_homing_init(int id,
                            double servo_period,
                            int njoints,
                            int nextrajoints)
{
    int i;
    all_joints   = njoints;
    extra_joints = nextrajoints;

    if (servo_period < 1e-9) {
        rtapi_print_msg(RTAPI_MSG_ERR,"%s: bad servo_period:%g\n",
                        __FUNCTION__,
                        servo_period);
        return -1;
    }
    if (base_make_joint_home_pins(id,all_joints)) {
        rtapi_print_msg(RTAPI_MSG_ERR,"%s: base_make_joint_home_pins fail\n",
                        __FUNCTION__);
        return -1;
    }

    servo_freq = 1/servo_period;
    homing_active = 0;
    for (i=0; i < all_joints; i++) {
        H[i].home_state      =  HOME_IDLE;
        H[i].home_search_vel =  0;
        H[i].home_latch_vel  =  0;
        H[i].home_final_vel  =  0;
        H[i].home_offset     =  0;
        H[i].home            =  0;
        H[i].home_flags      =  0;
        H[i].home_sequence   = 1000; //startup: unrealizable, positive seq no.
        H[i].volatile_home   =  0;
    }
    return 0;
}

static void base_read_homing_in_pins(int njoints)
{
    int jno;
    one_joint_home_data_t *addr;
    for (jno = 0; jno < njoints; jno++) {
        addr = &(joint_home_data->jhd[jno]);
        H[jno].home_sw      = *(addr->home_sw);      // IN
        H[jno].index_enable = *(addr->index_enable); // IO
    }
}

static void base_write_homing_out_pins(int njoints)
{
    int jno;
    one_joint_home_data_t *addr;
    for (jno = 0; jno < njoints; jno++) {
        addr = &(joint_home_data->jhd[jno]);
        *(addr->homing)       = H[jno].homing;       // OUT
        *(addr->homed)        = H[jno].homed;        // OUT
        *(addr->home_state)   = H[jno].home_state;   // OUT
        *(addr->index_enable) = H[jno].index_enable; // IO
    }
}

static void base_do_home_joint(int jno) {
    if (jno == -1) {
        H[0].homed = 0; // ensure at least one unhomed
        do_home_all();
    } else {
        do_home_one_joint(jno); // apply rules if home_sequence negative
    }
}

static void base_do_cancel_homing(int jno) {
    if (H[jno].homing) {
        H[jno].home_state = HOME_ABORT;
    }else if(H[jno].joint_in_sequence){
        H[jno].home_state = HOME_ABORT;
    }
}

static void base_set_unhomed(int jno, motion_state_t motstate) {
    // Note: negative jno ==> unhome multiple joints
    if (jno < 0) { set_all_unhomed(jno,motstate); return; }

    if (jno > all_joints) {
        /* invalid joint number specified */
        rtapi_print_msg(RTAPI_MSG_ERR,
             _("Cannot unhome invalid joint %d (max %d)\n"), jno, (all_joints-1));
        return;
    }
    /* request was for one joint number */
    if (   (jno >= (all_joints - extra_joints) )  // jno is extrajoint
        && (motstate != EMCMOT_MOTION_DISABLED)) {
        rtapi_print_msg(RTAPI_MSG_ERR,
             _("Cannot unhome extrajoint <%d> with motion enabled\n"), jno);
        return;
    }
    if(_mot->joint_get_active_flag(_mot->ctx, jno) ) {
        if (base_get_homing(jno) ) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                 _("Cannot unhome while homing, joint %d\n"), jno);
            return;
        }
        if (!_mot->joint_get_inpos_flag(_mot->ctx, jno) ) {
            rtapi_print_msg(RTAPI_MSG_ERR,
                 _("Cannot unhome while moving, joint %d\n"), jno);
            return;
        }
        H[jno].homed = 0;
    } else {
        rtapi_print_msg(RTAPI_MSG_ERR,
             _("Cannot unhome inactive joint %d\n"), jno);
    }
} // base_set_unhomed()

static void base_set_joint_homing_params(int    jno,
                                         double offset,
                                         double home,
                                         double home_final_vel,
                                         double home_search_vel,
                                         double home_latch_vel,
                                         int    home_flags,
                                         int    home_sequence,
                                         bool   volatile_home
                                         )
{
    H[jno].home_offset     = offset;
    H[jno].home            = home;
    H[jno].home_final_vel  = home_final_vel;
    H[jno].home_search_vel = home_search_vel;
    H[jno].home_latch_vel  = home_latch_vel;
    H[jno].home_flags      = home_flags;
    H[jno].home_sequence   = home_sequence;
    H[jno].volatile_home   = volatile_home;
    update_home_is_synchronized();
}

static void base_update_joint_homing_params (int    jno,
                                             double offset,
                                             double home,
                                             int    home_sequence
                                            )
{
    H[jno].home_offset   = offset;
    H[jno].home          = home;
    H[jno].home_sequence = home_sequence;
    update_home_is_synchronized();
}

static bool base_get_allhomed(void) {
    int joint_num;

    for (joint_num = 0; joint_num < all_joints; joint_num++) {
        if (!_mot->joint_get_active_flag(_mot->ctx, joint_num)) {
            /* if joint is not active, don't even look at its limits */
            continue;
        }
        if (!base_get_homed(joint_num) ) {
            /* if any of the joints is not homed return false */
            return 0;
        }
    }
    /* return true if all active joints are homed*/
    return 1;
} // base_get_allhomed()

static bool base_get_homing_is_active(void) {
    return homing_active;
}

static int base_get_home_sequence(int jno) {
    return H[jno].home_sequence;
}

static bool base_get_homing(int jno) {
    return H[jno].homing;
}

static bool base_get_homed(int jno) {
    return H[jno].homed;
}

static bool base_get_index_enable(int jno) {
     return H[jno].index_enable;
}

static bool base_get_home_needs_unlock_first(int jno) {
    return (H[jno].home_flags & HOME_UNLOCK_FIRST) ? 1 : 0;
}

static bool base_get_home_is_idle(int jno) {
    return H[jno].home_state == HOME_IDLE ? 1 : 0;
}

static bool base_get_home_is_synchronized(int jno) {
    return H[jno].home_is_synchronized;
}

static bool base_get_homing_at_index_search_wait(int jno) {
    return H[jno].home_state == HOME_INDEX_SEARCH_WAIT ? 1 : 0;
}

static bool sync_now = 0;
static void sync_reset(void) { sync_now=0; return; }

static bool sync_ready(int joint_num)
{
    // defer a move until all joints in sequence are at current state
    if  (   ( ABS(H[joint_num].home_sequence) == current_sequence)
         && !sync_now) {
        int jno;
        for (jno = 0; jno < all_joints; jno++) {
            if (!H[jno].joint_in_sequence)                     {continue;}
            if (ABS(H[jno].home_sequence) != current_sequence) {continue;}
            if (H[jno].home_flags &  HOME_ABSOLUTE_ENCODER)    {continue;}
            if (H[jno].home_state != H[joint_num].home_state) {
                sync_now = 0; return 0; // not ready
            }
        }
        sync_now = 1;
    }
    return 1; // ready
} // sync_ready()

static int base_1joint_state_machine(int joint_num)
{
    double offset, tmp;
    int home_sw_active, homing_flag;
    bool immediate_state = 0;

    homing_flag = 0;
    home_sw_active = H[joint_num].home_sw;
    if (H[joint_num].home_state != HOME_IDLE) {
        homing_flag = 1; /* at least one joint is homing */
    }

    /* when a joint is homing, 'check_for_faults()' ignores its limit
       switches, so that this code can do the right thing with them. Once
       the homing process is finished, the 'check_for_faults()' resumes
       checking */

    /* homing state machine */

    /* Some portions of the homing sequence can run thru two or more
       states during a single servo period.  This is done using
       'immediate_state'.  If a state transition sets it true (non-zero),
       this 'do-while' will loop executing switch(home_state) immediately
       to run the new state code.  Otherwise, the loop will fall thru, and
       switch(home_state) runs only once per servo period. Do _not_ set
       'immediate_state' true unless you also change 'home_state', unless
       you want an infinite loop! */
    do {
        immediate_state = 0;
        switch (H[joint_num].home_state) {
        case HOME_IDLE:
            /* nothing to do */
            break;

        case HOME_START:
            /* This state is responsible for getting the homing process
               started.  It doesn't actually do anything, it simply
               determines what state is next */
            if (H[joint_num].home_flags & HOME_IS_SHARED && home_sw_active) {
                rtapi_print_msg(RTAPI_MSG_ERR, _("Cannot home while shared home switch is closed j=%d"),
                                joint_num);
                H[joint_num].home_state = HOME_IDLE;
                break;
            }
            /* set flags that communicate with the rest of EMC */
            if (   (H[joint_num].home_flags & HOME_NO_REHOME)
                &&  H[joint_num].homed
               ) {
               H[joint_num].home_state = HOME_IDLE;
               break; //no rehome allowed if absolute_enoder
            } else {
                H[joint_num].homing = 1;
                H[joint_num].homed = 0;
            }
            _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);    /* stop any existing motion */
            sync_reset();                 /* stop any interrupted/canceled sync */
            H[joint_num].pause_timer = 0; /* reset delay counter */
            /* figure out exactly what homing sequence is needed */
            if (H[joint_num].home_flags & HOME_ABSOLUTE_ENCODER) {
                H[joint_num].home_flags &= ~HOME_IS_SHARED; // shared not applicable
                H[joint_num].home_state = HOME_SET_SWITCH_POSITION;
                immediate_state = 1;
                // Note: H[joint_num].homed
                // is not set in case there is a final move requested
                break;
            }
            if (H[joint_num].home_flags & HOME_UNLOCK_FIRST) {
                H[joint_num].home_state = HOME_UNLOCK;
            } else {
                H[joint_num].home_state = HOME_UNLOCK_WAIT;
                immediate_state = 1;
            }
            break;

        case HOME_UNLOCK:
            // unlock now
            _mot->set_rotary_unlock(_mot->ctx, joint_num, 1);
            H[joint_num].home_state = HOME_UNLOCK_WAIT;
            break;

        case HOME_UNLOCK_WAIT:
            // if not yet unlocked, continue waiting
            if ((H[joint_num].home_flags & HOME_UNLOCK_FIRST) &&
                !_mot->get_rotary_unlock(_mot->ctx, joint_num)) break;

            // either we got here without an unlock needed, or the
            // unlock is now complete.
            if (H[joint_num].home_search_vel == 0.0) {
                if (H[joint_num].home_latch_vel == 0.0) {
                    /* both vels == 0 means home at current position */
                    H[joint_num].home_state = HOME_SET_SWITCH_POSITION;
                    immediate_state = 1;
                } else if (H[joint_num].home_flags & HOME_USE_INDEX) {
                    /* home using index pulse only */
                    H[joint_num].home_state = HOME_INDEX_ONLY_START;
                    immediate_state = 1;
                } else {
                    rtapi_print_msg(RTAPI_MSG_ERR,
                         _("invalid homing config: non-zero LATCH_VEL needs either SEARCH_VEL or USE_INDEX"));
                    H[joint_num].home_state = HOME_IDLE;
                }
            } else {
                if (H[joint_num].home_latch_vel != 0.0) {
                    /* need to find home switch */
                    H[joint_num].home_state = HOME_INITIAL_SEARCH_START;
                    immediate_state = 1;
                } else {
                    rtapi_print_msg(RTAPI_MSG_ERR,
                         _("invalid homing config: non-zero SEARCH_VEL needs LATCH_VEL"));
                    H[joint_num].home_state = HOME_IDLE;
                }
            }
            break;

        case HOME_INITIAL_BACKOFF_START:
            /* This state is called if the homing sequence starts at a
               location where the home switch is already tripped. It
               starts a move away from the switch. */
            /* is the joint still moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
                break;
            }
            H[joint_num].pause_timer = 0;
            /* set up a move at '-search_vel' to back off of switch */
            home_start_move(joint_num, - H[joint_num].home_search_vel);
            /* next state */
            H[joint_num].home_state = HOME_INITIAL_BACKOFF_WAIT;
            break;

        case HOME_INITIAL_BACKOFF_WAIT:
            /* This state is called while the machine is moving off of
               the home switch.  It terminates when the switch is cleared
               successfully.  If the move ends or hits a limit before it
               clears the switch, the home is aborted. */
            /* are we off home switch yet? */
            if (! home_sw_active) {
                /* yes, stop motion */
                _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                /* begin initial search */
                H[joint_num].home_state = HOME_INITIAL_SEARCH_START;
                immediate_state = 1;
                break;
            }
            ABORT_CHECK(joint_num);
            break;

        case HOME_INITIAL_SEARCH_START:
            /* This state is responsible for starting a move toward the
               home switch.  This move is at 'search_vel', which can be
               fairly fast, because once the switch is found another
               slower move will be used to set the exact home position. */
            /* is the joint already moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
                break;
            }
            H[joint_num].pause_timer = 0;
            /* make sure we aren't already on home switch */
            if (home_sw_active) {
                /* already on switch, need to back off it first */
                H[joint_num].home_state = HOME_INITIAL_BACKOFF_START;
                immediate_state = 1;
                break;
            }
            /* set up a move at 'search_vel' to find switch */
            home_start_move(joint_num, H[joint_num].home_search_vel);
            /* next state */
            H[joint_num].home_state = HOME_INITIAL_SEARCH_WAIT;
            break;

        case HOME_INITIAL_SEARCH_WAIT:
            /* This state is called while the machine is looking for the
               home switch.  It terminates when the switch is found.  If
               the move ends or hits a limit before it finds the switch,
               the home is aborted. */
            /* have we hit home switch yet? */
            if (home_sw_active) {
                /* yes, stop motion */
                _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                /* go to next step */
                H[joint_num].home_state = HOME_SET_COARSE_POSITION;
                immediate_state = 1;
                break;
            }
            ABORT_CHECK(joint_num);
            break;

        case HOME_SET_COARSE_POSITION:
            /* This state is called after the first time the switch is
               found.  At this point, we are approximately home. Although
               we will do another slower pass to get the exact home
               location, we reset the joint coordinates now so that screw
               error comp will be appropriate for this portion of the
               screw (previously we didn't know where we were at all). */
            /* set the current position to 'home_offset' */
            offset = H[joint_num].home_offset - _mot->joint_get_pos_fb(_mot->ctx, joint_num);
            /* this moves the internal position but does not affect the
               motor position */
            _mot->joint_set_pos_cmd(_mot->ctx, joint_num, _mot->joint_get_pos_cmd(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_pos_fb(_mot->ctx, joint_num, _mot->joint_get_pos_fb(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_free_tp_curr_pos(_mot->ctx, joint_num, _mot->joint_get_free_tp_curr_pos(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_motor_offset(_mot->ctx, joint_num, _mot->joint_get_motor_offset(_mot->ctx, joint_num) - (offset));
            /* The next state depends on the signs of 'search_vel' and
               'latch_vel'.  If they are the same, that means we must
               back up, then do the final homing moving the same
               direction as the initial search, on a rising edge of the
               switch.  If they are opposite, it means that the final
               homing will take place on a falling edge as the machine
               moves off of the switch. */
            tmp = H[joint_num].home_search_vel * H[joint_num].home_latch_vel;
            if (tmp > 0.0) {
                /* search and latch vel are same direction */
                H[joint_num].home_state = HOME_FINAL_BACKOFF_START;
            } else {
                /* search and latch vel are opposite directions */
                H[joint_num].home_state = HOME_FALL_SEARCH_START;
            }
            immediate_state = 1;
            break;

        case HOME_FINAL_BACKOFF_START:
            /* This state is called once the approximate location of the
               switch has been found.  It is responsible for starting a
               move that will back off of the switch in preparation for a
               final slow move that captures the exact switch location. */
            /* is the joint already moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
                break;
            }
            H[joint_num].pause_timer = 0;
            /* we should still be on the switch */
            if (! home_sw_active) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                     _("Home switch inactive before start of backoff move j=%d"),
                     joint_num);
                H[joint_num].home_state = HOME_IDLE;
                break;
            }
            /* set up a move at '-search_vel' to back off of switch */
            home_start_move(joint_num, - H[joint_num].home_search_vel);
            /* next state */
            H[joint_num].home_state = HOME_FINAL_BACKOFF_WAIT;
            break;

        case HOME_FINAL_BACKOFF_WAIT:
            /* This state is called while the machine is moving off of
               the home switch after finding its approximate location.
               It terminates when the switch is cleared successfully.  If
               the move ends or hits a limit before it clears the switch,
               the home is aborted. */
            /* are we off home switch yet? */
            if (! home_sw_active) {
                /* yes, stop motion */
                _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                /* begin final search */
                H[joint_num].home_state = HOME_RISE_SEARCH_START;
                immediate_state = 1;
                break;
            }
            ABORT_CHECK(joint_num);
            break;

        case HOME_RISE_SEARCH_START:
            /* This state is called to start the final search for the
               point where the home switch trips.  It moves at
               'latch_vel' and looks for a rising edge on the switch */
            /* is the joint already moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
                break;
            }
            H[joint_num].pause_timer = 0;
            /* we should still be off of the switch */
            if (home_sw_active) {
                rtapi_print_msg(RTAPI_MSG_ERR, _("Home switch active before start of latch move j=%d"),
                                joint_num);
                H[joint_num].home_state = HOME_IDLE;
                break;
            }
            /* set up a move at 'latch_vel' to locate the switch */
            home_start_move(joint_num, H[joint_num].home_latch_vel);
            /* next state */
            H[joint_num].home_state = HOME_RISE_SEARCH_WAIT;
            break;

        case HOME_RISE_SEARCH_WAIT:
            /* This state is called while the machine is moving towards
               the home switch on its final, low speed pass.  It
               terminates when the switch is detected. If the move ends
               or hits a limit before it hits the switch, the home is
               aborted. */
            /* have we hit the home switch yet? */
            if (home_sw_active) {
                /* yes, where do we go next? */
                if (H[joint_num].home_flags & HOME_USE_INDEX) {
                    /* look for index pulse */
                    H[joint_num].home_state = HOME_INDEX_SEARCH_START;
                    immediate_state = 1;
                    break;
                } else {
                    /* no index pulse, stop motion */
                    _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                    /* go to next step */
                    H[joint_num].home_state = HOME_SET_SWITCH_POSITION;
                    immediate_state = 1;
                    break;
                }
            }
            ABORT_CHECK(joint_num);
            break;

        case HOME_FALL_SEARCH_START:
            /* This state is called to start the final search for the
               point where the home switch releases.  It moves at
               'latch_vel' and looks for a falling edge on the switch */
            /* is the joint already moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
                break;
            }
            H[joint_num].pause_timer = 0;
            /* we should still be on the switch */
            if (!home_sw_active) {
                rtapi_print_msg(RTAPI_MSG_ERR,
                     _("Home switch inactive before start of latch move j=%d"),
                     joint_num);
                H[joint_num].home_state = HOME_IDLE;
                break;
            }
            /* set up a move at 'latch_vel' to locate the switch */
            home_start_move(joint_num, H[joint_num].home_latch_vel);
            /* next state */
            H[joint_num].home_state = HOME_FALL_SEARCH_WAIT;
            break;

        case HOME_FALL_SEARCH_WAIT:
            /* This state is called while the machine is moving away from
               the home switch on its final, low speed pass.  It
               terminates when the switch is cleared. If the move ends or
               hits a limit before it clears the switch, the home is
               aborted. */
            /* have we cleared the home switch yet? */
            if (!home_sw_active) {
                /* yes, where do we go next? */
                if (H[joint_num].home_flags & HOME_USE_INDEX) {
                    /* look for index pulse */
                    H[joint_num].home_state = HOME_INDEX_SEARCH_START;
                    immediate_state = 1;
                    break;
                } else {
                    /* no index pulse, stop motion */
                    _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                    /* go to next step */
                    H[joint_num].home_state = HOME_SET_SWITCH_POSITION;
                    immediate_state = 1;
                    break;
                }
            }
            ABORT_CHECK(joint_num);
            break;

        case HOME_SET_SWITCH_POSITION:
            /* This state is called when the machine has determined the
               switch position as accurately as possible.  It sets the
               current joint position to 'home_offset', which is the
               location of the home switch in joint coordinates. */
            /* set the current position to 'home_offset' */
            if (H[joint_num].home_flags & HOME_ABSOLUTE_ENCODER) {
                offset = H[joint_num].home_offset;
            } else {
                offset = H[joint_num].home_offset - _mot->joint_get_pos_fb(_mot->ctx, joint_num);
            }
            /* this moves the internal position but does not affect the
               motor position */
            _mot->joint_set_pos_cmd(_mot->ctx, joint_num, _mot->joint_get_pos_cmd(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_pos_fb(_mot->ctx, joint_num, _mot->joint_get_pos_fb(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_free_tp_curr_pos(_mot->ctx, joint_num, _mot->joint_get_free_tp_curr_pos(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_motor_offset(_mot->ctx, joint_num, _mot->joint_get_motor_offset(_mot->ctx, joint_num) - (offset));
            if (H[joint_num].home_flags & HOME_ABSOLUTE_ENCODER) {
                if (H[joint_num].home_flags & HOME_NO_FINAL_MOVE) {
                    H[joint_num].home_state = HOME_FINISHED;
                    immediate_state = 1;
                    H[joint_num].homed = 1; // finished absolute encoder
                    break;
                }
            }
            /* next state */
            H[joint_num].home_state = HOME_FINAL_MOVE_START;
            immediate_state = 1;
            break;

        case HOME_INDEX_ONLY_START:
            /* This state is used if the machine has been pre-positioned
               near the home position, and simply needs to find the
               next index pulse.  It starts a move at latch_vel, and
               sets index-enable, which tells the encoder driver to
               reset its counter to zero and clear the enable when the
               next index pulse arrives. */
            /* is the joint already moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
                break;
            }
            H[joint_num].pause_timer = 0;
            /* Although we don't know the exact home position yet, we
               we reset the joint coordinates now so that screw error
               comp will be appropriate for this portion of the screw
               (previously we didn't know where we were at all). */
            /* set the current position to 'home_offset' */
            offset = H[joint_num].home_offset - _mot->joint_get_pos_fb(_mot->ctx, joint_num);
            /* this moves the internal position but does not affect the
               motor position */
            _mot->joint_set_pos_cmd(_mot->ctx, joint_num, _mot->joint_get_pos_cmd(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_pos_fb(_mot->ctx, joint_num, _mot->joint_get_pos_fb(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_free_tp_curr_pos(_mot->ctx, joint_num, _mot->joint_get_free_tp_curr_pos(_mot->ctx, joint_num) + (offset));
            _mot->joint_set_motor_offset(_mot->ctx, joint_num, _mot->joint_get_motor_offset(_mot->ctx, joint_num) - (offset));
            /* set the index enable */
            H[joint_num].index_enable = 1;
            /* set up a move at 'latch_vel' to find the index pulse */
            home_start_move(joint_num, H[joint_num].home_latch_vel);
            /* next state */
            H[joint_num].home_state = HOME_INDEX_SEARCH_WAIT;
            break;

        case HOME_INDEX_SEARCH_START:
            /* This state is called after the machine has made a low
               speed pass to determine the limit switch location. It
               sets index-enable, which tells the encoder driver to
               reset its counter to zero and clear the enable when the
               next index pulse arrives. */
            /* set the index enable */
            H[joint_num].index_enable = 1;
            /* and move right into the waiting state */
            H[joint_num].home_state = HOME_INDEX_SEARCH_WAIT;
            immediate_state = 1;
            ABORT_CHECK(joint_num);
            break;

        case HOME_INDEX_SEARCH_WAIT:
            /* This state is called after the machine has found the
               home switch and "armed" the encoder counter to reset on
               the next index pulse. It continues at low speed until
               an index pulse is detected, at which point it sets the
               final home position.  If the move ends or hits a limit
               before an index pulse occurs, the home is aborted. */
            /* has an index pulse arrived yet? encoder driver clears
               enable when it does */
            if ( H[joint_num].index_enable == 0 ) {
                /* yes, stop motion */
                _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                /* go to next step */
                H[joint_num].home_state = HOME_SET_INDEX_POSITION;
                immediate_state = 1;
                break;
            }
            ABORT_CHECK(joint_num);
            break;

        case HOME_SET_INDEX_POSITION:
            /* This state is called when the encoder has been reset at
               the index pulse position.  It sets the current joint
               position to 'home_offset', which is the location of the
               index pulse in joint coordinates. */
            /* set the current position to 'home_offset' */
            _mot->joint_set_motor_offset(_mot->ctx, joint_num, - H[joint_num].home_offset);
            { double pf = _mot->joint_get_motor_pos_fb(_mot->ctx, joint_num)
                        - (_mot->joint_get_backlash_filt(_mot->ctx, joint_num)
                         + _mot->joint_get_motor_offset(_mot->ctx, joint_num));
              _mot->joint_set_pos_fb(_mot->ctx, joint_num, pf);
              _mot->joint_set_pos_cmd(_mot->ctx, joint_num, pf);
              _mot->joint_set_free_tp_curr_pos(_mot->ctx, joint_num, pf);
            }

            if (H[joint_num].home_flags & HOME_INDEX_NO_ENCODER_RESET) {
               /* Special case: encoder does not reset on index pulse.
                  This moves the internal position but does not affect
                  the motor position */
               offset = H[joint_num].home_offset - _mot->joint_get_pos_fb(_mot->ctx, joint_num);
               _mot->joint_set_pos_cmd(_mot->ctx, joint_num, _mot->joint_get_pos_cmd(_mot->ctx, joint_num) + (offset));
               _mot->joint_set_pos_fb(_mot->ctx, joint_num, _mot->joint_get_pos_fb(_mot->ctx, joint_num) + (offset));
               _mot->joint_set_free_tp_curr_pos(_mot->ctx, joint_num, _mot->joint_get_free_tp_curr_pos(_mot->ctx, joint_num) + (offset));
               _mot->joint_set_motor_offset(_mot->ctx, joint_num, _mot->joint_get_motor_offset(_mot->ctx, joint_num) - (offset));
            }

            /* next state */
            H[joint_num].home_state = HOME_FINAL_MOVE_START;
            immediate_state = 1;
            break;

        case HOME_FINAL_MOVE_START:
            /* This state is called once the joint coordinate system is
               set properly.  It moves to the actual 'home' position,
               which is not necessarily the position of the home switch
               or index pulse. */
            /* is the joint already moving? */
            if (_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, reset delay, wait until joint stops */
                H[joint_num].pause_timer = 0;
                break;
            }
            /* has delay timed out? */
            if (H[joint_num].pause_timer < (HOME_DELAY * servo_freq)) {
                /* no, update timer and wait some more */
                H[joint_num].pause_timer++;
            }
            H[joint_num].pause_timer = 0;

            // neg home sequence: sync final move
            if  (    (H[joint_num].home_sequence  < 0)
                  && !sync_ready(joint_num) ) {
                break; // not all joints at *this* state, wait for them
            }

            /* plan a final move to home position */
            _mot->joint_set_free_tp_pos_cmd(_mot->ctx, joint_num, H[joint_num].home);
            /* if home_vel is set (>0) then we use that, otherwise we rapid there */
            if (H[joint_num].home_final_vel > 0) {
                _mot->joint_set_free_tp_max_vel(_mot->ctx, joint_num, fabs(H[joint_num].home_final_vel));
                /* clamp on max vel for this joint */
                { double vl = _mot->joint_get_vel_limit(_mot->ctx, joint_num);
                    if (_mot->joint_get_free_tp_max_vel(_mot->ctx, joint_num) > vl)
                        _mot->joint_set_free_tp_max_vel(_mot->ctx, joint_num, vl);
                }
            } else {
                _mot->joint_set_free_tp_max_vel(_mot->ctx, joint_num, _mot->joint_get_vel_limit(_mot->ctx, joint_num));
            }
            /* start the move */
            _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 1);
            H[joint_num].home_state = HOME_FINAL_MOVE_WAIT;
            break;

        case HOME_FINAL_MOVE_WAIT:
            /* This state is called while the machine makes its final
               move to the home position.  It terminates when the machine
               arrives at the final location. If the move hits a limit
               before it arrives, the home is aborted. */

            // neg home sequence: sync final move
            if  (  (H[joint_num].home_sequence  < 0)
                && !sync_ready(joint_num) ) {
                break; // not all joints at *this* state, wait for them
            }

            /* have we arrived (and stopped) at home? */
            if (!_mot->joint_get_free_tp_active(_mot->ctx, joint_num)) {
                /* yes, stop motion */
                _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
                /* we're finally done */
                H[joint_num].home_state = HOME_LOCK;
                immediate_state = 1;
                break;
            }
            if (_mot->joint_get_on_pos_limit(_mot->ctx, joint_num) || _mot->joint_get_on_neg_limit(_mot->ctx, joint_num)) {
                /* on limit, check to see if we should trip */
                if (!(H[joint_num].home_flags & HOME_IGNORE_LIMITS)) {
                    /* not ignoring limits, time to quit */
                    rtapi_print_msg(RTAPI_MSG_ERR, _("hit limit in home state j=%d"),joint_num);
                    H[joint_num].home_state = HOME_ABORT;
                    immediate_state = 1;
                    break;
                }
            }
            break;

        case HOME_LOCK:
            if (H[joint_num].home_flags & HOME_UNLOCK_FIRST) {
                _mot->set_rotary_unlock(_mot->ctx, joint_num, 0);
            } else {
                immediate_state = 1;
            }
            H[joint_num].home_state = HOME_LOCK_WAIT;
            break;

        case HOME_LOCK_WAIT:
            // if not yet locked, continue waiting
            if ((H[joint_num].home_flags & HOME_UNLOCK_FIRST) &&
                _mot->get_rotary_unlock(_mot->ctx, joint_num)) break;

            // either we got here without a lock needed, or the
            // lock is now complete.
            H[joint_num].home_state = HOME_FINISHED;
            immediate_state = 1;
            break;

        case HOME_FINISHED:
            H[joint_num].homing = 0;
            H[joint_num].homed = 1; // finished
            H[joint_num].home_state = HOME_IDLE;
            if ( ! (H[joint_num].home_flags & HOME_ABSOLUTE_ENCODER)) {
                _mot->joint_set_free_tp_curr_pos(_mot->ctx, joint_num, H[joint_num].home);
            }
            immediate_state = 1;
            H[joint_num].joint_in_sequence = 0;
            break;

        case HOME_ABORT:
            H[joint_num].homing = 0;
            H[joint_num].homed = 0;
            H[joint_num].joint_in_sequence = 0;
            _mot->joint_set_free_tp_enable(_mot->ctx, joint_num, 0);
            H[joint_num].home_state = HOME_IDLE;
            H[joint_num].index_enable = 0;
            immediate_state = 1;
            break;

        default:
            /* should never get here */
            rtapi_print_msg(RTAPI_MSG_ERR, _("unknown state '%d' during homing j=%d"),
                            H[joint_num].home_state,joint_num);
            H[joint_num].home_state = HOME_ABORT;
            immediate_state = 1;
            break;
        }        /* end of switch(H[joint_num].home_state) */
    } while (immediate_state);

    return homing_flag;
} // base_1joint_state_machine()

static bool base_do_homing(void)
{
    int  joint_num;
    int  homing_flag = 0;
    bool beginning_allhomed = base_get_allhomed();

    do_homing_sequence();
    /* loop thru joints, treat each one individually */
    for (joint_num = 0; joint_num < all_joints; joint_num++) {
        if (!H[joint_num].joint_in_sequence)            { continue; }
        if (!_mot->joint_get_active_flag(_mot->ctx, joint_num)) { continue; }
        // DEFAULT joint homing state machine:
        homing_flag += base_1joint_state_machine(joint_num);
    }
    if ( homing_flag > 0 ) { /* one or more joint is homing */
        homing_active = 1;
    } else { /* is a homing sequence in progress? */
        if (sequence_state == HOME_SEQUENCE_IDLE) {
            /* no, single joint only, we're done */
            homing_active = 0;
        }
    }
    // return 1 if homing completed this period
    if (!beginning_allhomed && base_get_allhomed()) {homing_active=0; return 1;}
    return 0;
} // base_do_homing()

/***********************************************************************
*                      PUBLIC FUNCTIONS                                *
************************************************************************/

//========================================================
// ═══════════════════════════════════════════════════════════════════════════
// GMI cmod interface — direct implementation, no wrapper layer
// ═══════════════════════════════════════════════════════════════════════════

static int32_t gmi_home_init(void *ctx, int32_t comp_id, double servo_period,
    int32_t n_joints, int32_t n_extrajoints)
{
    (void)ctx;
    return base_homing_init(comp_id, servo_period, n_joints, n_extrajoints);
}

static int32_t gmi_home_set_joint_params(void *ctx, int32_t jno, double offset, double home,
    double home_final_vel, double home_search_vel,
    double home_latch_vel, int32_t home_flags,
    int32_t home_sequence, int32_t volatile_home)
{
    (void)ctx;
    base_set_joint_homing_params(jno, offset, home,
        home_final_vel, home_search_vel, home_latch_vel,
        home_flags, home_sequence, (bool)volatile_home);
    return 0;
}

static int32_t gmi_home_update_joint_params(void *ctx, int32_t jno, double home_offset,
    double home_home, int32_t home_sequence)
{
    (void)ctx;
    base_update_joint_homing_params(jno, home_offset, home_home, home_sequence);
    return 0;
}

static int32_t gmi_home_read_in_pins(void *ctx, int32_t njoints)
{
    (void)ctx;
    base_read_homing_in_pins(njoints);
    return 0;
}

static int32_t gmi_home_do_homing(void *ctx) { (void)ctx; return (int32_t)base_do_homing(); }

static int32_t gmi_home_write_out_pins(void *ctx, int32_t njoints)
{
    (void)ctx;
    base_write_homing_out_pins(njoints);
    return 0;
}

static int32_t gmi_home_do_home_joint(void *ctx, int32_t jno) { (void)ctx; base_do_home_joint(jno); return 0; }
static int32_t gmi_home_do_cancel(void *ctx, int32_t jno) { (void)ctx; base_do_cancel_homing(jno); return 0; }

static int32_t gmi_home_set_unhomed(void *ctx, int32_t jno, home_motion_state_t motstate)
{
    (void)ctx;
    base_set_unhomed(jno, (motion_state_t)motstate);
    return 0;
}

static int32_t gmi_home_get_allhomed(void *ctx) { (void)ctx; return (int32_t)base_get_allhomed(); }
static int32_t gmi_home_get_is_active(void *ctx) { (void)ctx; return (int32_t)base_get_homing_is_active(); }
static int32_t gmi_home_get_sequence(void *ctx, int32_t jno) { (void)ctx; return base_get_home_sequence(jno); }
static int32_t gmi_home_get_homing(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_homing(jno); }
static int32_t gmi_home_get_homed(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_homed(jno); }
static int32_t gmi_home_get_index_enable(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_index_enable(jno); }
static int32_t gmi_home_get_needs_unlock_first(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_home_needs_unlock_first(jno); }
static int32_t gmi_home_get_is_idle(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_home_is_idle(jno); }
static int32_t gmi_home_get_is_synchronized(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_home_is_synchronized(jno); }
static int32_t gmi_home_get_at_index_search_wait(void *ctx, int32_t jno) { (void)ctx; return (int32_t)base_get_homing_at_index_search_wait(jno); }

// ─── Callbacks table ────────────────────────────────────────────────────

static const home_callbacks_t home_cmod_callbacks = GMI_HOME_CALLBACKS;

// ─── cmod lifecycle ─────────────────────────────────────────────────────

static cmod_t home_cmod;
static const gomc_api_t *home_cmod_api;
static const char *home_mot_instance = "motmod";

static void home_cmod_destroy(cmod_t *self) { (void)self; }

static int home_cmod_init(cmod_t *self)
{
    (void)self;
    const mot_callbacks_t *mot = mot_api_get(home_cmod_api, home_mot_instance);
    if (!mot) return -1;
    _mot = mot;
    return 0;
}

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out)
{
    home_cmod_api = env->api;

    /* Parse mot_instance parameter */
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "mot_instance=", 13) == 0)
            home_mot_instance = argv[i] + 13;
    }

    int rc = home_api_register(env->api, name, &home_cmod_callbacks);
    if (rc != 0) {
        gomc_log_errorf(env->log, name,
            "failed to register home API: %d", rc);
        return rc;
    }

    home_cmod.Init    = home_cmod_init;
    home_cmod.Start   = NULL;
    home_cmod.Destroy = home_cmod_destroy;
    *out = &home_cmod;
    return 0;
}
