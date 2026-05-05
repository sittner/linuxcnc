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
#cgo CFLAGS: -I${SRCDIR}/../../../emc/nml_intf -I${SRCDIR}/../../.. -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../../include -I${SRCDIR}/../../../libnml/posemath -I${SRCDIR}/../../../libnml/rcs -I${SRCDIR}/../../../libnml/nml -I${SRCDIR}/../../../libnml/inifile -I${SRCDIR}/../../../libnml/os_intf -I${SRCDIR}/../../../emc -I${SRCDIR}/../../../emc/rs274ngc -I${SRCDIR}/../../../emc/tooldata
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -llinuxcnc -lnml -lposemath -ltooldata -lstdc++

#include "nml_shim.h"
#include "tool_shim.h"
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

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emccmdapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcerrorapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstatapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/toolsapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
	"github.com/sittner/linuxcnc/src/gomc/internal/config"
	"github.com/sittner/linuxcnc/src/gomc/pkg/gomc"
	"github.com/sittner/linuxcnc/src/gomc/pkg/inifile"
)

func init() {
	gomc.RegisterModule("emcgateway", newEmcGateway)
	apiserver.RegisterMeta(emcstatapi.EmcstatMeta)
	apiserver.RegisterMeta(emccmdapi.EmccmdMeta)
	apiserver.RegisterMeta(emcerrorapi.EmcerrorMeta)
}

// emcGateway implements gomc.Module.
type emcGateway struct {
	logger  *slog.Logger
	nmlFile string
	mu      sync.Mutex
	poslog  posLogger
}

func (m *emcGateway) Start() error { return nil }
func (m *emcGateway) Stop() {
	m.poslog.stopLogger()
}
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

	// Register API instances using generated dispatch.
	if err := emcstatapi.RegisterEmcstatAPI(apiserver.DefaultRegistry(), "emcstat", gw); err != nil {
		return nil, fmt.Errorf("emcgateway: register emcstat: %w", err)
	}
	if err := emccmdapi.RegisterEmccmdAPI(apiserver.DefaultRegistry(), "emccmd", gw); err != nil {
		return nil, fmt.Errorf("emcgateway: register emccmd: %w", err)
	}
	if err := emcerrorapi.RegisterEmcerrorAPI(apiserver.DefaultRegistry(), "emcerror", gw); err != nil {
		return nil, fmt.Errorf("emcgateway: register emcerror: %w", err)
	}
	toolFile := ini.Get("EMCIO", "TOOL_TABLE")
	// Load comments from tool table file at startup
	if toolFile != "" {
		cFile := C.CString(toolFile)
		C.tool_shim_load(cFile)
		C.free(unsafe.Pointer(cFile))
	}
	if err := toolsapi.RegisterToolsAPI(apiserver.DefaultRegistry(), "tools", &toolsImpl{
		toolTableFile: toolFile,
	}); err != nil {
		return nil, fmt.Errorf("emcgateway: register tools: %w", err)
	}

	// Register WebSocket watch APIs.
	if apiserver.DefaultWatchRegistry() == nil {
		apiserver.SetDefaultWatchRegistry(apiserver.NewWatchRegistry())
	}
	wreg := apiserver.DefaultWatchRegistry()

	// emcstat: get_stat (delta) + get_positions (drain-style) + poslogger commands.
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "emcstat",
		Instance: "emcstat",
		Watches: []apiserver.WatchFuncMeta{
			{
				Name:        "get_stat",
				DefaultRate: 50 * time.Millisecond,
				Delta:       true,
				Watch: func() (json.RawMessage, error) {
					result, err := gw.GetStat()
					if err != nil {
						return nil, err
					}
					return json.Marshal(result)
				},
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

	emcerrorapi.RegisterEmcerrorWatch(wreg, "emcerror", gw, nil)

	// Register command handlers on the watch WebSocket too.
	wreg.Register(&apiserver.WatchAPI{
		APIName:  "emccmd",
		Instance: "emccmd",
		Commands: emccmdapi.EmccmdCommands(gw),
	})

	logger.Info("NML gateway initialized")
	return gw, nil
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

// GetStat implements emcstatapi.EmcstatCallbacks.
func (gw *emcGateway) GetStat() (*emcstatapi.StatFull, error) {
	gw.mu.Lock()
	defer gw.mu.Unlock()

	var cstat C.nml_stat_t
	if rc := C.nml_shim_poll_stat(&cstat); rc != 0 {
		return nil, fmt.Errorf("stat poll failed")
	}
	return convertStat(&cstat), nil
}

// ─── Error Watch (generated via emcerrorapi.RegisterEmcerrorWatch) ───

// GetErrors implements emcerrorapi.EmcerrorCallbacks.
func (gw *emcGateway) GetErrors() ([]emcerrorapi.ErrorMessage, error) {
	gw.mu.Lock()
	defer gw.mu.Unlock()

	const maxErrors = 16
	var cerrs [maxErrors]C.nml_error_t
	n := C.nml_shim_poll_errors(&cerrs[0], maxErrors)
	if n == 0 {
		return []emcerrorapi.ErrorMessage{}, nil
	}

	msgs := make([]emcerrorapi.ErrorMessage, int(n))
	for i := 0; i < int(n); i++ {
		msgs[i] = emcerrorapi.ErrorMessage{
			Kind: emcerrorapi.ErrorKind(cerrs[i].kind),
			Text: C.GoString(&cerrs[i].text[0]),
		}
	}
	return msgs, nil
}

// ─── Command WebSocket ───

// (emccmd commands generated via emccmdapi.EmccmdCommands)
