// src/server/iocontrol/iocontrol.go
// IO controller integration package — stub implementation.
//
// The production build uses cgo bindings to the iocontrol shared library
// (iocontrol_shim.c / iocontrol_shim.h).  This stub allows the package
// to compile and the server to be tested without the full LinuxCNC build
// environment.
package iocontrol

import (
	"context"
	"fmt"
	"time"
)

// Config holds IO controller initialization parameters.
type Config struct {
	IniFile   string
	CycleTime float64 // seconds
}

// IOControl represents an initialized IO controller.
type IOControl struct {
	config Config
}

// Init initializes the IO controller.
func Init(cfg Config) (*IOControl, error) {
	if cfg.IniFile == "" {
		return nil, fmt.Errorf("iocontrol: IniFile is required")
	}
	return &IOControl{config: cfg}, nil
}

// Shutdown cleanly shuts down the IO controller.
func (io *IOControl) Shutdown() error {
	return nil
}

// Run executes the IO controller main loop until ctx is cancelled.
func (io *IOControl) Run(ctx context.Context) error {
	cycleTime := io.config.CycleTime
	if cycleTime <= 0 {
		cycleTime = 0.100
	}

	ticker := time.NewTicker(time.Duration(cycleTime * float64(time.Second)))
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			// Production: call iocontrol_shim_cycle()
		}
	}
}
