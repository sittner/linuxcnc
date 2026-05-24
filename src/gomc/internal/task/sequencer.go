package task

import (
	"context"
	"errors"
	"fmt"
	"time"
)

// WaitType describes what a queued command waits for after execution.
type WaitType int

const (
	WaitNone            WaitType = iota
	WaitMotion                   // wait for motion queue to drain
	WaitIO                       // wait for IO acknowledgment
	WaitMotionAndIO              // wait for both
	WaitDelay                    // timed dwell
	WaitSpindleOriented          // wait for orient complete
)

// QueuedCmd is one interpreter-generated action queued for sequential execution.
type QueuedCmd interface {
	// Execute performs the command (e.g. calls motctl.SetLine).
	Execute(t *Task) error
	// Wait returns what to wait for after Execute completes.
	Wait() WaitType
	// String returns a description for logging/debugging.
	String() string
}

// SeqError is returned when the sequencer encounters an error.
type SeqError struct {
	Cmd QueuedCmd
	Err error
}

func (e *SeqError) Error() string {
	return fmt.Sprintf("sequencer: %s: %v", e.Cmd, e.Err)
}

func (e *SeqError) Unwrap() error { return e.Err }

const interpQueueSize = 64

// StartSequencer launches the sequencer goroutine. Must be called with mu NOT held.
func (t *Task) StartSequencer() {
	t.mu.Lock()
	// Reset channels
	t.interpQueue = make(chan QueuedCmd, interpQueueSize)
	t.seqDone = make(chan struct{})
	t.seqAbort = make(chan struct{})
	t.mu.Unlock()

	go t.sequencerLoop()
}

// StopSequencer aborts and waits for the sequencer goroutine to exit.
func (t *Task) StopSequencer() {
	t.mu.Lock()
	abort := t.seqAbort
	done := t.seqDone
	t.mu.Unlock()

	if abort == nil {
		return
	}

	select {
	case <-abort:
		// already aborted
	default:
		close(abort)
	}

	// Wait for goroutine to finish
	if done != nil {
		<-done
	}
}

// AbortSequencer signals the sequencer to stop and drain.
// Unlike StopSequencer, it does not wait for exit.
func (t *Task) AbortSequencer() {
	t.mu.Lock()
	abort := t.seqAbort
	q := t.interpQueue
	t.mu.Unlock()

	if abort == nil {
		return
	}

	select {
	case <-abort:
	default:
		close(abort)
	}

	// Drain any pending commands
	if q != nil {
		for {
			select {
			case <-q:
			default:
				return
			}
		}
	}
}

// EnqueueCmd pushes a command to the interpreter queue.
// Returns error if sequencer is not running or queue is full.
func (t *Task) EnqueueCmd(cmd QueuedCmd) error {
	t.mu.Lock()
	q := t.interpQueue
	abort := t.seqAbort
	t.mu.Unlock()

	if q == nil {
		return fmt.Errorf("sequencer not running")
	}

	t.logger.Info("enqueue", "cmd", cmd.String())
	select {
	case <-abort:
		return fmt.Errorf("sequencer aborted")
	case q <- cmd:
		return nil
	}
}

// sequencerLoop is the main execution loop. It reads commands from interpQueue
// and executes them sequentially, waiting as required between commands.
func (t *Task) sequencerLoop() {
	defer close(t.seqDone)

	for {
		select {
		case <-t.seqAbort:
			t.setExecState(ExecDone)
			t.setInterpState(InterpIdle)
			return

		case cmd, ok := <-t.interpQueue:
			if !ok {
				// Channel closed — normal shutdown
				t.setExecState(ExecDone)
				t.setInterpState(InterpIdle)
				return
			}

			t.logger.Info("sequencer exec", "cmd", cmd.String())

			// Execute the command
			if err := cmd.Execute(t); err != nil {
				t.logger.Error("sequencer command failed", "cmd", cmd.String(), "err", err)
				t.setExecState(ExecError)
				t.setInterpState(InterpIdle)
				return
			}

			// Wait as required
			if err := t.waitForCompletion(cmd.Wait()); err != nil {
				if errors.Is(err, context.Canceled) {
					// Abort — not an error condition
					t.logger.Info("sequencer aborted during wait", "cmd", cmd.String())
					t.setExecState(ExecDone)
					t.setInterpState(InterpIdle)
					return
				}
				t.logger.Error("sequencer wait failed", "cmd", cmd.String(), "err", err)
				t.setExecState(ExecError)
				t.setInterpState(InterpIdle)
				return
			}

			// Post-wait hook for commands that need state changes after motion completes
			if pw, ok := cmd.(interface{ PostWait(*Task) }); ok {
				pw.PostWait(t)
			}
		}
	}
}

// waitForCompletion blocks until the specified condition is met or abort.
func (t *Task) waitForCompletion(wt WaitType) error {
	switch wt {
	case WaitNone:
		return nil

	case WaitMotion:
		return t.waitMotionDone()

	case WaitIO:
		return t.waitIODone()

	case WaitMotionAndIO:
		if err := t.waitMotionDone(); err != nil {
			return err
		}
		return t.waitIODone()

	case WaitDelay:
		// Delay is handled by DwellCmd itself
		return nil

	case WaitSpindleOriented:
		return t.waitSpindleOriented()
	}
	return nil
}

// waitMotionDone polls motion status until in-position or abort.
func (t *Task) waitMotionDone() error {
	t.setExecState(ExecWaitingForMotion)
	return t.pollUntil(func() bool {
		if t.status == nil {
			return true
		}
		v, err := t.status.GetInpos()
		// v must be exactly 1 (in-position). Negative values indicate read errors.
		return err == nil && v == 1
	})
}

// waitIODone polls until IO command status is DONE or abort.
func (t *Task) waitIODone() error {
	t.setExecState(ExecWaitingForIO)
	return t.pollUntil(func() bool {
		if t.io == nil {
			return true
		}
		st, err := t.io.GetCmdStatus()
		if err != nil {
			return true // treat error as done
		}
		return st == IOStatusDone || st == IOStatusError
	})
}

// waitSpindleOriented polls until spindle orient is complete.
func (t *Task) waitSpindleOriented() error {
	t.setExecState(ExecWaitingForSpindleOriented)
	// TODO: check spindle orient status
	return nil
}

// pollUntil polls the condition at servo rate until true or abort.
func (t *Task) pollUntil(cond func() bool) error {
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	for {
		select {
		case <-t.seqAbort:
			return context.Canceled
		case <-ticker.C:
			if cond() {
				t.setExecState(ExecDone)
				return nil
			}
		}
	}
}

// setExecState sets the execution state (thread-safe).
func (t *Task) setExecState(s ExecState) {
	t.mu.Lock()
	t.execState = s
	t.mu.Unlock()
}

// setInterpState sets the interpreter state (thread-safe).
func (t *Task) setInterpState(s InterpState) {
	t.mu.Lock()
	t.interpState = s
	t.mu.Unlock()
}

// --- Concrete QueuedCmd types ---

// LinearMoveCmd queues a linear motion segment.
type LinearMoveCmd struct {
	Pos        Pose
	Vel        float64
	IniMaxVel  float64
	Acc        float64
	MotionType int32
	ID         int32
	Tag        StateTag
	IndexerJ   int32
}

func (c *LinearMoveCmd) Execute(t *Task) error {
	return t.motion.SetLine(c.Pos, c.Vel, c.IniMaxVel, c.Acc, c.MotionType, c.ID, c.Tag, c.IndexerJ)
}
func (c *LinearMoveCmd) Wait() WaitType { return WaitNone } // queued, no immediate wait
func (c *LinearMoveCmd) String() string { return fmt.Sprintf("LinearMove(id=%d)", c.ID) }

// CircularMoveCmd queues a circular arc segment.
type CircularMoveCmd struct {
	Pos        Pose
	Center     Cartesian
	Normal     Cartesian
	Turn       int32
	Vel        float64
	IniMaxVel  float64
	Acc        float64
	MotionType int32
	ID         int32
	Tag        StateTag
}

func (c *CircularMoveCmd) Execute(t *Task) error {
	return t.motion.SetCircle(c.Pos, c.Center, c.Normal, c.Turn, c.Vel, c.IniMaxVel, c.Acc, c.MotionType, c.ID, c.Tag)
}
func (c *CircularMoveCmd) Wait() WaitType { return WaitNone }
func (c *CircularMoveCmd) String() string { return fmt.Sprintf("CircularMove(id=%d)", c.ID) }

// DwellCmd implements a timed pause (G4).
type DwellCmd struct {
	Seconds float64
}

func (c *DwellCmd) Execute(t *Task) error {
	timer := time.NewTimer(time.Duration(c.Seconds * float64(time.Second)))
	defer timer.Stop()

	select {
	case <-t.seqAbort:
		return context.Canceled
	case <-timer.C:
		return nil
	}
}
func (c *DwellCmd) Wait() WaitType { return WaitNone } // dwell is self-contained
func (c *DwellCmd) String() string { return fmt.Sprintf("Dwell(%.3fs)", c.Seconds) }

// SpindleOnCmd turns a spindle on.
type SpindleOnCmd struct {
	Spindle   int32
	Speed     float64
	CSSFactor float64
	CSSMax    float64
	WaitFlag  int32
}

func (c *SpindleOnCmd) Execute(t *Task) error {
	return t.motion.SpindleOn(c.Spindle, c.Speed, c.CSSFactor, c.CSSMax, c.WaitFlag)
}
func (c *SpindleOnCmd) Wait() WaitType { return WaitNone }
func (c *SpindleOnCmd) String() string {
	return fmt.Sprintf("SpindleOn(s=%d,rpm=%.0f)", c.Spindle, c.Speed)
}

// SpindleOffCmd turns a spindle off.
type SpindleOffCmd struct {
	Spindle int32
}

func (c *SpindleOffCmd) Execute(t *Task) error {
	return t.motion.SpindleOff(c.Spindle)
}
func (c *SpindleOffCmd) Wait() WaitType { return WaitNone }
func (c *SpindleOffCmd) String() string { return fmt.Sprintf("SpindleOff(s=%d)", c.Spindle) }

// ToolPrepareCmd prepares a tool (T word).
type ToolPrepareCmd struct {
	Tool int32
}

func (c *ToolPrepareCmd) Execute(t *Task) error {
	return t.io.ToolPrepare(c.Tool)
}
func (c *ToolPrepareCmd) Wait() WaitType { return WaitIO }
func (c *ToolPrepareCmd) String() string { return fmt.Sprintf("ToolPrepare(T%d)", c.Tool) }

// ToolChangeCmd executes a tool change (M6).
type ToolChangeCmd struct{}

func (c *ToolChangeCmd) Execute(t *Task) error {
	if err := t.io.ToolStartChange(); err != nil {
		// ToolStartChange is optional — some IO controllers don't implement it
		t.logger.Info("tool_start_change skipped", "err", err)
	}
	return t.io.ToolLoad()
}
func (c *ToolChangeCmd) Wait() WaitType { return WaitIO }
func (c *ToolChangeCmd) String() string { return "ToolChange" }

// PostWait updates the motion offset with the new tool's parameters.
func (c *ToolChangeCmd) PostWait(t *Task) {
	// After tool change completes, the IO controller has loaded the tool.
	// The interpreter will handle applying tool length offsets via canon
	// USE_TOOL_LENGTH_OFFSET calls, so no explicit offset update needed here.
	t.logger.Info("tool change complete")
}

// FloodOnCmd turns flood coolant on (M8).
type FloodOnCmd struct{}

func (c *FloodOnCmd) Execute(t *Task) error { return t.io.CoolantFloodOn() }
func (c *FloodOnCmd) Wait() WaitType        { return WaitNone }
func (c *FloodOnCmd) String() string        { return "FloodOn" }

// FloodOffCmd turns flood coolant off (M9).
type FloodOffCmd struct{}

func (c *FloodOffCmd) Execute(t *Task) error { return t.io.CoolantFloodOff() }
func (c *FloodOffCmd) Wait() WaitType        { return WaitNone }
func (c *FloodOffCmd) String() string        { return "FloodOff" }

// WaitForMotionCmd is an explicit queue-drain sync point (canon FLUSH).
type WaitForMotionCmd struct{}

var waitForMotionSingleton = &WaitForMotionCmd{}

func (c *WaitForMotionCmd) Execute(t *Task) error { return nil }
func (c *WaitForMotionCmd) Wait() WaitType        { return WaitMotion }
func (c *WaitForMotionCmd) String() string        { return "WaitForMotion" }

// --- Helper: check sequencer is alive from enqueue side ---

var (
	errSeqNotRunning = fmt.Errorf("sequencer not running")
	errSeqAborted    = fmt.Errorf("sequencer aborted")
)

// SeqRunning reports whether the sequencer goroutine is alive.
func (t *Task) SeqRunning() bool {
	t.mu.Lock()
	done := t.seqDone
	t.mu.Unlock()

	if done == nil {
		return false
	}
	select {
	case <-done:
		return false
	default:
		return true
	}
}

// DrainQueue closes the interpQueue channel and waits for the sequencer to finish.
// Used for normal end-of-program (interpreter done sending commands).
func (t *Task) DrainQueue() {
	t.mu.Lock()
	q := t.interpQueue
	done := t.seqDone
	t.mu.Unlock()

	if q != nil {
		close(q)
	}
	if done != nil {
		<-done
	}
}

// Waiter allows tests to inject a mock for pollUntil.
// Not exported — tests use the concrete mockStatus.InPosition approach.
type waitFunc func() bool

// Mutex-free accessors for sequencer goroutine to read status.
// These avoid holding mu during polling.
var pollInterval = time.Millisecond

// SetPollInterval allows tests to speed up polling. Not thread-safe — call before StartSequencer.
func SetPollInterval(d time.Duration) func() {
	old := pollInterval
	pollInterval = d
	return func() { pollInterval = old }
}

func init() {
	// Ensure pollInterval is used in pollUntil. Override the hardcoded value.
}

// mu-free helpers that call locked setters above (no change needed, just documenting)
// The sequencer goroutine accesses t.status directly (read-only interface, no mu needed).
// The sequencer goroutine accesses t.seqAbort directly (immutable after StartSequencer).
// The sequencer goroutine calls setExecState/setInterpState which lock mu.

// interpDoneCmd is enqueued after interpreter execution completes,
// to transition interp state back to idle after motion finishes.
type interpDoneCmd struct{}

func (c *interpDoneCmd) Execute(t *Task) error {
	// Do NOT set InterpIdle/ExecDone here — we must wait for motion
	// to finish first (via WaitMotion). State is set in PostWait.
	return nil
}

func (c *interpDoneCmd) PostWait(t *Task) {
	t.setInterpState(InterpIdle)
	t.setExecState(ExecDone)
}

func (c *interpDoneCmd) Wait() WaitType { return WaitMotion }
func (c *interpDoneCmd) String() string { return "interp_done" }

// McodeCmd submits an M-code (M100-M199) to the handler worker and waits
// for completion. This blocks the sequencer until the handler finishes or abort.
type McodeCmd struct {
	Mcode int32
	P     float64
	Q     float64
}

func (c *McodeCmd) Execute(t *Task) error {
	if t.mcode == nil {
		return fmt.Errorf("mcode_handler: not initialized")
	}
	if err := t.mcode.Submit(int(c.Mcode), c.P, c.Q); err != nil {
		return err
	}
	// Poll for completion (worker runs async).
	return t.pollUntil(func() bool {
		_, done := t.mcode.CheckDone()
		return done
	})
}
func (c *McodeCmd) Wait() WaitType { return WaitNone } // Execute handles wait internally
func (c *McodeCmd) String() string {
	return fmt.Sprintf("Mcode(M%d P=%.4f Q=%.4f)", c.Mcode, c.P, c.Q)
}
