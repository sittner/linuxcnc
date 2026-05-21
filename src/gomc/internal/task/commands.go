package task

// This file implements the 27 emccmd GMI methods.
// Each method corresponds to a UI command entry point.
// Pattern: guard → action → state update.

// AutoCmd constants.
const (
	AutoRun     int32 = 0
	AutoPause   int32 = 1
	AutoResume  int32 = 2
	AutoStep    int32 = 3
	AutoReverse int32 = 4
)

// JogType constants.
const (
	JogStop       int32 = 0
	JogContinuous int32 = 1
	JogIncrement  int32 = 2
)

// SpindleCmd constants.
const (
	SpindleOff      int32 = 0
	SpindleForward  int32 = 1
	SpindleReverse  int32 = -1
	SpindleIncrease int32 = 2
	SpindleDecrease int32 = -2
)

// SetState handles state transitions: estop, estop_reset, off, on.
func (t *Task) SetState(state int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	target := TaskState(state)
	switch target {
	case StateEstop:
		t.state = StateEstop
		_ = t.motion.Disable()
		_ = t.io.EstopOn()
		return nil

	case StateEstopReset:
		if t.state != StateEstop {
			return ErrEstop
		}
		t.state = StateEstopReset
		_ = t.io.EstopOff()
		return nil

	case StateOff:
		if err := t.requireNotEstop(); err != nil {
			return err
		}
		t.state = StateOff
		_ = t.motion.Disable()
		return nil

	case StateOn:
		if t.state != StateEstopReset && t.state != StateOff {
			return ErrNotOn
		}
		if err := t.motion.Enable(); err != nil {
			return err
		}
		t.state = StateOn
		return nil
	}
	return nil
}

// SetMode switches between MANUAL, MDI, and AUTO.
func (t *Task) SetMode(mode int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}

	target := TaskMode(mode)
	switch target {
	case ModeManual:
		t.mode = ModeManual
		return t.motion.SetFree()
	case ModeMDI:
		if err := t.requireInterpIdle(); err != nil {
			return err
		}
		t.mode = ModeMDI
		return t.motion.SetCoord()
	case ModeAuto:
		if err := t.requireInterpIdle(); err != nil {
			return err
		}
		t.mode = ModeAuto
		return t.motion.SetCoord()
	}
	return ErrWrongMode
}

// AutoCommand handles run/pause/resume/step/reverse in AUTO mode.
func (t *Task) AutoCommand(cmd int32, line int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if err := t.requireMode(ModeAuto); err != nil {
		return err
	}

	switch cmd {
	case AutoRun:
		if err := t.requireProgram(); err != nil {
			return err
		}
		t.interpState = InterpReading
		// TODO: start interpreter execution goroutine at line
		return nil

	case AutoPause:
		t.interpState = InterpPaused
		return t.motion.Pause()

	case AutoResume:
		t.interpState = InterpReading
		return t.motion.Resume()

	case AutoStep:
		if err := t.requireProgram(); err != nil {
			return err
		}
		// TODO: step one line
		return t.motion.Step(line)

	case AutoReverse:
		return t.motion.Reverse()
	}
	return nil
}

// MDI executes an MDI command string.
func (t *Task) MDI(command string) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if err := t.requireMode(ModeMDI); err != nil {
		return err
	}
	if err := t.requireInterpIdle(); err != nil {
		return err
	}

	t.interpState = InterpReading
	// TODO: feed command to interpreter
	_ = command
	return nil
}

// Jog handles continuous, incremental, and absolute jogs.
func (t *Task) Jog(jogType int32, jjogmode bool, axisOrJoint int32, velocity, distance float64) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.canJog(); err != nil {
		return err
	}

	isTeleop := int32(0)
	if !jjogmode {
		isTeleop = 1
	}

	switch jogType {
	case JogContinuous:
		return t.motion.JogCont(axisOrJoint, velocity, isTeleop)
	case JogIncrement:
		return t.motion.JogIncr(axisOrJoint, velocity, distance, isTeleop)
	}
	return nil
}

// JogStop stops a jog on the specified axis/joint.
func (t *Task) JogStop(jjogmode bool, axisOrJoint int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	isTeleop := int32(0)
	if !jjogmode {
		isTeleop = 1
	}
	return t.motion.JogAbort(axisOrJoint, isTeleop)
}

// Spindle controls spindle on/off/direction/increase/decrease.
func (t *Task) Spindle(cmd int32, speed float64, spindleNum, wait int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}

	switch cmd {
	case SpindleForward:
		return t.motion.SpindleOn(spindleNum, speed, 0, 0, wait)
	case SpindleReverse:
		return t.motion.SpindleOn(spindleNum, -speed, 0, 0, wait)
	case SpindleOff:
		return t.motion.SpindleOff(spindleNum)
	case SpindleIncrease:
		return t.motion.SpindleIncrease(spindleNum)
	case SpindleDecrease:
		return t.motion.SpindleDecrease(spindleNum)
	}
	return nil
}

// Home initiates homing for the specified joint.
func (t *Task) Home(joint int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.JointHome(joint)
}

// Unhome un-homes the specified joint.
func (t *Task) Unhome(joint int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.JointUnhome(joint)
}

// OverrideLimits temporarily overrides soft limits for homing.
func (t *Task) OverrideLimits() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.OverrideLimits(0) // joint 0 = all
}

// TeleopEnable enables/disables teleop mode.
func (t *Task) TeleopEnable(enable bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if enable {
		return t.motion.SetTeleop()
	}
	return t.motion.SetFree()
}

// SetFeedOverride sets the feed override percentage.
func (t *Task) SetFeedOverride(rate float64) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.SetFeedScale(rate)
}

// SetSpindleOverride sets spindle speed override.
func (t *Task) SetSpindleOverride(rate float64, spindleNum int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.SetSpindleScale(spindleNum, rate)
}

// SetRapidOverride sets the rapid override percentage.
func (t *Task) SetRapidOverride(rate float64) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.SetRapidScale(rate)
}

// SetMaxVelocity sets the maximum trajectory velocity.
func (t *Task) SetMaxVelocity(velocity float64) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	return t.motion.SetVelLimit(velocity)
}

// Flood turns flood coolant on or off.
func (t *Task) Flood(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if on {
		return t.io.CoolantFloodOn()
	}
	return t.io.CoolantFloodOff()
}

// Mist turns mist coolant on or off.
func (t *Task) Mist(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if on {
		return t.io.CoolantMistOn()
	}
	return t.io.CoolantMistOff()
}

// Brake engages/disengages spindle brake.
func (t *Task) Brake(on bool, spindleNum int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if on {
		return t.motion.SpindleBrakeEngage(spindleNum)
	}
	return t.motion.SpindleBrakeRelease(spindleNum)
}

// Lube turns lubrication on or off.
func (t *Task) Lube(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if on {
		return t.io.LubeOn()
	}
	return t.io.LubeOff()
}

// Abort aborts all motion and interpreter execution.
func (t *Task) Abort() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.interpState = InterpIdle
	t.execState = ExecDone
	return t.motion.Abort()
}

// TaskPlanSynch forces a sync between interpreter and motion.
func (t *Task) TaskPlanSynch() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// TODO: sync interpreter position with motion actual position
	return nil
}

// SetOptionalStop enables/disables optional stop (M1).
func (t *Task) SetOptionalStop(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// TODO: store in interpreter settings
	_ = on
	return nil
}

// SetBlockDelete enables/disables block delete (/).
func (t *Task) SetBlockDelete(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// TODO: store in interpreter settings
	_ = on
	return nil
}

// LoadToolTable reloads the tool table from file.
func (t *Task) LoadToolTable() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// TODO: reload tool table and notify interpreter
	return nil
}

// ProgramOpen opens a G-code program file.
func (t *Task) ProgramOpen(file string) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if err := t.requireMode(ModeAuto); err != nil {
		return err
	}
	if err := t.requireInterpIdle(); err != nil {
		return err
	}

	t.programFile = file
	t.programOpen = true
	// TODO: open file in interpreter
	return nil
}

// WaitComplete waits for motion to complete (with timeout).
func (t *Task) WaitComplete(timeout float64) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// TODO: wait for execState == ExecDone or timeout
	_ = timeout
	return nil
}

// SetDebug sets the debug level.
func (t *Task) SetDebug(debug int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	return t.motion.SetDebug(debug)
}
