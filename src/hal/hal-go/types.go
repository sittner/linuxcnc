package hal

// Direction represents the direction of data flow for a HAL pin.
// It corresponds to hal_pin_dir_t in the C HAL API.
type Direction int

const (
	// In indicates the pin is an input to the component (HAL_IN = 16).
	// The component reads values from this pin.
	In Direction = 16

	// Out indicates the pin is an output from the component (HAL_OUT = 32).
	// The component writes values to this pin.
	Out Direction = 32

	// IO indicates the pin is bidirectional (HAL_IO = HAL_IN | HAL_OUT).
	// The component can both read from and write to this pin.
	IO Direction = 48 // HAL_IN | HAL_OUT
)

// String returns the string representation of the direction.
func (d Direction) String() string {
	switch d {
	case In:
		return "IN"
	case Out:
		return "OUT"
	case IO:
		return "IO"
	default:
		return "UNKNOWN"
	}
}

// ParamDirection represents the direction of data flow for a HAL parameter.
// It corresponds to hal_param_dir_t in the C HAL API.
type ParamDirection int

const (
	// RO indicates the parameter is read-only (HAL_RO = 64).
	// The component can read but not write this parameter.
	RO ParamDirection = 64

	// RW indicates the parameter is read-write (HAL_RW = 192).
	// The component can both read and write this parameter.
	RW ParamDirection = 192
)

// String returns the string representation of the parameter direction.
func (d ParamDirection) String() string {
	switch d {
	case RO:
		return "RO"
	case RW:
		return "RW"
	default:
		return "UNKNOWN"
	}
}

// PinType represents the data type of a HAL pin or signal.
// It corresponds to hal_type_t in the C HAL API.
type PinType int

const (
	// TypeBit represents a boolean value (HAL_BIT = 1).
	TypeBit PinType = 1

	// TypeFloat represents a 64-bit floating point value (HAL_FLOAT = 2).
	TypeFloat PinType = 2

	// TypeS32 represents a signed 32-bit integer (HAL_S32 = 3).
	TypeS32 PinType = 3

	// TypeU32 represents an unsigned 32-bit integer (HAL_U32 = 4).
	TypeU32 PinType = 4
)

// String returns the string representation of the pin type.
func (t PinType) String() string {
	switch t {
	case TypeBit:
		return "BIT"
	case TypeFloat:
		return "FLOAT"
	case TypeS32:
		return "S32"
	case TypeU32:
		return "U32"
	default:
		return "UNKNOWN"
	}
}

// PinValue is a type constraint for values that can be stored in HAL pins.
// These correspond to the actual HAL data types supported by LinuxCNC.
type PinValue interface {
	bool | float64 | int32 | uint32
}
