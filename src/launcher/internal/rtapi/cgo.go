package rtapi

/*
#cgo CFLAGS: -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../.. -I${SRCDIR}/../../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib -lrtapi_uspace -Wl,-rpath,${SRCDIR}/../../../../lib

#include "rtapi_uspace.h"
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"unsafe"
)

// Init initializes the RTAPI uspace environment.
// Must be called before any other RTAPI functions.
// This sets up RT hardening, memory locking, etc.
// It is safe to call Init multiple times; initialization happens only once.
func Init() error {
	var err error
	initOnce.Do(func() {
		ret := C.rtapi_uspace_init()
		if ret != 0 {
			err = fmt.Errorf("rtapi_uspace_init failed with code %d", ret)
			return
		}
		initialized = true
	})
	return err
}

// LoadModule loads a realtime module by name with optional arguments.
// This replaces the old rtapi_app "load" command.
func LoadModule(name string, args ...string) error {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	if len(args) == 0 {
		ret := C.rtapi_load_module(cName, 0, nil)
		if ret != 0 {
			return fmt.Errorf("rtapi_load_module(%s) failed with code %d", name, ret)
		}
		return nil
	}

	// Build argv: [args...]
	cArgs := make([]*C.char, len(args))
	for i, arg := range args {
		cArgs[i] = C.CString(arg)
		defer C.free(unsafe.Pointer(cArgs[i]))
	}

	ret := C.rtapi_load_module(cName, C.int(len(cArgs)), &cArgs[0])
	if ret != 0 {
		return fmt.Errorf("rtapi_load_module(%s) failed with code %d", name, ret)
	}
	return nil
}

// UnloadModule unloads a previously loaded realtime module.
func UnloadModule(name string) error {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	ret := C.rtapi_unload_module(cName)
	if ret != 0 {
		return fmt.Errorf("rtapi_unload_module(%s) failed with code %d", name, ret)
	}
	return nil
}
