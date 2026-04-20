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

// ─── Stored mot API pointer ────────────────────────────────────────────────

static const mot_callbacks_t *home_mot;

// ─── GMI callback wrappers ─────────────────────────────────────────────────
//
// Each wrapper converts between the GMI signature (int return, *out for
// the actual result) and the native homing.c signature.

static int32_t gmi_home_init(int32_t comp_id, double servo_period,
    int32_t n_joints, int32_t n_extrajoints)
{
    return homing_init(comp_id, servo_period,
                       n_joints, n_extrajoints);
}

static int32_t gmi_home_set_joint_params(int32_t jno, double offset, double home,
    double home_final_vel, double home_search_vel,
    double home_latch_vel, int32_t home_flags,
    int32_t home_sequence, int32_t volatile_home)
{    set_joint_homing_params(jno, offset, home,
                            home_final_vel, home_search_vel,
                            home_latch_vel, home_flags,
                            home_sequence, (bool)volatile_home);
    return 0;
}

static int32_t gmi_home_update_joint_params(int32_t jno, double home_offset,
    double home_home, int32_t home_sequence)
{    update_joint_homing_params(jno, home_offset, home_home, home_sequence);
    return 0;
}

static int32_t gmi_home_read_in_pins(int32_t njoints)
{    read_homing_in_pins(njoints);
    return 0;
}

static int32_t gmi_home_do_homing(void)
{
    return (int32_t)do_homing();
}

static int32_t gmi_home_write_out_pins(int32_t njoints)
{    write_homing_out_pins(njoints);
    return 0;
}

static int32_t gmi_home_do_home_joint(int32_t jno)
{    do_home_joint(jno);
    return 0;
}

static int32_t gmi_home_do_cancel(int32_t jno)
{    do_cancel_homing(jno);
    return 0;
}

static int32_t gmi_home_set_unhomed(int32_t jno, home_motion_state_t motstate)
{    set_unhomed(jno, (motion_state_t)motstate);
    return 0;
}

static int32_t gmi_home_get_allhomed(void)
{
    return (int32_t)get_allhomed();
}

static int32_t gmi_home_get_is_active(void)
{
    return (int32_t)get_homing_is_active();
}

static int32_t gmi_home_get_sequence(int32_t jno)
{
    return get_home_sequence(jno);
}

static int32_t gmi_home_get_homing(int32_t jno)
{
    return (int32_t)get_homing(jno);
}

static int32_t gmi_home_get_homed(int32_t jno)
{
    return (int32_t)get_homed(jno);
}

static int32_t gmi_home_get_index_enable(int32_t jno)
{
    return (int32_t)get_index_enable(jno);
}

static int32_t gmi_home_get_needs_unlock_first(int32_t jno)
{
    return (int32_t)get_home_needs_unlock_first(jno);
}

static int32_t gmi_home_get_is_idle(int32_t jno)
{
    return (int32_t)get_home_is_idle(jno);
}

static int32_t gmi_home_get_is_synchronized(int32_t jno)
{
    return (int32_t)get_home_is_synchronized(jno);
}

static int32_t gmi_home_get_at_index_search_wait(int32_t jno)
{
    return (int32_t)get_homing_at_index_search_wait(jno);
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

    /* Wire mot API into homing.c for joint access. */
    homingSetMotAPI(home_mot);
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
