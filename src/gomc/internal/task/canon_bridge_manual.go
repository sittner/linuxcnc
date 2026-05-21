package task

// Manual implementations for canon callbacks with complex signatures.
// These are excluded from the generated canon_bridge.go.

/*
#define CANON_API_CGO
#include "../../generated/gmi/canon/canon_api.h"
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"
)

//export go_canon_nurbs_feed
func go_canon_nurbs_feed(ctx unsafe.Pointer, lineno C.int32_t, controlPoints *C.canon_control_point_t, numPoints C.size_t, k C.uint32_t) {
	_canon := cgo.Handle(uintptr(ctx)).Value().(*Canon)
	n := int(numPoints)
	pts := make([]ControlPoint, n)
	if n > 0 {
		cpts := unsafe.Slice(controlPoints, n)
		for i := 0; i < n; i++ {
			pts[i] = ControlPoint{
				X: float64(cpts[i].x),
				Y: float64(cpts[i].y),
				W: float64(cpts[i].w),
			}
		}
	}
	_canon.NurbsFeed(int32(lineno), pts, uint32(k))
}

//export go_canon_get_external_tool_table
func go_canon_get_external_tool_table(ctx unsafe.Pointer, pocket C.int32_t, toolno *C.int32_t, offset *C.double, diameter *C.double, frontangle *C.double, backangle *C.double, orientation *C.int32_t) C.int32_t {
	_canon := cgo.Handle(uintptr(ctx)).Value().(*Canon)
	tno, off, dia, fa, ba, ori, errCode := _canon.GetExternalToolTable(int32(pocket))
	if toolno != nil {
		*toolno = C.int32_t(tno)
	}
	if offset != nil {
		offSlice := unsafe.Slice(offset, 9)
		for i := 0; i < 9; i++ {
			offSlice[i] = C.double(off[i])
		}
	}
	if diameter != nil {
		*diameter = C.double(dia)
	}
	if frontangle != nil {
		*frontangle = C.double(fa)
	}
	if backangle != nil {
		*backangle = C.double(ba)
	}
	if orientation != nil {
		*orientation = C.int32_t(ori)
	}
	return C.int32_t(errCode)
}

//export go_canon_get_external_offsets
func go_canon_get_external_offsets(ctx unsafe.Pointer, offsets *C.double) {
	_canon := cgo.Handle(uintptr(ctx)).Value().(*Canon)
	var off [9]float64
	_canon.GetExternalOffsets(&off)
	if offsets != nil {
		offSlice := unsafe.Slice(offsets, 9)
		for i := 0; i < 9; i++ {
			offSlice[i] = C.double(off[i])
		}
	}
}

//export go_canon_get_external_parameter_file_name
func go_canon_get_external_parameter_file_name(ctx unsafe.Pointer, buf **C.char) {
	_canon := cgo.Handle(uintptr(ctx)).Value().(*Canon)
	var s string
	_canon.GetExternalParameterFileName(&s)
	// Note: the caller owns the returned string pointer.
	// We use a static buffer approach — the Go string is only valid
	// until the next call. This matches the C++ emccanon behavior.
	if buf != nil {
		*buf = C.CString(s) // caller must not free (interpreter doesn't)
	}
}
