/*
 * nml_shim.h — C interface to the NML stat/command/error channels.
 *
 * This header provides extern "C" wrappers around the C++ NML API,
 * callable from Go via cgo. The implementation is in nml_shim.cc.
 *
 * Used by the emcgateway gomod to read machine status, send commands,
 * and poll error messages without exposing C++ to cgo.
 */

#ifndef NML_SHIM_H
#define NML_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sizes matching LinuxCNC internal constants */
#define NML_SHIM_MAX_JOINTS   16
#define NML_SHIM_MAX_AXIS     9
#define NML_SHIM_MAX_SPINDLES 8
#define NML_SHIM_ACTIVE_G_CODES 17
#define NML_SHIM_ACTIVE_M_CODES 10
#define NML_SHIM_ACTIVE_SETTINGS 5
#define NML_SHIM_LINELEN 256  /* LINELEN(255) + NUL */

/* RCS status codes (from rcs.hh) */
#define NML_SHIM_RCS_DONE  1
#define NML_SHIM_RCS_EXEC  2
#define NML_SHIM_RCS_ERROR 3

/* ─── Types ─── */

typedef struct {
    double x, y, z;
    double a, b, c;
    double u, v, w;
} nml_position_t;

typedef struct {
    int homed;
    int homing;
    int enabled;
    int fault;
    double min_soft_limit;
    double max_soft_limit;
    int min_hard_limit;
    int max_hard_limit;
    int override_limits;
    double velocity;
    double input;       /* actual position */
    double output;      /* commanded position */
    int limit;          /* bitmask: 1=minHard, 2=maxHard, 4=minSoft, 8=maxSoft */
} nml_joint_info_t;

typedef struct {
    double speed;
    int direction;
    int brake;
    int enabled;
    double override;
    int override_enabled;
    int homed;
    int orient_state;
    int orient_fault;
} nml_spindle_info_t;

typedef struct {
    double velocity;
    double min_position_limit;
    double max_position_limit;
} nml_axis_info_t;

typedef struct {
    /* Task */
    int task_mode;
    int task_state;
    int interp_state;
    int exec_state;
    char file[NML_SHIM_LINELEN];
    char command[NML_SHIM_LINELEN];
    int line;
    int motion_line;
    int current_line;
    int read_line;
    int queued_mdi_commands;
    int optional_stop;
    int block_delete;
    int task_paused;
    int g5x_index;

    /* Motion / trajectory */
    int motion_mode;
    int motion_enabled;
    int in_position;
    int motion_paused;
    double feedrate;
    double rapidrate;
    double max_velocity;
    double velocity;
    double distance_to_go;
    nml_position_t dtg;
    double current_vel;
    int motion_id;
    int motion_line_traj;   /* motion.traj.id mapped to motion_line in traj */
    int motion_type;        /* 0=jog, 1=traverse, 2=feed, 3=arc, 4=toolchange, 5=probing */

    /* Positions */
    nml_position_t position;
    nml_position_t actual_position;
    double joint_actual_position[NML_SHIM_MAX_JOINTS];
    nml_position_t probed_position;
    nml_position_t g5x_offset;
    nml_position_t g92_offset;
    nml_position_t tool_offset;
    double rotation_xy;

    /* Arrays */
    nml_joint_info_t joints[NML_SHIM_MAX_JOINTS];
    nml_spindle_info_t spindle[NML_SHIM_MAX_SPINDLES];
    nml_axis_info_t axis[NML_SHIM_MAX_AXIS];
    int active_gcodes[NML_SHIM_ACTIVE_G_CODES];
    int active_mcodes[NML_SHIM_ACTIVE_M_CODES];
    double active_settings[NML_SHIM_ACTIVE_SETTINGS];

    /* Scalars */
    int kinematics_type;
    int joints_count;
    int num_extrajoints;
    int axis_mask;
    int flood;
    int mist;
    int tool_in_spindle;
    int pocket_prepped;
    double linear_units;
    int homed[NML_SHIM_MAX_JOINTS];
    int limit[NML_SHIM_MAX_JOINTS];

    /* RCS state (emcStatus->status) */
    int state;
    /* echo_serial_number for wait_complete */
    int echo_serial_number;
    /* debug flags (emc_debug bitmask) */
    int debug;
} nml_stat_t;

typedef struct {
    int kind;           /* ErrorKind enum value */
    char text[NML_SHIM_LINELEN];
} nml_error_t;

/* ─── Lifecycle ─── */

/*
 * Open NML channels. nml_file is the path to the NML config file.
 * Returns 0 on success, -1 on failure.
 */
int nml_shim_init(const char *nml_file);

/*
 * Close all NML channels and free resources.
 */
void nml_shim_shutdown(void);

/* ─── Stat ─── */

/*
 * Poll the NML stat channel and copy the current state into out.
 * Returns 0 on success, -1 if no new data or channel invalid.
 */
int nml_shim_poll_stat(nml_stat_t *out);

/* ─── Commands ─── */

/*
 * Send a state command (EMC_TASK_SET_STATE).
 * state: STATE_ESTOP(1), ESTOP_RESET(2), OFF(3), ON(4)
 */
int nml_shim_set_state(int state);

/*
 * Send a mode command (EMC_TASK_SET_MODE).
 * mode: MANUAL(1), AUTO(2), MDI(3)
 */
int nml_shim_set_mode(int mode);

/*
 * Send an auto command (run/pause/resume/step/reverse/forward).
 * cmd: RUN(0), PAUSE(1), RESUME(2), STEP(3), REVERSE(4), FORWARD(5)
 * line: starting line for RUN, ignored for others
 */
int nml_shim_auto_cmd(int cmd, int line);

/*
 * Send an MDI command string.
 */
int nml_shim_mdi(const char *command);

/*
 * Send a jog command.
 * jog_type: STOP(0), CONTINUOUS(1), INCREMENT(2)
 */
int nml_shim_jog(int jog_type, int jjogmode, int axis_or_joint,
                 double velocity, double distance);

/* Send a jog stop. */
int nml_shim_jog_stop(int jjogmode, int axis_or_joint);

/*
 * Send a spindle command.
 * cmd: OFF(0), FORWARD(1), REVERSE(-1), INCREASE(10), DECREASE(11), CONSTANT(12)
 */
int nml_shim_spindle(int cmd, double speed, int spindle_num, int wait);

/* Home a joint (-1 = all). */
int nml_shim_home(int joint);

/* Unhome a joint (-1 = all). */
int nml_shim_unhome(int joint);

/* Override soft limits. */
int nml_shim_override_limits(void);

/* Enable/disable teleop mode. */
int nml_shim_teleop_enable(int enable);

/* Set feed override (0.0 - 1.0+). */
int nml_shim_set_feed_override(double rate);

/* Set spindle speed override. */
int nml_shim_set_spindle_override(double rate, int spindle_num);

/* Set rapid override. */
int nml_shim_set_rapid_override(double rate);

/* Set maximum velocity. */
int nml_shim_set_max_velocity(double velocity);

/* Flood coolant on/off. */
int nml_shim_flood(int on);

/* Mist coolant on/off. */
int nml_shim_mist(int on);

/* Spindle brake engage(1)/release(0). */
int nml_shim_brake(int on, int spindle_num);

/* Abort current operation. */
int nml_shim_abort(void);

/* Synchronize task planner. */
int nml_shim_task_plan_synch(void);

/* Set debug level (EMC_SET_DEBUG). */
int nml_shim_set_debug(int debug);

/* Set optional stop. */
int nml_shim_set_optional_stop(int on);

/* Set block delete. */
int nml_shim_set_block_delete(int on);

/* Reload tool table. */
int nml_shim_load_tool_table(void);

/* Open a program file. */
int nml_shim_program_open(const char *file);

/*
 * Wait for command completion.
 * Returns NML_SHIM_RCS_DONE, NML_SHIM_RCS_ERROR, or -1 on timeout.
 */
int nml_shim_wait_complete(double timeout);

/* ─── Errors ─── */

/*
 * Poll the NML error channel.
 * Writes up to max_errors messages into the errors array.
 * Returns the number of messages read (0 if none).
 */
int nml_shim_poll_errors(nml_error_t *errors, int max_errors);

#ifdef __cplusplus
}
#endif

#endif /* NML_SHIM_H */
