package lockfile_test

import (
	"os"
	"testing"

	"github.com/sittner/linuxcnc/src/launcher/lockfile"
)

// overrideLockPath is a helper that sets the lock file path for tests
// by temporarily redirecting filesystem operations to a temp file.
// Since LockFilePath is a constant, tests work with the real path but
// clean up carefully.

func TestAcquireRelease(t *testing.T) {
	// Ensure the lock file doesn't exist before we start.
	_ = os.Remove(lockfile.LockFilePath)

	if err := lockfile.Acquire(); err != nil {
		t.Fatalf("Acquire: %v", err)
	}

	// Lock file should now exist.
	if _, err := os.Stat(lockfile.LockFilePath); err != nil {
		t.Errorf("lock file not found after Acquire: %v", err)
	}

	if err := lockfile.Release(); err != nil {
		t.Fatalf("Release: %v", err)
	}

	// Lock file should be gone after Release.
	if _, err := os.Stat(lockfile.LockFilePath); !os.IsNotExist(err) {
		t.Errorf("lock file still exists after Release")
	}
}

func TestReleaseIdempotent(t *testing.T) {
	// Release on a non-existent lock file should not error.
	_ = os.Remove(lockfile.LockFilePath)
	if err := lockfile.Release(); err != nil {
		t.Fatalf("Release (no file): %v", err)
	}
}

func TestAcquireTwice(t *testing.T) {
	// Ensure lock file doesn't exist.
	_ = os.Remove(lockfile.LockFilePath)
	defer os.Remove(lockfile.LockFilePath)

	if err := lockfile.Acquire(); err != nil {
		t.Fatalf("first Acquire: %v", err)
	}
	t.Cleanup(func() { _ = lockfile.Release() })

	// Second Acquire when stdin is not a TTY (CI environment) should
	// auto-cleanup and succeed.
	if err := lockfile.Acquire(); err != nil {
		t.Fatalf("second Acquire (auto-cleanup): %v", err)
	}
}
