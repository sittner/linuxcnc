/********************************************************************
* Description: motion.c
*   Main module initialisation and cleanup routines.
*
* Author:
* License: GPL Version 2
* System: Linux
*
* Copyright (c) 2004 All rights reserved.
********************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "gomc_env.h"
#include "kins_api.h"
#include "mot_api.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "rtapi_string.h"       /* memset */
#include "hal.h"		/* decls for HAL implementation */
#include "motion.h"
#include "motion_struct.h"
#include "mot_priv.h"
#include "tp_types.h"
#include "tp_api.h"
#include "home_api.h"
#include "rtapi_math.h"
#include "axis.h"

// Forward declarations (defined later in this file)
extern const tp_callbacks_t   *motmod_tp_api;
extern const home_callbacks_t *motmod_home_api;

// Mark strings for translation, but defer translation to userspace
#define _(s) (s)

/***********************************************************************
*                    MODULE PARAMETERS                                 *
************************************************************************/

/* RTAPI shmem key - for comms with higher level user space stuff */
static int key = DEFAULT_SHMEM_KEY;	/* the shared memory key, default value */
static long base_period_nsec = 0;	/* fastest thread period */
int base_thread_fp = 0;	/* default is no floating point in base thread */
static long servo_period_nsec = 1000000;	/* servo thread period */
static long traj_period_nsec = 0;	/* trajectory planner period */
static int num_spindles = 1; /* default number of spindles is 1 */
int motion_num_spindles;
static int num_joints = EMCMOT_MAX_JOINTS;	/* default number of joints present */
static int num_extrajoints = 0;	/* default number of extra joints present */
static int num_dio = 0;	/* default number of motion synched DIO */

#define MAX_IO 64
static char *names_din[MAX_IO] = {0,};
static char *names_dout[MAX_IO] = {0,};

static int num_aio = 0;	/* default number of motion synched AIO */

static char *names_ain[MAX_IO] = {0,};
static char *names_aout[MAX_IO] = {0,};
static int num_misc_error = -1;   /* To check use of num_misc_error modparam */

static char *names_misc_errors[MAX_IO] = {0,};

static int unlock_joints_mask = 0;/* mask to select joints for unlock pins */
/***********************************************************************
*                  GLOBAL VARIABLE DEFINITIONS                         *
************************************************************************/

/* pointer to emcmot_hal_data_t struct in HAL shmem, with all HAL data */
emcmot_hal_data_t *emcmot_hal_data = 0;

/* allocate array for joint data */
emcmot_joint_t joints[EMCMOT_MAX_JOINTS];

/*
  Principles of communication:

  Data is copied in or out from the various types of comm mechanisms:
  mbuff mapped memory for Linux/RT-Linux, or OS shared memory for Unixes.

  emcmotStruct is ptr to this memory.

  emcmotCommand points to emcmotStruct->command,
  emcmotStatus points to emcmotStruct->status,
  emcmotError points to emcmotStruct->error, and
 */
emcmot_struct_t *emcmotStruct = 0;
/* ptrs to either buffered copies or direct memory for command and status */
struct emcmot_command_t *emcmotCommand = 0;
struct emcmot_status_t *emcmotStatus = 0;
struct emcmot_config_t *emcmotConfig = 0;
struct emcmot_internal_t *emcmotInternal = 0;
struct emcmot_error_t *emcmotError = 0;	/* unused for RT_FIFO */

/***********************************************************************
*                  LOCAL VARIABLE DECLARATIONS                         *
************************************************************************/

/* RTAPI shmem ID - for comms with higher level user space stuff */
static int emc_shmem_id;	/* the shared memory ID */

static int mot_comp_id;	/* component ID for motion module */

/***********************************************************************
*           KINEMATICS API WRAPPERS (via GMI kins_callbacks_t)         *
************************************************************************/

/* Pointer to kinematics callbacks, set during New() via kins_api_get(). */
static const kins_callbacks_t *motmod_kins;

/* These functions satisfy the legacy extern declarations in kinematics.h
   but delegate to the registered kins API callbacks. */

int kinematicsForward(const double *joint,
                      struct EmcPose *world,
                      const KINEMATICS_FORWARD_FLAGS *fflags,
                      KINEMATICS_INVERSE_FLAGS *iflags)
{
    uint64_t ifl = *iflags;
    int32_t result = motmod_kins->forward(joint, (kins_pose_t *)world,
                         (uint64_t)*fflags, &ifl);
    *iflags = ifl;
    return result;
}

int kinematicsInverse(const struct EmcPose *world,
                      double *joint,
                      const KINEMATICS_INVERSE_FLAGS *iflags,
                      KINEMATICS_FORWARD_FLAGS *fflags)
{
    uint64_t ffl = *fflags;
    int32_t result = motmod_kins->inverse((const kins_pose_t *)world, joint,
                         (uint64_t)*iflags, &ffl);
    *fflags = ffl;
    return result;
}

KINEMATICS_TYPE kinematicsType(void)
{
    return (KINEMATICS_TYPE)motmod_kins->type();
}

int kinematicsSwitchable(void)
{
    return motmod_kins->switchable();
}

int kinematicsSwitch(int switchkins_type)
{
    return motmod_kins->switch_(switchkins_type);
}

/***********************************************************************
*           MOT API CALLBACKS (provided by motmod, consumed by         *
*           tpmod and homemod via mot_api_get())                       *
************************************************************************/

/* --- I/O callbacks --- */

static void gmi_mot_dio_write(int32_t index, int8_t value)
{    emcmotDioWrite(index, value);
}

static void gmi_mot_aio_write(int32_t index, double value)
{    emcmotAioWrite(index, value);
}

/* --- Rotary unlock --- */

static void gmi_mot_set_rotary_unlock(int32_t jnum, int32_t unlock)
{    emcmotSetRotaryUnlock(jnum, unlock);
}

static int32_t gmi_mot_get_rotary_unlock(int32_t jnum)
{
    return emcmotGetRotaryIsUnlocked(jnum);
}

/* --- Axis limits --- */

static double gmi_mot_axis_get_vel_limit(int32_t axis)
{
    return axis_get_vel_limit(axis);
}

static double gmi_mot_axis_get_acc_limit(int32_t axis)
{
    return axis_get_acc_limit(axis);
}

/* --- Config getters (emcmotConfig fields, read-only) --- */

static int32_t gmi_mot_cfg_get_arc_blend_enable(void)
{
    return emcmotConfig->arcBlendEnable;
}

static int32_t gmi_mot_cfg_get_arc_blend_gap_cycles(void)
{
    return emcmotConfig->arcBlendGapCycles;
}

static int32_t gmi_mot_cfg_get_arc_blend_opt_depth(void)
{
    return emcmotConfig->arcBlendOptDepth;
}

static double gmi_mot_cfg_get_arc_blend_ramp_freq(void)
{
    return emcmotConfig->arcBlendRampFreq;
}

static double gmi_mot_cfg_get_arc_blend_tangent_kink_ratio(void)
{
    return emcmotConfig->arcBlendTangentKinkRatio;
}

static double gmi_mot_cfg_get_max_feed_scale(void)
{
    return emcmotConfig->maxFeedScale;
}

static int32_t gmi_mot_cfg_get_num_aio(void)
{
    return emcmotConfig->numAIO;
}

static int32_t gmi_mot_cfg_get_num_dio(void)
{
    return emcmotConfig->numDIO;
}

static int32_t gmi_mot_cfg_get_num_spindles(void)
{
    return emcmotConfig->numSpindles;
}

/* --- Status getters --- */

static double gmi_mot_status_get_net_feed_scale(void)
{
    return emcmotStatus->net_feed_scale;
}

static int32_t gmi_mot_status_get_stepping(void)
{
    return emcmotStatus->stepping;
}

static double gmi_mot_status_get_current_vel(void)
{
    return emcmotStatus->current_vel;
}

static int32_t gmi_mot_status_get_spindle_sync(void)
{
    return emcmotStatus->spindleSync;
}

static double gmi_mot_status_get_spindle_revs(int32_t spindle)
{
    return emcmotStatus->spindle_status[spindle].spindleRevs;
}

static int32_t gmi_mot_status_get_spindle_direction(int32_t spindle)
{
    return emcmotStatus->spindle_status[spindle].direction;
}

static int32_t gmi_mot_status_get_spindle_at_speed(int32_t spindle)
{
    return emcmotStatus->spindle_status[spindle].at_speed;
}

static double gmi_mot_status_get_spindle_speed_in(int32_t spindle)
{
    return emcmotStatus->spindle_status[spindle].spindleSpeedIn;
}

static int32_t gmi_mot_status_get_spindle_index_enable(int32_t spindle)
{
    return emcmotStatus->spindle_status[spindle].spindle_index_enable;
}

static uint8_t gmi_mot_status_get_enables_new(void)
{
    return emcmotStatus->enables_new;
}

static double gmi_mot_status_get_spindle_speed(int32_t spindle)
{
    return emcmotStatus->spindle_status[spindle].speed;
}

/* --- Status setters --- */

static void gmi_mot_status_set_current_vel(double vel)
{    emcmotStatus->current_vel = vel;
}

static void gmi_mot_status_set_requested_vel(double vel)
{    emcmotStatus->requested_vel = vel;
}

static void gmi_mot_status_set_distance_to_go(double dist)
{    emcmotStatus->distance_to_go = dist;
}

static void gmi_mot_status_set_dtg(mot_pose_t *dtg)
{    memcpy(&emcmotStatus->dtg, dtg, sizeof(EmcPose));
}

static void gmi_mot_status_or_motion_flag(uint32_t bits)
{    emcmotStatus->motionFlag |= bits;
}

static void gmi_mot_status_set_enables_queued(uint8_t val)
{    emcmotStatus->enables_queued = val;
}

static void gmi_mot_status_set_spindle_sync(int32_t val)
{    emcmotStatus->spindleSync = val;
}

static void gmi_mot_status_set_tcqlen(uint32_t len)
{    emcmotStatus->tcqlen = len;
}

static void gmi_mot_status_set_spindle_speed(int32_t spindle, double speed)
{    emcmotStatus->spindle_status[spindle].speed = speed;
}

static void gmi_mot_status_set_spindle_index_enable(int32_t spindle, int32_t enable)
{    emcmotStatus->spindle_status[spindle].spindle_index_enable = enable;
}

/* --- Joint accessors (for homing subsystem) --- */

static int32_t gmi_mot_get_num_joints(void)
{
    return num_joints;
}

static int32_t gmi_mot_joint_get_active_flag(int32_t jno)
{
    return GET_JOINT_ACTIVE_FLAG(&joints[jno]);
}

static int32_t gmi_mot_joint_get_inpos_flag(int32_t jno)
{
    return GET_JOINT_INPOS_FLAG(&joints[jno]);
}

static int32_t gmi_mot_joint_get_free_tp_active(int32_t jno)
{
    return joints[jno].free_tp.active;
}

static void gmi_mot_joint_set_free_tp_enable(int32_t jno, int32_t enable)
{    joints[jno].free_tp.enable = enable;
}

static double gmi_mot_joint_get_free_tp_pos_cmd(int32_t jno)
{
    return joints[jno].free_tp.pos_cmd;
}

static void gmi_mot_joint_set_free_tp_pos_cmd(int32_t jno, double val)
{    joints[jno].free_tp.pos_cmd = val;
}

static double gmi_mot_joint_get_free_tp_curr_pos(int32_t jno)
{
    return joints[jno].free_tp.curr_pos;
}

static void gmi_mot_joint_set_free_tp_curr_pos(int32_t jno, double val)
{    joints[jno].free_tp.curr_pos = val;
}

static void gmi_mot_joint_set_free_tp_max_vel(int32_t jno, double vel)
{    joints[jno].free_tp.max_vel = vel;
}

static double gmi_mot_joint_get_free_tp_max_vel(int32_t jno)
{
    return joints[jno].free_tp.max_vel;
}

static double gmi_mot_joint_get_pos_cmd(int32_t jno)
{
    return joints[jno].pos_cmd;
}

static void gmi_mot_joint_set_pos_cmd(int32_t jno, double val)
{    joints[jno].pos_cmd = val;
}

static double gmi_mot_joint_get_pos_fb(int32_t jno)
{
    return joints[jno].pos_fb;
}

static void gmi_mot_joint_set_pos_fb(int32_t jno, double val)
{    joints[jno].pos_fb = val;
}

static double gmi_mot_joint_get_motor_pos_fb(int32_t jno)
{
    return joints[jno].motor_pos_fb;
}

static double gmi_mot_joint_get_motor_offset(int32_t jno)
{
    return joints[jno].motor_offset;
}

static void gmi_mot_joint_set_motor_offset(int32_t jno, double val)
{    joints[jno].motor_offset = val;
}

static double gmi_mot_joint_get_backlash_filt(int32_t jno)
{
    return joints[jno].backlash_filt;
}

static double gmi_mot_joint_get_vel_limit(int32_t jno)
{
    return joints[jno].vel_limit;
}

static double gmi_mot_joint_get_max_pos_limit(int32_t jno)
{
    return joints[jno].max_pos_limit;
}

static double gmi_mot_joint_get_min_pos_limit(int32_t jno)
{
    return joints[jno].min_pos_limit;
}

static int32_t gmi_mot_joint_get_on_pos_limit(int32_t jno)
{
    return joints[jno].on_pos_limit;
}

static int32_t gmi_mot_joint_get_on_neg_limit(int32_t jno)
{
    return joints[jno].on_neg_limit;
}

/* mot API callback table */
static const mot_callbacks_t motmod_mot_callbacks = GMI_MOT_CALLBACKS;

/***********************************************************************
*                   LOCAL FUNCTION PROTOTYPES                          *
************************************************************************/
/* init_hal_io() exports HAL pins and parameters making data from
   the realtime control module visible and usable by the world
*/
static int init_hal_io(void);

/* functions called by init_hal_io() */

// halpins for ALL joints (kinematic joints and extra joints):
static int export_joint(int num,           joint_hal_t * addr);
// additional halpins for extrajoints:
static int export_extrajoint(int num, extrajoint_hal_t * addr);

static int export_spindle(int num, spindle_hal_t * addr);

/* init_comm_buffers() allocates and initializes the command,
   status, and error buffers used to communicate with the user
   space parts of emc.
*/
static int init_comm_buffers(void);

/* export_functions() exports realtime functions for the motion controller.
   Thread creation is handled externally (by the launcher loading the
   threads component); motmod only exports its functions here. The caller
   is responsible for adding these functions to the appropriate threads via
   `addf` (e.g., `addf motion-command-handler servo-thread`).
*/
static int export_functions(void);

/* functions called by export_functions() */
static int setTrajCycleTime(double secs);
static int setServoCycleTime(double secs);

static int module_intfc(void);
static int tp_init(void);
/***********************************************************************
*                     PUBLIC FUNCTION CODE                             *
************************************************************************/
int joint_is_lockable(int joint_num) {
    return (unlock_joints_mask & (1 << joint_num) );
}

void switch_to_teleop_mode(void) {
    int joint_num;
    emcmot_joint_t *joint;

    if (emcmotConfig->kinType != KINEMATICS_IDENTITY) {
        if (!motmod_home_api->get_allhomed()) {
            reportError(_("all joints must be homed before going into teleop mode"));
            return;
        }
    }

    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
        joint = &joints[joint_num];
        if (joint != 0) { joint->free_tp.enable = 0; }
    }

    emcmotInternal->teleoperating = 1;
    emcmotInternal->coordinating  = 0;
}


void emcmot_config_change(void)
{
    if (emcmotConfig->head == emcmotConfig->tail) {
	emcmotConfig->config_num++;
	emcmotStatus->config_num = emcmotConfig->config_num;
	emcmotConfig->head++;
    }
}

void reportError(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    emcmotErrorPutfv(emcmotError, fmt, args);
    va_end(args);
}

#ifndef va_copy
#define va_copy(dest, src) ((dest)=(src))
#endif

static rtapi_msg_handler_t old_handler = NULL;
static void emc_message_handler(msg_level_t level, const char *fmt, va_list ap)
{
    va_list apc;
    va_copy(apc, ap);
    if(level == RTAPI_MSG_ERR) emcmotErrorPutfv(emcmotError, fmt, apc);
    if(old_handler) old_handler(level, fmt, ap);
    va_end(apc);
}

int count_names(char *names[]){
  int namecount = 0;
  int i;
  for (i = 0; i < MAX_IO; i++) {
    if (((names[i] == NULL) || (*names[i] == 0))){
      break;
    }
    namecount = i + 1;
  }
  return namecount;
}

static int module_intfc() {
    motmod_tp_api->init();
    return 0;
}

static int tp_init() {
    if (-1 == motmod_tp_api->create(DEFAULT_TC_QUEUE_SIZE,mot_comp_id)) {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "MOTION: motmod_tp_api->create failed\n");
        return -1;
    }
    // tpInit is called from motmod_tp_api->create
    motmod_tp_api->set_cycle_time(emcmotConfig->trajCycleTime);
    motmod_tp_api->set_vmax(emcmotStatus->vel, emcmotStatus->vel);
    motmod_tp_api->set_amax(emcmotStatus->acc);
    motmod_tp_api->set_pos((tp_pose_t *)&emcmotStatus->carte_pos_cmd);
    return 0;
}

/***********************************************************************
*              ARGUMENT PARSING (replaces RTAPI_MP_* macros)           *
************************************************************************/

/* Parse "key=value" from argv[].  Supports int, long, and array-of-string.
   Returns 0 on success, -1 if a required value is malformed. */
static int parse_argv(int argc, const char **argv)
{
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!a) continue;

        if (strncmp(a, "key=", 4) == 0)                  key = atoi(a + 4);
        else if (strncmp(a, "base_period_nsec=", 17) == 0) base_period_nsec = atol(a + 17);
        else if (strncmp(a, "base_thread_fp=", 15) == 0)  base_thread_fp = atoi(a + 15);
        else if (strncmp(a, "servo_period_nsec=", 18) == 0) servo_period_nsec = atol(a + 18);
        else if (strncmp(a, "traj_period_nsec=", 17) == 0) traj_period_nsec = atol(a + 17);
        else if (strncmp(a, "num_spindles=", 13) == 0)    num_spindles = atoi(a + 13);
        else if (strncmp(a, "num_joints=", 11) == 0)      num_joints = atoi(a + 11);
        else if (strncmp(a, "num_extrajoints=", 16) == 0) num_extrajoints = atoi(a + 16);
        else if (strncmp(a, "num_dio=", 8) == 0)          num_dio = atoi(a + 8);
        else if (strncmp(a, "num_aio=", 8) == 0)          num_aio = atoi(a + 8);
        else if (strncmp(a, "num_misc_error=", 15) == 0)  num_misc_error = atoi(a + 15);
        else if (strncmp(a, "unlock_joints_mask=", 19) == 0) unlock_joints_mask = atoi(a + 19);
        /* Array-of-string params: names_din=foo,bar,baz */
        else if (strncmp(a, "names_din=", 10) == 0) {
            const char *p = a + 10;
            for (int n = 0; n < MAX_IO && *p; n++) {
                const char *c = strchr(p, ',');
                size_t len = c ? (size_t)(c - p) : strlen(p);
                names_din[n] = strndup(p, len);
                p = c ? c + 1 : p + len;
            }
        }
        else if (strncmp(a, "names_dout=", 11) == 0) {
            const char *p = a + 11;
            for (int n = 0; n < MAX_IO && *p; n++) {
                const char *c = strchr(p, ',');
                size_t len = c ? (size_t)(c - p) : strlen(p);
                names_dout[n] = strndup(p, len);
                p = c ? c + 1 : p + len;
            }
        }
        else if (strncmp(a, "names_ain=", 10) == 0) {
            const char *p = a + 10;
            for (int n = 0; n < MAX_IO && *p; n++) {
                const char *c = strchr(p, ',');
                size_t len = c ? (size_t)(c - p) : strlen(p);
                names_ain[n] = strndup(p, len);
                p = c ? c + 1 : p + len;
            }
        }
        else if (strncmp(a, "names_aout=", 11) == 0) {
            const char *p = a + 11;
            for (int n = 0; n < MAX_IO && *p; n++) {
                const char *c = strchr(p, ',');
                size_t len = c ? (size_t)(c - p) : strlen(p);
                names_aout[n] = strndup(p, len);
                p = c ? c + 1 : p + len;
            }
        }
        else if (strncmp(a, "names_misc_errors=", 18) == 0) {
            const char *p = a + 18;
            for (int n = 0; n < MAX_IO && *p; n++) {
                const char *c = strchr(p, ',');
                size_t len = c ? (size_t)(c - p) : strlen(p);
                names_misc_errors[n] = strndup(p, len);
                p = c ? c + 1 : p + len;
            }
        }
        /* Ignore unknown params — HAL file may pass extra args */
    }
    return 0;
}

static void free_name_arrays(void)
{
    for (int i = 0; i < MAX_IO; i++) {
        free(names_din[i]);   names_din[i] = NULL;
        free(names_dout[i]);  names_dout[i] = NULL;
        free(names_ain[i]);   names_ain[i] = NULL;
        free(names_aout[i]);  names_aout[i] = NULL;
        free(names_misc_errors[i]); names_misc_errors[i] = NULL;
    }
}

/***********************************************************************
*                    cmod LIFECYCLE                                     *
************************************************************************/

/* Forward declaration — filled in below. */
static void motmod_Destroy(cmod_t *self);

/* Module-level cmod_t (motmod is a singleton). */
static cmod_t motmod_cmod;

/* Store the env pointer for Destroy(). */
static const cmod_env_t *motmod_env;

/* GMI API pointers — set in Init(), used by bridge inlines */
const tp_callbacks_t   *motmod_tp_api;
const home_callbacks_t *motmod_home_api;

static int motmod_init(cmod_t *self);

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out)
{
    int retval;

    motmod_env = env;

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: New() starting...\n");

    /* Parse module arguments from argv */
    if (parse_argv(argc, argv) != 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: argument parsing failed\n"));
        return -1;
    }

    /* connect to the HAL and RTAPI */
    mot_comp_id = hal_init_ex(name, env->dl_handle, COMPONENT_TYPE_REALTIME);
    if (mot_comp_id < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: hal_init_ex() failed\n"));
	return -1;
    }

    /* Register the mot reverse-callback API so tpmod/homemod can look it up
       in their Init() functions. */
    retval = mot_api_register(env->api, "default", &motmod_mot_callbacks);
    if (retval != 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: failed to register mot API: %d\n"), retval);
	hal_exit(mot_comp_id);
	return -1;
    }

    if (( num_joints < 1 ) || ( num_joints > EMCMOT_MAX_JOINTS )) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: num_joints is %d, must be between 1 and %d\n"), num_joints, EMCMOT_MAX_JOINTS);
	hal_exit(mot_comp_id);
	return -1;
    }

    if (( num_extrajoints < 0 ) || ( num_extrajoints > num_joints )) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("\nMOTION: num_extrajoints is %d, must be between 0 and %d\n\n"), num_extrajoints, num_joints);
	hal_exit(mot_comp_id);
	return -1;
    }
    if (num_extrajoints > 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
            _("\nMOTION: kinematicjoints=%2d\n            extrajoints=%2d\n           Total joints=%2d\n\n"),
            num_joints-num_extrajoints, num_extrajoints, num_joints
            );
    }

    if (( num_spindles < 0 ) || ( num_spindles > EMCMOT_MAX_SPINDLES )) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: num_spindles is %d, must be between 0 and %d\n"), num_spindles, EMCMOT_MAX_SPINDLES);
	hal_exit(mot_comp_id);
	return -1;
    }
    motion_num_spindles = num_spindles;

    if(num_dio && (names_dout[0] || names_din[0])){
      rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: Can't specify both names and number for digital pins\n"));
      return -1;
    }
    else if(names_dout[0] || names_din[0]){
      num_dio = count_names(names_dout);
      num_dio = (num_dio > count_names(names_din)) ? num_dio : count_names(names_din);
    }
    else if(!num_dio){
      num_dio = DEFAULT_DIO;
    }


    if (( num_dio < 1 ) || ( num_dio > EMCMOT_MAX_DIO )) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: num_dio is %d, must be between 1 and %d\n"), num_dio, EMCMOT_MAX_DIO);
	hal_exit(mot_comp_id);
	return -1;
    }

  if(num_aio && (names_aout[0] || names_ain[0])){
    rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: Can't specify both names and number for analog pins\n"));
    return -1;
  }
  else if(names_aout[0] || names_ain[0]){
    num_aio = count_names(names_aout);
    num_aio = (num_aio > count_names(names_ain)) ? num_aio : count_names(names_ain);
  }
  else if(!num_aio){
    num_aio = DEFAULT_AIO;
  }

    if (( num_aio < 1 ) || ( num_aio > EMCMOT_MAX_AIO )) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: num_aio is %d, must be between 1 and %d\n"), num_aio, EMCMOT_MAX_AIO);
	hal_exit(mot_comp_id);
	return -1;
    }

  if(num_misc_error != -1 && (names_misc_errors[0])){
    rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: Can't specify both names and number for misc error\n"));
    return -1;
  }
  else if(names_misc_errors[0]){
    num_misc_error = count_names(names_misc_errors);
  }
  else if (num_misc_error < 0) {
    num_misc_error = DEFAULT_MISC_ERROR;
  }

  if (( num_misc_error < 0 ) || ( num_misc_error > EMCMOT_MAX_MISC_ERROR )) {
    rtapi_print_msg(RTAPI_MSG_ERR,
                    _("MOTION: num_misc_error is %d, must be between 0 and %d\n"), num_misc_error, EMCMOT_MAX_MISC_ERROR);
    hal_exit(mot_comp_id);
    return -1;
  }

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: New() complete\n");

    /* Set up cmod interface */
    motmod_cmod.Init    = motmod_init;
    motmod_cmod.Start   = NULL;
    motmod_cmod.Stop    = NULL;
    motmod_cmod.Destroy = motmod_Destroy;
    motmod_cmod.priv    = NULL;

    *out = &motmod_cmod;
    return 0;
}

/*
 * motmod Init() — look up APIs registered by other modules during New(),
 * then initialize the trajectory planner and homing subsystem.
 *
 * By the time Init() runs, all modules' New() have completed (APIs
 * registered) and earlier-loaded modules' Init() have also completed
 * (tpmod/homemod have wired their function pointers via the mot API).
 */
static int motmod_init(cmod_t *self)
{
    int retval;
    (void)self;

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: Init() starting...\n");

    /* --- Cross-module API lookups (must come first) --- */

    /* Look up the kinematics API registered by the kins module */
    motmod_kins = kins_api_get(motmod_env->api, "kinematics");
    if (!motmod_kins) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: kinematics API not registered (is kins module loaded?)\n"));
	return -1;
    }

    /* Look up the trajectory planner API registered by the tp module */
    motmod_tp_api = tp_api_get(motmod_env->api, "default");
    if (!motmod_tp_api) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: tp API not registered (is tp module loaded?)\n"));
	return -1;
    }

    /* Look up the homing API registered by the home module */
    motmod_home_api = home_api_get(motmod_env->api, "default");
    if (!motmod_home_api) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: home API not registered (is home module loaded?)\n"));
	return -1;
    }

    /* --- Validation (depends on kins) --- */

    if ( (num_extrajoints > 0) && (kinematicsType() != KINEMATICS_BOTH) ) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("\nMOTION: nonzero num_extrajoints requires KINEMATICS_BOTH\n\n"));
        return -1;
    }

    /* --- HAL pins, shared memory, RT function export --- */

    retval = init_hal_io();
    if (retval != 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: init_hal_io() failed\n"));
	return -1;
    }

    retval = init_comm_buffers();
    if (retval != 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: init_comm_buffers() failed\n"));
	return -1;
    }

    retval = export_functions();
    if (retval != 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: export_functions() failed\n"));
	return -1;
    }

    /* --- Subsystem initialization --- */

    if (module_intfc()) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: module_intfc() failed\n"));
	return -1;
    }
    if (tp_init()) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: tp_init() failed\n"));
	return -1;
    }

    /* Initialize homing via GMI home API */
    if (motmod_home_api->init(mot_comp_id,
                              emcmotConfig->servoCycleTime,
                              num_joints,
                              num_extrajoints) != 0) {
        rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: homing init failed\n"));
        return -1;
    }

    old_handler = rtapi_get_msg_handler();
    rtapi_set_msg_handler(emc_message_handler);

    hal_ready(mot_comp_id);

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: Init() complete\n");
    return 0;
}

static void motmod_Destroy(cmod_t *self)
{
    int retval;
    (void)self;

    rtapi_set_msg_handler(old_handler);

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: Destroy() started.\n");

    /* free shared memory */
    retval = rtapi_shmem_delete(emc_shmem_id, mot_comp_id);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: rtapi_shmem_delete() failed, returned %d\n"), retval);
    }
    /* disconnect from HAL and RTAPI */
    retval = hal_exit(mot_comp_id);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    _("MOTION: hal_exit() failed, returned %d\n"), retval);
    }

    free_name_arrays();

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: Destroy() finished.\n");
}

/***********************************************************************
*                         LOCAL FUNCTION CODE                          *
************************************************************************/

#define CALL_CHECK(expr) do {           \
        int _retval;                    \
        _retval = expr;                 \
        if (_retval) return _retval;    \
    } while (0);

/* init_hal_io() exports HAL pins and parameters making data from
   the realtime control module visible and usable by the world
*/
static int init_hal_io(void)
{
    int n, retval;
    joint_hal_t      *joint_data;
    extrajoint_hal_t *ejoint_data;

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: init_hal_io() starting...\n");

    /* allocate shared memory for machine data */
    emcmot_hal_data = hal_malloc(sizeof(emcmot_hal_data_t));
    if (emcmot_hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: emcmot_hal_data malloc failed\n"));
	return -1;
    }

    /* export machine wide hal pins */
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->probe_input), mot_comp_id, "motion.probe-input"));
    CALL_CHECK(hal_pin_float_newf(HAL_IN, &(emcmot_hal_data->adaptive_feed), mot_comp_id, "motion.adaptive-feed"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->feed_hold), mot_comp_id, "motion.feed-hold"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->feed_inhibit), mot_comp_id, "motion.feed-inhibit"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->homing_inhibit), mot_comp_id, "motion.homing-inhibit"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->jog_inhibit), mot_comp_id, "motion.jog-inhibit"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->jog_stop), mot_comp_id, "motion.jog-stop"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->jog_stop_immediate), mot_comp_id, "motion.jog-stop-immediate"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->tp_reverse), mot_comp_id, "motion.tp-reverse"));
    CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->enable), mot_comp_id, "motion.enable"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->is_all_homed), mot_comp_id, "motion.is-all-homed"));

    /* state tags pins */
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->feed_upm), mot_comp_id, "motion.feed-upm"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->feed_inches_per_minute), mot_comp_id, "motion.feed-inches-per-minute"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->feed_inches_per_second), mot_comp_id, "motion.feed-inches-per-second"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->feed_mm_per_minute), mot_comp_id, "motion.feed-mm-per-minute"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->feed_mm_per_second), mot_comp_id, "motion.feed-mm-per-second"));

    /* export motion-synched digital output pins */
    /* export motion digital input pins */
    if (names_din[0]){
        for (n = 0; n < num_dio; n++) {
            if (names_din[n] == NULL || (*names_din[n] == 0)) {break;}
            CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->synch_di[n]), mot_comp_id, "motion.din-%s", names_din[n]));
        }
    } else {
        for (n = 0; n < num_dio; n++) {
            CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->synch_di[n]), mot_comp_id, "motion.digital-in-%02d", n));
        }
    }

    if (names_dout[0]){
        for (n = 0; n < num_dio; n++) {
            if (names_dout[n] == NULL || (*names_dout[n] == 0)) {break;}
            CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->synch_do[n]), mot_comp_id, "motion.dout-%s", names_dout[n]));
        }
    } else {
        for (n = 0; n < num_dio; n++) {
            CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->synch_do[n]), mot_comp_id, "motion.digital-out-%02d",n));
        }
    }

    /* export motion-synched analog output pins */
    /* export motion analog input pins */
    if (names_ain[0]) {
        for (n = 0; n < num_aio; n++) {
            if (names_ain[n] == NULL || (*names_ain[n] == 0)) {break;}
            CALL_CHECK(hal_pin_float_newf(HAL_IN, &(emcmot_hal_data->analog_input[n]), mot_comp_id, "motion.ain-%s", names_ain[n]));
        }
    } else {
        for (n = 0; n < num_aio; n++) {
            CALL_CHECK(hal_pin_float_newf(HAL_IN, &(emcmot_hal_data->analog_input[n]), mot_comp_id, "motion.analog-in-%02d", n));
        }
    }
    if (names_aout[0]) {
        for (n = 0; n < num_aio; n++) {
            if (names_aout[n] == NULL || (*names_aout[n] == 0)) {break;}
            CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->analog_output[n]), mot_comp_id, "motion.aout-%s", names_aout[n]));
        }
    } else {
        for (n = 0; n < num_aio; n++) {
            CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->analog_output[n]), mot_comp_id, "motion.analog-out-%02d", n));
        }
    }

    if (names_misc_errors[0]) {
        for (n = 0; n < num_misc_error; n++) {
            if (names_misc_errors[n] == NULL || (*names_misc_errors[n] == 0)) {break;}
            CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->misc_error[n]), mot_comp_id, "motion.err-%s", names_misc_errors[n]));
        }
    } else {
        /* export misc error input pins */
        for (n = 0; n < num_misc_error; n++) {
            CALL_CHECK(hal_pin_bit_newf(HAL_IN, &(emcmot_hal_data->misc_error[n]), mot_comp_id, "motion.misc-error-%02d", n));
        }
    }

    /* export machine wide hal pins */
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->motion_enabled), mot_comp_id, "motion.motion-enabled"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->in_position), mot_comp_id, "motion.in-position"));
    CALL_CHECK(hal_pin_s32_newf(HAL_OUT, &(emcmot_hal_data->motion_type), mot_comp_id, "motion.motion-type"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->coord_mode), mot_comp_id, "motion.coord-mode"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->teleop_mode), mot_comp_id, "motion.teleop-mode"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->coord_error), mot_comp_id, "motion.coord-error"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->on_soft_limit), mot_comp_id, "motion.on-soft-limit"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->current_vel), mot_comp_id, "motion.current-vel"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->requested_vel), mot_comp_id, "motion.requested-vel"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->distance_to_go), mot_comp_id, "motion.distance-to-go"));
    CALL_CHECK(hal_pin_s32_newf(HAL_OUT, &(emcmot_hal_data->program_line), mot_comp_id, "motion.program-line"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->jog_is_active), mot_comp_id, "motion.jog-is-active"));

    /* export debug parameters */
    /* these can be used to view any internal variable, simply change a line
       in control.c:output_to_hal() and recompile */
    CALL_CHECK(hal_param_bit_newf(HAL_RO, &(emcmot_hal_data->debug_bit_0), mot_comp_id, "motion.debug-bit-0"));
    CALL_CHECK(hal_param_bit_newf(HAL_RO, &(emcmot_hal_data->debug_bit_1), mot_comp_id, "motion.debug-bit-1"));
    CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->debug_float_0), mot_comp_id, "motion.debug-float-0"));
    CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->debug_float_1), mot_comp_id, "motion.debug-float-1"));
    CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->debug_float_2), mot_comp_id, "motion.debug-float-2"));
    CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->debug_float_3), mot_comp_id, "motion.debug-float-3"));
    CALL_CHECK(hal_param_s32_newf(HAL_RO, &(emcmot_hal_data->debug_s32_0), mot_comp_id, "motion.debug-s32-0"));
    CALL_CHECK(hal_param_s32_newf(HAL_RO, &(emcmot_hal_data->debug_s32_1), mot_comp_id, "motion.debug-s32-1"));

    // FIXME - debug only, remove later
    // export HAL parameters for some trajectory planner internal variables
    // so they can be scoped
    CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->traj_pos_out), mot_comp_id, "traj.pos_out"));
    CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->traj_vel_out), mot_comp_id, "traj.vel_out"));
    CALL_CHECK(hal_param_u32_newf(HAL_RO, &(emcmot_hal_data->traj_active_tc), mot_comp_id, "traj.active_tc"));

    for (n = 0; n < 4; n++) {
        CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->tc_pos[n]), mot_comp_id, "tc.%d.pos", n));
        CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->tc_vel[n]), mot_comp_id, "tc.%d.vel", n));
        CALL_CHECK(hal_param_float_newf(HAL_RO, &(emcmot_hal_data->tc_acc[n]), mot_comp_id, "tc.%d.acc", n));
    }
    // end of exporting trajectory planner internals

    // export timing related HAL pins so they can be scoped and/or connected
    CALL_CHECK(hal_pin_u32_newf(HAL_OUT, &(emcmot_hal_data->last_period), mot_comp_id, "motion.servo.last-period"));

    // export timing related HAL pins so they can be scoped
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_x), mot_comp_id, "motion.tooloffset.x"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_y), mot_comp_id, "motion.tooloffset.y"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_z), mot_comp_id, "motion.tooloffset.z"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_a), mot_comp_id, "motion.tooloffset.a"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_b), mot_comp_id, "motion.tooloffset.b"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_c), mot_comp_id, "motion.tooloffset.c"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_u), mot_comp_id, "motion.tooloffset.u"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_v), mot_comp_id, "motion.tooloffset.v"));
    CALL_CHECK(hal_pin_float_newf(HAL_OUT, &(emcmot_hal_data->tooloffset_w), mot_comp_id, "motion.tooloffset.w"));

    /* Always create switchkins-type pin; it's a no-op if kins isn't switchable. */
    CALL_CHECK(hal_pin_float_newf(HAL_IN, &(emcmot_hal_data->switchkins_type), mot_comp_id, "motion.switchkins-type"));

    /* initialize machine wide pins and parameters */
    *(emcmot_hal_data->adaptive_feed) = 1.0;
    *(emcmot_hal_data->feed_hold) = 0;
    *(emcmot_hal_data->feed_inhibit) = 0;
    *(emcmot_hal_data->homing_inhibit) = 0;
    *(emcmot_hal_data->jog_inhibit) = 0;
    *(emcmot_hal_data->jog_stop) = 0;
    *(emcmot_hal_data->jog_stop_immediate) = 0;
    *(emcmot_hal_data->is_all_homed) = 0;

    *(emcmot_hal_data->probe_input) = 0;
    /* default value of enable is TRUE, so simple machines
       can leave it disconnected */
    *(emcmot_hal_data->enable) = 1;

    /* motion synched dio, init to not enabled */
    for (n = 0; n < num_dio; n++) {
        *(emcmot_hal_data->synch_do[n]) = 0;
        *(emcmot_hal_data->synch_di[n]) = 0;
    }

    for (n = 0; n < num_aio; n++) {
        *(emcmot_hal_data->analog_output[n]) = 0.0;
        *(emcmot_hal_data->analog_input[n]) = 0.0;
    }

    for (n = 0; n < num_misc_error; n++) {
        *(emcmot_hal_data->misc_error[n]) = 0;
    }

    /*! \todo FIXME - these don't really need initialized, since they are written
       with data from the emcmotStatus struct */
    *(emcmot_hal_data->motion_enabled) = 0;
    *(emcmot_hal_data->in_position) = 0;
    *(emcmot_hal_data->motion_type) = 0;
    *(emcmot_hal_data->coord_mode) = 0;
    *(emcmot_hal_data->teleop_mode) = 0;
    *(emcmot_hal_data->coord_error) = 0;
    *(emcmot_hal_data->on_soft_limit) = 0;

    /* init debug parameters */
    emcmot_hal_data->debug_bit_0 = 0;
    emcmot_hal_data->debug_bit_1 = 0;
    emcmot_hal_data->debug_float_0 = 0.0;
    emcmot_hal_data->debug_float_1 = 0.0;
    emcmot_hal_data->debug_float_2 = 0.0;
    emcmot_hal_data->debug_float_3 = 0.0;

    *(emcmot_hal_data->last_period) = 0;

    /* export spindle pins and params */
    for (n = 0; n < num_spindles; n++) {
        retval = export_spindle(n, &(emcmot_hal_data->spindle[n]));
        if (retval != 0){
            rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: spindle %d pin export failed"), n);
            return -1;
        }
    }
    /* export joint pins and parameters */
    for (n = 0; n < num_joints; n++) {
        joint_data = &(emcmot_hal_data->joint[n]);
        /* export all vars */
        retval = export_joint(n, joint_data);
        if (retval != 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: joint %d pin/param export failed\n"), n);
            return -1;
        }
        *(joint_data->amp_enable) = 0;

        /* We'll init the index model to EXT_ENCODER_INDEX_MODEL_RAW for now,
           because it is always supported. */
    }
    /* export joint pins and parameters */
    for (n = 0; n < num_extrajoints; n++) {
        ejoint_data = &(emcmot_hal_data->ejoint[n]);
        retval = export_extrajoint(n + num_joints - num_extrajoints,ejoint_data);
        if (retval != 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, _("MOTION: ejoint %d pin/param export failed\n"), n);
            return -1;
        }
    }

    CALL_CHECK(axis_init_hal_io(mot_comp_id));

    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->eoffset_limited), mot_comp_id, "motion.eoffset-limited"));
    CALL_CHECK(hal_pin_bit_newf(HAL_OUT, &(emcmot_hal_data->eoffset_active), mot_comp_id, "motion.eoffset-active"));

    /* Done! */
    rtapi_print_msg(RTAPI_MSG_INFO,	"MOTION: init_hal_io() complete, %d axes.\n", n);
    return 0;
}

static int export_spindle(int num, spindle_hal_t * addr){
	int retval, msg;

    msg = rtapi_get_msg_level();
    rtapi_set_msg_level(RTAPI_MSG_WARN);

    if ((retval = hal_pin_bit_newf(HAL_IO, &(addr->spindle_index_enable), mot_comp_id, "spindle.%d.index-enable", num)) != 0) return retval;

    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->spindle_on), mot_comp_id, "spindle.%d.on", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->spindle_forward), mot_comp_id, "spindle.%d.forward", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->spindle_reverse), mot_comp_id, "spindle.%d.reverse", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->spindle_brake), mot_comp_id, "spindle.%d.brake", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->spindle_speed_out), mot_comp_id, "spindle.%d.speed-out", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->spindle_speed_out_abs), mot_comp_id, "spindle.%d.speed-out-abs", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->spindle_speed_out_rps), mot_comp_id, "spindle.%d.speed-out-rps", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->spindle_speed_out_rps_abs), mot_comp_id, "spindle.%d.speed-out-rps-abs", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->spindle_speed_cmd_rps), mot_comp_id, "spindle.%d.speed-cmd-rps", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->spindle_inhibit), mot_comp_id, "spindle.%d.inhibit", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->spindle_amp_fault), mot_comp_id, "spindle.%d.amp-fault-in", num)) != 0) return retval;
    *(addr->spindle_inhibit) = 0;

    // spindle orient pins
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->spindle_orient_angle), mot_comp_id, "spindle.%d.orient-angle", num)) < 0) return retval;
    if ((retval = hal_pin_s32_newf(HAL_OUT, &(addr->spindle_orient_mode), mot_comp_id, "spindle.%d.orient-mode", num)) < 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->spindle_orient), mot_comp_id, "spindle.%d.orient", num)) < 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->spindle_locked), mot_comp_id, "spindle.%d.locked", num)) < 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->spindle_is_oriented), mot_comp_id, "spindle.%d.is-oriented", num)) < 0) return retval;
    if ((retval = hal_pin_s32_newf(HAL_IN, &(addr->spindle_orient_fault), mot_comp_id, "spindle.%d.orient-fault", num)) < 0) return retval;
    *(addr->spindle_orient_angle) = 0.0;
    *(addr->spindle_orient_mode) = 0;
    *(addr->spindle_orient) = 0;

    if ((retval = hal_pin_float_newf(HAL_IN, &(addr->spindle_revs), mot_comp_id, "spindle.%d.revs", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_IN, &(addr->spindle_speed_in), mot_comp_id, "spindle.%d.speed-in", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->spindle_is_atspeed), mot_comp_id, "spindle.%d.at-speed", num)) != 0) return retval;
    *(addr->spindle_is_atspeed) = 1;
    /* restore saved message level */
    rtapi_set_msg_level(msg);
    return 0;
}

static int export_joint(int num, joint_hal_t * addr)
{
    int retval, msg;

    /* This function exports a lot of stuff, which results in a lot of
       logging if msg_level is at INFO or ALL. So we save the current value
       of msg_level and restore it later.  If you actually need to log this
       function's actions, change the second line below */
    msg = rtapi_get_msg_level();
    rtapi_set_msg_level(RTAPI_MSG_WARN);

    /* export joint pins */
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->coarse_pos_cmd), mot_comp_id, "joint.%d.coarse-pos-cmd", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->joint_pos_cmd), mot_comp_id, "joint.%d.pos-cmd", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->joint_pos_fb), mot_comp_id, "joint.%d.pos-fb", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->motor_pos_cmd), mot_comp_id, "joint.%d.motor-pos-cmd", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_IN, &(addr->motor_pos_fb), mot_comp_id, "joint.%d.motor-pos-fb", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->motor_offset), mot_comp_id, "joint.%d.motor-offset", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->pos_lim_sw), mot_comp_id, "joint.%d.pos-lim-sw-in", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->neg_lim_sw), mot_comp_id, "joint.%d.neg-lim-sw-in", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->amp_enable), mot_comp_id, "joint.%d.amp-enable-out", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->amp_fault), mot_comp_id, "joint.%d.amp-fault-in", num)) != 0) return retval;
    if ((retval = hal_pin_s32_newf(HAL_IN,   &(addr->jjog_counts), mot_comp_id, "joint.%d.jog-counts", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN,   &(addr->jjog_enable), mot_comp_id, "joint.%d.jog-enable", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_IN, &(addr->jjog_scale), mot_comp_id, "joint.%d.jog-scale", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_IN,   &(addr->jjog_vel_mode), mot_comp_id, "joint.%d.jog-vel-mode", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->joint_vel_cmd), mot_comp_id, "joint.%d.vel-cmd", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->joint_acc_cmd), mot_comp_id, "joint.%d.acc-cmd", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->backlash_corr), mot_comp_id, "joint.%d.backlash-corr", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->backlash_filt), mot_comp_id, "joint.%d.backlash-filt", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->backlash_vel), mot_comp_id, "joint.%d.backlash-vel", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->f_error), mot_comp_id, "joint.%d.f-error", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->f_error_lim), mot_comp_id, "joint.%d.f-error-lim", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->free_pos_cmd), mot_comp_id, "joint.%d.free-pos-cmd", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_OUT, &(addr->free_vel_lim), mot_comp_id, "joint.%d.free-vel-lim", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->free_tp_enable), mot_comp_id, "joint.%d.free-tp-enable", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->kb_jjog_active), mot_comp_id, "joint.%d.kb-jog-active", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->wheel_jjog_active), mot_comp_id, "joint.%d.wheel-jog-active", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->in_position), mot_comp_id, "joint.%d.in-position", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->phl), mot_comp_id, "joint.%d.pos-hard-limit", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->nhl), mot_comp_id, "joint.%d.neg-hard-limit", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->active), mot_comp_id, "joint.%d.active", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->error), mot_comp_id, "joint.%d.error", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->f_errored), mot_comp_id, "joint.%d.f-errored", num)) != 0) return retval;
    if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->faulted), mot_comp_id, "joint.%d.faulted", num)) != 0) return retval;
    if ((retval = hal_pin_float_newf(HAL_IN,&(addr->jjog_accel_fraction),mot_comp_id,"joint.%d.jog-accel-fraction", num)) != 0) return retval;
    *addr->jjog_accel_fraction = 1.0; // fraction of accel for wheel jjogs

    if ( joint_is_lockable(num) ) {
        // these pins may be needed for rotary joints
        rtapi_print_msg(RTAPI_MSG_WARN,"motion.c: Creating unlock hal pins for joint %d\n",num);
        if ((retval = hal_pin_bit_newf(HAL_OUT, &(addr->unlock), mot_comp_id, "joint.%d.unlock", num)) != 0) return retval;
        if ((retval = hal_pin_bit_newf(HAL_IN, &(addr->is_unlocked), mot_comp_id, "joint.%d.is-unlocked", num)) != 0) return retval;
    }

    /* restore saved message level */
    rtapi_set_msg_level(msg);
    return 0;
}

static int export_extrajoint(int num, extrajoint_hal_t * addr)
{
    int retval;
    /* export extrajoint pins */
    if ((retval = hal_pin_float_newf(HAL_IN,  &(addr->posthome_cmd),  mot_comp_id,
                                            "joint.%d.posthome-cmd",  num)) != 0) return retval;
    return 0;
}

/* init_comm_buffers() allocates and initializes the command,
   status, and error buffers used to communicate with the user
   space parts of emc.
*/
static int init_comm_buffers(void)
{
    int joint_num, spindle_num, n;
    emcmot_joint_t *joint;
    int retval;

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: init_comm_buffers() starting...\n");

    emcmotStruct = 0;
    emcmotInternal = 0;
    emcmotStatus = 0;
    emcmotCommand = 0;
    emcmotConfig = 0;

    /* allocate and initialize the shared memory structure */
    emc_shmem_id = rtapi_shmem_new(key, mot_comp_id, sizeof(emcmot_struct_t));
    if (emc_shmem_id < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "MOTION: rtapi_shmem_new failed, returned %d\n", emc_shmem_id);
	return -1;
    }
    retval = rtapi_shmem_getptr(emc_shmem_id, (void **) &emcmotStruct);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "MOTION: rtapi_shmem_getptr failed, returned %d\n", retval);
	return -1;
    }

    /* zero shared memory before doing anything else. */
    memset(emcmotStruct, 0, sizeof(emcmot_struct_t));

    /* we'll reference emcmotStruct directly */
    emcmotCommand = &emcmotStruct->command;
    emcmotStatus = &emcmotStruct->status;
    emcmotConfig = &emcmotStruct->config;
    emcmotInternal = &emcmotStruct->internal;
    emcmotError = &emcmotStruct->error;

    /* init error struct */
    emcmotErrorInit(emcmotError);

    /* init command struct */
    emcmotCommand->command = 0;
    emcmotCommand->commandNum = 0;

    /* init status struct */
    emcmotStatus->head = 0;
    emcmotStatus->commandEcho = 0;
    emcmotStatus->commandNumEcho = 0;
    emcmotStatus->commandStatus = 0;

    /* init more stuff */
    emcmotInternal->head = 0;
    emcmotConfig->head = 0;

    emcmotStatus->motionFlag = 0;
    SET_MOTION_ERROR_FLAG(0);
    SET_MOTION_COORD_FLAG(0);
    SET_MOTION_TELEOP_FLAG(0);
    emcmotInternal->split = 0;
    emcmotStatus->heartbeat = 0;

    ALL_JOINTS                   = num_joints;      // emcmotConfig->numJoints from [KINS]JOINTS
    emcmotConfig->numExtraJoints = num_extrajoints; // from motmod num_extrajoints=
    emcmotStatus->numExtraJoints = num_extrajoints;

    emcmotConfig->numSpindles = num_spindles;
    emcmotConfig->numDIO = num_dio;
    emcmotConfig->numAIO = num_aio;
    emcmotConfig->numMiscError = num_misc_error;

    ZERO_EMC_POSE(emcmotStatus->carte_pos_cmd);
    ZERO_EMC_POSE(emcmotStatus->carte_pos_fb);
    emcmotStatus->vel = 0.0;
    emcmotConfig->limitVel = 0.0;
    emcmotStatus->acc = 0.0;
    emcmotStatus->feed_scale = 1.0;
    emcmotStatus->rapid_scale = 1.0;
    emcmotStatus->net_feed_scale = 1.0;
    /* adaptive feed is off by default, feed override, spindle
       override, and feed hold are on */
    emcmotStatus->enables_new = FS_ENABLED | SS_ENABLED | FH_ENABLED;
    emcmotStatus->enables_queued = emcmotStatus->enables_new;
    emcmotStatus->id = 0;
    emcmotStatus->depth = 0;
    emcmotStatus->activeDepth = 0;
    emcmotStatus->paused = 0;
    emcmotStatus->overrideLimitMask = 0;
    SET_MOTION_INPOS_FLAG(1);
    SET_MOTION_ENABLE_FLAG(0);
    /* record the kinematics type of the machine */
    emcmotConfig->kinType = kinematicsType();
    emcmot_config_change();

    for (spindle_num = 0; spindle_num < EMCMOT_MAX_SPINDLES; spindle_num++){
        emcmotStatus->spindle_status[spindle_num].scale = 1.0;
        emcmotStatus->spindle_status[spindle_num].speed = 0.0;
    }

    axis_init_all();

    /* init per-joint stuff */
    for (joint_num = 0; joint_num < ALL_JOINTS; joint_num++) {
	/* point to structure for this joint */
	joint = &joints[joint_num];

	/* init the config fields with some "reasonable" defaults" */
	joint->type = 0;
	joint->max_pos_limit = 1.0;
	joint->min_pos_limit = -1.0;
	joint->vel_limit = 1.0;
	joint->acc_limit = 1.0;
	joint->min_ferror = 0.01;
	joint->max_ferror = 1.0;
	joint->backlash = 0.0;

	joint->comp.entries = 0;
	joint->comp.entry = &(joint->comp.array[0]);
	/* the compensation code has -DBL_MAX at one end of the table
	   and +DBL_MAX at the other so _all_ commanded positions are
	   guaranteed to be covered by the table */
	joint->comp.array[0].nominal = -DBL_MAX;
	joint->comp.array[0].fwd_trim = 0.0;
	joint->comp.array[0].rev_trim = 0.0;
	joint->comp.array[0].fwd_slope = 0.0;
	joint->comp.array[0].rev_slope = 0.0;
	for ( n = 1 ; n < EMCMOT_COMP_SIZE+2 ; n++ ) {
	    joint->comp.array[n].nominal = DBL_MAX;
	    joint->comp.array[n].fwd_trim = 0.0;
	    joint->comp.array[n].rev_trim = 0.0;
	    joint->comp.array[n].fwd_slope = 0.0;
	    joint->comp.array[n].rev_slope = 0.0;
	}

	/* init joint flags */
	joint->flag = 0;
	SET_JOINT_INPOS_FLAG(joint, 1);

	/* init status info */
	joint->coarse_pos = 0.0;
	joint->pos_cmd = 0.0;
	joint->vel_cmd = 0.0;
	joint->acc_cmd = 0.0;
	joint->backlash_corr = 0.0;
	joint->backlash_filt = 0.0;
	joint->backlash_vel = 0.0;
	joint->motor_pos_cmd = 0.0;
	joint->motor_pos_fb = 0.0;
	joint->pos_fb = 0.0;
	joint->ferror = 0.0;
	joint->ferror_limit = joint->min_ferror;
	joint->ferror_high_mark = 0.0;

	/* init internal info */
	cubicInit(&(joint->cubic));
    }

    emcmotStatus->tail = 0;

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: init_comm_buffers() complete\n");
    return 0;
}

/* export_functions() exports the realtime functions that implement
   the motion controller. Thread creation is handled externally by
   the launcher (which loads the threads component); motmod only
   exports its functions so they can be added to threads via addf
   (e.g., `addf motion-command-handler servo-thread` and
   `addf motion-controller servo-thread`).
*/
static int export_functions(void)
{
    double base_period_sec, servo_period_sec;
    int servo_base_ratio;
    int retval;

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: export_functions() starting...\n");

    /* if base_period not specified, assume same as servo_period */
    if (base_period_nsec == 0) {
	base_period_nsec = servo_period_nsec;
    }
    if (traj_period_nsec == 0) {
	traj_period_nsec = servo_period_nsec;
    }
    /* servo period must be greater or equal to base period */
    if (servo_period_nsec < base_period_nsec) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "MOTION: bad servo period %ld nsec\n", servo_period_nsec);
	return -1;
    }
    /* convert desired periods to floating point */
    base_period_sec = base_period_nsec * 0.000000001;
    servo_period_sec = servo_period_nsec * 0.000000001;
    /* calculate period ratios, round to nearest integer */
    servo_base_ratio = (servo_period_sec / base_period_sec) + 0.5;
    /* revise desired periods to be integer multiples of each other */
    servo_period_nsec = base_period_nsec * servo_base_ratio;
    /* export realtime functions that do the real work */
    retval = hal_export_funct("motion-controller", emcmotController, 0	/* arg
	 */ , 1 /* uses_fp */ , 0 /* reentrant */ , mot_comp_id);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "MOTION: failed to export controller function\n");
	return -1;
    }
    retval = hal_export_funct("motion-command-handler", emcmotCommandHandler, 0	/* arg
	 */ , 1 /* uses_fp */ , 0 /* reentrant */ , mot_comp_id);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "MOTION: failed to export command handler function\n");
	return -1;
    }
/*! \todo Another #if 0 */
#if 0
    /*! \todo FIXME - currently the traj planner is called from the controller */
    /* eventually it will be a separate function */
    retval = hal_export_funct("motion-traj-planner", emcmotTrajPlanner, 0	/* arg
	 */ , 1 /* uses_fp */ ,
	0 /* reentrant */ , mot_comp_id);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "MOTION: failed to export traj planner function\n");
	return -1;
    }
#endif

    // if we don't set cycle times based on these guesses, emc doesn't
    // start up right
    setServoCycleTime(servo_period_nsec * 1e-9);
    setTrajCycleTime(traj_period_nsec * 1e-9);

    rtapi_print_msg(RTAPI_MSG_INFO, "MOTION: export_functions() complete\n");
    return 0;
}

void emcmotSetCycleTime(unsigned long nsec )
{
    int servo_mult;
    servo_mult = traj_period_nsec / nsec;
    if(servo_mult < 0) servo_mult = 1;
    setTrajCycleTime(nsec * 1e-9);
    setServoCycleTime(nsec * servo_mult * 1e-9);
}

/* call this when setting the trajectory cycle time */
static int setTrajCycleTime(double secs)
{
    static int t;

    rtapi_print_msg(RTAPI_MSG_INFO,
	"MOTION: setting Traj cycle time to %ld nsecs\n", (long) (secs * 1e9));

    /* make sure it's not zero */
    if (secs <= 0.0) {
	return -1;
    }

    emcmot_config_change();

    /* compute the interpolation rate as nearest integer to traj/servo */
    if(emcmotConfig->servoCycleTime)
        emcmotConfig->interpolationRate =
            (int) (secs / emcmotConfig->servoCycleTime + 0.5);
    else
        emcmotConfig->interpolationRate = 1;

    /* set traj planner */
    motmod_tp_api->set_cycle_time(secs);

    /* set the free planners, cubic interpolation rate and segment time */
    for (t = 0; t < ALL_JOINTS; t++) {
	cubicSetInterpolationRate(&(joints[t].cubic),
	    emcmotConfig->interpolationRate);
    }

    /* copy into status out */
    emcmotConfig->trajCycleTime = secs;

    return 0;
}

/* call this when setting the servo cycle time */
static int setServoCycleTime(double secs)
{
    static int t;

    rtapi_print_msg(RTAPI_MSG_INFO,
	"MOTION: setting Servo cycle time to %ld nsecs\n", (long) (secs * 1e9));

    /* make sure it's not zero */
    if (secs <= 0.0) {
	return -1;
    }

    emcmot_config_change();

    /* compute the interpolation rate as nearest integer to traj/servo */
    emcmotConfig->interpolationRate =
	(int) (emcmotConfig->trajCycleTime / secs + 0.5);

    /* set the cubic interpolation rate and PID cycle time */
    for (t = 0; t < ALL_JOINTS; t++) {
	cubicSetInterpolationRate(&(joints[t].cubic),
	    emcmotConfig->interpolationRate);
	cubicSetSegmentTime(&(joints[t].cubic), secs);
    }

    /* copy into status out */
    emcmotConfig->servoCycleTime = secs;

    return 0;
}
