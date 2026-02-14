package hal

import (
	"fmt"
	"sync"
)

// Pin represents a HAL pin with type-safe access.
//
// Pins are the connection points between HAL components. They can be
// linked to signals, which allow components to exchange data. The generic
// type parameter T ensures type safety at compile time.
//
// Type T must be one of: bool, float64, int32, uint32 (matching HAL types
// HAL_BIT, HAL_FLOAT, HAL_S32, HAL_U32).
type Pin[T PinValue] struct {
	// name is the fully-qualified pin name (e.g., "component.pinname").
	name string

	// direction is the pin direction (In, Out, or IO).
	direction Direction

	// value holds the current pin value (stub for Phase 1).
	// Phase 2+ will use a pointer to HAL shared memory.
	value T

	// comp is the component that owns this pin.
	comp *Component

	// mu protects the pin value.
	mu sync.RWMutex
}

// NewPin creates a new pin with the specified type and direction.
//
// The name should be just the pin name (e.g., "input"), not the full name.
// The component name will be prepended automatically (e.g., "mycomp.input").
//
// Valid directions are In (component reads), Out (component writes), or
// IO (bidirectional).
//
// In Phase 1, this is a stub implementation. Phase 2+ will add the actual
// CGO call to hal_pin_*_new().
//
// Type inference example:
//   pin, err := NewPin[float64](comp, "speed", hal.In)
//   pin, err := NewPin[bool](comp, "enable", hal.In)
//   pin, err := NewPin[int32](comp, "count", hal.Out)
func NewPin[T PinValue](c *Component, name string, dir Direction) (*Pin[T], error) {
	if c == nil {
		return nil, newError("NewPin", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return nil, newError("NewPin", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != In && dir != Out && dir != IO {
		return nil, newError("NewPin", "invalid direction", -22)
	}

	// Build fully-qualified pin name
	fullName := fmt.Sprintf("%s.%s", c.Name(), name)

	// Phase 1 stub: Just create the pin structure
	// Phase 2+ will call the appropriate hal_pin_*_new() function based on type T:
	//   - bool -> hal_pin_bit_new()
	//   - float64 -> hal_pin_float_new()
	//   - int32 -> hal_pin_s32_new()
	//   - uint32 -> hal_pin_u32_new()

	pin := &Pin[T]{
		name:      fullName,
		direction: dir,
		value:     *new(T), // Zero value for type T
		comp:      c,
	}

	return pin, nil
}

// Get reads the current pin value.
//
// For input pins, this reads the value written by the connected signal.
// For output pins, this reads the value last written by Set().
//
// In Phase 1, this returns the stub value. Phase 2+ will dereference
// the pointer to HAL shared memory.
func (p *Pin[T]) Get() T {
	p.mu.RLock()
	defer p.mu.RUnlock()
	// Phase 2+ will be: return *(*T)(p.ptr)
	return p.value
}

// Set writes a value to the pin.
//
// For output pins, this writes the value that will be read by connected
// components. For input pins, calling Set() has no effect (the value is
// overwritten by the connected signal).
//
// In Phase 1, this updates the stub value. Phase 2+ will write to HAL
// shared memory.
func (p *Pin[T]) Set(value T) {
	p.mu.Lock()
	defer p.mu.Unlock()
	// Phase 2+ will be: *(*T)(p.ptr) = value
	p.value = value
}

// Name returns the fully-qualified pin name.
func (p *Pin[T]) Name() string {
	return p.name
}

// Direction returns the pin direction.
func (p *Pin[T]) Direction() Direction {
	return p.direction
}

// Type returns the HAL type of the pin.
func (p *Pin[T]) Type() PinType {
	// Use type assertion to determine the HAL type
	var t T
	switch any(t).(type) {
	case bool:
		return TypeBit
	case float64:
		return TypeFloat
	case int32:
		return TypeS32
	case uint32:
		return TypeU32
	default:
		return -1 // Should never happen due to PinValue constraint
	}
}

// String returns a string representation of the pin.
func (p *Pin[T]) String() string {
	p.mu.RLock()
	defer p.mu.RUnlock()
	return fmt.Sprintf("Pin{name=%s, type=%s, dir=%s, value=%v}",
		p.name, p.Type(), p.direction, p.value)
}
