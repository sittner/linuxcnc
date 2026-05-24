// Package emcgateway provides a GMI gateway that exposes the LinuxCNC
// task controller's stat/command/error channels as REST and WebSocket endpoints.
//
// Architecture:
//   - Stat: pushed from milltask cmod via push_watch → PushWatch → WS
//   - Command: dispatched through emccmd C callbacks registered by milltask
//   - Error: pushed via ring drain (emcerror @publish)
//   - Tools: REST API backed by tool_shim (shared memory)
package emcgateway

/*
#cgo CFLAGS: -I${SRCDIR}/../../../emc/nml_intf -I${SRCDIR}/../../../emc/tooldata -I${SRCDIR}/../../.. -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../../include -I${SRCDIR}/../../generated/gmi/emccmd -I${SRCDIR}/../../pkg/cmodule
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -llinuxcnc -ltooldata -lstdc++

#include "tool_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"time"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstatapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/toolsapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"

	// emccmd C callback API — init() registers EmccmdMeta for REST dispatch.
	_ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/emccmd"

	// emcstat push converter — init() registers PushConverter for push_watch.
	_ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstat"
)

func init() {
	gomc.RegisterModule("emcgateway", newEmcGateway)
	apiserver.RegisterMeta(emcstatapi.EmcstatMeta)
}

// emcGateway implements gomc.Module.
type emcGateway struct {
	logger *slog.Logger
	poslog posLogger
	pw     *apiserver.PushWatch // stat push watch (populated by milltask push_watch)
}

func (m *emcGateway) Start() error { return nil }
func (m *emcGateway) Stop() {
	m.poslog.stopLogger()
}
func (m *emcGateway) Destroy() {}

func newEmcGateway(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
	logger = logger.With("module", "emcgateway")

	// Parse milltask instance name from args (default: "milltask").
	milltaskInstance := "milltask"
	for _, a := range args {
		if len(a) > 18 && a[:18] == "milltask_instance=" {
			milltaskInstance = a[18:]
		}
	}

	gw := &emcGateway{
		logger: logger,
	}

	// Get or create the PushWatch that milltask will push stat data into.
	// The PushConverter was registered by emcstat package's init().
	gw.pw = apiserver.GetOrCreatePushWatch("emcstat", milltaskInstance, "get_stat")
	if gw.pw == nil {
		return nil, fmt.Errorf("emcgateway: no push converter registered for emcstat")
	}

	// Register REST API backed by PushWatch (skip if already registered by C milltask).
	if err := emcstatapi.RegisterEmcstatAPI(apiserver.DefaultRegistry(), milltaskInstance, gw); err != nil {
		logger.Info("emcstat REST already registered (C milltask), skipping", "err", err)
	}

	// Tools API.
	toolFile := ini.Get("EMCIO", "TOOL_TABLE")
	if toolFile != "" {
		cFile := C.CString(toolFile)
		C.tool_shim_load(cFile)
		C.free(unsafe.Pointer(cFile))
	}
	if err := toolsapi.RegisterToolsAPI(apiserver.DefaultRegistry(), milltaskInstance, &toolsImpl{
		toolTableFile:    toolFile,
		milltaskInstance: milltaskInstance,
	}); err != nil {
		logger.Info("tools REST already registered, skipping", "err", err)
	}

	// Register WebSocket watch APIs.
	if apiserver.DefaultWatchRegistry() == nil {
		apiserver.SetDefaultWatchRegistry(apiserver.NewWatchRegistry())
	}
	wreg := apiserver.DefaultWatchRegistry()

	// emcstat: get_stat (push-backed) + get_positions (drain-style) + poslogger commands.
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "emcstat",
		Instance: milltaskInstance,
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "get_stat",
				DefaultRate: 50 * time.Millisecond,
				Watch:       gw.pw.WatchFunc,
			},
			{
				Name:        "get_positions",
				DefaultRate: 100 * time.Millisecond,
				Watch:       func() (json.RawMessage, error) { return gw.pollPositions() },
			},
		},
		Commands: []apiserver.CommandMeta{
			{Name: "start_logger", Handler: gw.cmdStartLogger},
			{Name: "stop_logger", Handler: gw.cmdStopLogger},
			{Name: "clear_logger", Handler: gw.cmdClearLogger},
		},
	})

	// Register command handlers on the watch WebSocket too.
	emccmdAPI := apiserver.DefaultRegistry().GetByAPI("emccmd", milltaskInstance)
	if emccmdAPI != nil && emccmdAPI.Meta != nil {
		apiserver.DefaultRegistry().RecordConsumer(name, "emccmd", milltaskInstance)
		cmds := make([]apiserver.CommandMeta, 0, len(emccmdAPI.Meta.Funcs))
		for _, fn := range emccmdAPI.Meta.Funcs {
			fn := fn // capture
			cb := emccmdAPI.Callbacks
			cmds = append(cmds, apiserver.CommandMeta{
				Name: fn.Name,
				Handler: func(req json.RawMessage) (json.RawMessage, error) {
					res, err := fn.Dispatch(cb, []byte(req))
					return json.RawMessage(res), err
				},
			})
		}
		wreg.Register(&apiserver.WatchAPI{
			APIName:  "emccmd",
			Instance: milltaskInstance,
			Commands: cmds,
		})
	}

	logger.Info("gateway initialized (stat via push_watch)")
	return gw, nil
}

// GetStat implements emcstatapi.EmcstatCallbacks for REST GET.
// Returns the latest pushed stat data.
func (gw *emcGateway) GetStat() (*emcstatapi.StatFull, error) {
	data, err := gw.pw.WatchFunc()
	if err != nil {
		return nil, err
	}
	if data == nil {
		return nil, fmt.Errorf("no stat data available yet")
	}
	var s emcstatapi.StatFull
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, err
	}
	return &s, nil
}
