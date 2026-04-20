// homemod.c — GMI cmod wrapper for the homing subsystem.
//
// Provides the "home" API by wrapping existing homing.c functions behind
// GMI callback pointers.  motmod consumes this API via home_api_get().
//
// License: GPL Version 2

#include <stdint.h>
#include <string.h>
#include "gomc_env.h"
#include "home_api.h"
#include "mot_api.h"
#include "motion.h"
#include "homing.h"

// ─── Stored API pointer ────────────────────────────────────────────────────

static const gomc_api_t *homemod_api;

// ─── Mot API adapter functions ─────────────────────────────────────────────
// homing.c expects legacy function-pointer signatures via homeMotFunctions().

static const mot_callbacks_t *home_mot;

static void adapt_set_rotary_unlock(int jnum, int unlock)
{
    home_mot->set_rotary_unlock(jnum, unlock);
}

static int adapt_get_rotary_is_unlocked(int jnum)
{
    int32_t out;
    home_mot->get_rotary_unlock(jnum, &out);
    return out;
}

// ─── GMI callback wrappers ─────────────────────────────────────────────────
//
// Each wrapper converts between the GMI signature (int return, *out for
// the actual result) and the native homing.c signature.

static int gmi_home_init(
    int32_t comp_id, double servo_period,
    int32_t n_joints, int32_t n_extrajoints,
    uint64_t joints_ptr,
    int32_t *out)
{
    *out = homing_init(comp_id, servo_period,
                       n_joints, n_extrajoints,
                       (emcmot_joint_t *)(uintptr_t)joints_ptr);
    return 0;
}

static int gmi_home_set_joint_params(
    int32_t jno, double offset, double home,
    double home_final_vel, double home_search_vel,
    double home_latch_vel, int32_t home_flags,
    int32_t home_sequence, int32_t volatile_home,
    int32_t *out)
{
    set_joint_homing_params(jno, offset, home,
                            home_final_vel, home_search_vel,
                            home_latch_vel, home_flags,
                            home_sequence, (bool)volatile_home);
    *out = 0;
    return 0;
}

static int gmi_home_update_joint_params(
    int32_t jno, double home_offset,
    double home_home, int32_t home_sequence,
    int32_t *out)
{
    update_joint_homing_params(jno, home_offset, home_home, home_sequence);
    *out = 0;
    return 0;
}

static int gmi_home_read_in_pins(int32_t njoints, int32_t *out)
{
    read_homing_in_pins(njoints);
    *out = 0;
    return 0;
}

static int gmi_home_do_homing(int32_t *out)
{
    *out = (int32_t)do_homing();
    return 0;
}

static int gmi_home_write_out_pins(int32_t njoints, int32_t *out)
{
    write_homing_out_pins(njoints);
    *out = 0;
    return 0;
}

static int gmi_home_do_home_joint(int32_t jno, int32_t *out)
{
    do_home_joint(jno);
    *out = 0;
    return 0;
}

static int gmi_home_do_cancel(int32_t jno, int32_t *out)
{
    do_cancel_homing(jno);
    *out = 0;
    return 0;
}

static int gmi_home_set_unhomed(
    int32_t jno, const home_motion_state_t *motstate,
    int32_t *out)
{
    set_unhomed(jno, (motion_state_t)*motstate);
    *out = 0;
    return 0;
}

static int gmi_home_get_allhomed(int32_t *out)
{
    *out = (int32_t)get_allhomed();
    return 0;
}

static int gmi_home_get_is_active(int32_t *out)
{
    *out = (int32_t)get_homing_is_active();
    return 0;
}

static int gmi_home_get_sequence(int32_t jno, int32_t *out)
{
    *out = get_home_sequence(jno);
    return 0;
}

static int gmi_home_get_homing(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_homing(jno);
    return 0;
}

static int gmi_home_get_homed(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_homed(jno);
    return 0;
}

static int gmi_home_get_index_enable(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_index_enable(jno);
    return 0;
}

static int gmi_home_get_needs_unlock_first(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_home_needs_unlock_first(jno);
    return 0;
}

static int gmi_home_get_is_idle(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_home_is_idle(jno);
    return 0;
}

static int gmi_home_get_is_synchronized(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_home_is_synchronized(jno);
    return 0;
}

static int gmi_home_get_at_index_search_wait(int32_t jno, int32_t *out)
{
    *out = (int32_t)get_homing_at_index_search_wait(jno);
    return 0;
}

// ─── Callbacks table ────────────────────────────────────────────────────────

static const home_callbacks_t homemod_callbacks = GMI_HOME_CALLBACKS;

// ─── cmod lifecycle ─────────────────────────────────────────────────────────

static cmod_t homemod_cmod;

static void homemod_destroy(cmod_t *self) { (void)self; }

static int homemod_init(cmod_t *self)
{
    (void)self;

    /* Look up the mot reverse-callback API registered by motmod. */
    home_mot = mot_api_get(homemod_api, "default");
    if (!home_mot)
        return -1;

    /* Wire legacy homing.c function-pointer statics through mot API adapters. */
    homeMotFunctions(adapt_set_rotary_unlock,
                     adapt_get_rotary_is_unlocked);
    return 0;
}

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out)
{
    (void)argc; (void)argv;

    homemod_api = env->api;

    int rc = home_api_register(env->api, "default", &homemod_callbacks);
    if (rc != 0) {
        gomc_log_errorf(env->log, name,
            "failed to register home API: %d", rc);
        return rc;
    }

    homemod_cmod.Init    = homemod_init;
    homemod_cmod.Start   = NULL;
    homemod_cmod.Destroy = homemod_destroy;
    *out = &homemod_cmod;
    return 0;
}
