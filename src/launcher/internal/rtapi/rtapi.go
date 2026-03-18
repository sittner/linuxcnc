// Package rtapi provides Go bindings to librtapi_uspace.so
// for realtime module loading and RTAPI initialization.
package rtapi

import "sync"

var (
	initOnce sync.Once
	initErr  error
)

// initialized tracks whether Init() has been called successfully.
var initialized bool

// IsInitialized returns true if RTAPI has been initialized successfully.
func IsInitialized() bool {
	return initialized
}

// MustInit calls Init and panics on error.
func MustInit() {
	if err := Init(); err != nil {
		panic(err)
	}
	initialized = true
}
