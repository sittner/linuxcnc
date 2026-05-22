package task

import (
	"fmt"
	"math"
)

// Canon unit systems.
const (
	CanonUnitsInches = 1
	CanonUnitsMM     = 2
	CanonUnitsCM     = 3
)

// Canon plane selection.
const (
	CanonPlaneXY = 1
	CanonPlaneYZ = 2
	CanonPlaneXZ = 3
	CanonPlaneUV = 4
	CanonPlaneVW = 5
	CanonPlaneWU = 6
)

// Canon motion modes.
const (
	CanonExact      = 1
	CanonContinuous = 2
	CanonExactPath  = 3
)

// Canon feed reference.
const (
	CanonWorkpiece = 1
	CanonXYZ       = 2
)

// CanonState holds the interpreter-side state that tracks coordinate
// systems, units, feed rates, and other modal state needed to convert
// G-code interpreter callbacks into motctl commands.
type CanonState struct {
	// Coordinate offsets (stored in mm, absolute)
	g5xOffset  Pose
	g92Offset  Pose
	xyRotation float64

	// Current endpoint (absolute, mm)
	endPoint Pose

	// Units and plane
	lengthUnits int32 // CanonUnitsInches/MM/CM
	activePlane int32

	// Tool offset (mm)
	toolOffset Pose

	// Motion parameters
	motionMode      int32   // CanonExact/Continuous/ExactPath
	motionTolerance float64 // blending tolerance (mm)
	naivecamTol     float64 // naive CAM tolerance (mm)
	feedMode        int32   // 0=normal, 1=inverse-time, 2=units-per-rev

	// Feed rates (internal, mm/sec or deg/sec)
	linearFeedRate  float64
	angularFeedRate float64
	traverseRate    float64

	// Spindle
	spindleNum   int32 // current spindle for synch motion
	spindleSpeed [8]float64
	spindleMode  float64

	// Flags
	feedOverrideEnabled  bool
	speedOverrideEnabled [8]bool
	feedHoldEnabled      bool
	adaptiveFeedEnabled  bool
	optionalProgramStop  bool
	blockDelete          bool

	// State tag (passed to motion segments)
	tag StateTag

	// Motion line ID counter
	lineNo int32
}

// NewCanonState returns a CanonState with sensible defaults.
func NewCanonState() *CanonState {
	cs := &CanonState{
		lengthUnits:         CanonUnitsMM,
		activePlane:         CanonPlaneXY,
		motionMode:          CanonExact,
		linearFeedRate:      25.4, // 1 inch/sec default
		traverseRate:        0,    // set from INI
		feedOverrideEnabled: true,
		feedHoldEnabled:     true,
	}
	for i := range cs.speedOverrideEnabled {
		cs.speedOverrideEnabled[i] = true
	}
	return cs
}

// unitScale returns the conversion factor from program units to mm.
func (cs *CanonState) unitScale() float64 {
	switch cs.lengthUnits {
	case CanonUnitsInches:
		return 25.4
	case CanonUnitsCM:
		return 10.0
	default:
		return 1.0
	}
}

// fromProg converts a program-unit length to internal mm.
func (cs *CanonState) fromProg(v float64) float64 {
	return v * cs.unitScale()
}

// toProg converts internal mm to program units.
func (cs *CanonState) toProg(v float64) float64 {
	return v / cs.unitScale()
}

// rotate applies the XY rotation to x,y coordinates.
func rotate(x, y, angle float64) (float64, float64) {
	if angle == 0 {
		return x, y
	}
	sin, cos := math.Sincos(angle * math.Pi / 180.0)
	return x*cos - y*sin, x*sin + y*cos
}

// toAbsolute converts program coordinates to absolute mm coordinates,
// applying G5x offset, G92 offset, and XY rotation.
func (cs *CanonState) toAbsolute(x, y, z, a, b, c, u, v, w float64) Pose {
	// Apply rotation to the offset-subtracted position
	rx, ry := rotate(cs.fromProg(x)-cs.g5xOffset.X-cs.g92Offset.X,
		cs.fromProg(y)-cs.g5xOffset.Y-cs.g92Offset.Y,
		cs.xyRotation)

	return Pose{
		X: rx + cs.g5xOffset.X + cs.g92Offset.X,
		Y: ry + cs.g5xOffset.Y + cs.g92Offset.Y,
		Z: cs.fromProg(z),
		A: a, // angles: already in degrees
		B: b,
		C: c,
		U: cs.fromProg(u),
		V: cs.fromProg(v),
		W: cs.fromProg(w),
	}
}

// Canon is the set of canon callback implementations that push QueuedCmds
// to the Task's sequencer queue. It holds the CanonState and a reference
// to the owning Task.
type Canon struct {
	state *CanonState
	task  *Task
}

// NewCanon creates a Canon instance tied to a Task.
func NewCanon(t *Task) *Canon {
	cs := NewCanonState()
	return &Canon{
		state: cs,
		task:  t,
	}
}

// --- State-setting callbacks (modify canon state, no queued commands) ---

func (c *Canon) InitCanon() {
	*c.state = *NewCanonState()
}

func (c *Canon) SetG5xOffset(origin int32, x, y, z, a, b, _c, u, v, w float64) {
	s := c.state
	s.g5xOffset = Pose{
		X: s.fromProg(x), Y: s.fromProg(y), Z: s.fromProg(z),
		A: a, B: b, C: _c,
		U: s.fromProg(u), V: s.fromProg(v), W: s.fromProg(w),
	}
}

func (c *Canon) SetG92Offset(x, y, z, a, b, _c, u, v, w float64) {
	s := c.state
	s.g92Offset = Pose{
		X: s.fromProg(x), Y: s.fromProg(y), Z: s.fromProg(z),
		A: a, B: b, C: _c,
		U: s.fromProg(u), V: s.fromProg(v), W: s.fromProg(w),
	}
}

func (c *Canon) SetXYRotation(t float64) {
	c.state.xyRotation = t
}

func (c *Canon) UseLengthUnits(units int32) {
	c.state.lengthUnits = units
}

func (c *Canon) SelectPlane(plane int32) {
	c.state.activePlane = plane
}

func (c *Canon) SetTraverseRate(rate float64) {
	c.state.traverseRate = rate
}

func (c *Canon) SetFeedRate(rate float64) {
	s := c.state
	s.linearFeedRate = s.fromProg(rate) / 60.0 // input is units/min → mm/sec
}

func (c *Canon) SetFeedReference(reference int32) {
	// Stored but rarely used in modern configs
}

func (c *Canon) SetFeedMode(spindle, mode int32) {
	c.state.feedMode = mode
	c.state.spindleNum = spindle
}

func (c *Canon) SetMotionControlMode(mode int32, tolerance float64) {
	s := c.state
	s.motionMode = mode
	s.motionTolerance = s.fromProg(tolerance)
}

func (c *Canon) SetNaivecamTolerance(tolerance float64) {
	c.state.naivecamTol = c.state.fromProg(tolerance)
}

func (c *Canon) SetCutterRadiusCompensation(radius float64) {
	// Cutter comp is handled by the interpreter, not the canon layer
}

func (c *Canon) StartCutterRadiusCompensation(direction int32) {}
func (c *Canon) StopCutterRadiusCompensation()                 {}

func (c *Canon) UpdateEndPoint(x, y, z, a, b, _c, u, v, w float64) {
	c.state.endPoint = c.state.toAbsolute(x, y, z, a, b, _c, u, v, w)
}

func (c *Canon) UpdateTag(tagPtr uint64) {
	// Tag is passed as opaque pointer from interpreter
	// In Go milltask, we store it directly
	_ = tagPtr // TODO: decode tag from interpreter shared memory
}

func (c *Canon) UseLengthOffset(x, y, z, a, b, _c, u, v, w float64) {
	s := c.state
	s.toolOffset = Pose{
		X: x, Y: y, Z: z,
		A: a, B: b, C: _c,
		U: u, V: v, W: w,
	}
}

// --- Action callbacks (push QueuedCmd to sequencer) ---

func (c *Canon) StraightTraverse(lineno int32, x, y, z, a, b, _c, u, v, w float64) {
	s := c.state
	pos := s.toAbsolute(x, y, z, a, b, _c, u, v, w)
	s.endPoint = pos
	s.lineNo = lineno

	trav := c.task.maxVelocity
	cmd := &LinearMoveCmd{
		Pos:        pos,
		Vel:        trav,
		IniMaxVel:  trav,
		Acc:        c.task.maxAcceleration,
		MotionType: 1, // EMC_MOTION_TYPE_TRAVERSE
		ID:         lineno,
		Tag:        s.tag,
		IndexerJ:   -1,
	}
	c.enqueue(cmd)
}

func (c *Canon) StraightFeed(lineno int32, x, y, z, a, b, _c, u, v, w float64) {
	s := c.state
	pos := s.toAbsolute(x, y, z, a, b, _c, u, v, w)
	s.endPoint = pos
	s.lineNo = lineno

	cmd := &LinearMoveCmd{
		Pos:        pos,
		Vel:        s.linearFeedRate,
		IniMaxVel:  c.task.maxVelocity,
		Acc:        c.task.maxAcceleration,
		MotionType: 2, // EMC_MOTION_TYPE_FEED
		ID:         lineno,
		Tag:        s.tag,
		IndexerJ:   -1,
	}
	c.enqueue(cmd)

	// Set motion parameters before the move
	c.enqueueMotionParams()
}

func (c *Canon) ArcFeed(lineno int32, firstEnd, secondEnd, firstAxis, secondAxis float64,
	rotation int32, axisEndPoint, a, b, _c, u, v, w float64) {

	s := c.state
	s.lineNo = lineno

	// Convert arc endpoints based on active plane
	var pos Pose
	var center, normal Cartesian

	switch s.activePlane {
	case CanonPlaneXY:
		pos = s.toAbsolute(firstEnd, secondEnd, axisEndPoint, a, b, _c, u, v, w)
		center = Cartesian{
			X: s.fromProg(firstAxis) + s.endPoint.X,
			Y: s.fromProg(secondAxis) + s.endPoint.Y,
			Z: 0,
		}
		normal = Cartesian{X: 0, Y: 0, Z: 1}
	case CanonPlaneXZ:
		pos = s.toAbsolute(secondEnd, axisEndPoint, firstEnd, a, b, _c, u, v, w)
		center = Cartesian{
			X: 0,
			Y: s.fromProg(secondAxis) + s.endPoint.Y,
			Z: s.fromProg(firstAxis) + s.endPoint.Z,
		}
		normal = Cartesian{X: 0, Y: -1, Z: 0}
	case CanonPlaneYZ:
		pos = s.toAbsolute(axisEndPoint, firstEnd, secondEnd, a, b, _c, u, v, w)
		center = Cartesian{
			X: s.fromProg(secondAxis) + s.endPoint.X,
			Y: s.fromProg(firstAxis) + s.endPoint.Y,
			Z: 0,
		}
		normal = Cartesian{X: -1, Y: 0, Z: 0}
	default:
		pos = s.toAbsolute(firstEnd, secondEnd, axisEndPoint, a, b, _c, u, v, w)
		normal = Cartesian{X: 0, Y: 0, Z: 1}
	}

	s.endPoint = pos

	cmd := &CircularMoveCmd{
		Pos:        pos,
		Center:     center,
		Normal:     normal,
		Turn:       rotation,
		Vel:        s.linearFeedRate,
		IniMaxVel:  c.task.maxVelocity,
		Acc:        c.task.maxAcceleration,
		MotionType: 3, // EMC_MOTION_TYPE_ARC
		ID:         lineno,
		Tag:        s.tag,
	}
	c.enqueue(cmd)
	c.enqueueMotionParams()
}

func (c *Canon) RigidTap(lineno int32, x, y, z, scale float64) {
	s := c.state
	pos := Pose{X: s.fromProg(x), Y: s.fromProg(y), Z: s.fromProg(z)}
	s.lineNo = lineno

	cmd := &RigidTapCmd{
		Pos:   pos,
		Vel:   s.linearFeedRate,
		Acc:   c.task.maxAcceleration,
		Scale: scale,
		ID:    lineno,
		Tag:   s.tag,
	}
	c.enqueue(cmd)
}

func (c *Canon) StraightProbe(lineno int32, x, y, z, a, b, _c, u, v, w float64, probeType uint8) {
	s := c.state
	pos := s.toAbsolute(x, y, z, a, b, _c, u, v, w)
	s.lineNo = lineno

	cmd := &ProbeCmd{
		Pos:        pos,
		Vel:        s.linearFeedRate,
		IniMaxVel:  c.task.maxVelocity,
		Acc:        c.task.maxAcceleration,
		MotionType: 4, // EMC_MOTION_TYPE_PROBING
		ProbeType:  probeType,
		ID:         lineno,
		Tag:        s.tag,
	}
	c.enqueue(cmd)
}

func (c *Canon) Dwell(seconds float64) {
	c.enqueue(&DwellCmd{Seconds: seconds})
}

func (c *Canon) Stop() {
	c.enqueue(waitForMotionSingleton)
}

func (c *Canon) Finish() {
	c.enqueue(waitForMotionSingleton)
}

func (c *Canon) StartSpindleClockwise(spindle, waitForAtspeed int32) {
	s := c.state
	c.enqueue(&SpindleOnCmd{
		Spindle:  spindle,
		Speed:    s.spindleSpeed[spindle],
		WaitFlag: waitForAtspeed,
	})
}

func (c *Canon) StartSpindleCounterclockwise(spindle, waitForAtspeed int32) {
	s := c.state
	c.enqueue(&SpindleOnCmd{
		Spindle:  spindle,
		Speed:    -s.spindleSpeed[spindle],
		WaitFlag: waitForAtspeed,
	})
}

func (c *Canon) SetSpindleSpeed(spindle int32, rpm float64) {
	c.state.spindleSpeed[spindle] = rpm
}

func (c *Canon) StopSpindleTurning(spindle int32) {
	c.enqueue(&SpindleOffCmd{Spindle: spindle})
}

func (c *Canon) OrientSpindle(spindle int32, orientation float64, mode int32) {
	c.enqueue(&SpindleOrientCmd{Spindle: spindle, Orientation: orientation, Mode: mode})
}

func (c *Canon) WaitSpindleOrientComplete(spindle int32, timeout float64) {
	c.enqueue(&WaitSpindleOrientedCmd{Spindle: spindle, Timeout: timeout})
}

func (c *Canon) SelectTool(tool int32) {
	// T word — record selected tool for subsequent M6
	c.enqueue(&ToolPrepareCmd{Tool: tool})
}

func (c *Canon) StartChange() {
	// M6 start — wait for motion to complete first
	c.enqueue(waitForMotionSingleton)
}

func (c *Canon) ChangeTool(slot int32) {
	c.enqueue(&ToolChangeCmd{})
}

func (c *Canon) FloodOn()  { c.enqueue(&FloodOnCmd{}) }
func (c *Canon) FloodOff() { c.enqueue(&FloodOffCmd{}) }
func (c *Canon) MistOn()   { c.enqueue(&MistOnCmd{}) }
func (c *Canon) MistOff()  { c.enqueue(&MistOffCmd{}) }

func (c *Canon) EnableFeedOverride() {
	c.state.feedOverrideEnabled = true
	c.enqueue(&FeedOverrideEnableCmd{Enable: true})
}

func (c *Canon) DisableFeedOverride() {
	c.state.feedOverrideEnabled = false
	c.enqueue(&FeedOverrideEnableCmd{Enable: false})
}

func (c *Canon) EnableSpeedOverride(spindle int32) {
	c.state.speedOverrideEnabled[spindle] = true
}

func (c *Canon) DisableSpeedOverride(spindle int32) {
	c.state.speedOverrideEnabled[spindle] = false
}

func (c *Canon) EnableFeedHold() {
	c.state.feedHoldEnabled = true
	c.enqueue(&FeedHoldEnableCmd{Enable: true})
}

func (c *Canon) DisableFeedHold() {
	c.state.feedHoldEnabled = false
	c.enqueue(&FeedHoldEnableCmd{Enable: false})
}

func (c *Canon) EnableAdaptiveFeed() {
	c.state.adaptiveFeedEnabled = true
	c.enqueue(&AdaptiveFeedEnableCmd{Enable: true})
}

func (c *Canon) DisableAdaptiveFeed() {
	c.state.adaptiveFeedEnabled = false
	c.enqueue(&AdaptiveFeedEnableCmd{Enable: false})
}

func (c *Canon) SetMotionOutputBit(index int32) {
	c.enqueue(&SetDoutSyncCmd{Index: index, StartValue: 1, EndValue: 1})
}

func (c *Canon) ClearMotionOutputBit(index int32) {
	c.enqueue(&SetDoutSyncCmd{Index: index, StartValue: 0, EndValue: 0})
}

func (c *Canon) SetAuxOutputBit(index int32) {
	c.enqueue(&SetDoutCmd{Index: index, Value: 1})
}

func (c *Canon) ClearAuxOutputBit(index int32) {
	c.enqueue(&SetDoutCmd{Index: index, Value: 0})
}

func (c *Canon) SetMotionOutputValue(index int32, value float64) {
	c.enqueue(&SetAoutSyncCmd{Index: index, StartValue: value, EndValue: value})
}

func (c *Canon) SetAuxOutputValue(index int32, value float64) {
	c.enqueue(&SetAoutCmd{Index: index, Value: value})
}

func (c *Canon) ProgramStop() {
	c.enqueue(waitForMotionSingleton)
}

func (c *Canon) OptionalProgramStop() {
	if c.state.optionalProgramStop {
		c.enqueue(waitForMotionSingleton)
	}
}

func (c *Canon) ProgramEnd() {
	c.enqueue(waitForMotionSingleton)
}

func (c *Canon) Comment(s string)   {}
func (c *Canon) Message(s string)   { c.task.logger.Info("MSG: " + s) }
func (c *Canon) LogMsg(s string)    {}
func (c *Canon) LogOpen(s string)   {}
func (c *Canon) LogAppend(s string) {}
func (c *Canon) LogClose()          {}

func (c *Canon) CanonError(msg string) {
	c.task.logger.Error("canon error", "msg", msg)
}

func (c *Canon) SetBlockDelete(enabled int32) {
	c.state.blockDelete = enabled != 0
}

func (c *Canon) GetBlockDelete() int32 {
	if c.state.blockDelete {
		return 1
	}
	return 0
}

func (c *Canon) SetOptionalProgramStop(enabled int32) {
	c.state.optionalProgramStop = enabled != 0
}

func (c *Canon) GetOptionalProgramStop() int32 {
	if c.state.optionalProgramStop {
		return 1
	}
	return 0
}

func (c *Canon) OnReset() {
	c.InitCanon()
}

func (c *Canon) TurnProbeOn()  {}
func (c *Canon) TurnProbeOff() {}

func (c *Canon) StartSpeedFeedSynch(spindle int32, feedPerRev float64, velocityMode int32) {
	c.state.feedMode = 2 // units per rev
	c.state.spindleNum = spindle
	c.enqueue(&SpindleSyncCmd{
		Sync:       c.state.fromProg(feedPerRev),
		MotionType: velocityMode,
	})
}

func (c *Canon) StopSpeedFeedSynch() {
	c.state.feedMode = 0
	c.enqueue(&SpindleSyncCmd{Sync: 0, MotionType: 0})
}

// --- Stub methods (required by canon_callbacks_t, not yet fully implemented) ---

func (c *Canon) ClampAxis(axis int32)   {}
func (c *Canon) UnclampAxis(axis int32) {}
func (c *Canon) PalletShuttle()         {}

func (c *Canon) WaitInput(index, inputType, waitType int32, timeout float64) int32 {
	return 0
}

func (c *Canon) LockRotary(lineno, joint int32) int32 {
	c.enqueue(&LockRotaryCmd{Lineno: lineno, Joint: joint, Lock: true})
	return 0
}

func (c *Canon) UnlockRotary(lineno, joint int32) int32 {
	c.enqueue(&LockRotaryCmd{Lineno: lineno, Joint: joint, Lock: false})
	return 0
}

func (c *Canon) SetParameterFileName(name string) {}

func (c *Canon) SetSpindleMode(spindle int32, mode float64) {
	c.state.spindleMode = mode
}

func (c *Canon) SetToolTableEntry(pocket, toolno int32, ox, oy, oz, oa, ob, oc, ou, ov, ow, diameter, frontangle, backangle float64, orientation int32) {
	// TODO: update tool table via IOController
}

func (c *Canon) ReloadTooldata() {
	// TODO: signal tool table reload
}

func (c *Canon) ChangeToolNumber(number int32) {
	c.enqueue(&ToolChangeCmd{})
}

func (c *Canon) NurbsFeed(lineno int32, controlPoints []ControlPoint, k uint32) {
	// TODO: NURBS feed support
}

// ControlPoint is a NURBS control point.
type ControlPoint struct {
	X, Y, W float64
}

// LockRotaryCmd queues a rotary axis lock/unlock.
type LockRotaryCmd struct {
	Lineno int32
	Joint  int32
	Lock   bool
}

func (cmd *LockRotaryCmd) Execute(t *Task) error {
	// TODO: implement via motion controller
	return nil
}
func (cmd *LockRotaryCmd) Wait() WaitType { return WaitNone }
func (cmd *LockRotaryCmd) String() string {
	if cmd.Lock {
		return fmt.Sprintf("LockRotary(joint=%d)", cmd.Joint)
	}
	return fmt.Sprintf("UnlockRotary(joint=%d)", cmd.Joint)
}

// --- Internal helpers ---

func (c *Canon) enqueue(cmd QueuedCmd) {
	if err := c.task.EnqueueCmd(cmd); err != nil {
		c.task.logger.Error("canon enqueue failed", "cmd", cmd.String(), "err", err)
	}
}

// enqueueMotionParams sets vel/acc/term-cond before a feed move.
func (c *Canon) enqueueMotionParams() {
	s := c.state
	c.enqueue(&SetMotionParamsCmd{
		Vel:       s.linearFeedRate,
		Acc:       c.task.maxAcceleration,
		TermCond:  s.motionMode,
		Tolerance: s.motionTolerance,
	})
}

// --- Additional QueuedCmd types for canon ---

// RigidTapCmd queues a rigid tap.
type RigidTapCmd struct {
	Pos   Pose
	Vel   float64
	Acc   float64
	Scale float64
	ID    int32
	Tag   StateTag
}

func (c *RigidTapCmd) Execute(t *Task) error {
	return t.motion.RigidTap(c.Pos, c.Vel, c.Vel, c.Acc, c.Scale, c.ID, c.Tag)
}
func (c *RigidTapCmd) Wait() WaitType { return WaitNone }
func (c *RigidTapCmd) String() string { return fmt.Sprintf("RigidTap(id=%d)", c.ID) }

// ProbeCmd queues a probe move.
type ProbeCmd struct {
	Pos        Pose
	Vel        float64
	IniMaxVel  float64
	Acc        float64
	MotionType int32
	ProbeType  uint8
	ID         int32
	Tag        StateTag
}

func (c *ProbeCmd) Execute(t *Task) error {
	return t.motion.Probe(c.Pos, c.Vel, c.IniMaxVel, c.Acc, c.MotionType, c.ProbeType, c.ID, c.Tag)
}
func (c *ProbeCmd) Wait() WaitType { return WaitMotion }
func (c *ProbeCmd) String() string { return fmt.Sprintf("Probe(id=%d)", c.ID) }

// SpindleOrientCmd orients a spindle.
type SpindleOrientCmd struct {
	Spindle     int32
	Orientation float64
	Mode        int32
}

func (c *SpindleOrientCmd) Execute(t *Task) error {
	return t.motion.SpindleOrient(c.Spindle, c.Orientation, c.Mode)
}
func (c *SpindleOrientCmd) Wait() WaitType { return WaitNone }
func (c *SpindleOrientCmd) String() string {
	return fmt.Sprintf("SpindleOrient(s=%d)", c.Spindle)
}

// WaitSpindleOrientedCmd waits for orient to complete.
type WaitSpindleOrientedCmd struct {
	Spindle int32
	Timeout float64
}

func (c *WaitSpindleOrientedCmd) Execute(t *Task) error { return nil }
func (c *WaitSpindleOrientedCmd) Wait() WaitType        { return WaitSpindleOriented }
func (c *WaitSpindleOrientedCmd) String() string        { return "WaitSpindleOriented" }

// MistOnCmd turns mist on.
type MistOnCmd struct{}

func (c *MistOnCmd) Execute(t *Task) error { return t.io.CoolantMistOn() }
func (c *MistOnCmd) Wait() WaitType        { return WaitNone }
func (c *MistOnCmd) String() string        { return "MistOn" }

// MistOffCmd turns mist off.
type MistOffCmd struct{}

func (c *MistOffCmd) Execute(t *Task) error { return t.io.CoolantMistOff() }
func (c *MistOffCmd) Wait() WaitType        { return WaitNone }
func (c *MistOffCmd) String() string        { return "MistOff" }

// SetMotionParamsCmd sets velocity/acceleration/termination before a move.
type SetMotionParamsCmd struct {
	Vel       float64
	Acc       float64
	TermCond  int32
	Tolerance float64
}

func (c *SetMotionParamsCmd) Execute(t *Task) error {
	if err := t.motion.SetVel(c.Vel); err != nil {
		return err
	}
	if c.Acc > 0 {
		if err := t.motion.SetAcc(c.Acc); err != nil {
			return err
		}
	}
	return t.motion.SetTermCond(c.TermCond, c.Tolerance)
}
func (c *SetMotionParamsCmd) Wait() WaitType { return WaitNone }
func (c *SetMotionParamsCmd) String() string { return "SetMotionParams" }

// FeedOverrideEnableCmd enables/disables feed override.
type FeedOverrideEnableCmd struct{ Enable bool }

func (c *FeedOverrideEnableCmd) Execute(t *Task) error {
	v := int32(0)
	if c.Enable {
		v = 1
	}
	return t.motion.FeedScaleEnable(v)
}
func (c *FeedOverrideEnableCmd) Wait() WaitType { return WaitNone }
func (c *FeedOverrideEnableCmd) String() string { return "FeedOverrideEnable" }

// FeedHoldEnableCmd enables/disables feed hold.
type FeedHoldEnableCmd struct{ Enable bool }

func (c *FeedHoldEnableCmd) Execute(t *Task) error {
	v := int32(0)
	if c.Enable {
		v = 1
	}
	return t.motion.FeedHoldEnable(v)
}
func (c *FeedHoldEnableCmd) Wait() WaitType { return WaitNone }
func (c *FeedHoldEnableCmd) String() string { return "FeedHoldEnable" }

// AdaptiveFeedEnableCmd enables/disables adaptive feed.
type AdaptiveFeedEnableCmd struct{ Enable bool }

func (c *AdaptiveFeedEnableCmd) Execute(t *Task) error {
	v := int32(0)
	if c.Enable {
		v = 1
	}
	return t.motion.AdaptiveFeedEnable(v)
}
func (c *AdaptiveFeedEnableCmd) Wait() WaitType { return WaitNone }
func (c *AdaptiveFeedEnableCmd) String() string { return "AdaptiveFeedEnable" }

// SetDoutCmd sets a digital output immediately.
type SetDoutCmd struct {
	Index int32
	Value int32
}

func (c *SetDoutCmd) Execute(t *Task) error { return t.motion.SetDout(c.Index, c.Value) }
func (c *SetDoutCmd) Wait() WaitType        { return WaitNone }
func (c *SetDoutCmd) String() string        { return fmt.Sprintf("SetDout(%d=%d)", c.Index, c.Value) }

// SetDoutSyncCmd sets a digital output synchronized with motion.
type SetDoutSyncCmd struct {
	Index      int32
	StartValue int32
	EndValue   int32
}

func (c *SetDoutSyncCmd) Execute(t *Task) error {
	// TODO: use SetDoutSynched when available in MotionController interface
	return t.motion.SetDout(c.Index, c.StartValue)
}
func (c *SetDoutSyncCmd) Wait() WaitType { return WaitNone }
func (c *SetDoutSyncCmd) String() string {
	return fmt.Sprintf("SetDoutSync(%d=%d→%d)", c.Index, c.StartValue, c.EndValue)
}

// SetAoutCmd sets an analog output immediately.
type SetAoutCmd struct {
	Index int32
	Value float64
}

func (c *SetAoutCmd) Execute(t *Task) error { return t.motion.SetAout(c.Index, c.Value) }
func (c *SetAoutCmd) Wait() WaitType        { return WaitNone }
func (c *SetAoutCmd) String() string        { return fmt.Sprintf("SetAout(%d=%.2f)", c.Index, c.Value) }

// SetAoutSyncCmd sets an analog output synchronized with motion.
type SetAoutSyncCmd struct {
	Index      int32
	StartValue float64
	EndValue   float64
}

func (c *SetAoutSyncCmd) Execute(t *Task) error {
	return t.motion.SetAout(c.Index, c.StartValue)
}
func (c *SetAoutSyncCmd) Wait() WaitType { return WaitNone }
func (c *SetAoutSyncCmd) String() string {
	return fmt.Sprintf("SetAoutSync(%d=%.2f→%.2f)", c.Index, c.StartValue, c.EndValue)
}

// SpindleSyncCmd sets spindle synchronization for subsequent moves.
type SpindleSyncCmd struct {
	Sync       float64
	MotionType int32
}

func (c *SpindleSyncCmd) Execute(t *Task) error {
	return t.motion.SetVel(c.Sync) // TODO: use SetSpindlesync when wired
}
func (c *SpindleSyncCmd) Wait() WaitType { return WaitNone }
func (c *SpindleSyncCmd) String() string { return fmt.Sprintf("SpindleSync(%.3f)", c.Sync) }
