// Package realtime manages the LinuxCNC realtime environment (uspace only).
//
// This package wraps the in-process rtapi.Engine, providing the Start/Stop
// lifecycle that the launcher orchestrates.  The external rtapi_app process
// and its Unix socket IPC protocol have been eliminated; RT modules are now
// loaded directly in-process via Engine.LoadModule().
//
// Kernel-module paths (RTAI, Xenomai) are intentionally not implemented here.
package realtime

import (
	"fmt"
	"log/slog"
	"os"

	"github.com/sittner/linuxcnc/src/launcher/rtapi"
)

// Manager manages the LinuxCNC uspace realtime environment.
type Manager struct {
	logger *slog.Logger
	engine *rtapi.Engine
}

// New returns a new Manager backed by an in-process rtapi.Engine.
// If logger is nil a default structured logger writing to stderr is used.
func New(cfg rtapi.Config, logger *slog.Logger) *Manager {
	if logger == nil {
		logger = slog.New(slog.NewTextHandler(os.Stderr, nil))
	}
	return &Manager{
		logger: logger,
		engine: rtapi.New(cfg, logger),
	}
}

// Start initialises the in-process realtime environment.
//
// It calls engine.Init() which performs RT hardening (iopl, mlockall,
// RLIMIT_RTPRIO, etc.) and then drops all elevated privileges so that the
// rest of the launcher runs as a normal user process.
func (m *Manager) Start() error {
	m.logger.Info("starting in-process realtime environment")
	if err := m.engine.Init(); err != nil {
		return fmt.Errorf("realtime: engine init failed: %w", err)
	}
	if m.engine.IsRealtime() {
		m.logger.Info("realtime environment ready (hard-RT mode)")
	} else {
		m.logger.Info("realtime environment ready (non-RT mode)")
	}
	return nil
}

// Stop performs an orderly shutdown of the realtime environment.
//
// It calls engine.Shutdown() to stop RT threads and unload modules, then
// cleans up IPC resources (shared memory segments).
func (m *Manager) Stop() error {
	m.logger.Info("stopping realtime environment")

	if err := m.engine.Shutdown(); err != nil {
		m.logger.Error("engine shutdown error", "error", err)
	}

	if err := m.cleanupIPC(); err != nil {
		m.logger.Warn("IPC cleanup encountered errors", "error", err)
	}

	m.logger.Info("realtime environment stopped")
	return nil
}

// LoadModule loads a realtime (.so) module in-process.
//
// This replaces "halcmd loadrt <name> [args...]" exec calls: the module is
// loaded directly via dlopen() inside this process, enabling shared-memory
// access to HAL data structures without IPC.
func (m *Manager) LoadModule(name string, args []string) error {
	return m.engine.LoadModule(name, args)
}

// UnloadModule unloads a previously-loaded realtime module.
func (m *Manager) UnloadModule(name string) error {
	return m.engine.UnloadModule(name)
}
