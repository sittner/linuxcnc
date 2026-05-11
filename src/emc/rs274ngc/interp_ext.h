/********************************************************************
* Description: interp_ext.h
*   Extension API for the RS274NGC interpreter.
*   Allows cmod/gomod handlers to register remap prologs/epilogs
*   and O-word subroutine handlers, replacing the removed Python
*   dispatch.
*
* License: GPL Version 2
********************************************************************/
#ifndef INTERP_EXT_H
#define INTERP_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return values (match INTERP_OK / INTERP_ERROR / INTERP_EXECUTE_FINISH) */
#define INTERP_EXT_OK              0
#define INTERP_EXT_ERROR          -1
#define INTERP_EXT_EXECUTE_FINISH  3

/* Context passed to handler callbacks at call time */
typedef struct interp_ext_ctx {
    void *interp;           /* opaque interpreter handle */

    /* --- Named parameters --- */
    double (*get_param)(void *interp, const char *name);
    int    (*set_param)(void *interp, const char *name, double val);

    /* --- Tool table --- */
    int (*find_tool_pocket)(void *interp, int tool_number);

    /* --- Error reporting --- */
    void (*set_error)(void *interp, const char *msg);

    /* --- Current remap block word access --- */
    int    (*block_t_flag)(void *interp);
    int    (*block_t_number)(void *interp);
    int    (*block_s_flag)(void *interp);
    double (*block_s_number)(void *interp);
    int    (*block_f_flag)(void *interp);
    double (*block_f_number)(void *interp);
    int    (*block_q_flag)(void *interp);
    double (*block_q_number)(void *interp);
    int    (*block_builtin_used)(void *interp);
    int    (*block_motion_code)(void *interp);   /* g_modes[1] */

    /* --- Setup state read --- */
    int    (*get_selected_tool)(void *interp);
    int    (*get_selected_pocket)(void *interp);
    int    (*get_current_tool)(void *interp);
    int    (*get_current_pocket)(void *interp);
    int    (*get_cutter_comp_side)(void *interp);
    int    (*get_value_returned)(void *interp);
    double (*get_return_value)(void *interp);
    double (*get_feed_rate)(void *interp);
    int    (*get_feed_mode)(void *interp);
    double (*get_speed)(void *interp, int spindle);
    int    (*get_motion_mode)(void *interp);
    int    (*get_plane)(void *interp);

    /* --- Setup state write --- */
    void (*set_selected_tool)(void *interp, int tool);
    void (*set_selected_pocket)(void *interp, int pocket);
    void (*set_current_tool)(void *interp, int tool);
    void (*set_current_pocket)(void *interp, int pocket);
    void (*set_speed_value)(void *interp, int spindle, double speed);
    void (*set_feed_rate_value)(void *interp, double feed);
    void (*set_motion_mode)(void *interp, int mode);
    void (*set_toolchange_flag)(void *interp, int flag);
    void (*call_set_tool_parameters)(void *interp);

    /* --- Canon calls (go through the interpreter's canon interface) --- */
    void (*canon_select_tool)(void *interp, int tool);
    void (*canon_change_tool)(void *interp, int pocket);
    void (*canon_change_tool_number)(void *interp, int pocket);
    void (*canon_enqueue_set_spindle_speed)(void *interp, int spindle, double speed);
    void (*canon_enqueue_set_feed_rate)(void *interp, double rate);

    /* Resume phase counter (0 = first call, incremented after each EXECUTE_FINISH) */
    int phase;

    /* Per-registration user data (passed back from register call) */
    void *user;
} interp_ext_ctx_t;

/* --- Handler callback typedefs --- */

/* O-word sub: O<name> call [#1] [#2] ...
 * args/n_args: positional arguments passed to the sub
 * retval: set by handler if it wants to return a value */
typedef int (*interp_oword_fn)(interp_ext_ctx_t *ctx,
                               const char *name,
                               const double *args, int n_args,
                               double *retval);

/* Remap prolog: validate/transform args before body executes */
typedef int (*interp_remap_prolog_fn)(interp_ext_ctx_t *ctx,
                                      const char *name);

/* Remap epilog: commit results after NGC body returns */
typedef int (*interp_remap_epilog_fn)(interp_ext_ctx_t *ctx,
                                      const char *name);

/* --- C-linkage registration API (exported from librs274.so) --- */

int interp_ext_register_oword(void *interp, const char *name,
                              interp_oword_fn fn, void *user);

int interp_ext_register_remap_prolog(void *interp, const char *name,
                                     interp_remap_prolog_fn fn, void *user);

int interp_ext_register_remap_epilog(void *interp, const char *name,
                                     interp_remap_epilog_fn fn, void *user);

/* Query whether a handler is registered (used by interpreter dispatch) */
int interp_ext_has_oword(void *interp, const char *name);
int interp_ext_has_remap_handler(void *interp, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* INTERP_EXT_H */
