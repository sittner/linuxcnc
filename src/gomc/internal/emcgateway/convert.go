package emcgateway

// C stat struct → Go stat struct conversion.

/*
#include "nml_shim.h"
*/
import "C"

import (
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstatapi"
)

const (
	maxJoints   = C.NML_SHIM_MAX_JOINTS
	maxSpindles = C.NML_SHIM_MAX_SPINDLES
	maxAxis     = C.NML_SHIM_MAX_AXIS
)

func convertPos(p *C.nml_position_t) emcstatapi.Position {
	return emcstatapi.Position{
		X: float64(p.x), Y: float64(p.y), Z: float64(p.z),
		A: float64(p.a), B: float64(p.b), C: float64(p.c),
		U: float64(p.u), V: float64(p.v), W: float64(p.w),
	}
}

func convertTask(cs *C.nml_stat_t) emcstatapi.StatTaskInfo {
	return emcstatapi.StatTaskInfo{
		Mode:              emcstatapi.TaskMode(cs.task_mode),
		State:             emcstatapi.TaskState(cs.task_state),
		InterpState:       emcstatapi.InterpState(cs.interp_state),
		ExecState:         emcstatapi.ExecState(cs.exec_state),
		File:              C.GoString(&cs.file[0]),
		Command:           C.GoString(&cs.command[0]),
		Line:              int32(cs.line),
		MotionLine:        int32(cs.motion_line),
		CurrentLine:       int32(cs.current_line),
		ReadLine:          int32(cs.read_line),
		QueuedMdiCommands: int32(cs.queued_mdi_commands),
		OptionalStop:      cs.optional_stop != 0,
		BlockDelete:       cs.block_delete != 0,
		TaskPaused:        cs.task_paused != 0,
		G5xIndex:          int32(cs.g5x_index),
	}
}

func convertMotion(cs *C.nml_stat_t) emcstatapi.StatMotionInfo {
	return emcstatapi.StatMotionInfo{
		Mode:         emcstatapi.TrajMode(cs.motion_mode),
		Enabled:      cs.motion_enabled != 0,
		InPosition:   cs.in_position != 0,
		Paused:       cs.motion_paused != 0,
		Feedrate:     float64(cs.feedrate),
		Rapidrate:    float64(cs.rapidrate),
		MaxVelocity:  float64(cs.max_velocity),
		Velocity:     float64(cs.velocity),
		DistanceToGo: float64(cs.distance_to_go),
		Dtg:          convertPos(&cs.dtg),
		CurrentVel:   float64(cs.current_vel),
		MotionId:     int32(cs.motion_id),
		MotionLine:   int32(cs.motion_line_traj),
	}
}

func convertJoints(cs *C.nml_stat_t) []emcstatapi.JointInfo {
	jointsCount := int(cs.joints_count)
	if jointsCount > maxJoints {
		jointsCount = maxJoints
	}
	joints := make([]emcstatapi.JointInfo, jointsCount)
	for i := 0; i < jointsCount; i++ {
		j := &cs.joints[i]
		joints[i] = emcstatapi.JointInfo{
			Homed:          j.homed != 0,
			Homing:         j.homing != 0,
			Enabled:        j.enabled != 0,
			Fault:          j.fault != 0,
			MinSoftLimit:   float64(j.min_soft_limit),
			MaxSoftLimit:   float64(j.max_soft_limit),
			MinHardLimit:   j.min_hard_limit != 0,
			MaxHardLimit:   j.max_hard_limit != 0,
			OverrideLimits: j.override_limits != 0,
			Velocity:       float64(j.velocity),
			Input:          float64(j.input),
			Output:         float64(j.output),
			Limit:          int32(j.limit),
		}
	}
	return joints
}

func convertSpindles(cs *C.nml_stat_t) []emcstatapi.SpindleInfo {
	spindles := make([]emcstatapi.SpindleInfo, maxSpindles)
	for i := 0; i < maxSpindles; i++ {
		sp := &cs.spindle[i]
		spindles[i] = emcstatapi.SpindleInfo{
			Speed:           float64(sp.speed),
			Direction:       int32(sp.direction),
			Brake:           sp.brake != 0,
			Enabled:         sp.enabled != 0,
			Override:        float64(sp.override),
			OverrideEnabled: sp.override_enabled != 0,
			Homed:           sp.homed != 0,
			OrientState:     int32(sp.orient_state),
			OrientFault:     int32(sp.orient_fault),
		}
	}
	return spindles
}

func convertAxes(cs *C.nml_stat_t) []emcstatapi.AxisInfo {
	axes := make([]emcstatapi.AxisInfo, maxAxis)
	for i := 0; i < maxAxis; i++ {
		ax := &cs.axis[i]
		axes[i] = emcstatapi.AxisInfo{
			Velocity:         float64(ax.velocity),
			MinPositionLimit: float64(ax.min_position_limit),
			MaxPositionLimit: float64(ax.max_position_limit),
		}
	}
	return axes
}

func convertStat(cs *C.nml_stat_t) *emcstatapi.StatFull {
	s := &emcstatapi.StatFull{}

	s.Task = convertTask(cs)
	s.Motion = convertMotion(cs)

	// Positions
	s.Position = convertPos(&cs.position)
	s.ActualPosition = convertPos(&cs.actual_position)
	s.ProbedPosition = convertPos(&cs.probed_position)
	s.G5xOffset = convertPos(&cs.g5x_offset)
	s.G92Offset = convertPos(&cs.g92_offset)
	s.ToolOffset = convertPos(&cs.tool_offset)
	s.RotationXy = float64(cs.rotation_xy)

	// Joint actual positions
	for i := 0; i < maxJoints; i++ {
		s.JointActualPosition[i] = float64(cs.joint_actual_position[i])
	}

	s.Joints = convertJoints(cs)
	s.Spindle = convertSpindles(cs)
	s.Axis = convertAxes(cs)

	// G-codes, M-codes, settings
	s.ActiveGcodes = make([]int32, C.NML_SHIM_ACTIVE_G_CODES)
	for i := 0; i < int(C.NML_SHIM_ACTIVE_G_CODES); i++ {
		s.ActiveGcodes[i] = int32(cs.active_gcodes[i])
	}
	s.ActiveMcodes = make([]int32, C.NML_SHIM_ACTIVE_M_CODES)
	for i := 0; i < int(C.NML_SHIM_ACTIVE_M_CODES); i++ {
		s.ActiveMcodes[i] = int32(cs.active_mcodes[i])
	}
	s.ActiveSettings = make([]float64, C.NML_SHIM_ACTIVE_SETTINGS)
	for i := 0; i < int(C.NML_SHIM_ACTIVE_SETTINGS); i++ {
		s.ActiveSettings[i] = float64(cs.active_settings[i])
	}

	// Scalars
	s.KinematicsType = emcstatapi.KinematicsType(cs.kinematics_type)
	s.JointsCount = int32(cs.joints_count)
	s.NumExtrajoints = int32(cs.num_extrajoints)
	s.AxisMask = int32(cs.axis_mask)
	s.Flood = cs.flood != 0
	s.Mist = cs.mist != 0
	s.ToolInSpindle = int32(cs.tool_in_spindle)
	s.PocketPrepped = int32(cs.pocket_prepped)
	s.LinearUnits = float64(cs.linear_units)
	s.State = int32(cs.state)
	s.Debug = int32(cs.debug)

	// Per-joint homed/limit arrays
	for i := 0; i < maxJoints; i++ {
		s.Homed[i] = cs.homed[i] != 0
		s.Limit[i] = int32(cs.limit[i])
	}

	return s
}
