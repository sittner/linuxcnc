// Package task implements the milltask gomod — the CNC task controller
// that coordinates motion, I/O, and the G-code interpreter.
//
// All state lives in the Task struct (no globals), making the module
// inherently multi-instance capable.
package task

import (
	"fmt"
	"log/slog"
	"sync"
)

// TaskState represents the machine state (estop, on, etc.)
type TaskState int32

const (
	StateEstop      TaskState = 1
	StateEstopReset TaskState = 2
	StateOff        TaskState = 3
	StateOn         TaskState = 4
)

func (s TaskState) String() string {
	switch s {
	case StateEstop:
		return "ESTOP"
	case StateEstopReset:
		return "ESTOP_RESET"
	case StateOff:
		return "OFF"
	case StateOn:
		return "ON"
	default:
		return fmt.Sprintf("TaskState(%d)", int(s))
	}
}

// TaskMode represents the current operating mode.
type TaskMode int32

const (
	ModeManual TaskMode = 1
	ModeMDI    TaskMode = 3
	ModeAuto   TaskMode = 2
)

func (m TaskMode) String() string {
	switch m {
	case ModeManual:
		return "MANUAL"
	case ModeMDI:
		return "MDI"
	case ModeAuto:
		return "AUTO"
	default:
		return fmt.Sprintf("TaskMode(%d)", int(m))
	}
}

// InterpState represents the interpreter execution state.
type InterpState int32

const (
	InterpIdle    InterpState = 1
	InterpReading InterpState = 2
	InterpPaused  InterpState = 3
	InterpWaiting InterpState = 4
)

// ExecState represents the task execution state.
type ExecState int32

const (
	ExecDone                      ExecState = 1
	ExecWaitingForMotion          ExecState = 2
	ExecWaitingForIO              ExecState = 3
	ExecWaitingForPause           ExecState = 4
	ExecWaitingForDelay           ExecState = 5
	ExecWaitingForSystemCmd       ExecState = 6
	ExecWaitingForSpindleOriented ExecState = 7
	ExecError                     ExecState = 8
)

// MotionController is the interface to motmod (motctl GMI API).
// Methods match the motctl.gmi function names.
type MotionController interface {
	// Motion queue
	SetLine(pos Pose, vel, iniMaxvel, acc float64, motionType, id int32, tag StateTag, indexerJnum int32) error
	SetCircle(pos Pose, center, normal Cartesian, turn int32, vel, iniMaxvel, acc float64, motionType, id int32, tag StateTag) error
	Probe(pos Pose, vel, iniMaxvel, acc float64, motionType int32, probeType uint8, id int32, tag StateTag) error
	RigidTap(pos Pose, vel, iniMaxvel, acc float64, scale float64, id int32, tag StateTag) error

	// Motion control
	Abort() error
	Pause() error
	Resume() error
	Step(id int32) error
	Reverse() error
	Forward() error
	SetFree() error
	SetCoord() error
	SetTeleop() error
	Enable() error
	Disable() error

	// Jogging
	JogCont(num int32, vel float64, isTeleop int32) error
	JogIncr(num int32, vel, incr float64, isTeleop int32) error
	JogAbs(num int32, vel, pos float64, isTeleop int32) error
	JogAbort(num int32, isTeleop int32) error

	// Spindle
	SpindleOn(spindle int32, speed float64, css_factor float64, css_max float64, wait int32) error
	SpindleOff(spindle int32) error
	SpindleOrient(spindle int32, orientation float64, mode int32) error
	SpindleIncrease(spindle int32) error
	SpindleDecrease(spindle int32) error
	SpindleBrakeEngage(spindle int32) error
	SpindleBrakeRelease(spindle int32) error
	SetSpindleScale(spindle int32, scale float64) error

	// Overrides
	SetFeedScale(scale float64) error
	SetRapidScale(scale float64) error
	SetMaxFeedOverride(max float64) error
	FeedScaleEnable(enable int32) error
	AdaptiveFeedEnable(enable int32) error
	FeedHoldEnable(enable int32) error

	// Limits and homing
	OverrideLimits(joint int32) error
	JointHome(joint int32) error
	JointUnhome(joint int32) error

	// Parameters
	SetVel(vel float64) error
	SetVelLimit(vel float64) error
	SetAcc(acc float64) error
	SetTermCond(cond int32, tolerance float64) error
	SetOffset(offset Pose) error
	SetDebug(level int32) error

	// I/O
	SetDout(index, value int32) error
	SetAout(index int32, value float64) error
}

// IOController is the interface to iocontrol (emcio GMI API).
type IOController interface {
	FloodOn() error
	FloodOff() error
	MistOn() error
	MistOff() error
	LubeOn() error
	LubeOff() error
	ToolPrepare(pocket, tool int) error
	ToolChange() error
	Estop() error
	EstopReset() error
}

// MotionStatus provides read access to motion state (motstat GMI API).
type MotionStatus interface {
	Enabled() bool
	InPosition() bool
	Paused() bool
	MotionType() int32
	CurrentVel() float64
	AxisMask() int32
	JointCount() int32
	SpindleCount() int32
}

// Pose represents a 9-axis position.
type Pose struct {
	X, Y, Z float64
	A, B, C float64
	U, V, W float64
}

// Cartesian represents a 3D vector.
type Cartesian struct {
	X, Y, Z float64
}

// StateTag carries interpreter state for motion segments.
type StateTag struct {
	FieldsFloat [5]float32
	Fields      [8]int32
	PackedFlags uint64
}

// Task is the central controller state. One instance per machine.
type Task struct {
	mu sync.Mutex

	// Current state
	state       TaskState
	mode        TaskMode
	interpState InterpState
	execState   ExecState

	// Configuration
	numJoints   int
	numSpindles int
	axisMask    int32

	// Dependencies (injected, mockable for tests)
	motion MotionController
	io     IOController
	status MotionStatus
	interp Interpreter
	logger *slog.Logger

	// Canon state (interpreter callback context)
	canon *Canon

	// Program state
	programFile string
	programOpen bool

	// Sequencer
	interpQueue chan QueuedCmd
	seqDone     chan struct{} // closed when sequencer goroutine exits
	seqAbort    chan struct{} // close to abort sequencer
}

// NewTask creates a new Task with dependencies injected.
func NewTask(motion MotionController, io IOController, status MotionStatus, logger *slog.Logger) *Task {
	t := &Task{
		state:       StateEstop,
		mode:        ModeManual,
		interpState: InterpIdle,
		execState:   ExecDone,
		motion:      motion,
		io:          io,
		status:      status,
		logger:      logger,
	}
	t.canon = NewCanon(t)
	return t
}

// SetInterpreter sets the interpreter dependency. Must be called before
// running G-code programs. The interpreter should already have its canon
// callbacks wired via SetCanonCallbacks.
func (t *Task) SetInterpreter(interp Interpreter) {
	t.interp = interp
}
