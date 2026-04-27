/** halscope_rt — RT sample capture cmod for the HAL oscilloscope.
 *
 * This cmod replaces the legacy scope_rt.c. It is loaded once via HAL
 * config ("load halscope_rt") and exposes the halscope GMI API for
 * UI clients. The scope.sample RT function is exported via
 * hal_export_funct() and added to a HAL thread.
 *
 * Copyright (C) 2003 John Kasunich (original scope_rt.c)
 * Copyright (C) 2026 LinuxCNC contributors (cmod migration)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#include "gomc_env.h"
#include "halscope_api.h"
#include "hal.h"
#include "hal_priv.h"
#include "rtapi.h"
#include "rtapi_string.h"

/* --- Constants --- */

#define MAX_CHANNELS  16
#define DEFAULT_NUM_SAMPLES 16000

/* State enum matching the IDL ScopeState */
typedef enum {
    ST_IDLE = 0,
    ST_INIT,
    ST_PRE_TRIG,
    ST_TRIG_WAIT,
    ST_POST_TRIG,
    ST_DONE,
    ST_RESET
} scope_state_t;

/* Single sample value — union of all HAL types */
typedef union {
    unsigned char  d_u8;
    rtapi_u32      d_u32;
    rtapi_s32      d_s32;
    real_t         d_real;
    ireal_t        d_ireal;
} scope_data_t;

/* Per-channel info */
typedef struct {
    int         enabled;
    char        pin_name[HAL_NAME_LEN + 1];
    hal_type_t  data_type;
    int         data_len;       /* 0, 1, 4, or 8 */
    void       *data_addr;      /* resolved HAL data pointer */
} channel_t;

/* Trigger config */
typedef struct {
    int            channel;     /* 1-based, 0 = none */
    scope_data_t   level;
    int            edge;        /* 0 = falling, 1 = rising */
    int            force;
    int            auto_trig;
} trigger_t;

/* Main halscope instance */
typedef struct {
    cmod_t           base;          /* MUST be first */
    const cmod_env_t *env;
    char             *name;
    int               comp_id;

    /* Capture config */
    char             thread_name[HAL_NAME_LEN + 1];
    int              num_samples;    /* buffer capacity */
    int              rec_len;        /* samples per record */
    int              pre_trig;       /* pre-trigger samples */
    int              mult;           /* sample period multiplier */

    /* Channel config */
    channel_t        channels[MAX_CHANNELS];
    int              sample_len;     /* active channels count */

    /* Trigger */
    trigger_t        trig;

    /* State machine */
    volatile scope_state_t state;
    int              samples;        /* valid sample count */
    int              start;          /* first sample offset (in scope_data_t units) */
    int              curr;           /* current write position */
    int              buf_len;        /* buffer size in scope_data_t units */

    /* RT-only fields */
    int              mult_cntr;
    int              auto_timer;
    int              compare_result; /* for trigger edge detection */

    /* Sample buffer */
    scope_data_t    *buffer;
} halscope_t;

/* ------------------------------------------------------------------ */
/*                    RT SAMPLE FUNCTION                               */
/* ------------------------------------------------------------------ */

static void capture_sample(halscope_t *s)
{
    scope_data_t *dest;
    int n;

    dest = &s->buffer[s->curr];
    for (n = 0; n < MAX_CHANNELS; n++) {
        if (s->channels[n].data_len == 0 || s->channels[n].data_addr == NULL)
            continue;
        switch (s->channels[n].data_len) {
        case 1:
            dest->d_u8 = *((unsigned char *)s->channels[n].data_addr);
            dest++;
            break;
        case 4:
            dest->d_u32 = *((rtapi_u32 *)s->channels[n].data_addr);
            dest++;
            break;
        case 8: {
            ireal_t a, b;
            do {
                a = *((volatile ireal_t *)s->channels[n].data_addr);
                b = *((volatile ireal_t *)s->channels[n].data_addr);
            } while (a != b);
            dest->d_ireal = a;
            dest++;
            break;
        }
        default:
            break;
        }
    }
    s->curr += s->sample_len;
    if ((s->curr + s->sample_len) > s->buf_len) {
        s->curr = 0;
    }
}

static int check_trigger(halscope_t *s)
{
    int prev;
    scope_data_t *value;
    scope_data_t *level;

    if (s->trig.force) {
        s->trig.force = 0;
        return 1;
    }
    if (s->trig.auto_trig) {
        if (++s->auto_timer >= s->rec_len)
            return 1;
    } else {
        s->auto_timer = 0;
    }
    if (s->trig.channel == 0)
        return 0;

    int ch = s->trig.channel - 1;
    if (ch < 0 || ch >= MAX_CHANNELS || s->channels[ch].data_addr == NULL)
        return 0;

    value = (scope_data_t *)s->channels[ch].data_addr;
    level = &s->trig.level;
    prev = s->compare_result;

    switch (s->channels[ch].data_type) {
    case HAL_BIT:
        s->compare_result = value->d_u8;
        break;
    case HAL_FLOAT: {
        ireal_t tmp1 = value->d_ireal;
        ireal_t tmp2 = level->d_ireal;
        if (tmp1 & 0x8000000000000000ull) {
            if (tmp2 & 0x8000000000000000ull) {
                tmp1 ^= 0x8000000000000000ull;
                tmp2 ^= 0x8000000000000000ull;
                s->compare_result = (tmp1 < tmp2);
            } else {
                s->compare_result = 0;
            }
        } else {
            if (tmp2 & 0x8000000000000000ull) {
                s->compare_result = 1;
            } else {
                s->compare_result = (tmp1 > tmp2);
            }
        }
        break;
    }
    case HAL_S32:
        s->compare_result = (value->d_s32 > level->d_s32);
        break;
    case HAL_U32:
        s->compare_result = (value->d_u32 > level->d_u32);
        break;
    default:
        s->compare_result = 0;
        break;
    }

    if (s->trig.edge && s->compare_result && !prev)
        return 1;
    if (!s->trig.edge && !s->compare_result && prev)
        return 1;
    return 0;
}

static void halscope_sample(void *arg, long period)
{
    halscope_t *s = (halscope_t *)arg;
    (void)period;

    if (s->state == ST_RESET) {
        s->curr = 0;
        s->start = s->curr;
        s->samples = 0;
        s->trig.force = 0;
        s->state = ST_IDLE;
    }

    s->mult_cntr++;
    if (s->mult_cntr < s->mult)
        return;
    s->mult_cntr = 0;

    switch (s->state) {
    case ST_IDLE:
        break;

    case ST_INIT:
        s->curr = 0;
        s->start = s->curr;
        s->samples = 0;
        s->trig.force = 0;
        s->auto_timer = 0;
        s->compare_result = 0;
        s->state = ST_PRE_TRIG;
        break;

    case ST_PRE_TRIG:
        capture_sample(s);
        s->samples++;
        if (s->samples >= s->pre_trig) {
            s->state = ST_TRIG_WAIT;
            check_trigger(s); /* preset compare_result */
        }
        break;

    case ST_TRIG_WAIT:
        capture_sample(s);
        s->samples++;
        if (check_trigger(s)) {
            s->state = ST_POST_TRIG;
        } else {
            s->samples--;
            s->start += s->sample_len;
            if ((s->start + s->sample_len) > s->buf_len)
                s->start = 0;
        }
        break;

    case ST_POST_TRIG:
        capture_sample(s);
        s->samples++;
        if (s->samples >= s->rec_len)
            s->state = ST_DONE;
        break;

    case ST_DONE:
        break;

    default:
        s->state = ST_IDLE;
        break;
    }
}

/* ------------------------------------------------------------------ */
/*                  GMI API CALLBACK IMPLEMENTATIONS                   */
/* ------------------------------------------------------------------ */

/* Count active channels to compute sample_len */
static int count_active_channels(halscope_t *s)
{
    int n, count = 0;
    for (n = 0; n < MAX_CHANNELS; n++) {
        if (s->channels[n].enabled && s->channels[n].data_len > 0)
            count++;
    }
    return count;
}

static int32_t halscope_configure(void *ctx, const halscope_capture_config_t *config)
{
    halscope_t *s = (halscope_t *)ctx;

    /* Only configure when idle or done */
    if (s->state != ST_IDLE && s->state != ST_DONE)
        return -EBUSY;

    if (config->thread_name)
        rtapi_strlcpy(s->thread_name, config->thread_name, sizeof(s->thread_name));
    if (config->rec_len > 0 && config->rec_len <= s->num_samples)
        s->rec_len = config->rec_len;
    if (config->sample_period_mult > 0)
        s->mult = config->sample_period_mult;
    if (config->pre_trig >= 0 && config->pre_trig < s->rec_len)
        s->pre_trig = config->pre_trig;

    /* Recalculate buffer geometry */
    s->sample_len = count_active_channels(s);
    if (s->sample_len > 0)
        s->buf_len = (s->num_samples / s->sample_len) * s->sample_len;
    else
        s->buf_len = 0;

    return 0;
}

/* Resolve a HAL pin/signal/param name to a data pointer */
static int resolve_hal_name(const char *name, hal_type_t *type, int *data_len, void **data_addr)
{
    hal_pin_t *pin;
    hal_sig_t *sig;
    hal_param_t *param;

    /* Try pin first */
    pin = halpr_find_pin_by_name(name);
    if (pin != NULL) {
        *type = pin->type;
        /* For pins, the data pointer depends on whether it's linked to a signal */
        if (pin->signal) {
            sig = (hal_sig_t *)SHMPTR(pin->signal);
            *data_addr = SHMPTR(sig->data_ptr);
        } else {
            *data_addr = &pin->dummysig;
        }
        goto set_len;
    }

    /* Try signal */
    sig = halpr_find_sig_by_name(name);
    if (sig != NULL) {
        *type = sig->type;
        *data_addr = SHMPTR(sig->data_ptr);
        goto set_len;
    }

    /* Try parameter */
    param = halpr_find_param_by_name(name);
    if (param != NULL) {
        *type = param->type;
        *data_addr = SHMPTR(param->data_ptr);
        goto set_len;
    }

    return -ENOENT;

set_len:
    switch (*type) {
    case HAL_BIT:   *data_len = 1; break;
    case HAL_S32:
    case HAL_U32:   *data_len = 4; break;
    case HAL_FLOAT: *data_len = 8; break;
    default:        *data_len = 0; break;
    }
    return 0;
}

static int32_t halscope_set_channel(void *ctx, const halscope_channel_config_t *ch)
{
    halscope_t *s = (halscope_t *)ctx;

    if (ch->channel < 0 || ch->channel >= MAX_CHANNELS)
        return -EINVAL;

    channel_t *c = &s->channels[ch->channel];

    hal_type_t type;
    int data_len;
    void *data_addr;
    int ret = resolve_hal_name(ch->pin_name, &type, &data_len, &data_addr);
    if (ret != 0)
        return ret;

    c->enabled = 1;
    rtapi_strlcpy(c->pin_name, ch->pin_name, sizeof(c->pin_name));
    c->data_type = type;
    c->data_len = data_len;
    c->data_addr = data_addr;

    /* Recalculate sample_len */
    s->sample_len = count_active_channels(s);
    if (s->sample_len > 0)
        s->buf_len = (s->num_samples / s->sample_len) * s->sample_len;

    return 0;
}

static int32_t halscope_clear_channel(void *ctx, int32_t channel)
{
    halscope_t *s = (halscope_t *)ctx;

    if (channel < 0 || channel >= MAX_CHANNELS)
        return -EINVAL;

    memset(&s->channels[channel], 0, sizeof(channel_t));

    s->sample_len = count_active_channels(s);
    if (s->sample_len > 0)
        s->buf_len = (s->num_samples / s->sample_len) * s->sample_len;
    else
        s->buf_len = 0;

    return 0;
}

static int32_t halscope_set_trigger(void *ctx, const halscope_trigger_config_t *trig)
{
    halscope_t *s = (halscope_t *)ctx;

    if (trig->channel < 0 || trig->channel > MAX_CHANNELS)
        return -EINVAL;

    s->trig.channel = trig->channel;

    /* Store trigger level as ireal_t for the IEEE-754 comparison hack */
    {
        double d = trig->level;
        ireal_t *ip = (ireal_t *)&d;
        s->trig.level.d_ireal = *ip;
    }
    s->trig.edge = (trig->edge == 1) ? 1 : 0;
    s->trig.force = trig->force ? 1 : 0;
    s->trig.auto_trig = trig->auto_trig ? 1 : 0;

    return 0;
}

static int32_t halscope_arm(void *ctx)
{
    halscope_t *s = (halscope_t *)ctx;

    if (s->state != ST_IDLE && s->state != ST_DONE)
        return -EBUSY;
    if (s->sample_len == 0)
        return -EINVAL;
    if (s->rec_len == 0)
        return -EINVAL;

    /* Transition to INIT — the RT function picks it up */
    s->state = ST_INIT;
    return 0;
}

static int32_t halscope_reset(void *ctx)
{
    halscope_t *s = (halscope_t *)ctx;

    /* Setting RESET tells the RT function to clean up */
    s->state = ST_RESET;
    return 0;
}

static halscope_scope_status_t halscope_get_status(void *ctx)
{
    halscope_t *s = (halscope_t *)ctx;
    halscope_scope_status_t st;

    memset(&st, 0, sizeof(st));
    st.state = (halscope_scope_state_t)s->state;
    st.samples = s->samples;
    st.rec_len = s->rec_len;
    st.pre_trig = s->pre_trig;
    st.sample_len = s->sample_len;

    /* Build channel info list from active channels */
    int n_active = 0;
    for (int n = 0; n < MAX_CHANNELS; n++) {
        if (s->channels[n].enabled)
            n_active++;
    }

    if (n_active > 0) {
        halscope_channel_info_t *info = calloc(n_active, sizeof(*info));
        if (info) {
            int idx = 0;
            for (int n = 0; n < MAX_CHANNELS; n++) {
                if (!s->channels[n].enabled)
                    continue;
                info[idx].channel = n;
                info[idx].pin_name = s->channels[n].pin_name;
                info[idx].data_type = (halscope_hal_type_t)s->channels[n].data_type;
                info[idx].enabled = true;
                idx++;
            }
            st.channels = info;
            st.channels_len = n_active;
        }
    } else {
        st.channels = NULL;
        st.channels_len = 0;
    }

    return st;
}

static halscope_list_pins_result_t halscope_list_pins(void *ctx, const char *pattern)
{
    halscope_list_pins_result_t result = { .data = NULL, .len = 0 };
    (void)ctx;

    const char *match = (pattern && pattern[0]) ? pattern : "*";

    /* First pass: count matching names */
    int count = 0;
    int next;
    hal_pin_t *pin;
    hal_sig_t *sig;
    hal_param_t *param;

    rtapi_mutex_get(&hal_data->mutex);

    next = hal_data->pin_list_ptr;
    while (next != 0) {
        pin = SHMPTR(next);
        if (fnmatch(match, pin->name, 0) == 0)
            count++;
        next = pin->next_ptr;
    }
    next = hal_data->sig_list_ptr;
    while (next != 0) {
        sig = SHMPTR(next);
        if (fnmatch(match, sig->name, 0) == 0)
            count++;
        next = sig->next_ptr;
    }
    next = hal_data->param_list_ptr;
    while (next != 0) {
        param = SHMPTR(next);
        if (fnmatch(match, param->name, 0) == 0)
            count++;
        next = param->next_ptr;
    }

    if (count == 0) {
        rtapi_mutex_give(&hal_data->mutex);
        return result;
    }

    /* Allocate array of C strings */
    const char **names = malloc(count * sizeof(const char *));
    if (!names) {
        rtapi_mutex_give(&hal_data->mutex);
        return result;
    }

    /* Second pass: collect names (point into HAL shmem — valid while mutex held
       and as long as components aren't unloaded; dispatch copies to Go strings
       before we return) */
    int idx = 0;
    next = hal_data->pin_list_ptr;
    while (next != 0 && idx < count) {
        pin = SHMPTR(next);
        if (fnmatch(match, pin->name, 0) == 0)
            names[idx++] = pin->name;
        next = pin->next_ptr;
    }
    next = hal_data->sig_list_ptr;
    while (next != 0 && idx < count) {
        sig = SHMPTR(next);
        if (fnmatch(match, sig->name, 0) == 0)
            names[idx++] = sig->name;
        next = sig->next_ptr;
    }
    next = hal_data->param_list_ptr;
    while (next != 0 && idx < count) {
        param = SHMPTR(next);
        if (fnmatch(match, param->name, 0) == 0)
            names[idx++] = param->name;
        next = param->next_ptr;
    }

    rtapi_mutex_give(&hal_data->mutex);

    result.data = names;
    result.len = idx;
    return result;
}

static halscope_scope_status_t halscope_watch_state(void *ctx)
{
    return halscope_get_status(ctx);
}

static halscope_watch_samples_result_t halscope_watch_samples(void *ctx)
{
    halscope_t *s = (halscope_t *)ctx;
    halscope_watch_samples_result_t result = { .data = NULL, .len = 0 };

    /* Only return samples when capture is complete */
    if (s->state != ST_DONE || s->buffer == NULL || s->samples == 0)
        return result;

    /* Binary layout:
     *   [4 bytes: sample_count (uint32 LE)]
     *   [4 bytes: sample_len   (uint32 LE)]
     *   [4 bytes: start_offset (uint32 LE)]
     *   [4 bytes: reserved     (uint32 LE)]
     *   [sample_count × sample_len × 8 bytes: scope_data_t values]
     *
     * The caller (Go dispatch) takes ownership of this allocation.
     */
    int header_size = 16;
    int data_size = s->samples * s->sample_len * sizeof(scope_data_t);
    uint8_t *buf = malloc(header_size + data_size);
    if (!buf)
        return result;

    /* Header */
    uint32_t *hdr = (uint32_t *)buf;
    hdr[0] = (uint32_t)s->samples;
    hdr[1] = (uint32_t)s->sample_len;
    hdr[2] = (uint32_t)s->start;
    hdr[3] = 0;

    /* Copy sample data from ring buffer, handling wrap-around */
    uint8_t *dst = buf + header_size;
    int pos = s->start;
    int remaining = s->samples * s->sample_len;
    int copied = 0;

    while (copied < remaining) {
        int chunk = remaining - copied;
        int avail = s->buf_len - pos;
        if (chunk > avail)
            chunk = avail;
        memcpy(dst + copied * sizeof(scope_data_t),
               &s->buffer[pos],
               chunk * sizeof(scope_data_t));
        copied += chunk;
        pos += chunk;
        if (pos >= s->buf_len)
            pos = 0;
    }

    result.data = buf;
    result.len = header_size + data_size;
    return result;
}

/* ------------------------------------------------------------------ */
/*                        CMOD LIFECYCLE                               */
/* ------------------------------------------------------------------ */

static int halscope_Start(struct cmod *self)
{
    (void)self;
    return 0;
}

static void halscope_Stop(struct cmod *self)
{
    (void)self;
}

static void halscope_Destroy(struct cmod *self)
{
    halscope_t *s = (halscope_t *)self;
    if (s->buffer) {
        free(s->buffer);
        s->buffer = NULL;
    }
    if (s->comp_id > 0 && s->env->hal)
        s->env->hal->exit(s->env->hal->ctx, s->comp_id);
    free(s->name);
    free(s);
}

/* Callbacks struct */
static halscope_callbacks_t halscope_cb = {
    .ctx            = NULL,
    .configure      = halscope_configure,
    .set_channel    = halscope_set_channel,
    .clear_channel  = halscope_clear_channel,
    .set_trigger    = halscope_set_trigger,
    .arm            = halscope_arm,
    .reset          = halscope_reset,
    .get_status     = halscope_get_status,
    .list_pins      = halscope_list_pins,
    .watch_state    = halscope_watch_state,
    .watch_samples  = halscope_watch_samples,
};

/* ------------------------------------------------------------------ */
/*                        CONSTRUCTOR                                  */
/* ------------------------------------------------------------------ */

int New(const cmod_env_t *env, const char *name,
        int argc, const char **argv, cmod_t **out)
{
    halscope_t *s;
    int n, retval;
    int num_samples = DEFAULT_NUM_SAMPLES;

    /* Parse arguments */
    for (n = 0; n < argc; n++) {
        if (strncmp(argv[n], "num_samples=", 12) == 0) {
            num_samples = atoi(argv[n] + 12);
            if (num_samples < 1000)
                num_samples = 1000;
            if (num_samples > 1000000)
                num_samples = 1000000;
        }
    }

    /* Allocate instance */
    s = calloc(1, sizeof(halscope_t));
    if (!s) {
        gomc_log_errorf(env->log, "halscope", "out of memory");
        return -ENOMEM;
    }

    s->base.Start   = halscope_Start;
    s->base.Stop    = halscope_Stop;
    s->base.Destroy = halscope_Destroy;
    s->env  = env;
    s->name = strdup(name);
    s->num_samples = num_samples;
    s->mult = 1;
    s->rec_len = num_samples;
    s->pre_trig = num_samples / 2;
    s->state = ST_IDLE;

    /* Allocate sample buffer */
    s->buffer = calloc(num_samples, sizeof(scope_data_t));
    if (!s->buffer) {
        gomc_log_errorf(env->log, "halscope",
                        "failed to allocate sample buffer (%d samples)",
                        num_samples);
        free(s->name);
        free(s);
        return -ENOMEM;
    }

    /* Init HAL component */
    if (env->hal == NULL) {
        gomc_log_errorf(env->log, "halscope", "HAL environment required");
        free(s->buffer);
        free(s->name);
        free(s);
        return -EINVAL;
    }

    s->comp_id = env->hal->init(env->hal->ctx, name, env->dl_handle,
                                GOMC_HAL_COMP_REALTIME);
    if (s->comp_id < 0) {
        int err = s->comp_id;
        gomc_log_errorf(env->log, "halscope", "hal_init failed: %d", err);
        free(s->buffer);
        free(s->name);
        free(s);
        return err;
    }

    /* Export the RT sample function */
    retval = env->hal->export_funct(env->hal->ctx, "halscope.sample",
                                    halscope_sample, s,
                                    1 /* uses_fp */, 0 /* reentrant */,
                                    s->comp_id);
    if (retval != 0) {
        gomc_log_errorf(env->log, "halscope",
                        "failed to export sample function: %d", retval);
        env->hal->exit(env->hal->ctx, s->comp_id);
        free(s->buffer);
        free(s->name);
        free(s);
        return retval;
    }

    env->hal->ready(env->hal->ctx, s->comp_id);

    /* Register GMI API */
    if (env->api != NULL) {
        halscope_cb.ctx = s;
        retval = halscope_api_register(env->api, name, &halscope_cb);
        if (retval != 0) {
            gomc_log_errorf(env->log, "halscope",
                            "API register failed: %d", retval);
            /* Non-fatal — HAL function still works, just no REST/WS */
        }
    }

    gomc_log_infof(env->log, "halscope", "loaded, %d samples, comp_id=%d",
                   num_samples, s->comp_id);

    *out = &s->base;
    return 0;
}
