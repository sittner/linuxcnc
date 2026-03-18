package realtime

import (
	"log/slog"
	"os"
	"testing"
)

// TestNew verifies that New() returns a non-nil Manager with the expected
// defaults.
func TestNew(t *testing.T) {
	m := New(nil)
	if m == nil {
		t.Fatal("New() returned nil")
	}
	if m.logger == nil {
		t.Error("logger should not be nil")
	}
}

// TestStartDevZeroAccessible verifies that Start() succeeds when /dev/zero is
// accessible (which it always should be on Linux).
func TestStartDevZeroAccessible(t *testing.T) {
	if _, err := os.Stat(shmDev); os.IsNotExist(err) {
		t.Skipf("%s does not exist, skipping", shmDev)
	}

	m := New(nil)

	// Start() should succeed: /dev/zero is accessible.
	if err := m.Start(); err != nil {
		t.Fatalf("Start() returned unexpected error: %v", err)
	}
}

// TestStartFailsWhenDevZeroMissing verifies that checkDevZero() returns an
// error for a non-existent path.
func TestStartFailsWhenDevZeroMissing(t *testing.T) {
	if err := checkDevZeroAt("/nonexistent/device"); err == nil {
		t.Error("expected error for non-existent device, got nil")
	}
}

// TestStop verifies that Stop() succeeds and cleans up IPC without error.
func TestStop(t *testing.T) {
	m := New(slog.New(slog.NewTextHandler(os.Stderr, nil)))

	if err := m.Stop(); err != nil {
		t.Fatalf("Stop() returned unexpected error: %v", err)
	}
}

// TestCleanupIPC verifies that cleanupIPC() runs without panic or error when
// no LinuxCNC IPC resources are present.
func TestCleanupIPC(t *testing.T) {
	m := New(slog.New(slog.NewTextHandler(os.Stderr, nil)))
	// ipcrm on non-existent keys should not return a hard error (it exits
	// non-zero but we suppress that intentionally — matching the bash script).
	if err := m.cleanupIPC(); err != nil {
		t.Errorf("cleanupIPC() returned unexpected error: %v", err)
	}
}
