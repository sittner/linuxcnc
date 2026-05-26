package task

import (
	"fmt"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emccmdapi"
	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstatapi"
)

// Compile-time interface checks.
var _ emccmdapi.EmccmdCallbacks = (*milltaskModule)(nil)
var _ emcstatapi.EmcstatCallbacks = (*milltaskModule)(nil)

// errNotReady is returned by command handlers before Start() completes.
var errNotReady = fmt.Errorf("milltask: not ready")

// RCS status codes returned to C callers (halui, etc.)
// The C milltask returns RCS_DONE(1) on success; match that for compatibility.
const (
	rcsDone  int32 = 1 // RCS_DONE: command completed
	rcsExec  int32 = 2 // RCS_EXEC: command accepted, executing
	rcsError int32 = 3 // RCS_ERROR: command rejected
)

func (m *milltaskModule) ready() error {
	if m.task == nil || m.stopped {
		return errNotReady
	}
	return nil
}

// --- EmccmdCallbacks implementation ---

func (m *milltaskModule) SetState(state int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetState(state)
}

func (m *milltaskModule) SetMode(mode int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetMode(mode)
}

func (m *milltaskModule) AutoCmd(cmd emccmdapi.AutoCmd, line int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.AutoCommand(int32(cmd), line)
}

func (m *milltaskModule) Mdi(command string) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.MDI(command)
}

func (m *milltaskModule) Jog(jogType emccmdapi.JogType, jjogmode bool, axisOrJoint int32, velocity float64, distance float64) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Jog(int32(jogType), jjogmode, axisOrJoint, velocity, distance)
}

func (m *milltaskModule) JogStop(jjogmode bool, axisOrJoint int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.JogStop(jjogmode, axisOrJoint)
}

func (m *milltaskModule) Spindle(cmd emccmdapi.SpindleCmd, speed float64, spindleNum int32, wait int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Spindle(int32(cmd), speed, spindleNum, wait)
}

func (m *milltaskModule) Home(joint int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Home(joint)
}

func (m *milltaskModule) Unhome(joint int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Unhome(joint)
}

func (m *milltaskModule) OverrideLimits() (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.OverrideLimits()
}

func (m *milltaskModule) TeleopEnable(enable bool) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.TeleopEnable(enable)
}

func (m *milltaskModule) SetFeedOverride(rate float64) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetFeedOverride(rate)
}

func (m *milltaskModule) SetSpindleOverride(rate float64, spindleNum int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetSpindleOverride(rate, spindleNum)
}

func (m *milltaskModule) SetRapidOverride(rate float64) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetRapidOverride(rate)
}

func (m *milltaskModule) SetMaxVelocity(velocity float64) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetMaxVelocity(velocity)
}

func (m *milltaskModule) Flood(on bool) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Flood(on)
}

func (m *milltaskModule) Mist(on bool) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Mist(on)
}

func (m *milltaskModule) Brake(on bool, spindleNum int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Brake(on, spindleNum)
}

func (m *milltaskModule) Lube(on bool) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Lube(on)
}

func (m *milltaskModule) Abort() (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.Abort()
}

func (m *milltaskModule) TaskPlanSynch() (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.TaskPlanSynch()
}

func (m *milltaskModule) SetOptionalStop(on bool) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetOptionalStop(on)
}

func (m *milltaskModule) SetBlockDelete(on bool) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetBlockDelete(on)
}

func (m *milltaskModule) LoadToolTable() (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.LoadToolTable()
}

func (m *milltaskModule) ProgramOpen(file string) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.ProgramOpen(file)
}

func (m *milltaskModule) WaitComplete(timeout float64) (int32, error) {
	// Always report done for now (no motion queue to wait on).
	return rcsDone, nil
}

func (m *milltaskModule) SetDebug(debug int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetDebug(debug)
}

func (m *milltaskModule) SetJogAxis(axis int32) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetJogAxis(axis)
}

func (m *milltaskModule) SetJogIncrement(increment float64) (int32, error) {
	if err := m.ready(); err != nil {
		return rcsError, err
	}
	return rcsDone, m.task.SetJogIncrement(increment)
}

// --- EmcstatCallbacks implementation ---

func (m *milltaskModule) GetStat() (*emcstatapi.StatFull, error) {
	t := m.task
	if t == nil {
		return &emcstatapi.StatFull{
			Task: emcstatapi.StatTaskInfo{
				State:       emcstatapi.TaskState_ESTOP,
				Mode:        emcstatapi.TaskMode_MANUAL,
				InterpState: emcstatapi.InterpState_IDLE,
				ExecState:   emcstatapi.ExecState_DONE,
			},
		}, nil
	}
	stat := t.BuildStat()
	return stat, nil
}
