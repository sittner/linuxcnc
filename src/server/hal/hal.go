// src/server/hal/hal.go
// HAL integration package — stub implementation.
//
// The production build uses cgo bindings to the HAL shared library
// (hal_shim.c / hal_shim.h).  This stub allows the package to compile
// and the server to be tested without the full LinuxCNC build environment.
package hal

import (
	"fmt"

	"linuxcnc/server/config"
)

// Config holds HAL initialization parameters.
type Config struct {
	ComponentName string
}

// HAL represents an initialized HAL instance.
type HAL struct {
	config Config
}

// Init initializes the HAL subsystem.
func Init(cfg Config) (*HAL, error) {
	if cfg.ComponentName == "" {
		return nil, fmt.Errorf("hal: ComponentName is required")
	}
	return &HAL{config: cfg}, nil
}

// Shutdown cleanly shuts down HAL.
func (h *HAL) Shutdown() error {
	return nil
}

// Ready signals that HAL setup is complete and threads may start.
func (h *HAL) Ready() error {
	return nil
}

// ExecuteCommand executes a single HAL command string.
func (h *HAL) ExecuteCommand(cmd string) error {
	return nil
}

// Loader handles loading HAL configuration files.
type Loader struct {
	hal    *HAL
	config *config.Config
}

// NewLoader creates a new HAL configuration loader.
func NewLoader(h *HAL, cfg *config.Config) *Loader {
	return &Loader{hal: h, config: cfg}
}

// LoadFiles loads multiple HAL configuration files in order.
func (l *Loader) LoadFiles(files []string) error {
	return nil
}
