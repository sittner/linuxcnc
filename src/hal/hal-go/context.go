package hal

/*
#include <stdlib.h>
#include "hal.h"
*/
import "C"
import "unsafe"

// Context represents a thread-local HAL context for efficient pin/param access.
//
// The context API provides thread-local buffering with explicit sync operations,
// reducing shared memory contention and improving real-time determinism.
//
// Usage pattern:
//  1. Create pins/params using handle-based API (NewPinHandle, NewParamHandle)
//  2. Create a context with NewContext()
//  3. In your loop:
//     - Call SyncRead() to copy shared memory to local buffers
//     - Read/write pins and params using Get/Set methods
//     - Call SyncWrite() to write modified values back to shared memory
//  4. Destroy context when done
type Context struct {
	// ctx is the underlying C hal_ctx_t pointer
	ctx *C.hal_ctx_t

	// compID is the component ID this context is associated with
	compID int
}

// NewContext creates a new thread-local context for the given component.
//
// The component must have been initialized with hal_init() and all pins/params
// should be created with the handle-based API (NewPinHandle, NewParamHandle)
// before creating the context.
//
// Each thread should create its own context for thread-local access.
//
// Returns the context on success, or an error if creation fails.
func NewContext(comp *Component) (*Context, error) {
	if comp == nil {
		return nil, newError("NewContext", "component is nil", -22)
	}

	ctx := C.hal_ctx_create(C.int(comp.id))
	if ctx == nil {
		return nil, newError("NewContext", "failed to create HAL context", -12)
	}

	return &Context{
		ctx:    ctx,
		compID: comp.id,
	}, nil
}

// SyncRead copies all pin and parameter values from shared memory to
// thread-local buffers.
//
// This must be called before accessing any pins or parameters through
// the context. It creates a snapshot of the current values and prepares
// for diff-based writing.
//
// Returns an error if called twice without an intervening SyncWrite().
func (c *Context) SyncRead() error {
	if c.ctx == nil {
		return newError("SyncRead", "context has been destroyed", -22)
	}

	ret := C.hal_ctx_sync_read(c.ctx)
	if ret != 0 {
		return halError(int(ret), "hal_ctx_sync_read")
	}

	return nil
}

// SyncWrite copies modified pin and parameter values from thread-local
// buffers back to shared memory.
//
// This must be called after SyncRead() and any pin/param modifications.
// It compares the current values against the snapshot taken during SyncRead()
// and only writes changed values, minimizing shared memory writes.
//
// After SyncWrite(), you must call SyncRead() again before accessing data.
//
// Returns an error if called without a prior SyncRead().
func (c *Context) SyncWrite() error {
	if c.ctx == nil {
		return newError("SyncWrite", "context has been destroyed", -22)
	}

	ret := C.hal_ctx_sync_write(c.ctx)
	if ret != 0 {
		return halError(int(ret), "hal_ctx_sync_write")
	}

	return nil
}

// GetPinBit gets a bit (boolean) pin value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewPinBitHandle().
func (c *Context) GetPinBit(handle PinHandle) bool {
	if c.ctx == nil {
		return false
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	val := C.hal_ctx_pin_bit_get(c.ctx, cHandle)
	return bool(val)
}

// SetPinBit sets a bit (boolean) pin value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetPinBit(handle PinHandle, val bool) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	C.hal_ctx_pin_bit_set(c.ctx, cHandle, C.hal_bit_t(val))
}

// GetPinFloat gets a float pin value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewPinFloatHandle().
func (c *Context) GetPinFloat(handle PinHandle) float64 {
	if c.ctx == nil {
		return 0.0
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	val := C.hal_ctx_pin_float_get(c.ctx, cHandle)
	return float64(val)
}

// SetPinFloat sets a float pin value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetPinFloat(handle PinHandle, val float64) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	C.hal_ctx_pin_float_set(c.ctx, cHandle, C.hal_float_t(val))
}

// GetPinS32 gets a signed 32-bit integer pin value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewPinS32Handle().
func (c *Context) GetPinS32(handle PinHandle) int32 {
	if c.ctx == nil {
		return 0
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	val := C.hal_ctx_pin_s32_get(c.ctx, cHandle)
	return int32(val)
}

// SetPinS32 sets a signed 32-bit integer pin value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetPinS32(handle PinHandle, val int32) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	C.hal_ctx_pin_s32_set(c.ctx, cHandle, C.hal_s32_t(val))
}

// GetPinU32 gets an unsigned 32-bit integer pin value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewPinU32Handle().
func (c *Context) GetPinU32(handle PinHandle) uint32 {
	if c.ctx == nil {
		return 0
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	val := C.hal_ctx_pin_u32_get(c.ctx, cHandle)
	return uint32(val)
}

// SetPinU32 sets an unsigned 32-bit integer pin value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetPinU32(handle PinHandle, val uint32) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_pin_handle_t{
		_pin:  unsafe.Pointer(handle._pin),
		_type: C.hal_type_t(handle.pinType),
	}
	C.hal_ctx_pin_u32_set(c.ctx, cHandle, C.hal_u32_t(val))
}

// GetParamBit gets a bit (boolean) parameter value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewParamBitHandle().
func (c *Context) GetParamBit(handle ParamHandle) bool {
	if c.ctx == nil {
		return false
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	val := C.hal_ctx_param_bit_get(c.ctx, cHandle)
	return bool(val)
}

// SetParamBit sets a bit (boolean) parameter value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetParamBit(handle ParamHandle, val bool) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	C.hal_ctx_param_bit_set(c.ctx, cHandle, C.hal_bit_t(val))
}

// GetParamFloat gets a float parameter value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewParamFloatHandle().
func (c *Context) GetParamFloat(handle ParamHandle) float64 {
	if c.ctx == nil {
		return 0.0
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	val := C.hal_ctx_param_float_get(c.ctx, cHandle)
	return float64(val)
}

// SetParamFloat sets a float parameter value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetParamFloat(handle ParamHandle, val float64) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	C.hal_ctx_param_float_set(c.ctx, cHandle, C.hal_float_t(val))
}

// GetParamS32 gets a signed 32-bit integer parameter value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewParamS32Handle().
func (c *Context) GetParamS32(handle ParamHandle) int32 {
	if c.ctx == nil {
		return 0
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	val := C.hal_ctx_param_s32_get(c.ctx, cHandle)
	return int32(val)
}

// SetParamS32 sets a signed 32-bit integer parameter value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetParamS32(handle ParamHandle, val int32) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	C.hal_ctx_param_s32_set(c.ctx, cHandle, C.hal_s32_t(val))
}

// GetParamU32 gets an unsigned 32-bit integer parameter value from the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The handle must be obtained from NewParamU32Handle().
func (c *Context) GetParamU32(handle ParamHandle) uint32 {
	if c.ctx == nil {
		return 0
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	val := C.hal_ctx_param_u32_get(c.ctx, cHandle)
	return uint32(val)
}

// SetParamU32 sets an unsigned 32-bit integer parameter value in the thread-local context.
//
// Must be called between SyncRead() and SyncWrite().
// The value will be written to shared memory when SyncWrite() is called.
func (c *Context) SetParamU32(handle ParamHandle, val uint32) {
	if c.ctx == nil {
		return
	}
	cHandle := C.hal_param_handle_t{
		_param: unsafe.Pointer(handle._param),
		_type:  C.hal_type_t(handle.paramType),
	}
	C.hal_ctx_param_u32_set(c.ctx, cHandle, C.hal_u32_t(val))
}

// Destroy frees all resources associated with the context.
//
// After calling Destroy(), the context cannot be used anymore.
// It is safe to call Destroy() multiple times.
func (c *Context) Destroy() {
	if c.ctx != nil {
		C.hal_ctx_destroy(c.ctx)
		c.ctx = nil
	}
}
