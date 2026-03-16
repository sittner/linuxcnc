package realtime

import (
	"log/slog"
	"os"
	"testing"

	"github.com/sittner/linuxcnc/src/launcher/rtapi"
)

// TestNew verifies that New() returns a non-nil Manager with the expected
// defaults.
func TestNew(t *testing.T) {
	m := New(rtapi.Config{InstanceName: "test"}, nil)
	if m == nil {
		t.Fatal("New() returned nil")
	}
	if m.logger == nil {
		t.Error("logger should not be nil")
	}
	if m.engine == nil {
		t.Error("engine should not be nil")
	}
}

// TestNewWithLogger verifies that a provided logger is used.
func TestNewWithLogger(t *testing.T) {
	logger := slog.New(slog.NewTextHandler(os.Stderr, nil))
	m := New(rtapi.Config{InstanceName: "test"}, logger)
	if m.logger != logger {
		t.Error("custom logger should be used")
	}
}

// TestStartStop verifies that Start() and Stop() succeed.
// The engine stubs always succeed, so this tests the lifecycle API contract.
func TestStartStop(t *testing.T) {
	m := New(rtapi.Config{InstanceName: "test-start-stop"},
		slog.New(slog.NewTextHandler(os.Stderr, nil)))

	if err := m.Start(); err != nil {
		t.Fatalf("Start() returned unexpected error: %v", err)
	}

	if err := m.Stop(); err != nil {
		t.Fatalf("Stop() returned unexpected error: %v", err)
	}
}

// TestStopWithoutStart verifies that Stop() is safe to call without a prior
// Start() (engine.Shutdown() is idempotent).
func TestStopWithoutStart(t *testing.T) {
	m := New(rtapi.Config{InstanceName: "test-stop-only"},
		slog.New(slog.NewTextHandler(os.Stderr, nil)))

	if err := m.Stop(); err != nil {
		t.Fatalf("Stop() on un-started manager returned error: %v", err)
	}
}

// TestLoadModuleRequiresStart verifies that LoadModule fails when Start() has
// not been called (engine not initialized).
func TestLoadModuleRequiresStart(t *testing.T) {
	m := New(rtapi.Config{InstanceName: "test"}, nil)

	if err := m.LoadModule("threads", []string{"name1=servo-thread"}); err == nil {
		t.Error("LoadModule() should fail before Start()")
	}
}

// TestLoadModuleAfterStart verifies that LoadModule succeeds after Start().
func TestLoadModuleAfterStart(t *testing.T) {
	m := New(rtapi.Config{InstanceName: "test-load"},
		slog.New(slog.NewTextHandler(os.Stderr, nil)))

	if err := m.Start(); err != nil {
		t.Fatalf("Start() unexpected error: %v", err)
	}
	defer func() { _ = m.Stop() }()

	if err := m.LoadModule("threads", []string{"name1=servo-thread", "period1=1000000"}); err != nil {
		t.Fatalf("LoadModule() unexpected error: %v", err)
	}
}

// TestCleanupIPC verifies that cleanupIPC() runs without panic or error when
// no LinuxCNC IPC resources are present.
func TestCleanupIPC(t *testing.T) {
	m := New(rtapi.Config{InstanceName: "test"},
		slog.New(slog.NewTextHandler(os.Stderr, nil)))
	// ipcrm on non-existent keys should not return a hard error (it exits
	// non-zero but we suppress that intentionally — matching the bash script).
	if err := m.cleanupIPC(); err != nil {
		t.Errorf("cleanupIPC() returned unexpected error: %v", err)
	}
}
