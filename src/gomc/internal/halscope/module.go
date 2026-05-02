// Package halscope implements the HAL oscilloscope as a gomod.
//
// The RT sample function runs in a HAL thread (C code via cgo).
// All non-RT logic — API handlers, watch loops, buffer read-out — lives
// in Go.  The halscope.gmi IDL generates only client code (TS/Python)
// and WS/REST routing types.
package halscope

/*
#cgo CFLAGS: -I${SRCDIR}/../../../hal -I${SRCDIR}/../../.. -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -llinuxcnchal

#include "halscope_rt.h"
#include "hal_priv.h"

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fnmatch.h>
#include <errno.h>

// hal_export_funct wrapper — Go can't take address of C function directly.
static int go_hal_export_funct(const char *name, halscope_t *s,
                               int comp_id) {
    return hal_export_funct(name, halscope_sample, s, 1, 0, comp_id);
}

// Self dl_handle for RT component registration.
static void *self_dl_handle(void) {
    return dlopen(NULL, RTLD_NOW);
}

// --- SHMPTR wrappers (cgo cannot invoke C macros) ---

static hal_thread_t *shmptr_thread(rtapi_intptr_t off) {
    return (hal_thread_t *)SHMPTR(off);
}
static hal_pin_t *shmptr_pin(rtapi_intptr_t off) {
    return (hal_pin_t *)SHMPTR(off);
}
static hal_sig_t *shmptr_sig(rtapi_intptr_t off) {
    return (hal_sig_t *)SHMPTR(off);
}
static hal_param_t *shmptr_param(rtapi_intptr_t off) {
    return (hal_param_t *)SHMPTR(off);
}
static void *shmptr_void(rtapi_intptr_t off) {
    return SHMPTR(off);
}

// --- Accessor for hal_data (extern global, tricky from cgo) ---
static hal_data_t *get_hal_data(void) { return hal_data; }

// --- Union setter (cgo cannot access C union fields) ---
static void set_trigger_level(halscope_data_t *d, double v) {
    d->d_ireal = *(ireal_t *)&v;
}

static void set_trigger_level_s32(halscope_data_t *d, int32_t v) {
    d->d_s32 = v;
}

static void set_trigger_level_u32(halscope_data_t *d, uint32_t v) {
    d->d_u32 = v;
}

*/
import "C"

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func init() {
	gomc.RegisterModule("halscope", newHalscope)
	registerHalscopeMeta()
}

// halscope implements gomc.Module.
type halscope struct {
	logger    *slog.Logger
	s         *C.halscope_t // shared state — RT reads, Go writes
	compID    C.int
	mu        sync.Mutex // protects non-atomic config writes
	name      string     // HAL component name (from load command)
	functName string     // HAL function name: name + ".sample"
}

func newHalscope(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	numSamples := C.int(C.HALSCOPE_DEFAULT_NUM_SAMPLES)
	// TODO: parse args for num_samples=N

	s := C.halscope_alloc(numSamples)
	if s == nil {
		return nil, fmt.Errorf("halscope: failed to allocate instance")
	}

	// Create HAL RT component.  We need the gomc-server binary's own
	// dl_handle so HAL can lock it during RT thread execution.
	dlHandle := C.self_dl_handle()
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	compID := C.hal_init_ex(cName, dlHandle, C.COMPONENT_TYPE_REALTIME)
	if compID < 0 {
		C.halscope_free(s)
		return nil, fmt.Errorf("halscope: hal_init_ex failed: %d", int(compID))
	}

	// Export the RT sample function to HAL.
	functName := name + ".sample"
	cFunctName := C.CString(functName)
	defer C.free(unsafe.Pointer(cFunctName))
	rv := C.go_hal_export_funct(cFunctName, s, compID)
	if rv != 0 {
		C.hal_exit(compID)
		C.halscope_free(s)
		return nil, fmt.Errorf("halscope: hal_export_funct failed: %d", int(rv))
	}

	C.hal_ready(compID)

	m := &halscope{
		logger:    logger,
		s:         s,
		compID:    compID,
		name:      name,
		functName: functName,
	}

	// Register REST API.
	reg := apiserver.DefaultRegistry()
	if reg != nil {
		m.registerREST(reg, name)
	}

	// Register WebSocket watch API.
	wreg := apiserver.DefaultWatchRegistry()
	if wreg == nil {
		apiserver.SetDefaultWatchRegistry(apiserver.NewWatchRegistry())
		wreg = apiserver.DefaultWatchRegistry()
	}
	if wreg != nil {
		m.registerWatch(wreg, name)
	}

	logger.Info("halscope loaded", "name", name, "num_samples", int(numSamples), "comp_id", int(compID))
	return m, nil
}

func (m *halscope) Start() error { return nil }

func (m *halscope) Stop() {}

func (m *halscope) Destroy() {
	// Unwire from thread if attached.
	s := m.s
	if s.thread_name[0] != 0 {
		cThread := C.GoString(&s.thread_name[0])
		ct := C.CString(cThread)
		cf := C.CString(m.functName)
		C.hal_del_funct_from_thread(cf, ct)
		C.free(unsafe.Pointer(ct))
		C.free(unsafe.Pointer(cf))
	}
	C.hal_exit(m.compID)
	C.halscope_free(s)
}

// ------------------------------------------------------------------ //
//                     REST API REGISTRATION                           //
// ------------------------------------------------------------------ //

func (m *halscope) registerREST(reg *apiserver.Registry, instance string) {
	if err := reg.Register("halscope", 1, instance, unsafe.Pointer(m)); err != nil {
		m.logger.Error("halscope: register REST API failed", "err", err)
	}
}

func registerHalscopeMeta() {
	apiserver.RegisterMeta(&apiserver.APIMeta{
		Name:       "halscope",
		Version:    1,
		RESTExport: true,
		Prefix:     "halscope",
		Funcs: []apiserver.FuncMeta{
			{Name: "list_threads", Method: "GET", Path: "/threads",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchListThreads(req)
				}},
			{Name: "configure", Method: "POST", Path: "/configure",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchConfigure(req)
				}},
			{Name: "set_channel", Method: "POST", Path: "/channel",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchSetChannel(req)
				}},
			{Name: "clear_channel", Method: "DELETE", Path: "/channel/{channel}",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchClearChannel(req)
				}},
			{Name: "set_trigger", Method: "POST", Path: "/trigger",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchSetTrigger(req)
				}},
			{Name: "arm", Method: "POST", Path: "/arm",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchArm(req)
				}},
			{Name: "force_trigger", Method: "POST", Path: "/force_trigger",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchForceTrigger(req)
				}},
			{Name: "set_continuous", Method: "POST", Path: "/set_continuous",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchSetContinuous(req)
				}},
			{Name: "reset", Method: "POST", Path: "/reset",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchReset(req)
				}},
			{Name: "get_status", Method: "GET", Path: "/status",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchGetStatus(req)
				}},
			{Name: "list_pins", Method: "GET", Path: "/pins",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					return (*halscope)(cb).dispatchListPins(req)
				}},
		},
	})
}

func (m *halscope) registerWatch(wreg *apiserver.WatchRegistry, instance string) {
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "halscope",
		Instance: instance,
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "watch_state",
				DefaultRate: 100 * time.Millisecond,
				Watch:       m.watchState,
			},
			{
				Name:        "watch_samples",
				DefaultRate: 100 * time.Millisecond,
				BinaryWatch: m.watchSamples,
			},
		},
		Commands: nil,
	})
}

// ------------------------------------------------------------------ //
//                     WATCH FUNCTIONS                                  //
// ------------------------------------------------------------------ //

func (m *halscope) watchState() (json.RawMessage, error) {
	st := m.getStatus()
	return json.Marshal(st)
}

func (m *halscope) watchSamples() ([]byte, uint64, error) {
	s := m.s

	db := int(C.halscope_atomic_load_int((*C.int)(unsafe.Pointer(&s.done_buf)), C.memory_order_acquire))
	if db < 0 || s.done_len == 0 {
		return nil, 0, nil
	}

	gen := uint64(C.halscope_atomic_load_uint((*C.uint)(unsafe.Pointer(&s.done_gen)), C.memory_order_acquire))

	// Borrow the done buffer — increment refcount so RT won't reuse it.
	C.halscope_atomic_fetch_add_int((*C.int)(unsafe.Pointer(&s.bufs[db].readers)), 1, C.memory_order_acquire)

	// Verify done_buf hasn't changed — guards against TOCTOU race where
	// RT completes a new capture between our load and refcount increment.
	db2 := int(C.halscope_atomic_load_int((*C.int)(unsafe.Pointer(&s.done_buf)), C.memory_order_acquire))
	if db2 != db {
		C.halscope_atomic_fetch_sub_int((*C.int)(unsafe.Pointer(&s.bufs[db].readers)), 1, C.memory_order_release)
		return nil, 0, nil
	}

	totalLen := int(s.done_len)
	hdrSize := int(C.halscope_get_header_size())
	dataBytes := totalLen - hdrSize
	ringStartBytes := int(s.done_ring_start) * 8 // sizeof(double)

	result := make([]byte, totalLen)
	src := s.bufs[db].data

	// Copy header.
	C.memcpy(unsafe.Pointer(&result[0]), unsafe.Pointer(src), C.size_t(hdrSize))

	// Copy + linearize data.
	srcData := unsafe.Add(unsafe.Pointer(src), hdrSize)
	dstData := unsafe.Add(unsafe.Pointer(&result[0]), hdrSize)

	if ringStartBytes == 0 || ringStartBytes >= dataBytes {
		C.memcpy(dstData, srcData, C.size_t(dataBytes))
	} else {
		part1 := dataBytes - ringStartBytes
		C.memcpy(dstData, unsafe.Add(srcData, ringStartBytes), C.size_t(part1))
		C.memcpy(unsafe.Add(dstData, part1), srcData, C.size_t(ringStartBytes))
	}

	// Release borrow.
	C.halscope_atomic_fetch_sub_int((*C.int)(unsafe.Pointer(&s.bufs[db].readers)), 1, C.memory_order_release)

	return result, gen, nil
}

// ------------------------------------------------------------------ //
//                     DISPATCH FUNCTIONS                               //
// ------------------------------------------------------------------ //

func (m *halscope) dispatchListThreads(_ []byte) ([]byte, error) {
	type threadInfo struct {
		Name     string `json:"name"`
		PeriodNs int64  `json:"periodNs"`
	}

	var threads []threadInfo
	next := C.get_hal_data().thread_list_ptr
	for next != 0 {
		t := C.shmptr_thread(next)
		threads = append(threads, threadInfo{
			Name:     C.GoString(&t.name[0]),
			PeriodNs: int64(t.period),
		})
		next = t.next_ptr
	}
	return json.Marshal(threads)
}

func (m *halscope) dispatchConfigure(req []byte) ([]byte, error) {
	var params struct {
		Config struct {
			ThreadName       string `json:"threadName"`
			RecLen           int    `json:"recLen"`
			SamplePeriodMult int    `json:"samplePeriodMult"`
			PreTrig          int    `json:"preTrig"`
		} `json:"config"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid params: %w", err)
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	s := m.s
	state := C.halscope_atomic_load_state((*C.halscope_state_t)(unsafe.Pointer(&s.state)), C.memory_order_acquire)
	if state != C.HALSCOPE_ST_IDLE && state != C.HALSCOPE_ST_DONE {
		return json.Marshal(-int(C.EBUSY))
	}

	cfg := params.Config

	// Handle thread (re-)assignment.
	if cfg.ThreadName != "" {
		currentThread := C.GoString(&s.thread_name[0])
		if currentThread != "" && currentThread != cfg.ThreadName {
			ct := C.CString(currentThread)
			cf := C.CString(m.functName)
			C.hal_del_funct_from_thread(cf, ct)
			C.free(unsafe.Pointer(ct))
			C.free(unsafe.Pointer(cf))
			s.thread_name[0] = 0
		}
		if C.GoString(&s.thread_name[0]) != cfg.ThreadName {
			cf := C.CString(m.functName)
			ct := C.CString(cfg.ThreadName)
			rv := C.hal_add_funct_to_thread(cf, ct, -1)
			C.free(unsafe.Pointer(cf))
			C.free(unsafe.Pointer(ct))
			if rv != 0 {
				return json.Marshal(int(rv))
			}
			cName := C.CString(cfg.ThreadName)
			C.strncpy(&s.thread_name[0], cName, C.size_t(C.HAL_NAME_LEN))
			C.free(unsafe.Pointer(cName))
		}
	}

	if cfg.RecLen > 0 && cfg.RecLen <= int(s.num_samples) {
		s.rec_len = C.int(cfg.RecLen)
	}
	if cfg.SamplePeriodMult > 0 {
		s.mult = C.int(cfg.SamplePeriodMult)
	}
	if cfg.PreTrig >= 0 && cfg.PreTrig < int(s.rec_len) {
		s.pre_trig = C.int(cfg.PreTrig)
	}

	s.sample_len = C.int(m.countActiveChannels())

	return json.Marshal(0)
}

func (m *halscope) dispatchSetChannel(req []byte) ([]byte, error) {
	var params struct {
		Ch struct {
			Channel int    `json:"channel"`
			PinName string `json:"pinName"`
		} `json:"ch"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid params: %w", err)
	}

	ch := params.Ch
	if ch.Channel < 0 || ch.Channel >= C.HALSCOPE_MAX_CHANNELS {
		return json.Marshal(-int(C.EINVAL))
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	// Resolve HAL name.
	var halType C.hal_type_t
	var dataLen C.int
	var dataAddr unsafe.Pointer
	cName := C.CString(ch.PinName)
	defer C.free(unsafe.Pointer(cName))

	rv := m.resolveHALName(cName, &halType, &dataLen, &dataAddr)
	if rv != 0 {
		return json.Marshal(int(rv))
	}

	s := m.s
	c := &s.channels[ch.Channel]
	c.enabled = 1
	cPN := C.CString(ch.PinName)
	C.strncpy(&c.pin_name[0], cPN, C.size_t(C.HAL_NAME_LEN))
	C.free(unsafe.Pointer(cPN))
	c.data_type = halType
	c.data_len = dataLen
	c.data_addr = dataAddr

	if s.trig.channel < 0 {
		s.trig.channel = C.int(ch.Channel)
	}
	s.sample_len = C.int(m.countActiveChannels())

	return json.Marshal(0)
}

func (m *halscope) dispatchClearChannel(req []byte) ([]byte, error) {
	var params struct {
		Channel int `json:"channel"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid params: %w", err)
	}

	if params.Channel < 0 || params.Channel >= C.HALSCOPE_MAX_CHANNELS {
		return json.Marshal(-int(C.EINVAL))
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	C.memset(unsafe.Pointer(&m.s.channels[params.Channel]), 0,
		C.size_t(unsafe.Sizeof(m.s.channels[0])))
	m.s.sample_len = C.int(m.countActiveChannels())

	return json.Marshal(0)
}

func (m *halscope) dispatchSetTrigger(req []byte) ([]byte, error) {
	var params struct {
		Trig struct {
			Channel  int     `json:"channel"`
			Level    float64 `json:"level"`
			Edge     int     `json:"edge"`
			AutoTrig bool    `json:"autoTrig"`
		} `json:"trig"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid params: %w", err)
	}

	t := params.Trig
	if t.Channel < -1 || t.Channel >= C.HALSCOPE_MAX_CHANNELS {
		return json.Marshal(-int(C.EINVAL))
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	s := m.s
	s.trig.channel = C.int(t.Channel)

	// Store level in the correct union member for the trigger channel's type.
	// For HAL_FLOAT, store as ireal_t for IEEE-754 bit comparison in RT.
	// For S32/U32, store as integer so the RT comparison reads the right value.
	if t.Channel >= 0 && t.Channel < C.HALSCOPE_MAX_CHANNELS {
		switch s.channels[t.Channel].data_type {
		case C.HAL_S32:
			C.set_trigger_level_s32(&s.trig.level, C.int32_t(t.Level))
		case C.HAL_U32:
			C.set_trigger_level_u32(&s.trig.level, C.uint32_t(t.Level))
		default:
			C.set_trigger_level(&s.trig.level, C.double(t.Level))
		}
	} else {
		C.set_trigger_level(&s.trig.level, C.double(t.Level))
	}

	if t.Edge == 1 {
		s.trig.edge = 1
	} else {
		s.trig.edge = 0
	}
	if t.AutoTrig {
		s.trig.auto_trig = 1
	} else {
		s.trig.auto_trig = 0
	}

	return json.Marshal(0)
}

func (m *halscope) dispatchArm(_ []byte) ([]byte, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	s := m.s
	state := C.halscope_atomic_load_state((*C.halscope_state_t)(unsafe.Pointer(&s.state)), C.memory_order_acquire)
	if state != C.HALSCOPE_ST_IDLE && state != C.HALSCOPE_ST_DONE {
		return json.Marshal(-int(C.EBUSY))
	}
	if s.sample_len == 0 || s.rec_len == 0 {
		return json.Marshal(-int(C.EINVAL))
	}
	if s.thread_name[0] == 0 {
		return json.Marshal(-int(C.EINVAL))
	}

	C.halscope_atomic_store_state((*C.halscope_state_t)(unsafe.Pointer(&s.state)), C.HALSCOPE_ST_INIT, C.memory_order_release)
	return json.Marshal(0)
}

func (m *halscope) dispatchForceTrigger(_ []byte) ([]byte, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	state := C.halscope_atomic_load_state((*C.halscope_state_t)(unsafe.Pointer(&m.s.state)), C.memory_order_acquire)
	if state != C.HALSCOPE_ST_PRE_TRIG && state != C.HALSCOPE_ST_TRIG_WAIT {
		return json.Marshal(-int(C.EINVAL))
	}
	m.s.trig.force = 1
	return json.Marshal(0)
}

func (m *halscope) dispatchSetContinuous(req []byte) ([]byte, error) {
	var params struct {
		Enabled bool `json:"enabled"`
	}
	if err := json.Unmarshal(req, &params); err != nil {
		return nil, fmt.Errorf("invalid params: %w", err)
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	if params.Enabled {
		C.halscope_atomic_store_int((*C.int)(unsafe.Pointer(&m.s.continuous)), 1, C.memory_order_release)
	} else {
		C.halscope_atomic_store_int((*C.int)(unsafe.Pointer(&m.s.continuous)), 0, C.memory_order_release)
	}
	return json.Marshal(0)
}

func (m *halscope) dispatchReset(_ []byte) ([]byte, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	C.halscope_atomic_store_int((*C.int)(unsafe.Pointer(&m.s.continuous)), 0, C.memory_order_release)
	C.halscope_atomic_store_state((*C.halscope_state_t)(unsafe.Pointer(&m.s.state)), C.HALSCOPE_ST_RESET, C.memory_order_release)
	return json.Marshal(0)
}

func (m *halscope) dispatchGetStatus(_ []byte) ([]byte, error) {
	return json.Marshal(m.getStatus())
}

func (m *halscope) dispatchListPins(req []byte) ([]byte, error) {
	var params struct {
		Pattern string `json:"pattern"`
		Kind    string `json:"kind"`
	}
	if len(req) > 0 {
		json.Unmarshal(req, &params)
	}

	match := params.Pattern
	if match == "" {
		match = "*"
	}
	cMatch := C.CString(match)
	defer C.free(unsafe.Pointer(cMatch))

	wantPins := params.Kind == "" || params.Kind == "pin"
	wantSigs := params.Kind == "" || params.Kind == "sig"
	wantParams := params.Kind == "" || params.Kind == "param"

	var names []string

	C.rtapi_mutex_get(&C.get_hal_data().mutex)

	if wantPins {
		next := C.get_hal_data().pin_list_ptr
		for next != 0 {
			pin := C.shmptr_pin(next)
			if C.fnmatch(cMatch, &pin.name[0], 0) == 0 {
				names = append(names, C.GoString(&pin.name[0]))
			}
			next = pin.next_ptr
		}
	}
	if wantSigs {
		next := C.get_hal_data().sig_list_ptr
		for next != 0 {
			sig := C.shmptr_sig(next)
			if C.fnmatch(cMatch, &sig.name[0], 0) == 0 {
				names = append(names, C.GoString(&sig.name[0]))
			}
			next = sig.next_ptr
		}
	}
	if wantParams {
		next := C.get_hal_data().param_list_ptr
		for next != 0 {
			param := C.shmptr_param(next)
			if C.fnmatch(cMatch, &param.name[0], 0) == 0 {
				names = append(names, C.GoString(&param.name[0]))
			}
			next = param.next_ptr
		}
	}

	C.rtapi_mutex_give(&C.get_hal_data().mutex)

	return json.Marshal(names)
}

// ------------------------------------------------------------------ //
//                     HELPERS                                         //
// ------------------------------------------------------------------ //

// halscope_state_t alias for readability in Go.
type halscope_state_t = C.halscope_state_t

type scopeStatus struct {
	State            int           `json:"state"`
	Samples          int           `json:"samples"`
	RecLen           int           `json:"recLen"`
	PreTrig          int           `json:"preTrig"`
	SampleLen        int           `json:"sampleLen"`
	SamplePeriodMult int           `json:"samplePeriodMult"`
	ThreadPeriodNs   int64         `json:"threadPeriodNs"`
	ThreadName       string        `json:"threadName"`
	TrigChannel      int           `json:"trigChannel"`
	Generation       uint32        `json:"generation"`
	Continuous       bool          `json:"continuous"`
	Channels         []channelInfo `json:"channels"`
}

type channelInfo struct {
	Channel  int    `json:"channel"`
	PinName  string `json:"pinName"`
	DataType int    `json:"dataType"`
	Enabled  bool   `json:"enabled"`
}

func (m *halscope) getStatus() scopeStatus {
	s := m.s
	st := scopeStatus{
		State:            int(C.halscope_atomic_load_state((*C.halscope_state_t)(unsafe.Pointer(&s.state)), C.memory_order_acquire)),
		Samples:          int(s.samples),
		RecLen:           int(s.rec_len),
		PreTrig:          int(s.pre_trig),
		SampleLen:        int(s.sample_len),
		SamplePeriodMult: int(s.mult),
		TrigChannel:      int(s.trig.channel),
		Generation:       uint32(atomic.LoadUint32((*uint32)(unsafe.Pointer(&s.done_gen)))),
		Continuous:       C.halscope_atomic_load_int((*C.int)(unsafe.Pointer(&s.continuous)), C.memory_order_acquire) != 0,
	}

	threadName := C.GoString(&s.thread_name[0])
	st.ThreadName = threadName

	// Look up thread period.
	if threadName != "" {
		next := C.get_hal_data().thread_list_ptr
		for next != 0 {
			t := C.shmptr_thread(next)
			if C.GoString(&t.name[0]) == threadName {
				st.ThreadPeriodNs = int64(t.period)
				break
			}
			next = t.next_ptr
		}
	}

	// Build channel list.
	for n := 0; n < C.HALSCOPE_MAX_CHANNELS; n++ {
		if s.channels[n].enabled != 0 {
			st.Channels = append(st.Channels, channelInfo{
				Channel:  n,
				PinName:  C.GoString(&s.channels[n].pin_name[0]),
				DataType: int(s.channels[n].data_type),
				Enabled:  true,
			})
		}
	}
	if st.Channels == nil {
		st.Channels = []channelInfo{}
	}

	return st
}

func (m *halscope) countActiveChannels() int {
	count := 0
	for n := 0; n < C.HALSCOPE_MAX_CHANNELS; n++ {
		if m.s.channels[n].enabled != 0 && m.s.channels[n].data_len > 0 {
			count++
		}
	}
	return count
}

func (m *halscope) resolveHALName(cName *C.char, halType *C.hal_type_t, dataLen *C.int, dataAddr *unsafe.Pointer) int {
	C.rtapi_mutex_get(&C.get_hal_data().mutex)
	defer C.rtapi_mutex_give(&C.get_hal_data().mutex)

	// Try pin.
	pin := C.halpr_find_pin_by_name(cName)
	if pin != nil {
		*halType = pin._type
		if pin.signal != 0 {
			sig := C.shmptr_sig(pin.signal)
			*dataAddr = C.shmptr_void(sig.data_ptr)
		} else {
			*dataAddr = unsafe.Pointer(&pin.dummysig)
		}
		m.setDataLen(*halType, dataLen)
		return 0
	}

	// Try signal.
	sig := C.halpr_find_sig_by_name(cName)
	if sig != nil {
		*halType = sig._type
		*dataAddr = C.shmptr_void(sig.data_ptr)
		m.setDataLen(*halType, dataLen)
		return 0
	}

	// Try parameter.
	param := C.halpr_find_param_by_name(cName)
	if param != nil {
		*halType = param._type
		*dataAddr = C.shmptr_void(param.data_ptr)
		m.setDataLen(*halType, dataLen)
		return 0
	}

	return -int(C.ENOENT)
}

func (m *halscope) setDataLen(t C.hal_type_t, dataLen *C.int) {
	switch t {
	case C.HAL_BIT:
		*dataLen = 1
	case C.HAL_S32, C.HAL_U32:
		*dataLen = 4
	case C.HAL_FLOAT:
		*dataLen = 8
	default:
		*dataLen = 0
	}
}
