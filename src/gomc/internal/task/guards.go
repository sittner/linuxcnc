package task

import "fmt"

// Guard errors returned when a command is rejected due to state/mode.
var (
	ErrNotOn     = fmt.Errorf("machine not on")
	ErrWrongMode = fmt.Errorf("wrong mode for command")
	ErrEstop     = fmt.Errorf("machine in estop")
	ErrBusy      = fmt.Errorf("interpreter busy")
	ErrNoProgram = fmt.Errorf("no program loaded")
)

// requireState checks that the machine is in the required state.
func (t *Task) requireState(required TaskState) error {
	if t.state != required {
		return fmt.Errorf("%w: need %s, have %s", ErrNotOn, required, t.state)
	}
	return nil
}

// requireOn checks that the machine is powered on.
func (t *Task) requireOn() error {
	if t.state != StateOn {
		return fmt.Errorf("%w: state is %s", ErrNotOn, t.state)
	}
	return nil
}

// requireMode checks that the machine is in the specified mode.
func (t *Task) requireMode(required TaskMode) error {
	if t.mode != required {
		return fmt.Errorf("%w: need %s, have %s", ErrWrongMode, required, t.mode)
	}
	return nil
}

// requireNotEstop checks that we are not in estop.
func (t *Task) requireNotEstop() error {
	if t.state == StateEstop {
		return ErrEstop
	}
	return nil
}

// requireInterpIdle checks that the interpreter is idle (for jog-while-idle).
func (t *Task) requireInterpIdle() error {
	if t.interpState != InterpIdle {
		return ErrBusy
	}
	return nil
}

// requireProgram checks that a program file is loaded (for AUTO RUN).
func (t *Task) requireProgram() error {
	if !t.programOpen {
		return ErrNoProgram
	}
	return nil
}

// canJog returns true if jogging is allowed in current state.
// Jogging is allowed in MANUAL mode, or in AUTO/MDI when interpreter is idle.
func (t *Task) canJog() error {
	if err := t.requireOn(); err != nil {
		return err
	}
	switch t.mode {
	case ModeManual:
		return nil
	case ModeAuto, ModeMDI:
		// Allow jog while idle (not running a program or MDI)
		return t.requireInterpIdle()
	default:
		return ErrWrongMode
	}
}
