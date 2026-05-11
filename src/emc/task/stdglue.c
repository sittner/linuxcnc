/********************************************************************
* Description: stdglue.c
*   Standard remap prolog/epilog handlers for LinuxCNC.
*   This cmod registers the same handlers that stdglue.py provided,
*   ported from Python to C.
*
*   Covers: T (prepare), M6 (change), M61 (settool),
*           S (setspeed), F (setfeed), and cycle prolog/epilog.
*
* License: GPL Version 2
********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gomc/pkg/cmodule/gomc_env.h"
#include "interp_ext.h"
#include "interp_ext_api.h"

/* TOLERANCE_EQUAL from the interpreter (used for float comparisons) */
#define TOLERANCE_EQUAL 1e-6

/* Feed modes matching the interpreter enum */
#define INVERSE_TIME 1

/* ================================================================
 * T — prepare_prolog / prepare_epilog
 * REMAP=T prolog=prepare_prolog ngc=prepare epilog=prepare_epilog
 * ================================================================ */

static int prepare_prolog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->block_t_flag(ctx->interp)) {
        ctx->set_error(ctx->interp, "T requires a tool number");
        return INTERP_EXT_ERROR;
    }
    int tool = ctx->block_t_number(ctx->interp);
    int pocket;
    if (tool) {
        pocket = ctx->find_tool_pocket(ctx->interp, tool);
        if (pocket < 0) {
            char msg[80];
            snprintf(msg, sizeof(msg), "T%d: pocket not found", tool);
            ctx->set_error(ctx->interp, msg);
            return INTERP_EXT_ERROR;
        }
    } else {
        pocket = -1; /* T0 = tool unload */
    }
    ctx->set_param(ctx->interp, "tool", (double)tool);
    ctx->set_param(ctx->interp, "pocket", (double)pocket);
    return INTERP_EXT_OK;
}

static int prepare_epilog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->get_value_returned(ctx->interp)) {
        ctx->set_error(ctx->interp,
            "T: remap procedure did not return a value");
        return INTERP_EXT_ERROR;
    }
    if (ctx->block_builtin_used(ctx->interp))
        return INTERP_EXT_OK;

    double rv = ctx->get_return_value(ctx->interp);
    if (rv > 0.0) {
        int tool = (int)ctx->get_param(ctx->interp, "tool");
        int pocket = (int)ctx->get_param(ctx->interp, "pocket");
        ctx->set_selected_tool(ctx->interp, tool);
        ctx->set_selected_pocket(ctx->interp, pocket);
        ctx->canon_select_tool(ctx->interp, tool);
        return INTERP_EXT_OK;
    } else {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "T%d: aborted (return code %.1f)",
                 (int)ctx->get_param(ctx->interp, "tool"), rv);
        ctx->set_error(ctx->interp, msg);
        return INTERP_EXT_ERROR;
    }
}

/* ================================================================
 * M6 — change_prolog / change_epilog
 * REMAP=M6 modalgroup=6 prolog=change_prolog ngc=change epilog=change_epilog
 * ================================================================ */

static int change_prolog(interp_ext_ctx_t *ctx, const char *name)
{
    /* iocontrol-v2 fault check */
    double p5600 = ctx->get_param(ctx->interp, "5600");
    if (p5600 > 0.0) {
        double p5601 = ctx->get_param(ctx->interp, "5601");
        if (p5601 < 0.0) {
            char msg[80];
            snprintf(msg, sizeof(msg),
                     "Toolchanger hard fault %d", (int)p5601);
            ctx->set_error(ctx->interp, msg);
            return INTERP_EXT_ERROR;
        }
    }

    if (ctx->get_selected_pocket(ctx->interp) < 0) {
        ctx->set_error(ctx->interp, "M6: no tool prepared");
        return INTERP_EXT_ERROR;
    }
    if (ctx->get_cutter_comp_side(ctx->interp)) {
        ctx->set_error(ctx->interp,
            "Cannot change tools with cutter radius compensation on");
        return INTERP_EXT_ERROR;
    }
    ctx->set_param(ctx->interp, "tool_in_spindle",
                   (double)ctx->get_current_tool(ctx->interp));
    ctx->set_param(ctx->interp, "selected_tool",
                   (double)ctx->get_selected_tool(ctx->interp));
    ctx->set_param(ctx->interp, "current_pocket",
                   (double)ctx->get_current_pocket(ctx->interp));
    ctx->set_param(ctx->interp, "selected_pocket",
                   (double)ctx->get_selected_pocket(ctx->interp));
    return INTERP_EXT_OK;
}

static int change_epilog(interp_ext_ctx_t *ctx, const char *name)
{
    /* Phase 0: first call after NGC body returns */
    if (ctx->phase == 0) {
        if (!ctx->get_value_returned(ctx->interp)) {
            ctx->set_error(ctx->interp,
                "M6: remap procedure did not return a value");
            return INTERP_EXT_ERROR;
        }
        /* iocontrol-v2 fault check */
        double p5600 = ctx->get_param(ctx->interp, "5600");
        if (p5600 > 0.0) {
            double p5601 = ctx->get_param(ctx->interp, "5601");
            if (p5601 < 0.0) {
                char msg[80];
                snprintf(msg, sizeof(msg),
                         "Toolchanger hard fault %d", (int)p5601);
                ctx->set_error(ctx->interp, msg);
                return INTERP_EXT_ERROR;
            }
        }
        if (ctx->block_builtin_used(ctx->interp))
            return INTERP_EXT_OK;

        double rv = ctx->get_return_value(ctx->interp);
        if (rv > 0.0) {
            int pocket = (int)ctx->get_param(ctx->interp, "selected_pocket");
            ctx->set_selected_pocket(ctx->interp, pocket);
            ctx->canon_change_tool(ctx->interp, pocket);
            ctx->set_current_pocket(ctx->interp, pocket);
            ctx->set_selected_pocket(ctx->interp, -1);
            ctx->set_selected_tool(ctx->interp, -1);
            ctx->call_set_tool_parameters(ctx->interp);
            ctx->set_toolchange_flag(ctx->interp, 1);
            return INTERP_EXT_EXECUTE_FINISH; /* pause, flush motion */
        } else {
            /* yield to print messages, then error */
            return INTERP_EXT_EXECUTE_FINISH;
        }
    }

    /* Phase 1: after EXECUTE_FINISH */
    double rv = ctx->get_return_value(ctx->interp);
    if (rv <= 0.0) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "M6 aborted (return code %.1f)", rv);
        ctx->set_error(ctx->interp, msg);
        return INTERP_EXT_ERROR;
    }
    return INTERP_EXT_OK;
}

/* ================================================================
 * M61 — settool_prolog / settool_epilog
 * REMAP=M61 modalgroup=6 prolog=settool_prolog ngc=settool epilog=settool_epilog
 * ================================================================ */

static int settool_prolog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->block_q_flag(ctx->interp)) {
        ctx->set_error(ctx->interp, "M61 requires a Q parameter");
        return INTERP_EXT_ERROR;
    }
    int tool = (int)ctx->block_q_number(ctx->interp);
    if (tool < 0) {
        ctx->set_error(ctx->interp, "M61: Q value < 0");
        return INTERP_EXT_ERROR;
    }
    int pocket = ctx->find_tool_pocket(ctx->interp, tool);
    if (pocket < 0) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "M61 failed: requested tool %d not in table", tool);
        ctx->set_error(ctx->interp, msg);
        return INTERP_EXT_ERROR;
    }
    ctx->set_param(ctx->interp, "tool", (double)tool);
    ctx->set_param(ctx->interp, "pocket", (double)pocket);
    return INTERP_EXT_OK;
}

static int settool_epilog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->get_value_returned(ctx->interp)) {
        ctx->set_error(ctx->interp,
            "M61: remap procedure did not return a value");
        return INTERP_EXT_ERROR;
    }
    if (ctx->block_builtin_used(ctx->interp))
        return INTERP_EXT_OK;

    double rv = ctx->get_return_value(ctx->interp);
    if (rv > 0.0) {
        int tool = (int)ctx->get_param(ctx->interp, "tool");
        int pocket = (int)ctx->get_param(ctx->interp, "pocket");
        ctx->set_current_tool(ctx->interp, tool);
        ctx->set_current_pocket(ctx->interp, pocket);
        ctx->canon_change_tool_number(ctx->interp, pocket);
        ctx->set_toolchange_flag(ctx->interp, 1);
        ctx->call_set_tool_parameters(ctx->interp);
        return INTERP_EXT_OK;
    } else {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "M61 aborted (return code %.1f)", rv);
        ctx->set_error(ctx->interp, msg);
        return INTERP_EXT_ERROR;
    }
}

/* ================================================================
 * S — setspeed_prolog / setspeed_epilog
 * REMAP=S prolog=setspeed_prolog ngc=setspeed epilog=setspeed_epilog
 * ================================================================ */

static int setspeed_prolog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->block_s_flag(ctx->interp)) {
        ctx->set_error(ctx->interp, "S requires a value");
        return INTERP_EXT_ERROR;
    }
    ctx->set_param(ctx->interp, "speed",
                   ctx->block_s_number(ctx->interp));
    return INTERP_EXT_OK;
}

static int setspeed_epilog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->get_value_returned(ctx->interp)) {
        ctx->set_error(ctx->interp,
            "S: remap procedure did not return a value");
        return INTERP_EXT_ERROR;
    }
    double rv = ctx->get_return_value(ctx->interp);
    if (rv < -TOLERANCE_EQUAL) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "S: remap procedure returned %f", rv);
        ctx->set_error(ctx->interp, msg);
        return INTERP_EXT_ERROR;
    }
    if (!ctx->block_builtin_used(ctx->interp)) {
        double speed = ctx->get_param(ctx->interp, "speed");
        ctx->set_speed_value(ctx->interp, 0, speed);
        ctx->canon_enqueue_set_spindle_speed(ctx->interp, 0, speed);
    }
    return INTERP_EXT_OK;
}

/* ================================================================
 * F — setfeed_prolog / setfeed_epilog
 * REMAP=F prolog=setfeed_prolog ngc=setfeed epilog=setfeed_epilog
 * ================================================================ */

static int setfeed_prolog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->block_f_flag(ctx->interp)) {
        ctx->set_error(ctx->interp, "F requires a value");
        return INTERP_EXT_ERROR;
    }
    ctx->set_param(ctx->interp, "feed",
                   ctx->block_f_number(ctx->interp));
    return INTERP_EXT_OK;
}

static int setfeed_epilog(interp_ext_ctx_t *ctx, const char *name)
{
    if (!ctx->get_value_returned(ctx->interp)) {
        ctx->set_error(ctx->interp,
            "F: remap procedure did not return a value");
        return INTERP_EXT_ERROR;
    }
    if (!ctx->block_builtin_used(ctx->interp)) {
        double feed = ctx->get_param(ctx->interp, "feed");
        ctx->set_feed_rate_value(ctx->interp, feed);
        ctx->canon_enqueue_set_feed_rate(ctx->interp, feed);
    }
    return INTERP_EXT_OK;
}

/* ================================================================
 * Cycle — cycle_prolog / cycle_epilog
 * Generic code-independent support for oword sub cycles.
 * REMAP=G84.3 modalgroup=1 argspec=xyzqp prolog=cycle_prolog ngc=g843 epilog=cycle_epilog
 * ================================================================ */

static int cycle_epilog(interp_ext_ctx_t *ctx, const char *name)
{
    /* Retain the current motion mode so next line keeps it */
    int motion = ctx->block_motion_code(ctx->interp);
    ctx->set_motion_mode(ctx->interp, motion);
    return INTERP_EXT_OK;
}

/* ================================================================
 * cmod lifecycle — lookup API in Start(), register handlers
 * ================================================================ */

typedef struct {
    cmod_t base;
    const cmod_env_t *env;
} stdglue_module;

static int stdglue_start(cmod_t *self)
{
    stdglue_module *m = (stdglue_module *)self->priv;

    /* Look up the interp_ext API registered by milltask in its Start() */
    if (!m->env->api) {
        fprintf(stderr, "stdglue: no API registry available\n");
        return -1;
    }
    const interp_ext_api_t *ext = interp_ext_api_get(m->env->api, "milltask");
    if (!ext) {
        fprintf(stderr, "stdglue: interp_ext API not found "
                "(milltask must be started first)\n");
        return -1;
    }

    /* Register all prolog/epilog handlers */
    ext->register_remap_prolog(ext->ctx, "prepare_prolog", prepare_prolog, NULL);
    ext->register_remap_epilog(ext->ctx, "prepare_epilog", prepare_epilog, NULL);
    ext->register_remap_prolog(ext->ctx, "change_prolog", change_prolog, NULL);
    ext->register_remap_epilog(ext->ctx, "change_epilog", change_epilog, NULL);
    ext->register_remap_prolog(ext->ctx, "settool_prolog", settool_prolog, NULL);
    ext->register_remap_epilog(ext->ctx, "settool_epilog", settool_epilog, NULL);
    ext->register_remap_prolog(ext->ctx, "setspeed_prolog", setspeed_prolog, NULL);
    ext->register_remap_epilog(ext->ctx, "setspeed_epilog", setspeed_epilog, NULL);
    ext->register_remap_prolog(ext->ctx, "setfeed_prolog", setfeed_prolog, NULL);
    ext->register_remap_epilog(ext->ctx, "setfeed_epilog", setfeed_epilog, NULL);
    ext->register_remap_epilog(ext->ctx, "cycle_epilog", cycle_epilog, NULL);

    return 0;
}

static void stdglue_destroy(cmod_t *self)
{
    free(self->priv);
}

int New(const cmod_env_t *env, const char *name,
       int argc, const char **argv, cmod_t **out)
{
    stdglue_module *m = calloc(1, sizeof(*m));
    if (!m)
        return -1;
    m->env = env;

    m->base.Init    = NULL;
    m->base.Start   = stdglue_start;
    m->base.Stop    = NULL;
    m->base.Destroy = stdglue_destroy;
    m->base.priv    = m;

    *out = &m->base;
    return 0;
}
