package task

import (
	"fmt"
	"math"

	"github.com/sittner/linuxcnc/src/gomc/pkg/hal"
)

const (
	haluiMaxSpindles = 8
	haluiMaxMDI      = 64
)

// halUI holds the "halui" HAL component and all its pins.
// It replaces the standalone halui cmod — pins are read directly by milltask's
// monitor goroutine, with zero IPC overhead.
type halUI struct {
	comp *hal.Component

	// Machine state control
	machineOn        *hal.Pin[bool]
	machineOff       *hal.Pin[bool]
	machineIsOn      *hal.Pin[bool] // output
	estopActivate    *hal.Pin[bool]
	estopReset       *hal.Pin[bool]
	estopIsActivated *hal.Pin[bool] // output

	// Mode control (input = request, output = status)
	modeManual   *hal.Pin[bool]
	modeIsManual *hal.Pin[bool] // output
	modeAuto     *hal.Pin[bool]
	modeIsAuto   *hal.Pin[bool] // output
	modeMDI      *hal.Pin[bool]
	modeIsMDI    *hal.Pin[bool] // output
	modeTeleop   *hal.Pin[bool]
	modeIsTeleop *hal.Pin[bool] // output
	modeJoint    *hal.Pin[bool]
	modeIsJoint  *hal.Pin[bool] // output

	// Coolant
	mistOn    *hal.Pin[bool]
	mistOff   *hal.Pin[bool]
	mistIsOn  *hal.Pin[bool] // output
	floodOn   *hal.Pin[bool]
	floodOff  *hal.Pin[bool]
	floodIsOn *hal.Pin[bool] // output

	// Lube
	lubeOn   *hal.Pin[bool]
	lubeOff  *hal.Pin[bool]
	lubeIsOn *hal.Pin[bool] // output

	// Program control
	programIsIdle    *hal.Pin[bool] // output
	programIsRunning *hal.Pin[bool] // output
	programIsPaused  *hal.Pin[bool] // output
	programRun       *hal.Pin[bool]
	programPause     *hal.Pin[bool]
	programResume    *hal.Pin[bool]
	programStep      *hal.Pin[bool]
	programStop      *hal.Pin[bool]
	programOsOn      *hal.Pin[bool]
	programOsOff     *hal.Pin[bool]
	programOsIsOn    *hal.Pin[bool] // output
	programBdOn      *hal.Pin[bool]
	programBdOff     *hal.Pin[bool]
	programBdIsOn    *hal.Pin[bool] // output

	// Tool info (output)
	toolNumber        *hal.Pin[uint32]  // output
	toolLengthOffsetX *hal.Pin[float64] // output
	toolLengthOffsetY *hal.Pin[float64] // output
	toolLengthOffsetZ *hal.Pin[float64] // output
	toolLengthOffsetA *hal.Pin[float64] // output
	toolLengthOffsetB *hal.Pin[float64] // output
	toolLengthOffsetC *hal.Pin[float64] // output
	toolLengthOffsetU *hal.Pin[float64] // output
	toolLengthOffsetV *hal.Pin[float64] // output
	toolLengthOffsetW *hal.Pin[float64] // output
	toolDiameter      *hal.Pin[float64] // output

	// Joint jog
	jjogSpeed          *hal.Pin[float64]
	jjogDeadband       *hal.Pin[float64]
	jjogMinus          [maxJoints + 1]*hal.Pin[bool] // [numJoints] = selected
	jjogPlus           [maxJoints + 1]*hal.Pin[bool]
	jjogAnalog         [maxJoints + 1]*hal.Pin[float64]
	jjogIncrement      [maxJoints + 1]*hal.Pin[float64]
	jjogIncrementPlus  [maxJoints + 1]*hal.Pin[bool]
	jjogIncrementMinus [maxJoints + 1]*hal.Pin[bool]

	// Axis (teleop) jog
	ajogSpeed          *hal.Pin[float64]
	ajogDeadband       *hal.Pin[float64]
	ajogMinus          [maxAxes + 1]*hal.Pin[bool] // [numAxes] = selected
	ajogPlus           [maxAxes + 1]*hal.Pin[bool]
	ajogAnalog         [maxAxes + 1]*hal.Pin[float64]
	ajogIncrement      [maxAxes + 1]*hal.Pin[float64]
	ajogIncrementPlus  [maxAxes + 1]*hal.Pin[bool]
	ajogIncrementMinus [maxAxes + 1]*hal.Pin[bool]

	// Joint/axis selection
	jointSelected   *hal.Pin[uint32] // output
	axisSelected    *hal.Pin[uint32] // output
	jointNrSelect   [maxJoints]*hal.Pin[bool]
	axisNrSelect    [maxAxes]*hal.Pin[bool]
	jointIsSelected [maxJoints]*hal.Pin[bool] // output
	axisIsSelected  [maxAxes]*hal.Pin[bool]   // output

	// Joint status (output)
	jointHome           [maxJoints + 1]*hal.Pin[bool]
	jointUnhome         [maxJoints + 1]*hal.Pin[bool]
	jointIsHomed        [maxJoints + 1]*hal.Pin[bool] // output
	jointHasFault       [maxJoints + 1]*hal.Pin[bool] // output
	jointOverrideLimits [maxJoints + 1]*hal.Pin[bool] // output
	jointOnSoftMinLimit [maxJoints + 1]*hal.Pin[bool] // output
	jointOnSoftMaxLimit [maxJoints + 1]*hal.Pin[bool] // output
	jointOnHardMinLimit [maxJoints + 1]*hal.Pin[bool] // output
	jointOnHardMaxLimit [maxJoints + 1]*hal.Pin[bool] // output

	// Feed override (encoder + buttons)
	foCounts      *hal.Pin[int32]
	foCountEnable *hal.Pin[bool]
	foDirectValue *hal.Pin[bool]
	foScale       *hal.Pin[float64]
	foValue       *hal.Pin[float64] // output
	foIncrease    *hal.Pin[bool]
	foDecrease    *hal.Pin[bool]
	foReset       *hal.Pin[bool]

	// Rapid override
	roCounts      *hal.Pin[int32]
	roCountEnable *hal.Pin[bool]
	roDirectValue *hal.Pin[bool]
	roScale       *hal.Pin[float64]
	roValue       *hal.Pin[float64] // output
	roIncrease    *hal.Pin[bool]
	roDecrease    *hal.Pin[bool]
	roReset       *hal.Pin[bool]

	// Max velocity
	mvCounts      *hal.Pin[int32]
	mvCountEnable *hal.Pin[bool]
	mvDirectValue *hal.Pin[bool]
	mvScale       *hal.Pin[float64]
	mvValue       *hal.Pin[float64] // output
	mvIncrease    *hal.Pin[bool]
	mvDecrease    *hal.Pin[bool]

	// Spindle override (per-spindle)
	soCounts      [haluiMaxSpindles + 1]*hal.Pin[int32]
	soCountEnable [haluiMaxSpindles + 1]*hal.Pin[bool]
	soDirectValue [haluiMaxSpindles + 1]*hal.Pin[bool]
	soScale       [haluiMaxSpindles + 1]*hal.Pin[float64]
	soValue       [haluiMaxSpindles + 1]*hal.Pin[float64] // output
	soIncrease    [haluiMaxSpindles + 1]*hal.Pin[bool]
	soDecrease    [haluiMaxSpindles + 1]*hal.Pin[bool]
	soReset       [haluiMaxSpindles + 1]*hal.Pin[bool]

	// Spindle control (per-spindle)
	spindleStart        [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleStop         [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleIsOn         [haluiMaxSpindles + 1]*hal.Pin[bool] // output
	spindleForward      [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleRunsForward  [haluiMaxSpindles + 1]*hal.Pin[bool] // output
	spindleReverse      [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleRunsBackward [haluiMaxSpindles + 1]*hal.Pin[bool] // output
	spindleIncrease     [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleDecrease     [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleBrakeOn      [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleBrakeOff     [haluiMaxSpindles + 1]*hal.Pin[bool]
	spindleBrakeIsOn    [haluiMaxSpindles + 1]*hal.Pin[bool] // output

	// Abort / home-all
	abort   *hal.Pin[bool]
	homeAll *hal.Pin[bool]

	// MDI commands (triggered by rising edge)
	mdiCommands [haluiMaxMDI]*hal.Pin[bool]

	// Axis position (output)
	axisPosCommanded [maxAxes + 1]*hal.Pin[float64]
	axisPosFeedback  [maxAxes + 1]*hal.Pin[float64]
	axisPosRelative  [maxAxes + 1]*hal.Pin[float64]

	// Misc
	unitsPerMM *hal.Pin[float64] // output
	cycleCount *hal.Pin[uint32]  // output

	// --- axisui pins (merged) ---
	// Notification control (input from HAL)
	notificationsClear      *hal.Pin[bool]
	notificationsClearInfo  *hal.Pin[bool]
	notificationsClearError *hal.Pin[bool]
	resumeInhibit           *hal.Pin[bool]

	// UI status (output to HAL)
	hasNotifications *hal.Pin[bool]
	errorActive      *hal.Pin[bool]
	isRunning        *hal.Pin[bool] // output: UI connected and alive

	// Cached previous values for edge detection
	old halUIValues

	// Config
	numJoints       int
	numSpindles     int
	axisMask        int32
	numMDI          int
	mdiCommands_str []string // INI-configured MDI command strings
}

// halUIValues stores the previous scan values for edge detection.
type halUIValues struct {
	machineOn     bool
	machineOff    bool
	estopActivate bool
	estopReset    bool

	modeManual bool
	modeAuto   bool
	modeMDI    bool
	modeTeleop bool
	modeJoint  bool

	mistOn   bool
	mistOff  bool
	floodOn  bool
	floodOff bool
	lubeOn   bool
	lubeOff  bool

	programRun    bool
	programPause  bool
	programResume bool
	programStep   bool
	programStop   bool
	programOsOn   bool
	programOsOff  bool
	programBdOn   bool
	programBdOff  bool

	jjogSpeed          float64
	ajogSpeed          float64
	jjogMinus          [maxJoints + 1]bool
	jjogPlus           [maxJoints + 1]bool
	jjogAnalog         [maxJoints + 1]float64
	jjogIncrementPlus  [maxJoints + 1]bool
	jjogIncrementMinus [maxJoints + 1]bool
	jointNrSelect      [maxJoints]bool
	jointHome          [maxJoints + 1]bool
	jointUnhome        [maxJoints + 1]bool

	ajogMinus          [maxAxes + 1]bool
	ajogPlus           [maxAxes + 1]bool
	ajogAnalog         [maxAxes + 1]float64
	ajogIncrementPlus  [maxAxes + 1]bool
	ajogIncrementMinus [maxAxes + 1]bool
	axisNrSelect       [maxAxes]bool

	foCounts int32
	roCounts int32
	mvCounts int32
	soCounts [haluiMaxSpindles + 1]int32

	foIncrease bool
	foDecrease bool
	foReset    bool
	roIncrease bool
	roDecrease bool
	roReset    bool
	mvIncrease bool
	mvDecrease bool
	soIncrease [haluiMaxSpindles + 1]bool
	soDecrease [haluiMaxSpindles + 1]bool
	soReset    [haluiMaxSpindles + 1]bool

	spindleStart    [haluiMaxSpindles + 1]bool
	spindleStop     [haluiMaxSpindles + 1]bool
	spindleForward  [haluiMaxSpindles + 1]bool
	spindleReverse  [haluiMaxSpindles + 1]bool
	spindleIncrease [haluiMaxSpindles + 1]bool
	spindleDecrease [haluiMaxSpindles + 1]bool
	spindleBrakeOn  [haluiMaxSpindles + 1]bool
	spindleBrakeOff [haluiMaxSpindles + 1]bool

	abort   bool
	homeAll bool

	mdiCommands [haluiMaxMDI]bool

	notificationsClear      bool
	notificationsClearInfo  bool
	notificationsClearError bool
}

// newHalUI creates a HAL component with the given name and all its pins.
// The component name determines the pin prefix (e.g. "halui" → "halui.machine.on").
func newHalUI(compName string, numJoints, numSpindles int, axisMask int32, mdiCommands []string) (*halUI, error) {
	comp, err := hal.NewComponent(compName)
	if err != nil {
		return nil, fmt.Errorf("halui: hal_init(%s): %w", compName, err)
	}

	h := &halUI{
		comp:            comp,
		numJoints:       numJoints,
		numSpindles:     numSpindles,
		axisMask:        axisMask,
		numMDI:          len(mdiCommands),
		mdiCommands_str: mdiCommands,
	}

	if err := h.createPins(); err != nil {
		return nil, err
	}

	comp.Ready()
	return h, nil
}

func (h *halUI) createPins() error {
	var err error
	c := h.comp

	// Machine state
	if h.machineOn, err = hal.NewPin[bool](c, "machine.on", hal.In); err != nil {
		return err
	}
	if h.machineOff, err = hal.NewPin[bool](c, "machine.off", hal.In); err != nil {
		return err
	}
	if h.machineIsOn, err = hal.NewPin[bool](c, "machine.is-on", hal.Out); err != nil {
		return err
	}
	if h.estopActivate, err = hal.NewPin[bool](c, "estop.activate", hal.In); err != nil {
		return err
	}
	if h.estopReset, err = hal.NewPin[bool](c, "estop.reset", hal.In); err != nil {
		return err
	}
	if h.estopIsActivated, err = hal.NewPin[bool](c, "estop.is-activated", hal.Out); err != nil {
		return err
	}

	// Mode
	if h.modeManual, err = hal.NewPin[bool](c, "mode.manual", hal.In); err != nil {
		return err
	}
	if h.modeIsManual, err = hal.NewPin[bool](c, "mode.is-manual", hal.Out); err != nil {
		return err
	}
	if h.modeAuto, err = hal.NewPin[bool](c, "mode.auto", hal.In); err != nil {
		return err
	}
	if h.modeIsAuto, err = hal.NewPin[bool](c, "mode.is-auto", hal.Out); err != nil {
		return err
	}
	if h.modeMDI, err = hal.NewPin[bool](c, "mode.mdi", hal.In); err != nil {
		return err
	}
	if h.modeIsMDI, err = hal.NewPin[bool](c, "mode.is-mdi", hal.Out); err != nil {
		return err
	}
	if h.modeTeleop, err = hal.NewPin[bool](c, "mode.teleop", hal.In); err != nil {
		return err
	}
	if h.modeIsTeleop, err = hal.NewPin[bool](c, "mode.is-teleop", hal.Out); err != nil {
		return err
	}
	if h.modeJoint, err = hal.NewPin[bool](c, "mode.joint", hal.In); err != nil {
		return err
	}
	if h.modeIsJoint, err = hal.NewPin[bool](c, "mode.is-joint", hal.Out); err != nil {
		return err
	}

	// Coolant
	if h.mistOn, err = hal.NewPin[bool](c, "mist.on", hal.In); err != nil {
		return err
	}
	if h.mistOff, err = hal.NewPin[bool](c, "mist.off", hal.In); err != nil {
		return err
	}
	if h.mistIsOn, err = hal.NewPin[bool](c, "mist.is-on", hal.Out); err != nil {
		return err
	}
	if h.floodOn, err = hal.NewPin[bool](c, "flood.on", hal.In); err != nil {
		return err
	}
	if h.floodOff, err = hal.NewPin[bool](c, "flood.off", hal.In); err != nil {
		return err
	}
	if h.floodIsOn, err = hal.NewPin[bool](c, "flood.is-on", hal.Out); err != nil {
		return err
	}

	// Lube
	if h.lubeOn, err = hal.NewPin[bool](c, "lube.on", hal.In); err != nil {
		return err
	}
	if h.lubeOff, err = hal.NewPin[bool](c, "lube.off", hal.In); err != nil {
		return err
	}
	if h.lubeIsOn, err = hal.NewPin[bool](c, "lube.is-on", hal.Out); err != nil {
		return err
	}

	// Program control
	if h.programIsIdle, err = hal.NewPin[bool](c, "program.is-idle", hal.Out); err != nil {
		return err
	}
	if h.programIsRunning, err = hal.NewPin[bool](c, "program.is-running", hal.Out); err != nil {
		return err
	}
	if h.programIsPaused, err = hal.NewPin[bool](c, "program.is-paused", hal.Out); err != nil {
		return err
	}
	if h.programRun, err = hal.NewPin[bool](c, "program.run", hal.In); err != nil {
		return err
	}
	if h.programPause, err = hal.NewPin[bool](c, "program.pause", hal.In); err != nil {
		return err
	}
	if h.programResume, err = hal.NewPin[bool](c, "program.resume", hal.In); err != nil {
		return err
	}
	if h.programStep, err = hal.NewPin[bool](c, "program.step", hal.In); err != nil {
		return err
	}
	if h.programStop, err = hal.NewPin[bool](c, "program.stop", hal.In); err != nil {
		return err
	}
	if h.programOsOn, err = hal.NewPin[bool](c, "program.optional-stop.on", hal.In); err != nil {
		return err
	}
	if h.programOsOff, err = hal.NewPin[bool](c, "program.optional-stop.off", hal.In); err != nil {
		return err
	}
	if h.programOsIsOn, err = hal.NewPin[bool](c, "program.optional-stop.is-on", hal.Out); err != nil {
		return err
	}
	if h.programBdOn, err = hal.NewPin[bool](c, "program.block-delete.on", hal.In); err != nil {
		return err
	}
	if h.programBdOff, err = hal.NewPin[bool](c, "program.block-delete.off", hal.In); err != nil {
		return err
	}
	if h.programBdIsOn, err = hal.NewPin[bool](c, "program.block-delete.is-on", hal.Out); err != nil {
		return err
	}

	// Tool (output)
	if h.toolNumber, err = hal.NewPin[uint32](c, "tool.number", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetX, err = hal.NewPin[float64](c, "tool.length_offset.x", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetY, err = hal.NewPin[float64](c, "tool.length_offset.y", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetZ, err = hal.NewPin[float64](c, "tool.length_offset.z", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetA, err = hal.NewPin[float64](c, "tool.length_offset.a", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetB, err = hal.NewPin[float64](c, "tool.length_offset.b", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetC, err = hal.NewPin[float64](c, "tool.length_offset.c", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetU, err = hal.NewPin[float64](c, "tool.length_offset.u", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetV, err = hal.NewPin[float64](c, "tool.length_offset.v", hal.Out); err != nil {
		return err
	}
	if h.toolLengthOffsetW, err = hal.NewPin[float64](c, "tool.length_offset.w", hal.Out); err != nil {
		return err
	}
	if h.toolDiameter, err = hal.NewPin[float64](c, "tool.diameter", hal.Out); err != nil {
		return err
	}

	// Joint jog
	if h.jjogSpeed, err = hal.NewPin[float64](c, "joint.jog-speed", hal.In); err != nil {
		return err
	}
	if h.jjogDeadband, err = hal.NewPin[float64](c, "joint.jog-deadband", hal.In); err != nil {
		return err
	}
	for i := 0; i <= h.numJoints; i++ {
		sfx := fmt.Sprintf("joint.%d", i)
		if i == h.numJoints {
			sfx = "joint.selected"
		}
		if h.jjogMinus[i], err = hal.NewPin[bool](c, sfx+".minus", hal.In); err != nil {
			return err
		}
		if h.jjogPlus[i], err = hal.NewPin[bool](c, sfx+".plus", hal.In); err != nil {
			return err
		}
		if h.jjogAnalog[i], err = hal.NewPin[float64](c, sfx+".analog", hal.In); err != nil {
			return err
		}
		if h.jjogIncrement[i], err = hal.NewPin[float64](c, sfx+".increment", hal.In); err != nil {
			return err
		}
		if h.jjogIncrementPlus[i], err = hal.NewPin[bool](c, sfx+".increment-plus", hal.In); err != nil {
			return err
		}
		if h.jjogIncrementMinus[i], err = hal.NewPin[bool](c, sfx+".increment-minus", hal.In); err != nil {
			return err
		}
	}

	// Axis (teleop) jog
	if h.ajogSpeed, err = hal.NewPin[float64](c, "axis.jog-speed", hal.In); err != nil {
		return err
	}
	if h.ajogDeadband, err = hal.NewPin[float64](c, "axis.jog-deadband", hal.In); err != nil {
		return err
	}
	letters := "xyzabcuvw"
	for i := 0; i < maxAxes; i++ {
		if h.axisMask&(1<<i) == 0 {
			continue
		}
		sfx := fmt.Sprintf("axis.%c", letters[i])
		if h.ajogMinus[i], err = hal.NewPin[bool](c, sfx+".minus", hal.In); err != nil {
			return err
		}
		if h.ajogPlus[i], err = hal.NewPin[bool](c, sfx+".plus", hal.In); err != nil {
			return err
		}
		if h.ajogAnalog[i], err = hal.NewPin[float64](c, sfx+".analog", hal.In); err != nil {
			return err
		}
		if h.ajogIncrement[i], err = hal.NewPin[float64](c, sfx+".increment", hal.In); err != nil {
			return err
		}
		if h.ajogIncrementPlus[i], err = hal.NewPin[bool](c, sfx+".increment-plus", hal.In); err != nil {
			return err
		}
		if h.ajogIncrementMinus[i], err = hal.NewPin[bool](c, sfx+".increment-minus", hal.In); err != nil {
			return err
		}
	}
	// "selected" axis jog (index = maxAxes slot, uses "axis.selected" prefix)
	if h.ajogMinus[maxAxes], err = hal.NewPin[bool](c, "axis.selected.minus", hal.In); err != nil {
		return err
	}
	if h.ajogPlus[maxAxes], err = hal.NewPin[bool](c, "axis.selected.plus", hal.In); err != nil {
		return err
	}
	if h.ajogAnalog[maxAxes], err = hal.NewPin[float64](c, "axis.selected.analog", hal.In); err != nil {
		return err
	}
	if h.ajogIncrement[maxAxes], err = hal.NewPin[float64](c, "axis.selected.increment", hal.In); err != nil {
		return err
	}
	if h.ajogIncrementPlus[maxAxes], err = hal.NewPin[bool](c, "axis.selected.increment-plus", hal.In); err != nil {
		return err
	}
	if h.ajogIncrementMinus[maxAxes], err = hal.NewPin[bool](c, "axis.selected.increment-minus", hal.In); err != nil {
		return err
	}

	// Joint/axis selection
	if h.jointSelected, err = hal.NewPin[uint32](c, "joint.selected", hal.Out); err != nil {
		return err
	}
	if h.axisSelected, err = hal.NewPin[uint32](c, "axis.selected", hal.Out); err != nil {
		return err
	}
	for i := 0; i < h.numJoints; i++ {
		if h.jointNrSelect[i], err = hal.NewPin[bool](c, fmt.Sprintf("joint.%d.select", i), hal.In); err != nil {
			return err
		}
		if h.jointIsSelected[i], err = hal.NewPin[bool](c, fmt.Sprintf("joint.%d.is-selected", i), hal.Out); err != nil {
			return err
		}
	}
	for i := 0; i < maxAxes; i++ {
		if h.axisMask&(1<<i) == 0 {
			continue
		}
		if h.axisNrSelect[i], err = hal.NewPin[bool](c, fmt.Sprintf("axis.%c.select", letters[i]), hal.In); err != nil {
			return err
		}
		if h.axisIsSelected[i], err = hal.NewPin[bool](c, fmt.Sprintf("axis.%c.is-selected", letters[i]), hal.Out); err != nil {
			return err
		}
	}

	// Joint home/unhome/status
	for i := 0; i <= h.numJoints; i++ {
		sfx := fmt.Sprintf("joint.%d", i)
		if i == h.numJoints {
			sfx = "joint.selected"
		}
		if h.jointHome[i], err = hal.NewPin[bool](c, sfx+".home", hal.In); err != nil {
			return err
		}
		if h.jointUnhome[i], err = hal.NewPin[bool](c, sfx+".unhome", hal.In); err != nil {
			return err
		}
		if h.jointIsHomed[i], err = hal.NewPin[bool](c, sfx+".is-homed", hal.Out); err != nil {
			return err
		}
		if h.jointHasFault[i], err = hal.NewPin[bool](c, sfx+".has-fault", hal.Out); err != nil {
			return err
		}
		if h.jointOverrideLimits[i], err = hal.NewPin[bool](c, sfx+".override-limits", hal.Out); err != nil {
			return err
		}
		if h.jointOnSoftMinLimit[i], err = hal.NewPin[bool](c, sfx+".on-soft-min-limit", hal.Out); err != nil {
			return err
		}
		if h.jointOnSoftMaxLimit[i], err = hal.NewPin[bool](c, sfx+".on-soft-max-limit", hal.Out); err != nil {
			return err
		}
		if h.jointOnHardMinLimit[i], err = hal.NewPin[bool](c, sfx+".on-hard-min-limit", hal.Out); err != nil {
			return err
		}
		if h.jointOnHardMaxLimit[i], err = hal.NewPin[bool](c, sfx+".on-hard-max-limit", hal.Out); err != nil {
			return err
		}
	}

	// Feed override
	if h.foCounts, err = hal.NewPin[int32](c, "feed-override.counts", hal.In); err != nil {
		return err
	}
	if h.foCountEnable, err = hal.NewPin[bool](c, "feed-override.count-enable", hal.In); err != nil {
		return err
	}
	if h.foDirectValue, err = hal.NewPin[bool](c, "feed-override.direct-value", hal.In); err != nil {
		return err
	}
	if h.foScale, err = hal.NewPin[float64](c, "feed-override.scale", hal.In); err != nil {
		return err
	}
	if h.foValue, err = hal.NewPin[float64](c, "feed-override.value", hal.Out); err != nil {
		return err
	}
	if h.foIncrease, err = hal.NewPin[bool](c, "feed-override.increase", hal.In); err != nil {
		return err
	}
	if h.foDecrease, err = hal.NewPin[bool](c, "feed-override.decrease", hal.In); err != nil {
		return err
	}
	if h.foReset, err = hal.NewPin[bool](c, "feed-override.reset", hal.In); err != nil {
		return err
	}

	// Rapid override
	if h.roCounts, err = hal.NewPin[int32](c, "rapid-override.counts", hal.In); err != nil {
		return err
	}
	if h.roCountEnable, err = hal.NewPin[bool](c, "rapid-override.count-enable", hal.In); err != nil {
		return err
	}
	if h.roDirectValue, err = hal.NewPin[bool](c, "rapid-override.direct-value", hal.In); err != nil {
		return err
	}
	if h.roScale, err = hal.NewPin[float64](c, "rapid-override.scale", hal.In); err != nil {
		return err
	}
	if h.roValue, err = hal.NewPin[float64](c, "rapid-override.value", hal.Out); err != nil {
		return err
	}
	if h.roIncrease, err = hal.NewPin[bool](c, "rapid-override.increase", hal.In); err != nil {
		return err
	}
	if h.roDecrease, err = hal.NewPin[bool](c, "rapid-override.decrease", hal.In); err != nil {
		return err
	}
	if h.roReset, err = hal.NewPin[bool](c, "rapid-override.reset", hal.In); err != nil {
		return err
	}

	// Max velocity
	if h.mvCounts, err = hal.NewPin[int32](c, "max-velocity.counts", hal.In); err != nil {
		return err
	}
	if h.mvCountEnable, err = hal.NewPin[bool](c, "max-velocity.count-enable", hal.In); err != nil {
		return err
	}
	if h.mvDirectValue, err = hal.NewPin[bool](c, "max-velocity.direct-value", hal.In); err != nil {
		return err
	}
	if h.mvScale, err = hal.NewPin[float64](c, "max-velocity.scale", hal.In); err != nil {
		return err
	}
	if h.mvValue, err = hal.NewPin[float64](c, "max-velocity.value", hal.Out); err != nil {
		return err
	}
	if h.mvIncrease, err = hal.NewPin[bool](c, "max-velocity.increase", hal.In); err != nil {
		return err
	}
	if h.mvDecrease, err = hal.NewPin[bool](c, "max-velocity.decrease", hal.In); err != nil {
		return err
	}

	// Spindle override + control (per-spindle)
	for i := 0; i < h.numSpindles; i++ {
		sfx := fmt.Sprintf("spindle.%d", i)
		if h.soCounts[i], err = hal.NewPin[int32](c, sfx+".override.counts", hal.In); err != nil {
			return err
		}
		if h.soCountEnable[i], err = hal.NewPin[bool](c, sfx+".override.count-enable", hal.In); err != nil {
			return err
		}
		if h.soDirectValue[i], err = hal.NewPin[bool](c, sfx+".override.direct-value", hal.In); err != nil {
			return err
		}
		if h.soScale[i], err = hal.NewPin[float64](c, sfx+".override.scale", hal.In); err != nil {
			return err
		}
		if h.soValue[i], err = hal.NewPin[float64](c, sfx+".override.value", hal.Out); err != nil {
			return err
		}
		if h.soIncrease[i], err = hal.NewPin[bool](c, sfx+".override.increase", hal.In); err != nil {
			return err
		}
		if h.soDecrease[i], err = hal.NewPin[bool](c, sfx+".override.decrease", hal.In); err != nil {
			return err
		}
		if h.soReset[i], err = hal.NewPin[bool](c, sfx+".override.reset", hal.In); err != nil {
			return err
		}
		if h.spindleStart[i], err = hal.NewPin[bool](c, sfx+".start", hal.In); err != nil {
			return err
		}
		if h.spindleStop[i], err = hal.NewPin[bool](c, sfx+".stop", hal.In); err != nil {
			return err
		}
		if h.spindleIsOn[i], err = hal.NewPin[bool](c, sfx+".is-on", hal.Out); err != nil {
			return err
		}
		if h.spindleForward[i], err = hal.NewPin[bool](c, sfx+".forward", hal.In); err != nil {
			return err
		}
		if h.spindleRunsForward[i], err = hal.NewPin[bool](c, sfx+".runs-forward", hal.Out); err != nil {
			return err
		}
		if h.spindleReverse[i], err = hal.NewPin[bool](c, sfx+".reverse", hal.In); err != nil {
			return err
		}
		if h.spindleRunsBackward[i], err = hal.NewPin[bool](c, sfx+".runs-backward", hal.Out); err != nil {
			return err
		}
		if h.spindleIncrease[i], err = hal.NewPin[bool](c, sfx+".increase", hal.In); err != nil {
			return err
		}
		if h.spindleDecrease[i], err = hal.NewPin[bool](c, sfx+".decrease", hal.In); err != nil {
			return err
		}
		if h.spindleBrakeOn[i], err = hal.NewPin[bool](c, sfx+".brake-on", hal.In); err != nil {
			return err
		}
		if h.spindleBrakeOff[i], err = hal.NewPin[bool](c, sfx+".brake-off", hal.In); err != nil {
			return err
		}
		if h.spindleBrakeIsOn[i], err = hal.NewPin[bool](c, sfx+".brake-is-on", hal.Out); err != nil {
			return err
		}
	}

	// Abort / home-all
	if h.abort, err = hal.NewPin[bool](c, "abort", hal.In); err != nil {
		return err
	}
	if h.homeAll, err = hal.NewPin[bool](c, "home-all", hal.In); err != nil {
		return err
	}

	// MDI commands
	for i := 0; i < h.numMDI; i++ {
		if h.mdiCommands[i], err = hal.NewPin[bool](c, fmt.Sprintf("mdi-command-%02d", i), hal.In); err != nil {
			return err
		}
	}

	// Axis position (output)
	for i := 0; i < maxAxes; i++ {
		if h.axisMask&(1<<i) == 0 {
			continue
		}
		l := string(letters[i])
		if h.axisPosCommanded[i], err = hal.NewPin[float64](c, "axis."+l+".pos-commanded", hal.Out); err != nil {
			return err
		}
		if h.axisPosFeedback[i], err = hal.NewPin[float64](c, "axis."+l+".pos-feedback", hal.Out); err != nil {
			return err
		}
		if h.axisPosRelative[i], err = hal.NewPin[float64](c, "axis."+l+".pos-relative", hal.Out); err != nil {
			return err
		}
	}

	// Misc
	if h.unitsPerMM, err = hal.NewPin[float64](c, "machine.units-per-mm", hal.Out); err != nil {
		return err
	}
	if h.cycleCount, err = hal.NewPin[uint32](c, "cycle-count", hal.Out); err != nil {
		return err
	}

	// axisui pins (merged)
	if h.notificationsClear, err = hal.NewPin[bool](c, "notifications.clear", hal.In); err != nil {
		return err
	}
	if h.notificationsClearInfo, err = hal.NewPin[bool](c, "notifications.clear-info", hal.In); err != nil {
		return err
	}
	if h.notificationsClearError, err = hal.NewPin[bool](c, "notifications.clear-error", hal.In); err != nil {
		return err
	}
	if h.resumeInhibit, err = hal.NewPin[bool](c, "resume-inhibit", hal.In); err != nil {
		return err
	}
	if h.hasNotifications, err = hal.NewPin[bool](c, "notifications.has-notifications", hal.Out); err != nil {
		return err
	}
	if h.errorActive, err = hal.NewPin[bool](c, "notifications.error", hal.Out); err != nil {
		return err
	}
	if h.isRunning, err = hal.NewPin[bool](c, "is-running", hal.Out); err != nil {
		return err
	}

	return nil
}

// pinGet safely reads a pin that may be nil (axis pins for non-existent axes).
func pinGet[T hal.PinValue](p *hal.Pin[T]) T {
	if p == nil {
		var zero T
		return zero
	}
	return p.Get()
}

// risingEdge returns true if pin went from false to true.
func risingEdge(new, old bool) bool {
	return new && !old
}

// check reads all input pins and dispatches commands for any that changed.
// Called from the monitor goroutine at 10ms intervals.
func (h *halUI) check(t *Task) {
	// Increment cycle count
	if h.cycleCount != nil {
		h.cycleCount.Set(h.cycleCount.Get() + 1)
	}

	h.checkStateAndMode(t)
	h.checkCoolant(t)
	h.checkProgram(t)
	h.checkOverrides(t)
	h.checkSpindles(t)
	h.checkJointJog(t)
	h.checkAxisJog(t)
	h.checkJointSelection(t)
	h.checkAxisSelection(t)
	h.checkHoming(t)
	h.checkMisc(t)
	h.checkMDI(t)
}

func (h *halUI) checkStateAndMode(t *Task) {
	if v := h.machineOn.Get(); risingEdge(v, h.old.machineOn) {
		_ = t.SetState(int32(StateOn))
	}
	h.old.machineOn = h.machineOn.Get()

	if v := h.machineOff.Get(); risingEdge(v, h.old.machineOff) {
		_ = t.SetState(int32(StateOff))
	}
	h.old.machineOff = h.machineOff.Get()

	if v := h.estopActivate.Get(); risingEdge(v, h.old.estopActivate) {
		_ = t.SetState(int32(StateEstop))
	}
	h.old.estopActivate = h.estopActivate.Get()

	if v := h.estopReset.Get(); risingEdge(v, h.old.estopReset) {
		_ = t.SetState(int32(StateEstopReset))
	}
	h.old.estopReset = h.estopReset.Get()

	if v := h.modeManual.Get(); risingEdge(v, h.old.modeManual) {
		_ = t.SetMode(int32(ModeManual))
	}
	h.old.modeManual = h.modeManual.Get()

	if v := h.modeAuto.Get(); risingEdge(v, h.old.modeAuto) {
		_ = t.SetMode(int32(ModeAuto))
	}
	h.old.modeAuto = h.modeAuto.Get()

	if v := h.modeMDI.Get(); risingEdge(v, h.old.modeMDI) {
		_ = t.SetMode(int32(ModeMDI))
	}
	h.old.modeMDI = h.modeMDI.Get()

	if v := h.modeTeleop.Get(); risingEdge(v, h.old.modeTeleop) {
		_ = t.TeleopEnable(true)
	}
	h.old.modeTeleop = h.modeTeleop.Get()

	if v := h.modeJoint.Get(); risingEdge(v, h.old.modeJoint) {
		_ = t.TeleopEnable(false)
	}
	h.old.modeJoint = h.modeJoint.Get()
}

func (h *halUI) checkCoolant(t *Task) {
	if v := h.mistOn.Get(); risingEdge(v, h.old.mistOn) {
		_ = t.Mist(true)
	}
	h.old.mistOn = h.mistOn.Get()

	if v := h.mistOff.Get(); risingEdge(v, h.old.mistOff) {
		_ = t.Mist(false)
	}
	h.old.mistOff = h.mistOff.Get()

	if v := h.floodOn.Get(); risingEdge(v, h.old.floodOn) {
		_ = t.Flood(true)
	}
	h.old.floodOn = h.floodOn.Get()

	if v := h.floodOff.Get(); risingEdge(v, h.old.floodOff) {
		_ = t.Flood(false)
	}
	h.old.floodOff = h.floodOff.Get()
}

func (h *halUI) checkProgram(t *Task) {
	if v := h.programRun.Get(); risingEdge(v, h.old.programRun) {
		_ = t.AutoCommand(AutoRun, 0)
	}
	h.old.programRun = h.programRun.Get()

	if v := h.programPause.Get(); risingEdge(v, h.old.programPause) {
		_ = t.AutoCommand(AutoPause, 0)
	}
	h.old.programPause = h.programPause.Get()

	if v := h.programResume.Get(); risingEdge(v, h.old.programResume) {
		_ = t.AutoCommand(AutoResume, 0)
	}
	h.old.programResume = h.programResume.Get()

	if v := h.programStep.Get(); risingEdge(v, h.old.programStep) {
		_ = t.AutoCommand(AutoStep, 0)
	}
	h.old.programStep = h.programStep.Get()

	if v := h.programStop.Get(); risingEdge(v, h.old.programStop) {
		_ = t.Abort()
	}
	h.old.programStop = h.programStop.Get()

	if v := h.programOsOn.Get(); risingEdge(v, h.old.programOsOn) {
		_ = t.SetOptionalStop(true)
	}
	h.old.programOsOn = h.programOsOn.Get()

	if v := h.programOsOff.Get(); risingEdge(v, h.old.programOsOff) {
		_ = t.SetOptionalStop(false)
	}
	h.old.programOsOff = h.programOsOff.Get()

	if v := h.programBdOn.Get(); risingEdge(v, h.old.programBdOn) {
		_ = t.SetBlockDelete(true)
	}
	h.old.programBdOn = h.programBdOn.Get()

	if v := h.programBdOff.Get(); risingEdge(v, h.old.programBdOff) {
		_ = t.SetBlockDelete(false)
	}
	h.old.programBdOff = h.programBdOff.Get()
}

func (h *halUI) checkOverrides(t *Task) {
	// Feed override encoder
	counts := h.foCounts.Get()
	if counts != h.old.foCounts {
		if h.foCountEnable.Get() {
			foValue := h.foValue.Get()
			if h.foDirectValue.Get() {
				foValue = float64(counts) * h.foScale.Get()
			} else {
				foValue += float64(counts-h.old.foCounts) * h.foScale.Get()
			}
			_ = t.SetFeedOverride(foValue)
		}
		h.old.foCounts = counts
	}
	if v := h.foIncrease.Get(); risingEdge(v, h.old.foIncrease) {
		_ = t.SetFeedOverride(h.foValue.Get() + h.foScale.Get())
	}
	h.old.foIncrease = h.foIncrease.Get()
	if v := h.foDecrease.Get(); risingEdge(v, h.old.foDecrease) {
		_ = t.SetFeedOverride(h.foValue.Get() - h.foScale.Get())
	}
	h.old.foDecrease = h.foDecrease.Get()
	if v := h.foReset.Get(); risingEdge(v, h.old.foReset) {
		_ = t.SetFeedOverride(1.0)
	}
	h.old.foReset = h.foReset.Get()

	// Rapid override encoder
	counts = h.roCounts.Get()
	if counts != h.old.roCounts {
		if h.roCountEnable.Get() {
			roValue := h.roValue.Get()
			if h.roDirectValue.Get() {
				roValue = float64(counts) * h.roScale.Get()
			} else {
				roValue += float64(counts-h.old.roCounts) * h.roScale.Get()
			}
			_ = t.SetRapidOverride(roValue)
		}
		h.old.roCounts = counts
	}
	if v := h.roIncrease.Get(); risingEdge(v, h.old.roIncrease) {
		_ = t.SetRapidOverride(h.roValue.Get() + h.roScale.Get())
	}
	h.old.roIncrease = h.roIncrease.Get()
	if v := h.roDecrease.Get(); risingEdge(v, h.old.roDecrease) {
		_ = t.SetRapidOverride(h.roValue.Get() - h.roScale.Get())
	}
	h.old.roDecrease = h.roDecrease.Get()
	if v := h.roReset.Get(); risingEdge(v, h.old.roReset) {
		_ = t.SetRapidOverride(1.0)
	}
	h.old.roReset = h.roReset.Get()

	// Max velocity encoder
	counts = h.mvCounts.Get()
	if counts != h.old.mvCounts {
		if h.mvCountEnable.Get() {
			mvValue := h.mvValue.Get()
			if h.mvDirectValue.Get() {
				mvValue = float64(counts) * h.mvScale.Get()
			} else {
				mvValue += float64(counts-h.old.mvCounts) * h.mvScale.Get()
			}
			_ = t.SetMaxVelocity(mvValue)
		}
		h.old.mvCounts = counts
	}
	if v := h.mvIncrease.Get(); risingEdge(v, h.old.mvIncrease) {
		_ = t.SetMaxVelocity(h.mvValue.Get() + h.mvScale.Get())
	}
	h.old.mvIncrease = h.mvIncrease.Get()
	if v := h.mvDecrease.Get(); risingEdge(v, h.old.mvDecrease) {
		_ = t.SetMaxVelocity(h.mvValue.Get() - h.mvScale.Get())
	}
	h.old.mvDecrease = h.mvDecrease.Get()

	// Spindle overrides
	for i := 0; i < h.numSpindles; i++ {
		counts := h.soCounts[i].Get()
		if counts != h.old.soCounts[i] {
			if h.soCountEnable[i].Get() {
				soValue := h.soValue[i].Get()
				if h.soDirectValue[i].Get() {
					soValue = float64(counts) * h.soScale[i].Get()
				} else {
					soValue += float64(counts-h.old.soCounts[i]) * h.soScale[i].Get()
				}
				_ = t.SetSpindleOverride(soValue, int32(i))
			}
			h.old.soCounts[i] = counts
		}
		if v := h.soIncrease[i].Get(); risingEdge(v, h.old.soIncrease[i]) {
			_ = t.SetSpindleOverride(h.soValue[i].Get()+h.soScale[i].Get(), int32(i))
		}
		h.old.soIncrease[i] = h.soIncrease[i].Get()
		if v := h.soDecrease[i].Get(); risingEdge(v, h.old.soDecrease[i]) {
			_ = t.SetSpindleOverride(h.soValue[i].Get()-h.soScale[i].Get(), int32(i))
		}
		h.old.soDecrease[i] = h.soDecrease[i].Get()
		if v := h.soReset[i].Get(); risingEdge(v, h.old.soReset[i]) {
			_ = t.SetSpindleOverride(1.0, int32(i))
		}
		h.old.soReset[i] = h.soReset[i].Get()
	}
}

func (h *halUI) checkSpindles(t *Task) {
	for i := 0; i < h.numSpindles; i++ {
		if v := h.spindleStart[i].Get(); risingEdge(v, h.old.spindleStart[i]) {
			_ = t.Spindle(SpindleForward, 0, int32(i), 0)
		}
		h.old.spindleStart[i] = h.spindleStart[i].Get()

		if v := h.spindleStop[i].Get(); risingEdge(v, h.old.spindleStop[i]) {
			_ = t.Spindle(SpindleOff, 0, int32(i), 0)
		}
		h.old.spindleStop[i] = h.spindleStop[i].Get()

		if v := h.spindleForward[i].Get(); risingEdge(v, h.old.spindleForward[i]) {
			_ = t.Spindle(SpindleForward, 0, int32(i), 0)
		}
		h.old.spindleForward[i] = h.spindleForward[i].Get()

		if v := h.spindleReverse[i].Get(); risingEdge(v, h.old.spindleReverse[i]) {
			_ = t.Spindle(SpindleReverse, 0, int32(i), 0)
		}
		h.old.spindleReverse[i] = h.spindleReverse[i].Get()

		if v := h.spindleIncrease[i].Get(); risingEdge(v, h.old.spindleIncrease[i]) {
			_ = t.Spindle(SpindleIncrease, 0, int32(i), 0)
		}
		h.old.spindleIncrease[i] = h.spindleIncrease[i].Get()

		if v := h.spindleDecrease[i].Get(); risingEdge(v, h.old.spindleDecrease[i]) {
			_ = t.Spindle(SpindleDecrease, 0, int32(i), 0)
		}
		h.old.spindleDecrease[i] = h.spindleDecrease[i].Get()
	}
}

func (h *halUI) checkJointJog(t *Task) {
	jjogSpeed := h.jjogSpeed.Get()
	speedChanged := math.Abs(jjogSpeed-h.old.jjogSpeed) > epsilon
	if speedChanged {
		h.old.jjogSpeed = jjogSpeed
	}

	for i := 0; i <= h.numJoints; i++ {
		joint := int32(i)
		if i == h.numJoints {
			// "selected" slot — use currently selected joint
			joint = int32(h.jointSelected.Get())
		}

		// Continuous jog minus
		v := pinGet(h.jjogMinus[i])
		if v != h.old.jjogMinus[i] || (v && speedChanged) {
			if v {
				_ = t.Jog(JogContinuous, true, joint, -jjogSpeed, 0)
			} else {
				_ = t.Jog(JogStop, true, joint, 0, 0)
			}
			h.old.jjogMinus[i] = v
		}

		// Continuous jog plus
		v = pinGet(h.jjogPlus[i])
		if v != h.old.jjogPlus[i] || (v && speedChanged) {
			if v {
				_ = t.Jog(JogContinuous, true, joint, jjogSpeed, 0)
			} else {
				_ = t.Jog(JogStop, true, joint, 0, 0)
			}
			h.old.jjogPlus[i] = v
		}

		// Analog jog
		analog := pinGet(h.jjogAnalog[i])
		deadband := h.jjogDeadband.Get()
		active := math.Abs(analog) > deadband
		if analog != h.old.jjogAnalog[i] || (active && speedChanged) {
			if active {
				_ = t.Jog(JogContinuous, true, joint, jjogSpeed*analog, 0)
			} else {
				_ = t.Jog(JogStop, true, joint, 0, 0)
			}
			h.old.jjogAnalog[i] = analog
		}

		// Incremental jog plus
		v = pinGet(h.jjogIncrementPlus[i])
		if risingEdge(v, h.old.jjogIncrementPlus[i]) {
			incr := pinGet(h.jjogIncrement[i])
			_ = t.Jog(JogIncrement, true, joint, jjogSpeed, incr)
		}
		h.old.jjogIncrementPlus[i] = v

		// Incremental jog minus
		v = pinGet(h.jjogIncrementMinus[i])
		if risingEdge(v, h.old.jjogIncrementMinus[i]) {
			incr := pinGet(h.jjogIncrement[i])
			_ = t.Jog(JogIncrement, true, joint, jjogSpeed, -incr)
		}
		h.old.jjogIncrementMinus[i] = v
	}
}

func (h *halUI) checkAxisJog(t *Task) {
	ajogSpeed := h.ajogSpeed.Get()
	speedChanged := math.Abs(ajogSpeed-h.old.ajogSpeed) > epsilon
	if speedChanged {
		h.old.ajogSpeed = ajogSpeed
	}

	for i := 0; i <= maxAxes; i++ {
		axis := int32(i)
		if i == maxAxes {
			// "selected" slot — use currently selected axis
			axis = int32(h.axisSelected.Get())
		} else if h.axisMask&(1<<i) == 0 {
			continue
		}

		// Continuous jog minus
		v := pinGet(h.ajogMinus[i])
		if v != h.old.ajogMinus[i] || (v && speedChanged) {
			if v {
				_ = t.Jog(JogContinuous, false, axis, -ajogSpeed, 0)
			} else {
				_ = t.Jog(JogStop, false, axis, 0, 0)
			}
			h.old.ajogMinus[i] = v
		}

		// Continuous jog plus
		v = pinGet(h.ajogPlus[i])
		if v != h.old.ajogPlus[i] || (v && speedChanged) {
			if v {
				_ = t.Jog(JogContinuous, false, axis, ajogSpeed, 0)
			} else {
				_ = t.Jog(JogStop, false, axis, 0, 0)
			}
			h.old.ajogPlus[i] = v
		}

		// Analog jog
		analog := pinGet(h.ajogAnalog[i])
		deadband := h.ajogDeadband.Get()
		active := math.Abs(analog) > deadband
		if analog != h.old.ajogAnalog[i] || (active && speedChanged) {
			if active {
				_ = t.Jog(JogContinuous, false, axis, ajogSpeed*analog, 0)
			} else {
				_ = t.Jog(JogStop, false, axis, 0, 0)
			}
			h.old.ajogAnalog[i] = analog
		}

		// Incremental jog plus
		v = pinGet(h.ajogIncrementPlus[i])
		if risingEdge(v, h.old.ajogIncrementPlus[i]) {
			incr := pinGet(h.ajogIncrement[i])
			_ = t.Jog(JogIncrement, false, axis, ajogSpeed, incr)
		}
		h.old.ajogIncrementPlus[i] = v

		// Incremental jog minus
		v = pinGet(h.ajogIncrementMinus[i])
		if risingEdge(v, h.old.ajogIncrementMinus[i]) {
			incr := pinGet(h.ajogIncrement[i])
			_ = t.Jog(JogIncrement, false, axis, ajogSpeed, -incr)
		}
		h.old.ajogIncrementMinus[i] = v
	}
}

func (h *halUI) checkJointSelection(t *Task) {
	for i := 0; i < h.numJoints; i++ {
		v := h.jointNrSelect[i].Get()
		if risingEdge(v, h.old.jointNrSelect[i]) {
			h.jointSelected.Set(uint32(i))
			// Update is-selected outputs
			for j := 0; j < h.numJoints; j++ {
				if h.jointIsSelected[j] != nil {
					h.jointIsSelected[j].Set(j == i)
				}
			}
		}
		h.old.jointNrSelect[i] = v
	}
}

func (h *halUI) checkAxisSelection(t *Task) {
	for i := 0; i < maxAxes; i++ {
		if h.axisNrSelect[i] == nil {
			continue
		}
		v := h.axisNrSelect[i].Get()
		if risingEdge(v, h.old.axisNrSelect[i]) {
			h.axisSelected.Set(uint32(i))
			// Update is-selected outputs
			for j := 0; j < maxAxes; j++ {
				if h.axisIsSelected[j] != nil {
					h.axisIsSelected[j].Set(j == i)
				}
			}
		}
		h.old.axisNrSelect[i] = v
	}
}

func (h *halUI) checkHoming(t *Task) {
	for i := 0; i <= h.numJoints; i++ {
		joint := int32(i)
		if i == h.numJoints {
			joint = int32(h.jointSelected.Get())
		}
		v := pinGet(h.jointHome[i])
		if risingEdge(v, h.old.jointHome[i]) {
			_ = t.Home(joint)
		}
		h.old.jointHome[i] = v

		v = pinGet(h.jointUnhome[i])
		if risingEdge(v, h.old.jointUnhome[i]) {
			_ = t.Unhome(joint)
		}
		h.old.jointUnhome[i] = v
	}

	// Home-all
	if v := h.homeAll.Get(); risingEdge(v, h.old.homeAll) {
		_ = t.Home(-1)
	}
	h.old.homeAll = h.homeAll.Get()
}

func (h *halUI) checkMisc(t *Task) {
	if v := h.abort.Get(); risingEdge(v, h.old.abort) {
		_ = t.Abort()
	}
	h.old.abort = h.abort.Get()
}

func (h *halUI) checkMDI(t *Task) {
	for i := 0; i < h.numMDI; i++ {
		v := h.mdiCommands[i].Get()
		if risingEdge(v, h.old.mdiCommands[i]) {
			if i < len(h.mdiCommands_str) {
				_ = t.MDI(h.mdiCommands_str[i])
			}
		}
		h.old.mdiCommands[i] = v
	}
}

// updateOutputs writes status information to the output pins.
// Called from the monitor goroutine after check().
func (h *halUI) updateOutputs(t *Task) {
	t.mu.Lock()
	state := t.state
	mode := t.mode
	interpState := t.interpState
	t.mu.Unlock()

	// Machine state
	h.machineIsOn.Set(state == StateOn)
	h.estopIsActivated.Set(state == StateEstop)

	// Mode
	h.modeIsManual.Set(mode == ModeManual)
	h.modeIsAuto.Set(mode == ModeAuto)
	h.modeIsMDI.Set(mode == ModeMDI)

	// Program state
	h.programIsIdle.Set(interpState == InterpIdle)
	h.programIsRunning.Set(interpState == InterpReading)
	h.programIsPaused.Set(interpState == InterpPaused)

	// Override values (read from motion status for feedback)
	if t.status != nil {
		ms, err := t.status.GetStatus()
		if err == nil {
			h.foValue.Set(ms.FeedScale)
			h.roValue.Set(ms.RapidScale)
			h.mvValue.Set(ms.LimitVel)
			for i := 0; i < h.numSpindles; i++ {
				if i < len(ms.Spindles) {
					h.soValue[i].Set(ms.Spindles[i].Scale)
				}
			}
		}
	}

	// UI alive
	h.isRunning.Set(true)
}
