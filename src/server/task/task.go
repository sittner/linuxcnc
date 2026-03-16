// src/server/task/task.go
// Task controller integration package — stub implementation.
//
// The production build uses cgo bindings to the task shared library
// (task_shim.c / task_shim.h).  This stub allows the package to compile
// and the server to be tested without the full LinuxCNC build environment.
package task

import (
	"context"
	"fmt"
	"time"
)

// Config holds Task controller initialization parameters.
type Config struct {
	IniFile   string
	CycleTime float64 // seconds
}

// Task represents an initialized Task controller.
type Task struct {
	config Config
}

// Init initializes the Task controller.
func Init(cfg Config) (*Task, error) {
	if cfg.IniFile == "" {
		return nil, fmt.Errorf("task: IniFile is required")
	}
	return &Task{config: cfg}, nil
}

// Shutdown cleanly shuts down the Task controller.
func (t *Task) Shutdown() error {
	return nil
}

// Run executes the Task controller main loop until ctx is cancelled.
func (t *Task) Run(ctx context.Context) error {
	cycleTime := t.config.CycleTime
	if cycleTime <= 0 {
		cycleTime = 0.010
	}

	ticker := time.NewTicker(time.Duration(cycleTime * float64(time.Second)))
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			// Production: call task_shim_cycle()
		}
	}
}
