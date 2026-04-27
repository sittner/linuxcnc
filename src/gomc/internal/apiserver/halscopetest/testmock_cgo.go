// This file provides mock C callbacks for testing the halscope dispatch layer.
// Since cgo cannot be used directly in _test.go files, the C code lives here
// and is called from halscope_dispatch_test.go.

package halscopetest

/*
#cgo CFLAGS: -I${SRCDIR}/../../../generated/gmi/halscope
#define HALSCOPE_API_CGO
#include "halscope_api.h"
#include <stdlib.h>
#include <string.h>

// --- Mock state ---

static int mock_state = 0;       // 0=IDLE, 1=ARMED, 2=CAPTURING, 3=DONE
static int mock_samples = 0;
static int mock_channel = -1;
static char mock_pin_name[64] = {0};
static int mock_armed = 0;
static char mock_thread_name[64] = {0};
static int mock_rec_len = 4000;
static int mock_pre_trig = 2000;

// --- Mock callbacks ---

static int32_t mock_configure(void *ctx, const halscope_capture_config_t *config) {
    (void)ctx;
    if (config->thread_name)
        strncpy(mock_thread_name, config->thread_name, sizeof(mock_thread_name)-1);
    mock_rec_len = config->rec_len;
    mock_pre_trig = config->pre_trig;
    return 0;
}

static int32_t mock_set_channel(void *ctx, const halscope_channel_config_t *ch) {
    (void)ctx;
    mock_channel = ch->channel;
    if (ch->pin_name)
        strncpy(mock_pin_name, ch->pin_name, sizeof(mock_pin_name)-1);
    return 0;
}

static int32_t mock_clear_channel(void *ctx, int32_t channel) {
    (void)ctx;
    if (channel == mock_channel) {
        mock_channel = -1;
        mock_pin_name[0] = 0;
    }
    return 0;
}

static int32_t mock_set_trigger(void *ctx, const halscope_trigger_config_t *trig) {
    (void)ctx;
    (void)trig;
    return 0;
}

static int32_t mock_arm(void *ctx) {
    (void)ctx;
    mock_state = 1;
    mock_armed = 1;
    return 0;
}

static int32_t mock_reset(void *ctx) {
    (void)ctx;
    mock_state = 0;
    mock_samples = 0;
    mock_armed = 0;
    return 0;
}

static halscope_scope_status_t mock_get_status(void *ctx) {
    (void)ctx;
    halscope_scope_status_t st;
    memset(&st, 0, sizeof(st));
    st.state = (halscope_scope_state_t)mock_state;
    st.samples = mock_samples;
    st.rec_len = mock_rec_len;
    st.pre_trig = mock_pre_trig;
    st.sample_len = (mock_channel >= 0) ? 1 : 0;

    if (mock_channel >= 0) {
        halscope_channel_info_t *info = (halscope_channel_info_t *)calloc(1, sizeof(*info));
        info->channel = mock_channel;
        info->pin_name = mock_pin_name;
        info->data_type = HALSCOPE_FLOAT;
        info->enabled = true;
        st.channels = info;
        st.channels_len = 1;
    }
    return st;
}

static halscope_list_pins_result_t mock_list_pins(void *ctx, const char *pattern) {
    (void)ctx;
    (void)pattern;
    halscope_list_pins_result_t result;
    result.len = 3;
    result.data = (const char **)malloc(3 * sizeof(const char *));
    result.data[0] = "joint.0.pos-cmd";
    result.data[1] = "joint.1.pos-cmd";
    result.data[2] = "joint.2.pos-cmd";
    return result;
}

static halscope_scope_status_t mock_watch_state(void *ctx) {
    return mock_get_status(ctx);
}

static halscope_watch_samples_result_t mock_watch_samples(void *ctx) {
    (void)ctx;
    halscope_watch_samples_result_t result = { .data = NULL, .len = 0 };
    return result;
}

static halscope_callbacks_t make_mock_callbacks(void) {
    halscope_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.ctx = NULL;
    cb.configure = mock_configure;
    cb.set_channel = mock_set_channel;
    cb.clear_channel = mock_clear_channel;
    cb.set_trigger = mock_set_trigger;
    cb.arm = mock_arm;
    cb.reset = mock_reset;
    cb.get_status = mock_get_status;
    cb.list_pins = mock_list_pins;
    cb.watch_state = mock_watch_state;
    cb.watch_samples = mock_watch_samples;
    return cb;
}

static void mock_reset_state(void) {
    mock_state = 0;
    mock_samples = 0;
    mock_channel = -1;
    mock_pin_name[0] = 0;
    mock_armed = 0;
    mock_thread_name[0] = 0;
    mock_rec_len = 4000;
    mock_pre_trig = 2000;
}
*/
import "C"

import "unsafe"

// MockCallbacks creates a C halscope_callbacks_t with mock implementations.
func MockCallbacks() unsafe.Pointer {
	cb := C.make_mock_callbacks()
	// Store on heap so pointer remains valid
	p := C.malloc(C.size_t(unsafe.Sizeof(cb)))
	*(*C.halscope_callbacks_t)(p) = cb
	return p
}

// MockResetState resets the mock C state between tests.
func MockResetState() {
	C.mock_reset_state()
}

// FreeMockCallbacks frees a callbacks pointer from MockCallbacks.
func FreeMockCallbacks(p unsafe.Pointer) {
	C.free(p)
}
