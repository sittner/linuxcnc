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

// --- EmccmdCallbacks implementation ---
// These delegate to Task methods once Start() has run.

func (m *milltaskModule) SetState(state int32) (int32, error) {
	if m.task == nil {
		return 0, errNotReady
	}
	return 0, m.task.SetState(state)
}

func (m *milltaskModule) SetMode(mode int32) (int32, error) {
	if m.task == nil {
		return 0, errNotReady
	}
	return 0, m.task.SetMode(mode)
}

func (m *milltaskModule) AutoCmd(cmd emccmdapi.AutoCmd, line int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Mdi(command string) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Jog(jogType emccmdapi.JogType, jjogmode bool, axisOrJoint int32, velocity float64, distance float64) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) JogStop(jjogmode bool, axisOrJoint int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Spindle(cmd emccmdapi.SpindleCmd, speed float64, spindleNum int32, wait int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Home(joint int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Unhome(joint int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) OverrideLimits() (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) TeleopEnable(enable bool) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetFeedOverride(rate float64) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetSpindleOverride(rate float64, spindleNum int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetRapidOverride(rate float64) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetMaxVelocity(velocity float64) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Flood(on bool) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Mist(on bool) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Brake(on bool, spindleNum int32) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Lube(on bool) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) Abort() (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) TaskPlanSynch() (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetOptionalStop(on bool) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetBlockDelete(on bool) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) LoadToolTable() (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) ProgramOpen(file string) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) WaitComplete(timeout float64) (int32, error) {
	return 0, errNotReady
}

func (m *milltaskModule) SetDebug(debug int32) (int32, error) {
	return 0, errNotReady
}

// --- EmcstatCallbacks implementation ---

func (m *milltaskModule) GetStat() (*emcstatapi.StatFull, error) {
	// Return minimal valid stat — enough for halui to start polling.
	return &emcstatapi.StatFull{
		Task: emcstatapi.StatTaskInfo{
			Mode:        emcstatapi.TaskMode_MANUAL,
			State:       emcstatapi.TaskState_ESTOP,
			InterpState: emcstatapi.InterpState_IDLE,
			ExecState:   emcstatapi.ExecState_DONE,
		},
	}, nil
}
