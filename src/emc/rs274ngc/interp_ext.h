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

    /* Read/write interpreter named parameters (#<name>) and numbered (#1..#5399) */
    double (*get_param)(void *interp, const char *name);
    int    (*set_param)(void *interp, const char *name, double val);

    /* Tool table lookup: returns pocket for tool number, or -1 */
    int (*find_tool_pocket)(void *interp, int tool_number);

    /* Set interpreter error message (printf-style) */
    void (*set_error)(void *interp, const char *msg);

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
