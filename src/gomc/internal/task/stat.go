package task

import (
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstatapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/motstat"
)

// BuildStat constructs a complete StatFull from task state + motion status.
// This is the single source of truth for all stat consumers (REST, WS, halui).
func (t *Task) BuildStat() *emcstatapi.StatFull {
	t.mu.Lock()
	stat := &emcstatapi.StatFull{
		Task: emcstatapi.StatTaskInfo{
			Mode:         emcstatapi.TaskMode(t.mode),
			State:        emcstatapi.TaskState(t.state),
			InterpState:  emcstatapi.InterpState(t.interpState),
			ExecState:    emcstatapi.ExecState(t.execState),
			OptionalStop: t.optionalStop,
			BlockDelete:  t.blockDelete,
		},
		JointsCount:    int32(t.numJoints),
		AxisMask:       t.axisMask,
		LinearUnits:    t.linearUnits,
		KinematicsType: emcstatapi.KinematicsType_IDENTITY,
		ActiveGcodes:   append([]int32(nil), t.activeGcodes...),
		ActiveSettings: append([]float64(nil), t.activeSettings...),
	}
	numJoints := t.numJoints
	numSpindles := t.numSpindles
	t.mu.Unlock()

	// Read motion status (lock-free, reads from shared memory).
	ms, err := t.status.GetStatus()
	if err != nil {
		// Return what we have from task state alone.
		return stat
	}

	// Kinematics type from motion module.
	stat.KinematicsType = emcstatapi.KinematicsType(ms.KinType)

	// Motion info.
	stat.Motion.Enabled = ms.Enabled != 0
	stat.Motion.InPosition = ms.Inpos != 0
	stat.Motion.Paused = ms.Paused != 0
	stat.Motion.Feedrate = ms.FeedScale
	stat.Motion.Rapidrate = ms.RapidScale
	stat.Motion.MaxVelocity = ms.LimitVel
	stat.Motion.Velocity = ms.RequestedVel
	stat.Motion.CurrentVel = ms.CurrentVel
	stat.Motion.DistanceToGo = ms.DistanceToGo
	stat.Motion.MotionId = ms.Id
	stat.Motion.MotionType = ms.MotionType
	stat.Motion.Dtg = emcstatapi.Position{
		X: ms.Dtg.X, Y: ms.Dtg.Y, Z: ms.Dtg.Z,
		A: ms.Dtg.A, B: ms.Dtg.B, C: ms.Dtg.C,
		U: ms.Dtg.U, V: ms.Dtg.V, W: ms.Dtg.W,
	}

	// Positions.
	stat.Position = poseToPosition(ms.CartePosCmd)
	stat.ActualPosition = poseToPosition(ms.CartePosFb)
	stat.ToolOffset = poseToPosition(ms.ToolOffset)
	stat.ProbedPosition = poseToPosition(ms.Probe.Pos)

	// Joint actual positions (feedback).
	for i := 0; i < numJoints && i < 16; i++ {
		stat.JointActualPosition[i] = ms.Joints[i].PosFb
	}

	// Joints array.
	if numJoints > 0 {
		stat.Joints = make([]emcstatapi.JointInfo, numJoints)
		for i := 0; i < numJoints; i++ {
			j := &ms.Joints[i]
			stat.Joints[i] = emcstatapi.JointInfo{
				Homed:          j.Homed != 0,
				Homing:         j.Homing != 0,
				Enabled:        j.Enabled != 0,
				Fault:          j.Fault != 0,
				MinSoftLimit:   j.MinPosLimit,
				MaxSoftLimit:   j.MaxPosLimit,
				MinHardLimit:   j.OnNegLimit != 0,
				MaxHardLimit:   j.OnPosLimit != 0,
				OverrideLimits: false, // TODO: from override_limit_mask
				Velocity:       j.VelCmd,
				Input:          j.PosFb,
				Output:         j.PosCmd,
			}
			stat.Homed[i] = j.Homed != 0
			if j.OnPosLimit != 0 {
				stat.Limit[i] = 1
			} else if j.OnNegLimit != 0 {
				stat.Limit[i] = -1
			}
		}
	}

	// Axes array (from axis_mask).
	nAxes := countAxes(stat.AxisMask)
	if nAxes > 0 {
		stat.Axis = make([]emcstatapi.AxisInfo, nAxes)
		for i := 0; i < nAxes && i < 9; i++ {
			ax := &ms.Axes[i]
			stat.Axis[i] = emcstatapi.AxisInfo{
				MinPositionLimit: ax.MinPosLimit,
				MaxPositionLimit: ax.MaxPosLimit,
				Velocity:         ax.VelLimit,
			}
		}
	}

	// Spindles.
	if numSpindles > 0 {
		stat.Spindle = make([]emcstatapi.SpindleInfo, numSpindles)
		for i := 0; i < numSpindles && i < 8; i++ {
			sp := &ms.Spindles[i]
			stat.Spindle[i] = emcstatapi.SpindleInfo{
				Speed:           sp.Speed,
				Direction:       sp.Direction,
				Brake:           sp.Brake != 0,
				Enabled:         sp.State != 0,
				Override:        sp.Scale,
				OverrideEnabled: true, // always enabled in our implementation
				Homed:           sp.Homed != 0,
				OrientState:     sp.OrientState,
				OrientFault:     sp.OrientFault,
			}
		}
	}

	return stat
}

// poseToPosition converts a motstat.Pose to emcstatapi.Position.
func poseToPosition(p motstat.Pose) emcstatapi.Position {
	return emcstatapi.Position{
		X: p.X, Y: p.Y, Z: p.Z,
		A: p.A, B: p.B, C: p.C,
		U: p.U, V: p.V, W: p.W,
	}
}

// countAxes returns the number of set bits in axis_mask.
func countAxes(mask int32) int {
	n := 0
	for m := uint32(mask); m != 0; m >>= 1 {
		n += int(m & 1)
	}
	return n
}
