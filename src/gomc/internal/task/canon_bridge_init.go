package task

// canon_bridge_init.go provides the function to build and install
// the canon callback table into the interpreter.

/*
#define CANON_API_CGO
#include "../../generated/gmi/canon/canon_api.h"
#include <stdint.h>

// Defined in canon_bridge_table.c
extern void build_go_canon_callbacks(canon_callbacks_t *cb, void *ctx);

// uintptr_to_voidptr converts a uintptr (cgo.Handle) to void* for C.
static inline void *uintptr_to_voidptr(uintptr_t h) { return (void *)h; }
*/
import "C"
import (
	"runtime/cgo"
	"unsafe"
)

// canonCallbackTable holds a persistent canon_callbacks_t that points
// to Go export functions. The cgo.Handle stored in ctx keeps the
// Canon alive for as long as the table is in use.
type canonCallbackTable struct {
	table  C.canon_callbacks_t
	handle cgo.Handle
}

// newCanonCallbackTable creates a C callback table that dispatches
// to the given Canon instance. The caller must call release() when done.
func newCanonCallbackTable(canon *Canon) *canonCallbackTable {
	ct := &canonCallbackTable{}
	ct.handle = cgo.NewHandle(canon)
	C.build_go_canon_callbacks(&ct.table, C.uintptr_to_voidptr(C.uintptr_t(ct.handle)))
	return ct
}

// ptr returns an unsafe.Pointer to the C callback table,
// suitable for passing to CInterp.SetCanonCallbacks().
func (ct *canonCallbackTable) ptr() unsafe.Pointer {
	return unsafe.Pointer(&ct.table)
}

// release frees the cgo handle. After this, the callback table
// must not be used.
func (ct *canonCallbackTable) release() {
	ct.handle.Delete()
}
