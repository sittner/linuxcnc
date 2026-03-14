// Package lockfile manages the LinuxCNC lock file (/tmp/linuxcnc.lock).
//
// The lock file prevents multiple concurrent LinuxCNC instances from running.
// If the lock file already exists when Acquire is called, the function prompts
// the user (when connected to a TTY) or proceeds with automatic cleanup.
package lockfile

import (
	"bufio"
	"fmt"
	"os"
	"strings"
)

// LockFilePath is the canonical path of the LinuxCNC lock file.
// It is a var (not const) so tests can override it with a temp path.
var LockFilePath = "/tmp/linuxcnc.lock"

// Acquire creates the lock file.  If the lock file already exists it checks
// whether to clean up the previous instance:
//   - When stdin is a TTY the user is prompted interactively.
//   - When stdin is not a TTY (e.g. automated tests) cleanup proceeds
//     automatically.
//
// If cleanup is non-nil it is called after the user confirms (or in
// automatic mode) and before the stale lock file is removed.  The caller
// should use cleanup to kill orphaned processes from the previous session.
//
// Returns an error if the lock file cannot be created.
func Acquire(cleanup func()) error {
	if _, err := os.Stat(LockFilePath); err == nil {
		// Lock file exists – ask user or auto-cleanup.
		if err := handleExistingLock(cleanup); err != nil {
			return err
		}
	}
	f, err := os.Create(LockFilePath)
	if err != nil {
		return fmt.Errorf("lockfile: creating %s: %w", LockFilePath, err)
	}
	return f.Close()
}

// Release removes the lock file.  It is safe to call even if the file no
// longer exists (e.g. after a crash cleanup).
func Release() error {
	if err := os.Remove(LockFilePath); err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("lockfile: removing %s: %w", LockFilePath, err)
	}
	return nil
}

// handleExistingLock is called when the lock file already exists.
// It either prompts the user or proceeds automatically, then calls the
// optional cleanup function before removing the stale lock file.
func handleExistingLock(cleanup func()) error {
	if isTTY() {
		fmt.Print("LinuxCNC is still running.  Restart it? [Y/n] ")
		reader := bufio.NewReader(os.Stdin)
		line, _ := reader.ReadString('\n')
		line = strings.TrimSpace(strings.ToLower(line))
		if line != "" && line != "y" && line != "yes" {
			return fmt.Errorf("lockfile: not starting new LinuxCNC instance")
		}
		fmt.Println("Cleaning up old LinuxCNC...")
	} else {
		fmt.Fprintln(os.Stderr, "lockfile: no TTY, cleaning up previous instance automatically")
	}
	if cleanup != nil {
		cleanup()
	}
	return Release()
}

// isTTY reports whether stdin is connected to a terminal.
func isTTY() bool {
	fi, err := os.Stdin.Stat()
	if err != nil {
		return false
	}
	return (fi.Mode() & os.ModeCharDevice) != 0
}
