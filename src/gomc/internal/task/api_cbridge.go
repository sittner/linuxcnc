package task

/*
#cgo CFLAGS: -I${SRCDIR}/../../generated/gmi/emccmd -I${SRCDIR}/../../generated/gmi/emcstat

#define EMCCMD_API_CGO
#define EMCSTAT_API_CGO
#include "emccmd_api.h"
#include "emcstat_api.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations of Go-exported thunks.
extern int32_t goTaskSetState(void *ctx, int32_t state);
extern int32_t goTaskSetMode(void *ctx, int32_t mode);
extern int32_t goTaskAutoCmd(void *ctx, emccmd_auto_cmd_t cmd, int32_t line);
extern int32_t goTaskMdi(void *ctx, char *command);
extern int32_t goTaskJog(void *ctx, emccmd_jog_type_t jog_type, bool jjogmode, int32_t axis_or_joint, double velocity, double distance);
extern int32_t goTaskJogStop(void *ctx, bool jjogmode, int32_t axis_or_joint);
extern int32_t goTaskSpindle(void *ctx, emccmd_spindle_cmd_t cmd, double speed, int32_t spindle_num, int32_t wait);
extern int32_t goTaskHome(void *ctx, int32_t joint);
extern int32_t goTaskUnhome(void *ctx, int32_t joint);
extern int32_t goTaskOverrideLimits(void *ctx);
extern int32_t goTaskTeleopEnable(void *ctx, bool enable);
extern int32_t goTaskSetFeedOverride(void *ctx, double rate);
extern int32_t goTaskSetSpindleOverride(void *ctx, double rate, int32_t spindle_num);
extern int32_t goTaskSetRapidOverride(void *ctx, double rate);
extern int32_t goTaskSetMaxVelocity(void *ctx, double velocity);
extern int32_t goTaskFlood(void *ctx, bool on);
extern int32_t goTaskMist(void *ctx, bool on);
extern int32_t goTaskBrake(void *ctx, bool on, int32_t spindle_num);
extern int32_t goTaskLube(void *ctx, bool on);
extern int32_t goTaskAbort(void *ctx);
extern int32_t goTaskTaskPlanSynch(void *ctx);
extern int32_t goTaskSetOptionalStop(void *ctx, bool on);
extern int32_t goTaskSetBlockDelete(void *ctx, bool on);
extern int32_t goTaskLoadToolTable(void *ctx);
extern int32_t goTaskProgramOpen(void *ctx, char *file);
extern int32_t goTaskWaitComplete(void *ctx, double timeout);
extern int32_t goTaskSetDebug(void *ctx, int32_t debug);
extern emcstat_stat_full_t goTaskGetStat(void *ctx);

// Allocate and populate the emccmd_callbacks_t struct.
static emccmd_callbacks_t *alloc_emccmd_cbs(void *ctx) {
    emccmd_callbacks_t *cbs = (emccmd_callbacks_t *)calloc(1, sizeof(emccmd_callbacks_t));
    cbs->ctx = ctx;
    cbs->set_state = goTaskSetState;
    cbs->set_mode = goTaskSetMode;
    cbs->auto_cmd = goTaskAutoCmd;
    cbs->mdi = (emccmd_mdi_fn)goTaskMdi;
    cbs->jog = goTaskJog;
    cbs->jog_stop = goTaskJogStop;
    cbs->spindle = goTaskSpindle;
    cbs->home = goTaskHome;
    cbs->unhome = goTaskUnhome;
    cbs->override_limits = goTaskOverrideLimits;
    cbs->teleop_enable = goTaskTeleopEnable;
    cbs->set_feed_override = goTaskSetFeedOverride;
    cbs->set_spindle_override = goTaskSetSpindleOverride;
    cbs->set_rapid_override = goTaskSetRapidOverride;
    cbs->set_max_velocity = goTaskSetMaxVelocity;
    cbs->flood = goTaskFlood;
    cbs->mist = goTaskMist;
    cbs->brake = goTaskBrake;
    cbs->lube = goTaskLube;
    cbs->abort = goTaskAbort;
    cbs->task_plan_synch = goTaskTaskPlanSynch;
    cbs->set_optional_stop = goTaskSetOptionalStop;
    cbs->set_block_delete = goTaskSetBlockDelete;
    cbs->load_tool_table = goTaskLoadToolTable;
    cbs->program_open = (emccmd_program_open_fn)goTaskProgramOpen;
    cbs->wait_complete = goTaskWaitComplete;
    cbs->set_debug = goTaskSetDebug;
    return cbs;
}

// Allocate and populate the emcstat_callbacks_t struct.
static emcstat_callbacks_t *alloc_emcstat_cbs(void *ctx) {
    emcstat_callbacks_t *cbs = (emcstat_callbacks_t *)calloc(1, sizeof(emcstat_callbacks_t));
    cbs->ctx = ctx;
    cbs->get_stat = goTaskGetStat;
    return cbs;
}
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emccmdapi"
	"github.com/sittner/linuxcnc/src/gomc/internal/apiserver"
)

// registerCAPIs allocates C-compatible callback structs and registers them
// with the API registry. This allows C modules (halui) to call milltask
// through the standard emccmd_api_get/emcstat_api_get mechanism.
func (m *milltaskModule) registerCAPIs(reg *apiserver.Registry, name string) (func(), error) {
	h := cgo.NewHandle(m)
	ctx := unsafe.Pointer(h)

	emccmdCbs := C.alloc_emccmd_cbs(ctx)
	if err := reg.Register("emccmd", 1, name, unsafe.Pointer(emccmdCbs)); err != nil {
		C.free(unsafe.Pointer(emccmdCbs))
		h.Delete()
		return nil, err
	}

	emcstatCbs := C.alloc_emcstat_cbs(ctx)
	if err := reg.Register("emcstat", 1, name, unsafe.Pointer(emcstatCbs)); err != nil {
		C.free(unsafe.Pointer(emcstatCbs))
		h.Delete()
		return nil, err
	}

	cleanup := func() {
		C.free(unsafe.Pointer(emccmdCbs))
		C.free(unsafe.Pointer(emcstatCbs))
		h.Delete()
	}
	return cleanup, nil
}

// --- Go exports called from C thunks ---

//export goTaskSetState
func goTaskSetState(ctx unsafe.Pointer, state C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetState(int32(state))
	return C.int32_t(r)
}

//export goTaskSetMode
func goTaskSetMode(ctx unsafe.Pointer, mode C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetMode(int32(mode))
	return C.int32_t(r)
}

//export goTaskAutoCmd
func goTaskAutoCmd(ctx unsafe.Pointer, cmd C.emccmd_auto_cmd_t, line C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.AutoCmd(emccmdapi.AutoCmd(cmd), int32(line))
	return C.int32_t(r)
}

//export goTaskMdi
func goTaskMdi(ctx unsafe.Pointer, command *C.char) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Mdi(C.GoString(command))
	return C.int32_t(r)
}

//export goTaskJog
func goTaskJog(ctx unsafe.Pointer, jogType C.emccmd_jog_type_t, jjogmode C.bool,
	axisOrJoint C.int32_t, velocity C.double, distance C.double) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Jog(emccmdapi.JogType(jogType), bool(jjogmode), int32(axisOrJoint), float64(velocity), float64(distance))
	return C.int32_t(r)
}

//export goTaskJogStop
func goTaskJogStop(ctx unsafe.Pointer, jjogmode C.bool, axisOrJoint C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.JogStop(bool(jjogmode), int32(axisOrJoint))
	return C.int32_t(r)
}

//export goTaskSpindle
func goTaskSpindle(ctx unsafe.Pointer, cmd C.emccmd_spindle_cmd_t, speed C.double,
	spindleNum C.int32_t, wait C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Spindle(emccmdapi.SpindleCmd(cmd), float64(speed), int32(spindleNum), int32(wait))
	return C.int32_t(r)
}

//export goTaskHome
func goTaskHome(ctx unsafe.Pointer, joint C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Home(int32(joint))
	return C.int32_t(r)
}

//export goTaskUnhome
func goTaskUnhome(ctx unsafe.Pointer, joint C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Unhome(int32(joint))
	return C.int32_t(r)
}

//export goTaskOverrideLimits
func goTaskOverrideLimits(ctx unsafe.Pointer) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.OverrideLimits()
	return C.int32_t(r)
}

//export goTaskTeleopEnable
func goTaskTeleopEnable(ctx unsafe.Pointer, enable C.bool) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.TeleopEnable(bool(enable))
	return C.int32_t(r)
}

//export goTaskSetFeedOverride
func goTaskSetFeedOverride(ctx unsafe.Pointer, rate C.double) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetFeedOverride(float64(rate))
	return C.int32_t(r)
}

//export goTaskSetSpindleOverride
func goTaskSetSpindleOverride(ctx unsafe.Pointer, rate C.double, spindleNum C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetSpindleOverride(float64(rate), int32(spindleNum))
	return C.int32_t(r)
}

//export goTaskSetRapidOverride
func goTaskSetRapidOverride(ctx unsafe.Pointer, rate C.double) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetRapidOverride(float64(rate))
	return C.int32_t(r)
}

//export goTaskSetMaxVelocity
func goTaskSetMaxVelocity(ctx unsafe.Pointer, velocity C.double) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetMaxVelocity(float64(velocity))
	return C.int32_t(r)
}

//export goTaskFlood
func goTaskFlood(ctx unsafe.Pointer, on C.bool) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Flood(bool(on))
	return C.int32_t(r)
}

//export goTaskMist
func goTaskMist(ctx unsafe.Pointer, on C.bool) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Mist(bool(on))
	return C.int32_t(r)
}

//export goTaskBrake
func goTaskBrake(ctx unsafe.Pointer, on C.bool, spindleNum C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Brake(bool(on), int32(spindleNum))
	return C.int32_t(r)
}

//export goTaskLube
func goTaskLube(ctx unsafe.Pointer, on C.bool) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Lube(bool(on))
	return C.int32_t(r)
}

//export goTaskAbort
func goTaskAbort(ctx unsafe.Pointer) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.Abort()
	return C.int32_t(r)
}

//export goTaskTaskPlanSynch
func goTaskTaskPlanSynch(ctx unsafe.Pointer) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.TaskPlanSynch()
	return C.int32_t(r)
}

//export goTaskSetOptionalStop
func goTaskSetOptionalStop(ctx unsafe.Pointer, on C.bool) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetOptionalStop(bool(on))
	return C.int32_t(r)
}

//export goTaskSetBlockDelete
func goTaskSetBlockDelete(ctx unsafe.Pointer, on C.bool) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetBlockDelete(bool(on))
	return C.int32_t(r)
}

//export goTaskLoadToolTable
func goTaskLoadToolTable(ctx unsafe.Pointer) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.LoadToolTable()
	return C.int32_t(r)
}

//export goTaskProgramOpen
func goTaskProgramOpen(ctx unsafe.Pointer, file *C.char) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.ProgramOpen(C.GoString(file))
	return C.int32_t(r)
}

//export goTaskWaitComplete
func goTaskWaitComplete(ctx unsafe.Pointer, timeout C.double) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.WaitComplete(float64(timeout))
	return C.int32_t(r)
}

//export goTaskSetDebug
func goTaskSetDebug(ctx unsafe.Pointer, debug C.int32_t) C.int32_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	r, _ := m.SetDebug(int32(debug))
	return C.int32_t(r)
}

//export goTaskGetStat
func goTaskGetStat(ctx unsafe.Pointer) C.emcstat_stat_full_t {
	m := cgo.Handle(ctx).Value().(*milltaskModule)
	stat, _ := m.GetStat()

	var result C.emcstat_stat_full_t
	if stat == nil {
		result.task.state = C.EMCSTAT_ESTOP
		result.task.mode = C.EMCSTAT_MANUAL
		result.task.interp_state = C.EMCSTAT_IDLE
		result.task.exec_state = C.EMCSTAT_DONE
		return result
	}

	// Task info.
	result.task.mode = C.emcstat_task_mode_t(stat.Task.Mode)
	result.task.state = C.emcstat_task_state_t(stat.Task.State)
	result.task.interp_state = C.emcstat_interp_state_t(stat.Task.InterpState)
	result.task.exec_state = C.emcstat_exec_state_t(stat.Task.ExecState)
	result.task.line = C.int32_t(stat.Task.Line)
	result.task.motion_line = C.int32_t(stat.Task.MotionLine)
	result.task.current_line = C.int32_t(stat.Task.CurrentLine)
	result.task.read_line = C.int32_t(stat.Task.ReadLine)
	result.task.optional_stop = C.bool(stat.Task.OptionalStop)
	result.task.block_delete = C.bool(stat.Task.BlockDelete)
	result.task.task_paused = C.bool(stat.Task.TaskPaused)
	result.task.g5x_index = C.int32_t(stat.Task.G5xIndex)

	// Motion info.
	result.motion.mode = C.emcstat_traj_mode_t(stat.Motion.Mode)
	result.motion.enabled = C.bool(stat.Motion.Enabled)
	result.motion.in_position = C.bool(stat.Motion.InPosition)
	result.motion.paused = C.bool(stat.Motion.Paused)
	result.motion.feedrate = C.double(stat.Motion.Feedrate)
	result.motion.rapidrate = C.double(stat.Motion.Rapidrate)
	result.motion.max_velocity = C.double(stat.Motion.MaxVelocity)
	result.motion.velocity = C.double(stat.Motion.Velocity)
	result.motion.distance_to_go = C.double(stat.Motion.DistanceToGo)
	result.motion.current_vel = C.double(stat.Motion.CurrentVel)

	// Scalar fields.
	result.kinematics_type = C.emcstat_kinematics_type_t(stat.KinematicsType)
	result.joints_count = C.int32_t(stat.JointsCount)
	result.axis_mask = C.int32_t(stat.AxisMask)
	result.flood = C.bool(stat.Flood)
	result.mist = C.bool(stat.Mist)
	result.lube_on = C.bool(stat.LubeOn)
	result.tool_in_spindle = C.int32_t(stat.ToolInSpindle)
	result.pocket_prepped = C.int32_t(stat.PocketPrepped)
	result.linear_units = C.double(stat.LinearUnits)

	// Joints array (C-allocated for halui's emcstat_free).
	if n := len(stat.Joints); n > 0 {
		joints := (*[1 << 20]C.emcstat_joint_info_t)(C.calloc(C.size_t(n), C.size_t(unsafe.Sizeof(C.emcstat_joint_info_t{}))))
		for i := 0; i < n; i++ {
			j := &stat.Joints[i]
			joints[i].homed = C.bool(j.Homed)
			joints[i].homing = C.bool(j.Homing)
			joints[i].enabled = C.bool(j.Enabled)
			joints[i].fault = C.bool(j.Fault)
			joints[i].min_soft_limit = C.double(j.MinSoftLimit)
			joints[i].max_soft_limit = C.double(j.MaxSoftLimit)
			joints[i].min_hard_limit = C.bool(j.MinHardLimit)
			joints[i].max_hard_limit = C.bool(j.MaxHardLimit)
			joints[i].override_limits = C.bool(j.OverrideLimits)
			joints[i].velocity = C.double(j.Velocity)
			joints[i].input = C.double(j.Input)
			joints[i].output = C.double(j.Output)
		}
		result.joints = &joints[0]
		result.joints_len = C.size_t(n)
	}

	// Homed/limit arrays.
	for i := 0; i < int(stat.JointsCount) && i < C.EMCSTAT_MAX_JOINTS; i++ {
		result.homed[i] = C.bool(stat.Homed[i])
		result.limit[i] = C.int32_t(stat.Limit[i])
	}

	return result
}
