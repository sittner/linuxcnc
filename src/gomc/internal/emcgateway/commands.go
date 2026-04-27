package emcgateway

// Command handlers — translate REST/WS JSON requests into NML shim calls.

/*
#include "nml_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"unsafe"
)

// ─── Command request types ───

type cmdStateReq struct {
	State int `json:"state"`
}
type cmdModeReq struct {
	Mode int `json:"mode"`
}
type cmdAutoReq struct {
	Cmd  int `json:"cmd"`
	Line int `json:"line"`
}
type cmdMdiReq struct {
	Command string `json:"command"`
}
type cmdJogReq struct {
	JogType     int     `json:"jog_type"`
	Jjogmode    flexInt `json:"jjogmode"`
	AxisOrJoint int     `json:"axis_or_joint"`
	Velocity    float64 `json:"velocity"`
	Distance    float64 `json:"distance"`
}
type cmdJogStopReq struct {
	Jjogmode    flexInt `json:"jjogmode"`
	AxisOrJoint int     `json:"axis_or_joint"`
}
type cmdSpindleReq struct {
	Cmd        int     `json:"cmd"`
	Speed      float64 `json:"speed"`
	SpindleNum int     `json:"spindle_num"`
	Wait       int     `json:"wait"`
}
type cmdJointReq struct {
	Joint int `json:"joint"`
}
type cmdBoolReq struct {
	Enable flexInt `json:"enable,omitempty"`
	On     flexInt `json:"on,omitempty"`
}
type cmdRateReq struct {
	Rate float64 `json:"rate"`
}
type cmdSpindleOverrideReq struct {
	Rate       float64 `json:"rate"`
	SpindleNum int     `json:"spindle_num"`
}
type cmdVelocityReq struct {
	Velocity float64 `json:"velocity"`
}
type cmdBrakeReq struct {
	On         flexInt `json:"on"`
	SpindleNum int     `json:"spindle_num"`
}
type cmdFileReq struct {
	File string `json:"file"`
}
type cmdTimeoutReq struct {
	Timeout float64 `json:"timeout"`
}
type cmdDebugReq struct {
	Debug int `json:"debug"`
}

// ─── Helpers ───

// flexInt accepts both JSON numbers (0, 1) and booleans (true, false).
type flexInt int

func (f *flexInt) UnmarshalJSON(b []byte) error {
	s := string(b)
	if s == "true" {
		*f = 1
		return nil
	}
	if s == "false" {
		*f = 0
		return nil
	}
	var n int
	if err := json.Unmarshal(b, &n); err != nil {
		return err
	}
	*f = flexInt(n)
	return nil
}

func cmdResult(rc C.int) (json.RawMessage, error) {
	if rc != 0 {
		return nil, fmt.Errorf("command failed (rc=%d)", int(rc))
	}
	return json.Marshal(map[string]int{"result": 0})
}

func unmarshal[T any](req json.RawMessage) (*T, error) {
	var v T
	if err := json.Unmarshal(req, &v); err != nil {
		return nil, fmt.Errorf("invalid request: %w", err)
	}
	return &v, nil
}

// ─── Command Handlers ───

func (gw *emcGateway) cmdSetState(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdStateReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_state(C.int(r.State)))
}

func (gw *emcGateway) cmdSetMode(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdModeReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_mode(C.int(r.Mode)))
}

func (gw *emcGateway) cmdAuto(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdAutoReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_auto_cmd(C.int(r.Cmd), C.int(r.Line)))
}

func (gw *emcGateway) cmdMdi(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdMdiReq](req)
	if err != nil {
		return nil, err
	}
	cCmd := C.CString(r.Command)
	defer C.free(unsafe.Pointer(cCmd))
	return cmdResult(C.nml_shim_mdi(cCmd))
}

func (gw *emcGateway) cmdJog(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdJogReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_jog(C.int(r.JogType), C.int(r.Jjogmode),
		C.int(r.AxisOrJoint), C.double(r.Velocity), C.double(r.Distance)))
}

func (gw *emcGateway) cmdJogStop(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdJogStopReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_jog_stop(C.int(r.Jjogmode), C.int(r.AxisOrJoint)))
}

func (gw *emcGateway) cmdSpindle(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdSpindleReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_spindle(C.int(r.Cmd), C.double(r.Speed),
		C.int(r.SpindleNum), C.int(r.Wait)))
}

func (gw *emcGateway) cmdHome(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdJointReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_home(C.int(r.Joint)))
}

func (gw *emcGateway) cmdUnhome(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdJointReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_unhome(C.int(r.Joint)))
}

func (gw *emcGateway) cmdOverrideLimits(req json.RawMessage) (json.RawMessage, error) {
	return cmdResult(C.nml_shim_override_limits())
}

func (gw *emcGateway) cmdTeleopEnable(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdBoolReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_teleop_enable(C.int(r.Enable)))
}

func (gw *emcGateway) cmdSetFeedOverride(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdRateReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_feed_override(C.double(r.Rate)))
}

func (gw *emcGateway) cmdSetSpindleOverride(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdSpindleOverrideReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_spindle_override(C.double(r.Rate), C.int(r.SpindleNum)))
}

func (gw *emcGateway) cmdSetRapidOverride(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdRateReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_rapid_override(C.double(r.Rate)))
}

func (gw *emcGateway) cmdSetMaxVelocity(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdVelocityReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_max_velocity(C.double(r.Velocity)))
}

func (gw *emcGateway) cmdFlood(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdBoolReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_flood(C.int(r.On)))
}

func (gw *emcGateway) cmdMist(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdBoolReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_mist(C.int(r.On)))
}

func (gw *emcGateway) cmdBrake(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdBrakeReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_brake(C.int(r.On), C.int(r.SpindleNum)))
}

func (gw *emcGateway) cmdAbort(req json.RawMessage) (json.RawMessage, error) {
	return cmdResult(C.nml_shim_abort())
}

func (gw *emcGateway) cmdTaskPlanSynch(req json.RawMessage) (json.RawMessage, error) {
	return cmdResult(C.nml_shim_task_plan_synch())
}

func (gw *emcGateway) cmdSetOptionalStop(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdBoolReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_optional_stop(C.int(r.On)))
}

func (gw *emcGateway) cmdSetBlockDelete(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdBoolReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_block_delete(C.int(r.On)))
}

func (gw *emcGateway) cmdLoadToolTable(req json.RawMessage) (json.RawMessage, error) {
	return cmdResult(C.nml_shim_load_tool_table())
}

func (gw *emcGateway) cmdProgramOpen(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdFileReq](req)
	if err != nil {
		return nil, err
	}
	cFile := C.CString(r.File)
	defer C.free(unsafe.Pointer(cFile))
	return cmdResult(C.nml_shim_program_open(cFile))
}

func (gw *emcGateway) cmdWaitComplete(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdTimeoutReq](req)
	if err != nil {
		return nil, err
	}
	rc := C.nml_shim_wait_complete(C.double(r.Timeout))
	return json.Marshal(map[string]int{"result": int(rc)})
}

func (gw *emcGateway) cmdSetDebug(req json.RawMessage) (json.RawMessage, error) {
	r, err := unmarshal[cmdDebugReq](req)
	if err != nil {
		return nil, err
	}
	return cmdResult(C.nml_shim_set_debug(C.int(r.Debug)))
}
