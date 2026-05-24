package task

// Canon getter callbacks — called by the interpreter to query current state.
// These read from the Task's MotionStatus interface or from canon state.

func (c *Canon) GetExternalFeedRate() float64 {
	return c.state.toProg(c.state.linearFeedRate * 60.0) // mm/sec → units/min
}

func (c *Canon) GetExternalTraverseRate() float64 {
	// Return in program units per minute (matching C emccanon behavior).
	// Read from task.maxVelocity (set by loadConfig) like C reads from STAT.
	return c.state.toProg(c.task.maxVelocity) * 60.0
}

func (c *Canon) GetExternalLengthUnitType() int32 {
	return c.state.lengthUnits
}

func (c *Canon) GetExternalLengthUnits() float64 {
	switch c.state.lengthUnits {
	case CanonUnitsInches:
		return 1.0 / 25.4
	case CanonUnitsCM:
		return 1.0 / 10.0
	default:
		return 1.0
	}
}

func (c *Canon) GetExternalAngleUnits() float64 {
	return 1.0 // always degrees
}

func (c *Canon) GetExternalMotionControlMode() int32 {
	return c.state.motionMode
}

func (c *Canon) GetExternalMotionControlTolerance() float64 {
	return c.state.toProg(c.state.motionTolerance)
}

func (c *Canon) GetExternalMotionControlNaivecamTolerance() float64 {
	return c.state.toProg(c.state.naivecamTol)
}

func (c *Canon) GetExternalFlood() int32 {
	// TODO: read from IO status
	return 0
}

func (c *Canon) GetExternalMist() int32 {
	// TODO: read from IO status
	return 0
}

// Position getters — return current position in program units.

func (c *Canon) GetExternalPositionX() float64 {
	return c.state.toProg(c.state.endPoint.X)
}

func (c *Canon) GetExternalPositionY() float64 {
	return c.state.toProg(c.state.endPoint.Y)
}

func (c *Canon) GetExternalPositionZ() float64 {
	return c.state.toProg(c.state.endPoint.Z)
}

func (c *Canon) GetExternalPositionA() float64 {
	return c.state.endPoint.A
}

func (c *Canon) GetExternalPositionB() float64 {
	return c.state.endPoint.B
}

func (c *Canon) GetExternalPositionC() float64 {
	return c.state.endPoint.C
}

func (c *Canon) GetExternalPositionU() float64 {
	return c.state.toProg(c.state.endPoint.U)
}

func (c *Canon) GetExternalPositionV() float64 {
	return c.state.toProg(c.state.endPoint.V)
}

func (c *Canon) GetExternalPositionW() float64 {
	return c.state.toProg(c.state.endPoint.W)
}

// Probe position getters — return probe trip position in program units.
// TODO: read actual probe position from motion status.

func (c *Canon) GetExternalProbePositionX() float64  { return 0 }
func (c *Canon) GetExternalProbePositionY() float64  { return 0 }
func (c *Canon) GetExternalProbePositionZ() float64  { return 0 }
func (c *Canon) GetExternalProbePositionA() float64  { return 0 }
func (c *Canon) GetExternalProbePositionB() float64  { return 0 }
func (c *Canon) GetExternalProbePositionC() float64  { return 0 }
func (c *Canon) GetExternalProbePositionU() float64  { return 0 }
func (c *Canon) GetExternalProbePositionV() float64  { return 0 }
func (c *Canon) GetExternalProbePositionW() float64  { return 0 }
func (c *Canon) GetExternalProbeValue() float64      { return 0 }
func (c *Canon) GetExternalProbeTrippedValue() int32 { return 0 }

// Spindle getters.

func (c *Canon) GetExternalSpeed(spindle int32) float64 {
	if spindle >= 0 && spindle < 8 {
		return c.state.spindleSpeed[spindle]
	}
	return 0
}

func (c *Canon) GetExternalSpindle(spindle int32) int32 {
	// CANON_STOPPED=1, CANON_CLOCKWISE=2, CANON_COUNTERCLOCKWISE=3
	if int(spindle) < len(c.state.spindleSpeed) {
		speed := c.state.spindleSpeed[spindle]
		if speed > 0 {
			return 2 // CANON_CLOCKWISE
		} else if speed < 0 {
			return 3 // CANON_COUNTERCLOCKWISE
		}
	}
	return 1 // CANON_STOPPED
}

// Tool getters.

func (c *Canon) GetExternalToolLengthXOffset() float64 { return c.state.toolOffset.X }
func (c *Canon) GetExternalToolLengthYOffset() float64 { return c.state.toolOffset.Y }
func (c *Canon) GetExternalToolLengthZOffset() float64 { return c.state.toolOffset.Z }
func (c *Canon) GetExternalToolLengthAOffset() float64 { return c.state.toolOffset.A }
func (c *Canon) GetExternalToolLengthBOffset() float64 { return c.state.toolOffset.B }
func (c *Canon) GetExternalToolLengthCOffset() float64 { return c.state.toolOffset.C }
func (c *Canon) GetExternalToolLengthUOffset() float64 { return c.state.toolOffset.U }
func (c *Canon) GetExternalToolLengthVOffset() float64 { return c.state.toolOffset.V }
func (c *Canon) GetExternalToolLengthWOffset() float64 { return c.state.toolOffset.W }

func (c *Canon) GetExternalToolSlot() int32 {
	// TODO: read from IO status
	return 0
}

func (c *Canon) GetExternalSelectedToolSlot() int32 {
	return 0
}

func (c *Canon) GetExternalToolTable(pocket int32) (toolno int32, offset [9]float64, diameter, frontangle, backangle float64, orientation int32, err int32) {
	// TODO: read from tool table
	return 0, [9]float64{}, 0, 0, 0, 0, 0
}

func (c *Canon) GetExternalTcFault() int32  { return 0 }
func (c *Canon) GetExternalTcReason() int32 { return 0 }

// Queue/status getters.

func (c *Canon) GetExternalQueueEmpty() int32 {
	if c.task.status != nil {
		v, err := c.task.status.GetInpos()
		if err == nil && v != 0 {
			return 1
		}
	}
	return 0
}

func (c *Canon) GetExternalAxisMask() int32 {
	return c.task.axisMask
}

func (c *Canon) GetExternalDigitalInput(index, def int32) int32 {
	return def
}

func (c *Canon) GetExternalAnalogInput(index int32, def float64) float64 {
	return def
}

func (c *Canon) GetExternalFeedOverrideEnable() int32 {
	if c.state.feedOverrideEnabled {
		return 1
	}
	return 0
}

func (c *Canon) GetExternalSpindleOverrideEnable(spindle int32) int32 {
	if spindle >= 0 && spindle < 8 && c.state.speedOverrideEnabled[spindle] {
		return 1
	}
	return 0
}

func (c *Canon) GetExternalAdaptiveFeedEnable() int32 {
	if c.state.adaptiveFeedEnabled {
		return 1
	}
	return 0
}

func (c *Canon) GetExternalFeedHoldEnable() int32 {
	if c.state.feedHoldEnabled {
		return 1
	}
	return 0
}

func (c *Canon) GetExternalPlane() int32 {
	return c.state.activePlane
}

func (c *Canon) GetExternalParameterFileName(buf *string) {
	*buf = c.parameterFileName
}

func (c *Canon) GetExternalOffsetApplied() int32 {
	return 0
}

func (c *Canon) GetExternalOffsets(offsets *[9]float64) {
	// TODO: return external offsets if applied
	*offsets = [9]float64{}
}

func (c *Canon) GetUserDefinedResult() float64 {
	return 0
}
