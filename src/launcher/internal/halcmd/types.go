package halcmd

import hal "linuxcnc.org/hal"

// Type aliases for HAL types used by the halcmd package.
// These are identical to the types in the hal package (not wrappers),
// so halcmd.PinType and hal.PinType are interchangeable.

// PinType is an alias for hal.PinType.
type PinType = hal.PinType

// Direction is an alias for hal.Direction.
type Direction = hal.Direction

// Pin type constants (aliases for the hal package constants).
const (
	TypeBit   = hal.TypeBit
	TypeFloat = hal.TypeFloat
	TypeS32   = hal.TypeS32
	TypeU32   = hal.TypeU32
	TypePort  = hal.TypePort
)

// Pin direction constants (aliases for the hal package constants).
const (
	In  = hal.In
	Out = hal.Out
	IO  = hal.IO
)
