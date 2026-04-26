// Package emcgateway provides a NML↔GMI gateway that exposes the LinuxCNC
// task controller's stat/command/error channels as REST and WebSocket endpoints.
//
// This is an anti-corruption layer: UI clients (axis.py) code against the
// clean GMI interface. When NML is later replaced internally, only the
// gateway implementation changes — axis.py is unaffected.
//
// Architecture:
//   - Stat: polls NML status channel, serves via watch (WebSocket push @50ms)
//   - Command: translates GMI function calls → NML command messages
//   - Error: polls NML error buffer, pushes new messages via watch @200ms
package emcgateway

/*
#cgo CFLAGS: -I${SRCDIR}/../../../emc/nml_intf -I${SRCDIR}/../../.. -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../../include -I${SRCDIR}/../../../libnml/posemath -I${SRCDIR}/../../../libnml/rcs -I${SRCDIR}/../../../libnml/nml -I${SRCDIR}/../../../libnml/inifile -I${SRCDIR}/../../../libnml/os_intf -I${SRCDIR}/../../../emc -I${SRCDIR}/../../../emc/rs274ngc
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -llinuxcnc -lnml -lposemath -lstdc++

#include "nml_shim.h"
#include <stdlib.h>
#include <string.h>
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"sync"
	"time"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/internal/config"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func init() {
	gomc.RegisterModule("emcgateway", newEmcGateway)
	registerStatMeta()
	registerCmdMeta()
	registerErrorMeta()
}

// emcGateway implements gomc.Module.
type emcGateway struct {
	logger  *slog.Logger
	nmlFile string
	mu      sync.Mutex
}

func (m *emcGateway) Start() error { return nil }
func (m *emcGateway) Stop()        {}
func (m *emcGateway) Destroy() {
	C.nml_shim_shutdown()
}

func newEmcGateway(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	logger = logger.With("module", "emcgateway")

	// Determine NML file path.
	nmlFile := config.DefaultNmlFile
	if v := ini.Get("EMC", "NML_FILE"); v != "" {
		nmlFile = v
	}

	logger.Info("initializing NML gateway", "nml_file", nmlFile)

	cNmlFile := C.CString(nmlFile)
	defer C.free(unsafe.Pointer(cNmlFile))

	if rc := C.nml_shim_init(cNmlFile); rc != 0 {
		return nil, fmt.Errorf("emcgateway: failed to open NML channels (nml_file=%s)", nmlFile)
	}

	gw := &emcGateway{
		logger:  logger,
		nmlFile: nmlFile,
	}

	// Register API instances with the apiserver.
	if err := apiserver.DefaultRegistry().Register("emcstat", 1, "emcstat", unsafe.Pointer(gw)); err != nil {
		return nil, fmt.Errorf("emcgateway: register emcstat: %w", err)
	}
	if err := apiserver.DefaultRegistry().Register("emccmd", 1, "emccmd", unsafe.Pointer(gw)); err != nil {
		return nil, fmt.Errorf("emcgateway: register emccmd: %w", err)
	}
	if err := apiserver.DefaultRegistry().Register("emcerror", 1, "emcerror", unsafe.Pointer(gw)); err != nil {
		return nil, fmt.Errorf("emcgateway: register emcerror: %w", err)
	}

	// Register WebSocket watch APIs.
	if apiserver.DefaultWatchRegistry() == nil {
		apiserver.SetDefaultWatchRegistry(apiserver.NewWatchRegistry())
	}
	apiserver.DefaultWatchRegistry().Register(newStatWatchAPI(gw))
	apiserver.DefaultWatchRegistry().Register(newErrorWatchAPI(gw))

	// Register command handlers on the watch WebSocket too.
	apiserver.DefaultWatchRegistry().Register(newCmdWatchAPI(gw))

	logger.Info("NML gateway initialized")
	return gw, nil
}

// ─── Stat Watch ───

func newStatWatchAPI(gw *emcGateway) *apiserver.WatchAPI {
	return &apiserver.WatchAPI{
		APIName:  "emcstat",
		Instance: "emcstat",
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "get_stat",
				DefaultRate: 50 * time.Millisecond,
				Watch:       func() (json.RawMessage, error) { return gw.pollStat() },
			},
		},
	}
}

// pollStat calls the NML shim to get current stat and marshals to JSON.
func (gw *emcGateway) pollStat() (json.RawMessage, error) {
	gw.mu.Lock()
	defer gw.mu.Unlock()

	var cstat C.nml_stat_t
	if rc := C.nml_shim_poll_stat(&cstat); rc != 0 {
		return nil, fmt.Errorf("stat poll failed")
	}

	stat := convertStat(&cstat)
	return json.Marshal(stat)
}

// ─── Error Watch ───

func newErrorWatchAPI(gw *emcGateway) *apiserver.WatchAPI {
	return &apiserver.WatchAPI{
		APIName:  "emcerror",
		Instance: "emcerror",
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "get_errors",
				DefaultRate: 200 * time.Millisecond,
				Watch:       func() (json.RawMessage, error) { return gw.pollErrors() },
			},
		},
	}
}

func (gw *emcGateway) pollErrors() (json.RawMessage, error) {
	gw.mu.Lock()
	defer gw.mu.Unlock()

	const maxErrors = 16
	var cerrs [maxErrors]C.nml_error_t
	n := C.nml_shim_poll_errors(&cerrs[0], maxErrors)
	if n == 0 {
		return json.Marshal([]errorMessage{})
	}

	msgs := make([]errorMessage, int(n))
	for i := 0; i < int(n); i++ {
		msgs[i] = errorMessage{
			Kind: int(cerrs[i].kind),
			Text: C.GoString(&cerrs[i].text[0]),
		}
	}
	return json.Marshal(msgs)
}

// ─── Command WebSocket/REST ───

func newCmdWatchAPI(gw *emcGateway) *apiserver.WatchAPI {
	return &apiserver.WatchAPI{
		APIName:  "emccmd",
		Instance: "emccmd",
		Commands: []apiserver.CommandMeta{
			{Name: "set_state", Handler: gw.cmdSetState},
			{Name: "set_mode", Handler: gw.cmdSetMode},
			{Name: "auto_cmd", Handler: gw.cmdAuto},
			{Name: "mdi", Handler: gw.cmdMdi},
			{Name: "jog", Handler: gw.cmdJog},
			{Name: "jog_stop", Handler: gw.cmdJogStop},
			{Name: "spindle", Handler: gw.cmdSpindle},
			{Name: "home", Handler: gw.cmdHome},
			{Name: "unhome", Handler: gw.cmdUnhome},
			{Name: "override_limits", Handler: gw.cmdOverrideLimits},
			{Name: "teleop_enable", Handler: gw.cmdTeleopEnable},
			{Name: "set_feed_override", Handler: gw.cmdSetFeedOverride},
			{Name: "set_spindle_override", Handler: gw.cmdSetSpindleOverride},
			{Name: "set_rapid_override", Handler: gw.cmdSetRapidOverride},
			{Name: "set_max_velocity", Handler: gw.cmdSetMaxVelocity},
			{Name: "flood", Handler: gw.cmdFlood},
			{Name: "mist", Handler: gw.cmdMist},
			{Name: "brake", Handler: gw.cmdBrake},
			{Name: "abort", Handler: gw.cmdAbort},
			{Name: "task_plan_synch", Handler: gw.cmdTaskPlanSynch},
			{Name: "set_optional_stop", Handler: gw.cmdSetOptionalStop},
			{Name: "set_block_delete", Handler: gw.cmdSetBlockDelete},
			{Name: "load_tool_table", Handler: gw.cmdLoadToolTable},
			{Name: "program_open", Handler: gw.cmdProgramOpen},
			{Name: "wait_complete", Handler: gw.cmdWaitComplete},
		},
	}
}

// ─── REST Meta Registration ───

func registerStatMeta() {
	apiserver.RegisterMeta(&apiserver.APIMeta{
		Name:       "emcstat",
		Version:    1,
		RESTExport: true,
		Prefix:     "emcstat",
		Funcs: []apiserver.FuncMeta{
			{
				Name:   "get_stat",
				Method: "GET",
				Path:   "/stat",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					gw := (*emcGateway)(cb)
					return gw.pollStat()
				},
			},
		},
	})
}

func registerCmdMeta() {
	apiserver.RegisterMeta(&apiserver.APIMeta{
		Name:       "emccmd",
		Version:    1,
		RESTExport: true,
		Prefix:     "emccmd",
		Funcs:      emccmdRESTFuncs(),
	})
}

func registerErrorMeta() {
	apiserver.RegisterMeta(&apiserver.APIMeta{
		Name:       "emcerror",
		Version:    1,
		RESTExport: true,
		Prefix:     "emcerror",
		Funcs: []apiserver.FuncMeta{
			{
				Name:   "get_errors",
				Method: "GET",
				Path:   "/errors",
				Dispatch: func(cb unsafe.Pointer, req []byte) ([]byte, error) {
					gw := (*emcGateway)(cb)
					return gw.pollErrors()
				},
			},
		},
	})
}

func emccmdRESTFuncs() []apiserver.FuncMeta {
	dispatch := func(name string, handler func(*emcGateway, json.RawMessage) (json.RawMessage, error)) apiserver.DispatchFunc {
		return func(cb unsafe.Pointer, req []byte) ([]byte, error) {
			gw := (*emcGateway)(cb)
			return handler(gw, json.RawMessage(req))
		}
	}
	return []apiserver.FuncMeta{
		{Name: "set_state", Method: "POST", Path: "/state", Dispatch: dispatch("set_state", (*emcGateway).cmdSetState)},
		{Name: "set_mode", Method: "POST", Path: "/mode", Dispatch: dispatch("set_mode", (*emcGateway).cmdSetMode)},
		{Name: "auto_cmd", Method: "POST", Path: "/auto", Dispatch: dispatch("auto_cmd", (*emcGateway).cmdAuto)},
		{Name: "mdi", Method: "POST", Path: "/mdi", Dispatch: dispatch("mdi", (*emcGateway).cmdMdi)},
		{Name: "jog", Method: "POST", Path: "/jog", Dispatch: dispatch("jog", (*emcGateway).cmdJog)},
		{Name: "jog_stop", Method: "POST", Path: "/jog-stop", Dispatch: dispatch("jog_stop", (*emcGateway).cmdJogStop)},
		{Name: "spindle", Method: "POST", Path: "/spindle", Dispatch: dispatch("spindle", (*emcGateway).cmdSpindle)},
		{Name: "home", Method: "POST", Path: "/home", Dispatch: dispatch("home", (*emcGateway).cmdHome)},
		{Name: "unhome", Method: "POST", Path: "/unhome", Dispatch: dispatch("unhome", (*emcGateway).cmdUnhome)},
		{Name: "override_limits", Method: "POST", Path: "/override-limits", Dispatch: dispatch("override_limits", (*emcGateway).cmdOverrideLimits)},
		{Name: "teleop_enable", Method: "POST", Path: "/teleop", Dispatch: dispatch("teleop_enable", (*emcGateway).cmdTeleopEnable)},
		{Name: "set_feed_override", Method: "POST", Path: "/feed-override", Dispatch: dispatch("set_feed_override", (*emcGateway).cmdSetFeedOverride)},
		{Name: "set_spindle_override", Method: "POST", Path: "/spindle-override", Dispatch: dispatch("set_spindle_override", (*emcGateway).cmdSetSpindleOverride)},
		{Name: "set_rapid_override", Method: "POST", Path: "/rapid-override", Dispatch: dispatch("set_rapid_override", (*emcGateway).cmdSetRapidOverride)},
		{Name: "set_max_velocity", Method: "POST", Path: "/max-velocity", Dispatch: dispatch("set_max_velocity", (*emcGateway).cmdSetMaxVelocity)},
		{Name: "flood", Method: "POST", Path: "/flood", Dispatch: dispatch("flood", (*emcGateway).cmdFlood)},
		{Name: "mist", Method: "POST", Path: "/mist", Dispatch: dispatch("mist", (*emcGateway).cmdMist)},
		{Name: "brake", Method: "POST", Path: "/brake", Dispatch: dispatch("brake", (*emcGateway).cmdBrake)},
		{Name: "abort", Method: "POST", Path: "/abort", Dispatch: dispatch("abort", (*emcGateway).cmdAbort)},
		{Name: "task_plan_synch", Method: "POST", Path: "/task-plan-synch", Dispatch: dispatch("task_plan_synch", (*emcGateway).cmdTaskPlanSynch)},
		{Name: "set_optional_stop", Method: "POST", Path: "/optional-stop", Dispatch: dispatch("set_optional_stop", (*emcGateway).cmdSetOptionalStop)},
		{Name: "set_block_delete", Method: "POST", Path: "/block-delete", Dispatch: dispatch("set_block_delete", (*emcGateway).cmdSetBlockDelete)},
		{Name: "load_tool_table", Method: "POST", Path: "/load-tool-table", Dispatch: dispatch("load_tool_table", (*emcGateway).cmdLoadToolTable)},
		{Name: "program_open", Method: "POST", Path: "/program-open", Dispatch: dispatch("program_open", (*emcGateway).cmdProgramOpen)},
		{Name: "wait_complete", Method: "POST", Path: "/wait-complete", Dispatch: dispatch("wait_complete", (*emcGateway).cmdWaitComplete)},
	}
}
