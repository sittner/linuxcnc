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
	logger    *slog.Logger
	nmlFile   string
	mu        sync.Mutex
	poslog    posLogger
	prevStat  C.nml_stat_t        // shadow copy for section-level change detection
	curStat   emcstatapi.StatFull // current Go stat, updated in place per-section
	firstPoll bool                // true after first successful poll
	lastJSON  json.RawMessage     // cached last marshaled result for multi-subscriber fanout
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
				Watch:       gw.watchStat,
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

// watchStat is the WatchFunc for "get_stat". It polls the NML stat channel
// and compares individual sections of the C struct against a shadow copy.
// Only sections that actually changed get converted from C to Go (expensive:
// string copies, array iterations). The full Go struct is then marshalled
// to JSON every tick (cheap: ~5µs for this struct size). pushLoop's per-
// connection bytes.Equal suppresses duplicate sends to existing subscribers.
func (gw *emcGateway) watchStat() (json.RawMessage, error) {
	gw.mu.Lock()
	var cstat C.nml_stat_t
	if rc := C.nml_shim_poll_stat(&cstat); rc != 0 {
		gw.mu.Unlock()
		return nil, fmt.Errorf("stat poll failed")
	}

	// On first call, force all sections.
	forceAll := !gw.firstPoll
	if forceAll {
		gw.firstPoll = true
	}

	prev := &gw.prevStat
	anyChanged := forceAll
	s := &gw.curStat

	// Task section
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.task_mode), unsafe.Pointer(&prev.task_mode),
		C.size_t(unsafe.Offsetof(cstat.motion_mode)-unsafe.Offsetof(cstat.task_mode))) != 0 {
		s.Task = convertTask(&cstat)
		anyChanged = true
	}

	// Motion section
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.motion_mode), unsafe.Pointer(&prev.motion_mode),
		C.size_t(unsafe.Offsetof(cstat.position)-unsafe.Offsetof(cstat.motion_mode))) != 0 {
		s.Motion = convertMotion(&cstat)
		anyChanged = true
	}

	// Position
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.position), unsafe.Pointer(&prev.position),
		C.size_t(unsafe.Sizeof(cstat.position))) != 0 {
		s.Position = convertPos(&cstat.position)
		anyChanged = true
	}

	// Actual position
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.actual_position), unsafe.Pointer(&prev.actual_position),
		C.size_t(unsafe.Sizeof(cstat.actual_position))) != 0 {
		s.ActualPosition = convertPos(&cstat.actual_position)
		anyChanged = true
	}

	// Joint actual positions
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.joint_actual_position), unsafe.Pointer(&prev.joint_actual_position),
		C.size_t(unsafe.Sizeof(cstat.joint_actual_position))) != 0 {
		for i := 0; i < maxJoints; i++ {
			s.JointActualPosition[i] = float64(cstat.joint_actual_position[i])
		}
		anyChanged = true
	}

	// Probed position
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.probed_position), unsafe.Pointer(&prev.probed_position),
		C.size_t(unsafe.Sizeof(cstat.probed_position))) != 0 {
		s.ProbedPosition = convertPos(&cstat.probed_position)
		anyChanged = true
	}

	// G5x offset
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.g5x_offset), unsafe.Pointer(&prev.g5x_offset),
		C.size_t(unsafe.Sizeof(cstat.g5x_offset))) != 0 {
		s.G5xOffset = convertPos(&cstat.g5x_offset)
		anyChanged = true
	}

	// G92 offset
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.g92_offset), unsafe.Pointer(&prev.g92_offset),
		C.size_t(unsafe.Sizeof(cstat.g92_offset))) != 0 {
		s.G92Offset = convertPos(&cstat.g92_offset)
		anyChanged = true
	}

	// Tool offset
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.tool_offset), unsafe.Pointer(&prev.tool_offset),
		C.size_t(unsafe.Sizeof(cstat.tool_offset))) != 0 {
		s.ToolOffset = convertPos(&cstat.tool_offset)
		anyChanged = true
	}

	// Rotation XY
	if forceAll || cstat.rotation_xy != prev.rotation_xy {
		s.RotationXy = float64(cstat.rotation_xy)
		anyChanged = true
	}

	// Joints array
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.joints), unsafe.Pointer(&prev.joints),
		C.size_t(unsafe.Sizeof(cstat.joints))) != 0 {
		s.Joints = convertJoints(&cstat)
		anyChanged = true
	}

	// Spindles
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.spindle), unsafe.Pointer(&prev.spindle),
		C.size_t(unsafe.Sizeof(cstat.spindle))) != 0 {
		s.Spindle = convertSpindles(&cstat)
		anyChanged = true
	}

	// Axis
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.axis), unsafe.Pointer(&prev.axis),
		C.size_t(unsafe.Sizeof(cstat.axis))) != 0 {
		s.Axis = convertAxes(&cstat)
		anyChanged = true
	}

	// Active G-codes
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.active_gcodes), unsafe.Pointer(&prev.active_gcodes),
		C.size_t(unsafe.Sizeof(cstat.active_gcodes))) != 0 {
		gc := make([]int32, C.NML_SHIM_ACTIVE_G_CODES)
		for i := range gc {
			gc[i] = int32(cstat.active_gcodes[i])
		}
		s.ActiveGcodes = gc
		anyChanged = true
	}

	// Active M-codes
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.active_mcodes), unsafe.Pointer(&prev.active_mcodes),
		C.size_t(unsafe.Sizeof(cstat.active_mcodes))) != 0 {
		mc := make([]int32, C.NML_SHIM_ACTIVE_M_CODES)
		for i := range mc {
			mc[i] = int32(cstat.active_mcodes[i])
		}
		s.ActiveMcodes = mc
		anyChanged = true
	}

	// Active settings
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.active_settings), unsafe.Pointer(&prev.active_settings),
		C.size_t(unsafe.Sizeof(cstat.active_settings))) != 0 {
		as := make([]float64, C.NML_SHIM_ACTIVE_SETTINGS)
		for i := range as {
			as[i] = float64(cstat.active_settings[i])
		}
		s.ActiveSettings = as
		anyChanged = true
	}

	// Scalar fields (group them: kinematics_type through linear_units)
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.kinematics_type), unsafe.Pointer(&prev.kinematics_type),
		C.size_t(unsafe.Offsetof(cstat.homed)-unsafe.Offsetof(cstat.kinematics_type))) != 0 {
		s.KinematicsType = emcstatapi.KinematicsType(cstat.kinematics_type)
		s.JointsCount = int32(cstat.joints_count)
		s.NumExtrajoints = int32(cstat.num_extrajoints)
		s.AxisMask = int32(cstat.axis_mask)
		s.Flood = cstat.flood != 0
		s.Mist = cstat.mist != 0
		s.ToolInSpindle = int32(cstat.tool_in_spindle)
		s.PocketPrepped = int32(cstat.pocket_prepped)
		s.LinearUnits = float64(cstat.linear_units)
		anyChanged = true
	}

	// Homed array
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.homed), unsafe.Pointer(&prev.homed),
		C.size_t(unsafe.Sizeof(cstat.homed))) != 0 {
		for i := 0; i < maxJoints; i++ {
			s.Homed[i] = cstat.homed[i] != 0
		}
		anyChanged = true
	}

	// Limit array
	if forceAll || C.memcmp(unsafe.Pointer(&cstat.limit), unsafe.Pointer(&prev.limit),
		C.size_t(unsafe.Sizeof(cstat.limit))) != 0 {
		for i := 0; i < maxJoints; i++ {
			s.Limit[i] = int32(cstat.limit[i])
		}
		anyChanged = true
	}

	// State + echo_serial_number + debug
	if forceAll || cstat.state != prev.state || cstat.echo_serial_number != prev.echo_serial_number || cstat.debug != prev.debug {
		s.State = int32(cstat.state)
		s.Debug = int32(cstat.debug)
		anyChanged = true
	}

	// Update shadow
	gw.prevStat = cstat

	if !anyChanged {
		// Return cached last result so other subscribers (with different
		// pushLoop timers) still get the data. Their per-connection
		// bytes.Equal dedup will suppress if they already have it.
		cached := gw.lastJSON
		gw.mu.Unlock()
		return cached, nil
	}

	// Marshal the full struct. pushLoop's per-connection bytes.Equal
	// suppresses duplicate sends; the immediate-first-poll ensures new
	// subscribers always receive the complete state.
	data, err := json.Marshal(s)
	if err == nil {
		gw.lastJSON = data
	}
	gw.mu.Unlock()
	return data, err
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
