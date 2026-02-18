package hal

/*
#include <stdlib.h>
#include "hal.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

// PinHandle represents a handle to a HAL pin for use with the context API.
//
// Handles are lightweight integer identifiers that can be used to efficiently
// access pin values through a Context. They are returned by the NewPin*Handle
// functions and used with Context.GetPin*/SetPin* methods.
type PinHandle int

// ParamHandle represents a handle to a HAL parameter for use with the context API.
//
// Handles are lightweight integer identifiers that can be used to efficiently
// access parameter values through a Context. They are returned by the NewParam*Handle
// functions and used with Context.GetParam*/SetParam* methods.
type ParamHandle int

// NewPinBitHandle creates a new bit (boolean) pin and returns a handle.
//
// This is similar to NewPin[bool] but returns a handle instead of a Pin object.
// The handle can be used with Context methods for efficient thread-local access.
//
// The name should be just the pin name (e.g., "enable"), not the full name.
// The component name will be prepended automatically.
//
// This calls hal_pin_bit_new_handle() via CGO.
func NewPinBitHandle(comp *Component, name string, dir Direction) (PinHandle, error) {
	if comp == nil {
		return 0, newError("NewPinBitHandle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewPinBitHandle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != In && dir != Out && dir != IO {
		return 0, newError("NewPinBitHandle", "invalid direction", -22)
	}

	// Build fully-qualified pin name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_pin_handle_t
	ret := C.hal_pin_bit_new_handle(cName, C.hal_pin_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_pin_bit_new_handle")
	}

	return PinHandle(handle), nil
}

// NewPinFloatHandle creates a new float pin and returns a handle.
//
// This is similar to NewPin[float64] but returns a handle instead of a Pin object.
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_pin_float_new_handle() via CGO.
func NewPinFloatHandle(comp *Component, name string, dir Direction) (PinHandle, error) {
	if comp == nil {
		return 0, newError("NewPinFloatHandle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewPinFloatHandle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != In && dir != Out && dir != IO {
		return 0, newError("NewPinFloatHandle", "invalid direction", -22)
	}

	// Build fully-qualified pin name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_pin_handle_t
	ret := C.hal_pin_float_new_handle(cName, C.hal_pin_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_pin_float_new_handle")
	}

	return PinHandle(handle), nil
}

// NewPinS32Handle creates a new signed 32-bit integer pin and returns a handle.
//
// This is similar to NewPin[int32] but returns a handle instead of a Pin object.
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_pin_s32_new_handle() via CGO.
func NewPinS32Handle(comp *Component, name string, dir Direction) (PinHandle, error) {
	if comp == nil {
		return 0, newError("NewPinS32Handle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewPinS32Handle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != In && dir != Out && dir != IO {
		return 0, newError("NewPinS32Handle", "invalid direction", -22)
	}

	// Build fully-qualified pin name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_pin_handle_t
	ret := C.hal_pin_s32_new_handle(cName, C.hal_pin_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_pin_s32_new_handle")
	}

	return PinHandle(handle), nil
}

// NewPinU32Handle creates a new unsigned 32-bit integer pin and returns a handle.
//
// This is similar to NewPin[uint32] but returns a handle instead of a Pin object.
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_pin_u32_new_handle() via CGO.
func NewPinU32Handle(comp *Component, name string, dir Direction) (PinHandle, error) {
	if comp == nil {
		return 0, newError("NewPinU32Handle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewPinU32Handle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != In && dir != Out && dir != IO {
		return 0, newError("NewPinU32Handle", "invalid direction", -22)
	}

	// Build fully-qualified pin name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_pin_handle_t
	ret := C.hal_pin_u32_new_handle(cName, C.hal_pin_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_pin_u32_new_handle")
	}

	return PinHandle(handle), nil
}

// NewParamBitHandle creates a new bit (boolean) parameter and returns a handle.
//
// Parameters are similar to pins but are typically used for configuration
// values that change less frequently.
//
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_param_bit_new_handle() via CGO.
func NewParamBitHandle(comp *Component, name string, dir ParamDirection) (ParamHandle, error) {
	if comp == nil {
		return 0, newError("NewParamBitHandle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewParamBitHandle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != RO && dir != RW {
		return 0, newError("NewParamBitHandle", "invalid direction (must be RO or RW)", -22)
	}

	// Build fully-qualified parameter name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_param_handle_t
	ret := C.hal_param_bit_new_handle(cName, C.hal_param_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_param_bit_new_handle")
	}

	return ParamHandle(handle), nil
}

// NewParamFloatHandle creates a new float parameter and returns a handle.
//
// Parameters are similar to pins but are typically used for configuration
// values that change less frequently.
//
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_param_float_new_handle() via CGO.
func NewParamFloatHandle(comp *Component, name string, dir ParamDirection) (ParamHandle, error) {
	if comp == nil {
		return 0, newError("NewParamFloatHandle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewParamFloatHandle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != RO && dir != RW {
		return 0, newError("NewParamFloatHandle", "invalid direction (must be RO or RW)", -22)
	}

	// Build fully-qualified parameter name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_param_handle_t
	ret := C.hal_param_float_new_handle(cName, C.hal_param_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_param_float_new_handle")
	}

	return ParamHandle(handle), nil
}

// NewParamS32Handle creates a new signed 32-bit integer parameter and returns a handle.
//
// Parameters are similar to pins but are typically used for configuration
// values that change less frequently.
//
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_param_s32_new_handle() via CGO.
func NewParamS32Handle(comp *Component, name string, dir ParamDirection) (ParamHandle, error) {
	if comp == nil {
		return 0, newError("NewParamS32Handle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewParamS32Handle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != RO && dir != RW {
		return 0, newError("NewParamS32Handle", "invalid direction (must be RO or RW)", -22)
	}

	// Build fully-qualified parameter name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_param_handle_t
	ret := C.hal_param_s32_new_handle(cName, C.hal_param_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_param_s32_new_handle")
	}

	return ParamHandle(handle), nil
}

// NewParamU32Handle creates a new unsigned 32-bit integer parameter and returns a handle.
//
// Parameters are similar to pins but are typically used for configuration
// values that change less frequently.
//
// The handle can be used with Context methods for efficient thread-local access.
//
// This calls hal_param_u32_new_handle() via CGO.
func NewParamU32Handle(comp *Component, name string, dir ParamDirection) (ParamHandle, error) {
	if comp == nil {
		return 0, newError("NewParamU32Handle", "component is nil", -22)
	}

	if name == "" || len(name) > 47 {
		return 0, newError("NewParamU32Handle", ErrInvalidName.Message, ErrInvalidName.Code)
	}

	if dir != RO && dir != RW {
		return 0, newError("NewParamU32Handle", "invalid direction (must be RO or RW)", -22)
	}

	// Build fully-qualified parameter name
	fullName := fmt.Sprintf("%s.%s", comp.Name(), name)
	cName := C.CString(fullName)
	defer C.free(unsafe.Pointer(cName))

	var handle C.hal_param_handle_t
	ret := C.hal_param_u32_new_handle(cName, C.hal_param_dir_t(dir), &handle, C.int(comp.id))
	if ret != 0 {
		return 0, halError(int(ret), "hal_param_u32_new_handle")
	}

	return ParamHandle(handle), nil
}
