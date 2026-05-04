package emcgateway

// Command handlers — implement emccmdapi.EmccmdCallbacks by calling NML shim.

/*
#include "nml_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emccmdapi"
)

func boolToInt(b bool) C.int {
	if b {
		return 1
	}
	return 0
}

func rc(r C.int) (int32, error) {
	return int32(r), nil
}

func (gw *emcGateway) SetState(state int32) (int32, error) {
	return rc(C.nml_shim_set_state(C.int(state)))
}

func (gw *emcGateway) SetMode(mode int32) (int32, error) {
	return rc(C.nml_shim_set_mode(C.int(mode)))
}

func (gw *emcGateway) AutoCmd(cmd emccmdapi.AutoCmd, line int32) (int32, error) {
	return rc(C.nml_shim_auto_cmd(C.int(cmd), C.int(line)))
}

func (gw *emcGateway) Mdi(command string) (int32, error) {
	cCmd := C.CString(command)
	defer C.free(unsafe.Pointer(cCmd))
	return rc(C.nml_shim_mdi(cCmd))
}

func (gw *emcGateway) Jog(jogType emccmdapi.JogType, jjogmode bool, axisOrJoint int32, velocity float64, distance float64) (int32, error) {
	return rc(C.nml_shim_jog(C.int(jogType), C.int(boolToInt(jjogmode)),
		C.int(axisOrJoint), C.double(velocity), C.double(distance)))
}

func (gw *emcGateway) JogStop(jjogmode bool, axisOrJoint int32) (int32, error) {
	return rc(C.nml_shim_jog_stop(C.int(boolToInt(jjogmode)), C.int(axisOrJoint)))
}

func (gw *emcGateway) Spindle(cmd emccmdapi.SpindleCmd, speed float64, spindleNum int32, wait int32) (int32, error) {
	return rc(C.nml_shim_spindle(C.int(cmd), C.double(speed),
		C.int(spindleNum), C.int(wait)))
}

func (gw *emcGateway) Home(joint int32) (int32, error) {
	return rc(C.nml_shim_home(C.int(joint)))
}

func (gw *emcGateway) Unhome(joint int32) (int32, error) {
	return rc(C.nml_shim_unhome(C.int(joint)))
}

func (gw *emcGateway) OverrideLimits() (int32, error) {
	return rc(C.nml_shim_override_limits())
}

func (gw *emcGateway) TeleopEnable(enable bool) (int32, error) {
	return rc(C.nml_shim_teleop_enable(C.int(boolToInt(enable))))
}

func (gw *emcGateway) SetFeedOverride(rate float64) (int32, error) {
	return rc(C.nml_shim_set_feed_override(C.double(rate)))
}

func (gw *emcGateway) SetSpindleOverride(rate float64, spindleNum int32) (int32, error) {
	return rc(C.nml_shim_set_spindle_override(C.double(rate), C.int(spindleNum)))
}

func (gw *emcGateway) SetRapidOverride(rate float64) (int32, error) {
	return rc(C.nml_shim_set_rapid_override(C.double(rate)))
}

func (gw *emcGateway) SetMaxVelocity(velocity float64) (int32, error) {
	return rc(C.nml_shim_set_max_velocity(C.double(velocity)))
}

func (gw *emcGateway) Flood(on bool) (int32, error) {
	return rc(C.nml_shim_flood(C.int(boolToInt(on))))
}

func (gw *emcGateway) Mist(on bool) (int32, error) {
	return rc(C.nml_shim_mist(C.int(boolToInt(on))))
}

func (gw *emcGateway) Brake(on bool, spindleNum int32) (int32, error) {
	return rc(C.nml_shim_brake(C.int(boolToInt(on)), C.int(spindleNum)))
}

func (gw *emcGateway) Abort() (int32, error) {
	return rc(C.nml_shim_abort())
}

func (gw *emcGateway) TaskPlanSynch() (int32, error) {
	return rc(C.nml_shim_task_plan_synch())
}

func (gw *emcGateway) SetOptionalStop(on bool) (int32, error) {
	return rc(C.nml_shim_set_optional_stop(C.int(boolToInt(on))))
}

func (gw *emcGateway) SetBlockDelete(on bool) (int32, error) {
	return rc(C.nml_shim_set_block_delete(C.int(boolToInt(on))))
}

func (gw *emcGateway) LoadToolTable() (int32, error) {
	return rc(C.nml_shim_load_tool_table())
}

func (gw *emcGateway) ProgramOpen(file string) (int32, error) {
	cFile := C.CString(file)
	defer C.free(unsafe.Pointer(cFile))
	return rc(C.nml_shim_program_open(cFile))
}

func (gw *emcGateway) WaitComplete(timeout float64) (int32, error) {
	return rc(C.nml_shim_wait_complete(C.double(timeout)))
}

func (gw *emcGateway) SetDebug(debug int32) (int32, error) {
	return rc(C.nml_shim_set_debug(C.int(debug)))
}
