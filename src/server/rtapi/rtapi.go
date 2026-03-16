// src/server/rtapi/rtapi.go
// RTAPI integration package — stub implementation.
//
// The production build uses cgo bindings to the RTAPI shared library
// (rtapi_shim.c / rtapi_shim.h).  This stub allows the package to compile
// and the server to be tested without the full LinuxCNC build environment.
package rtapi

import "fmt"

// Config holds RTAPI initialization parameters.
type Config struct {
	InstanceName string
	Debug        bool
}

// RTAPI represents an initialized RTAPI instance.
type RTAPI struct {
	config Config
}

// Init initializes the RTAPI subsystem.
func Init(cfg Config) (*RTAPI, error) {
	if cfg.InstanceName == "" {
		return nil, fmt.Errorf("rtapi: InstanceName is required")
	}
	return &RTAPI{config: cfg}, nil
}

// Shutdown cleanly shuts down RTAPI.
func (r *RTAPI) Shutdown() error {
	return nil
}

// LoadModule loads a realtime module.
func (r *RTAPI) LoadModule(name string, args string) error {
	return nil
}

// UnloadModule unloads a realtime module.
func (r *RTAPI) UnloadModule(name string) error {
	return nil
}
