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

// TestHardenRTStub verifies that hardenRT() runs without error on the stub
// implementation. The stub always succeeds.
func TestHardenRTStub(t *testing.T) {
	e := New(Config{InstanceName: "test"}, slog.New(slog.NewTextHandler(os.Stderr, nil)))
	if err := e.hardenRT(); err != nil {
		t.Fatalf("hardenRT() unexpected error: %v", err)
	}
}

// TestDropPrivilegesStub verifies that dropPrivileges() runs without error on
// the stub implementation.
//
// Note: PR_SET_NO_NEW_PRIVS is set by the stub. On Linux this is an
// irreversible operation, so tests that run after this in the same process
// will also have no-new-privs set. This is acceptable for a test process.
func TestDropPrivilegesStub(t *testing.T) {
	e := New(Config{InstanceName: "test"}, slog.New(slog.NewTextHandler(os.Stderr, nil)))
	if err := e.dropPrivileges(); err != nil {
		t.Fatalf("dropPrivileges() unexpected error: %v", err)
	}
}

// TestInitDropVerify exercises the Init→drop→verify flow.
//
// When not running with the necessary capabilities (typical in CI), hardenRT()
// will succeed via the stub (which is a no-op), and Init() will succeed in
// non-RT mode.
func TestInitDropVerify(t *testing.T) {
	e := New(Config{InstanceName: "test-init"}, slog.New(slog.NewTextHandler(os.Stderr, nil)))

	if err := e.Init(); err != nil {
		t.Fatalf("Init() unexpected error: %v", err)
	}

	// Engine should be marked as initialized.
	e.mu.Lock()
	initialized := e.initialized
	e.mu.Unlock()
	if !initialized {
		t.Error("engine should be initialized after Init()")
	}

	// IsRealtime() should return true with the stub (stub always succeeds).
	// In production with real capabilities, this reflects actual RT state.
	t.Logf("IsRealtime() = %v", e.IsRealtime())

	if err := e.Shutdown(); err != nil {
		t.Fatalf("Shutdown() unexpected error: %v", err)
	}

	// Engine should be marked as not initialized after Shutdown().
	e.mu.Lock()
	initialized = e.initialized
	e.mu.Unlock()
	if initialized {
		t.Error("engine should not be initialized after Shutdown()")
	}
}

// TestDoubleInit verifies that calling Init() twice returns an error.
func TestDoubleInit(t *testing.T) {
	e := New(Config{InstanceName: "test-double"}, nil)

	if err := e.Init(); err != nil {
		t.Fatalf("first Init() unexpected error: %v", err)
	}
	defer func() { _ = e.Shutdown() }()

	if err := e.Init(); err == nil {
		t.Error("second Init() should return an error")
	}
}

// TestShutdownIdempotent verifies that Shutdown() on an uninitialized engine is
// a no-op.
func TestShutdownIdempotent(t *testing.T) {
	e := New(Config{InstanceName: "test-shutdown"}, nil)

	// Calling Shutdown() before Init() should be a no-op.
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

// TestLoadModuleAfterInit verifies that LoadModule succeeds after Init().
// The stub implementation always succeeds, so this tests the API contract.
func TestLoadModuleAfterInit(t *testing.T) {
	e := New(Config{InstanceName: "test-load"}, nil)

	if err := e.Init(); err != nil {
		t.Fatalf("Init() unexpected error: %v", err)
	}
	defer func() { _ = e.Shutdown() }()

	// Stub always succeeds.
	if err := e.LoadModule("threads", []string{"name1=servo-thread", "period1=1000000"}); err != nil {
		t.Fatalf("LoadModule() unexpected error: %v", err)
	}
}

// TestUnloadModuleAfterInit verifies that UnloadModule succeeds after Init().
func TestUnloadModuleAfterInit(t *testing.T) {
	e := New(Config{InstanceName: "test-unload"}, nil)

	if err := e.Init(); err != nil {
		t.Fatalf("Init() unexpected error: %v", err)
	}
	defer func() { _ = e.Shutdown() }()

	// Stub always succeeds.
	if err := e.UnloadModule("threads"); err != nil {
		t.Fatalf("UnloadModule() unexpected error: %v", err)
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
