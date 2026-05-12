/*
 * nml_shim.h — C interface to the NML stat and error channels.
 *
 * This header provides extern "C" wrappers around the C++ NML API,
 * callable from Go via cgo. The implementation is in nml_shim.cc.
 *
 * Used by the emcgateway gomod to read machine status and poll error
 * messages without exposing C++ to cgo.
 *
 * Command functions have been removed — commands now go through the
 * emccmd GMI API (emccmd_handlers.cc / emccmd_slot).
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
