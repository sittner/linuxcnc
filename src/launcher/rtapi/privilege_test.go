package rtapi

import (
"log/slog"
"os"
"testing"
)

// TestNew verifies that New() returns a non-nil Engine with expected defaults.
func TestNew(t *testing.T) {
e := New(Config{InstanceName: "test"}, nil)
if e == nil {
t.Fatal("New() returned nil")
}
if e.logger == nil {
t.Error("logger should not be nil")
}
if e.cfg.InstanceName != "test" {
t.Errorf("cfg.InstanceName = %q, want %q", e.cfg.InstanceName, "test")
}
if e.initialized {
t.Error("engine should not be initialized before Init()")
}
}

// TestNewWithLogger verifies that a provided logger is used.
func TestNewWithLogger(t *testing.T) {
logger := slog.New(slog.NewTextHandler(os.Stderr, nil))
e := New(Config{InstanceName: "test"}, logger)
if e.logger != logger {
t.Error("custom logger should be used")
}
}

// TestHardenRT verifies that hardenRT() is a no-op (harden_rt runs in the
// master goroutine) and always returns nil.
func TestHardenRT(t *testing.T) {
e := New(Config{InstanceName: "test"}, slog.New(slog.NewTextHandler(os.Stderr, nil)))
if err := e.hardenRT(); err != nil {
t.Fatalf("hardenRT() unexpected error: %v", err)
}
}

// TestDropPrivileges verifies that dropPrivileges() installs PR_SET_NO_NEW_PRIVS.
//
// Note: PR_SET_NO_NEW_PRIVS is irreversible in this process; all tests that
// run after this in the same binary will also have no-new-privs set. This is
// acceptable in a test process.
func TestDropPrivileges(t *testing.T) {
e := New(Config{InstanceName: "test"}, slog.New(slog.NewTextHandler(os.Stderr, nil)))
if err := e.dropPrivileges(); err != nil {
t.Fatalf("dropPrivileges() unexpected error: %v", err)
}
}

// TestShutdownIdempotent verifies that Shutdown() on an uninitialized engine
// is a no-op.
func TestShutdownIdempotent(t *testing.T) {
e := New(Config{InstanceName: "test-shutdown"}, nil)

if err := e.Shutdown(); err != nil {
t.Fatalf("Shutdown() on uninitialized engine returned error: %v", err)
}
}

// TestLoadModuleRequiresInit verifies that LoadModule returns an error when
// the engine is not initialized.
func TestLoadModuleRequiresInit(t *testing.T) {
e := New(Config{InstanceName: "test"}, nil)

err := e.LoadModule("threads", []string{"name1=servo-thread", "period1=1000000"})
if err == nil {
t.Error("LoadModule() should fail when engine is not initialized")
}
}

// TestUnloadModuleRequiresInit verifies that UnloadModule returns an error
// when the engine is not initialized.
func TestUnloadModuleRequiresInit(t *testing.T) {
e := New(Config{InstanceName: "test"}, nil)

if err := e.UnloadModule("threads"); err == nil {
t.Error("UnloadModule() should fail when engine is not initialized")
}
}

// TestIsRealtimeBeforeInit verifies that IsRealtime() returns false before
// Init() is called.
func TestIsRealtimeBeforeInit(t *testing.T) {
e := New(Config{InstanceName: "test"}, nil)
if e.IsRealtime() {
t.Error("IsRealtime() should return false before Init()")
}
}
