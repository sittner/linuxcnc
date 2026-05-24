package task

import (
	"fmt"
	"time"
)

// mcodeAbort signals the M-code handler worker to stop.
func (t *Task) mcodeAbort() {
	if t.mcode != nil {
		t.mcode.Abort()
	}
}

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
		if t.state == StateEstopReset {
			return nil // idempotent
		}
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
		if t.state == StateOn {
			return nil // idempotent
		}
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

// ProgramOpen opens a G-code file for execution.
func (t *Task) ProgramOpen(file string) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// No state/mode guards — the C milltask allows program-open in any
	// state (including ESTOP) and any mode. It's just loading a file.
	if t.interp != nil {
		// Close any previously open file before opening a new one.
		_ = t.interp.Close()
		if err := t.interp.Open(file); err != nil {
			return err
		}
	}
	t.programFile = file
	t.programOpen = true
	return nil
}

// AutoCommand handles run/pause/resume/step/reverse in AUTO mode.
func (t *Task) AutoCommand(cmd int32, line int32) error {
	t.mu.Lock()

	if err := t.requireOn(); err != nil {
		t.mu.Unlock()
		return err
	}
	if err := t.requireMode(ModeAuto); err != nil {
		t.mu.Unlock()
		return err
	}

	switch cmd {
	case AutoRun:
		if err := t.requireProgram(); err != nil {
			t.mu.Unlock()
			return err
		}
		if t.interp == nil {
			t.mu.Unlock()
			return fmt.Errorf("no interpreter configured")
		}
		t.interpState = InterpReading
		// Set up pause/resume channels for this run
		t.pauseCh = make(chan struct{})
		t.resumeCh = make(chan struct{})
		interp := t.interp
		startLine := line
		t.mu.Unlock()
		// Synch interpreter with current machine position
		if err := interp.Synch(); err != nil {
			t.logger.Error("interp synch failed before run", "err", err)
		}
		go t.runProgram(interp, startLine)
		return nil

	case AutoPause:
		t.interpState = InterpPaused
		// Signal interpreter goroutine to pause
		if t.pauseCh != nil {
			select {
			case <-t.pauseCh:
			default:
				close(t.pauseCh)
			}
		}
		t.mu.Unlock()
		return t.motion.Pause()

	case AutoResume:
		t.interpState = InterpReading
		// Signal interpreter goroutine to resume
		if t.resumeCh != nil {
			select {
			case <-t.resumeCh:
			default:
				close(t.resumeCh)
			}
		}
		t.mu.Unlock()
		return t.motion.Resume()

	case AutoStep:
		if err := t.requireProgram(); err != nil {
			t.mu.Unlock()
			return err
		}
		t.mu.Unlock()
		// TODO: step one line
		return t.motion.Step(line)

	case AutoReverse:
		t.mu.Unlock()
		return t.motion.Reverse()
	}
	t.mu.Unlock()
	return nil
}

// MDI executes an MDI command string.
func (t *Task) MDI(command string) error {
	t.mu.Lock()

	if err := t.requireOn(); err != nil {
		t.mu.Unlock()
		return err
	}
	if err := t.requireMode(ModeMDI); err != nil {
		t.mu.Unlock()
		return err
	}
	if err := t.requireInterpIdle(); err != nil {
		t.mu.Unlock()
		return err
	}
	if t.interp == nil {
		t.mu.Unlock()
		return fmt.Errorf("no interpreter configured")
	}

	t.interpState = InterpReading
	interp := t.interp
	t.mu.Unlock()

	// Set active canon for M-code callbacks (no ctx parameter).
	setActiveCanon(t.canon)

	// Synch interpreter with current machine position before MDI.
	if err := interp.Synch(); err != nil {
		t.logger.Error("interp synch failed before MDI", "err", err)
	}

	// Execute the MDI string — this triggers canon callbacks that enqueue
	// motion commands to the sequencer.
	rc, err := interp.ExecuteString(command)
	t.updateActiveCodes(interp)
	if err != nil {
		t.setInterpState(InterpIdle)
		return fmt.Errorf("MDI execute: %w", err)
	}

	switch rc {
	case InterpError:
		t.setInterpState(InterpIdle)
		return fmt.Errorf("MDI interpreter error")
	case InterpExecuteFinish:
		// MDI needs motion to finish before returning to idle
		t.EnqueueCmd(&interpDoneCmd{})
	default:
		// Normal completion — still wait for queued motion
		t.EnqueueCmd(&interpDoneCmd{})
	}
	return nil
}

// runProgram runs the interpreter read/execute loop for the open program.
// Called in a goroutine from AutoRun.
//
// Flow control:
// - Checks abort between lines (sequencer abort = program cancel)
// - Checks pause between lines (blocks until resume or abort)
// - Handles INTERP_EXECUTE_FINISH (wait for motion to drain before continuing)
func (t *Task) runProgram(interp Interpreter, startLine int32) {
	_ = startLine // TODO: seek to startLine

	// Set active canon for M-code callbacks (no ctx parameter).
	setActiveCanon(t.canon)
	defer clearActiveCanon()

	for {
		// Check for abort and pause between interpreter lines
		if t.checkAbortOrPause() {
			t.setInterpState(InterpIdle)
			return
		}

		rc, err := interp.Read()
		if err != nil {
			t.logger.Error("interpreter read error", "err", err, "rc", rc)
			t.setInterpState(InterpIdle)
			return
		}
		if rc == InterpEndfile {
			// End of file — enqueue done marker and exit.
			t.EnqueueCmd(&interpDoneCmd{})
			return
		}
		if rc == InterpExit {
			// M2/M30 signalled at read time — enqueue done and exit.
			t.EnqueueCmd(&interpDoneCmd{})
			return
		}

		rc, err = interp.Execute()
		if err != nil {
			t.logger.Error("interpreter execute error", "err", err, "rc", rc)
			t.setInterpState(InterpIdle)
			return
		}
		t.updateActiveCodes(interp)

		switch rc {
		case InterpExecuteFinish:
			// Interpreter says "wait for motion/IO to complete before
			// continuing" (tool change, probe, dwell, M-code, etc.)
			t.EnqueueCmd(waitForMotionSingleton)
			// Also wait for the sequencer to actually drain before
			// reading the next line (backpressure).
			if t.waitSequencerDrain() {
				return // aborted
			}
			// Synch interpreter with machine state after wait
			if err := interp.Synch(); err != nil {
				t.logger.Error("interp synch after execute_finish", "err", err)
			}
		case InterpExit:
			// M2/M30 program end — enqueue done marker and let
			// sequencer drain remaining motion before marking idle.
			t.EnqueueCmd(&interpDoneCmd{})
			return
		case InterpError:
			t.logger.Error("interpreter error", "rc", rc)
			t.setInterpState(InterpIdle)
			return
		case InterpOK:
			// Normal — continue reading
		}
	}
}

// checkAbortOrPause checks the abort channel and, if paused, blocks until
// resumed or aborted. Returns true if aborted (caller should return).
func (t *Task) checkAbortOrPause() bool {
	t.mu.Lock()
	abort := t.seqAbort
	pauseCh := t.pauseCh
	resumeCh := t.resumeCh
	t.mu.Unlock()

	// Check abort first
	select {
	case <-abort:
		return true
	default:
	}

	// Check if paused
	select {
	case <-pauseCh:
		// We're paused — wait for resume or abort
		select {
		case <-abort:
			return true
		case <-resumeCh:
			// Allocate fresh channels for next pause/resume cycle
			t.mu.Lock()
			t.pauseCh = make(chan struct{})
			t.resumeCh = make(chan struct{})
			t.mu.Unlock()
			return false
		}
	default:
		return false
	}
}

// waitSequencerDrain waits until the sequencer queue is empty and the
// sequencer is idle (ExecDone). Returns true if aborted.
func (t *Task) waitSequencerDrain() bool {
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	for {
		t.mu.Lock()
		abort := t.seqAbort
		qLen := len(t.interpQueue)
		exec := t.execState
		t.mu.Unlock()

		if qLen == 0 && exec == ExecDone {
			return false
		}

		select {
		case <-abort:
			return true
		case <-ticker.C:
		}
	}
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
	case JogStop:
		return t.motion.JogAbort(axisOrJoint, isTeleop)
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
		if err := t.io.CoolantFloodOn(); err != nil {
			return err
		}
		t.floodOn = true
		return nil
	}
	if err := t.io.CoolantFloodOff(); err != nil {
		return err
	}
	t.floodOn = false
	return nil
}

// Mist turns mist coolant on or off.
func (t *Task) Mist(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if err := t.requireOn(); err != nil {
		return err
	}
	if on {
		if err := t.io.CoolantMistOn(); err != nil {
			return err
		}
		t.mistOn = true
		return nil
	}
	if err := t.io.CoolantMistOff(); err != nil {
		return err
	}
	t.mistOn = false
	return nil
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
// Matches C milltask emcTaskAbort behavior: abort motion, abort IO,
// stop spindles, turn off coolant, clear interpreter queue, close program.
func (t *Task) Abort() error {
	t.mu.Lock()

	t.interpState = InterpIdle
	t.execState = ExecDone

	// Capture state before unlock
	numSpindles := t.numSpindles
	interp := t.interp
	t.mu.Unlock()

	// Abort sequencer (signals goroutine, drains queue)
	t.AbortSequencer()

	// Abort M-code handler if running
	t.mcodeAbort()

	// Abort motion
	_ = t.motion.Abort()

	// Abort IO controller
	_ = t.io.IoAbort(0)

	// Stop all spindles
	for i := 0; i < numSpindles; i++ {
		_ = t.motion.SpindleOff(int32(i))
	}

	// Turn off coolant
	_ = t.io.CoolantFloodOff()
	_ = t.io.CoolantMistOff()

	t.mu.Lock()
	t.floodOn = false
	t.mistOn = false
	t.mu.Unlock()

	// Notify interpreter of abort and close file
	if interp != nil {
		_ = interp.Abort(0, "user abort")
		_ = interp.Close()
		_ = interp.Reset()
	}

	// Restart sequencer for next operation
	t.StartSequencer()

	return nil
}

// TaskPlanSynch forces a sync between interpreter and motion.
func (t *Task) TaskPlanSynch() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.interp == nil {
		return nil
	}
	return t.interp.Synch()
}

// SetOptionalStop enables/disables optional stop (M1).
func (t *Task) SetOptionalStop(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.optionalStop = on
	return nil
}

// SetBlockDelete enables/disables block delete (/).
func (t *Task) SetBlockDelete(on bool) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.blockDelete = on
	return nil
}

// LoadToolTable reloads the tool table from file.
func (t *Task) LoadToolTable() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	// TODO: reload tool table and notify interpreter
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
