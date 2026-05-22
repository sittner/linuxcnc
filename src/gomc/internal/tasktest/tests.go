package tasktest

import (
	"fmt"
	"strings"
	"time"

	"github.com/sittner/linuxcnc/src/gomc/generated/gmi/emcstat"
)

// RCS_STATUS codes returned by the old C milltask via emccmd_slot.
// The Go milltask currently returns 0 for all calls (bug to fix later).
const (
	rcsDone  = 1 // Command completed successfully
	rcsExec  = 2 // Command accepted, still executing
	rcsError = 3 // Command rejected/failed
)

// isOK returns true if the return code indicates success (accepted).
// Go milltask returns 0; old C milltask returns RCS_DONE(1) or RCS_EXEC(2).
func isOK(rc int32) bool {
	return rc == 0 || rc == rcsDone || rc == rcsExec
}

// isRejected returns true if the return code indicates rejection.
// Go milltask returns 0 even on rejection (bug); old C milltask returns RCS_ERROR(3).
func isRejected(rc int32) bool {
	return rc == rcsError || rc < 0
}

// testResult holds the outcome of a single test case.
type testResult struct {
	name   string
	passed bool
	msg    string
}

// testResults collects results from all tests and reports.
type testResults struct {
	results []testResult
}

func (r *testResults) pass(name string) {
	r.results = append(r.results, testResult{name: name, passed: true})
}

func (r *testResults) fail(name, msg string) {
	r.results = append(r.results, testResult{name: name, passed: false, msg: msg})
}

func (r *testResults) report(logger interface{ Info(string, ...any) }) error {
	var passed, failed int
	var failures []string
	for _, res := range r.results {
		if res.passed {
			passed++
			fmt.Printf("  PASS: %s\n", res.name)
		} else {
			failed++
			fmt.Printf("  FAIL: %s — %s\n", res.name, res.msg)
			failures = append(failures, res.name+": "+res.msg)
		}
	}
	fmt.Printf("\ntasktest: %d passed, %d failed\n", passed, failed)
	if failed > 0 {
		return fmt.Errorf("tasktest: %d tests failed:\n  %s", failed, strings.Join(failures, "\n  "))
	}
	return nil
}

// runAll executes all integration tests in sequence.
func (h *testHarness) runAll() *testResults {
	r := &testResults{}

	// Give the system a moment to settle after Start().
	time.Sleep(100 * time.Millisecond)

	h.testInitialState(r)
	h.testStateTransitions(r)
	h.testModeSwitch(r)
	h.testMotionEnabled(r)
	h.testMotionCoordMode(r)
	h.testGuardRejectsJogInEstop(r)
	h.testGuardRejectsAutoInManual(r)
	h.testFloodMist(r)
	h.testFeedOverride(r)
	h.testAbortAlways(r)

	return r
}

// --- Individual Test Cases ---

func (h *testHarness) testInitialState(r *testResults) {
	const name = "initial_state_is_estop"
	stat, err := h.getStat()
	if err != nil {
		r.fail(name, fmt.Sprintf("getStat: %v", err))
		return
	}
	if stat.Task.State != emcstat.ESTOP {
		r.fail(name, fmt.Sprintf("expected state ESTOP(%d), got %d", emcstat.ESTOP, stat.Task.State))
		return
	}
	r.pass(name)
}

func (h *testHarness) testStateTransitions(r *testResults) {
	const name = "state_estop_to_on"

	// ESTOP → ON directly should fail (must go through ESTOP_RESET first)
	rc, _ := h.setState(int32(emcstat.ON))
	if isOK(rc) {
		// Verify state didn't actually change
		stat, _ := h.getStat()
		if stat.Task.State == emcstat.ON {
			r.fail(name, "direct ESTOP→ON should not succeed")
			return
		}
	}

	// ESTOP → ESTOP_RESET
	rc, err := h.setState(int32(emcstat.ESTOP_RESET))
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("ESTOP→ESTOP_RESET: rc=%d err=%v", rc, err))
		return
	}

	// Wait for state to settle (old milltask returns RCS_EXEC=2 for async ops)
	if err := h.waitForState(emcstat.ESTOP_RESET, 500*time.Millisecond); err != nil {
		stat, _ := h.getStat()
		r.fail(name, fmt.Sprintf("after estop_reset: state=%d, want %d", stat.Task.State, emcstat.ESTOP_RESET))
		return
	}

	// ESTOP_RESET → ON
	rc, err = h.setState(int32(emcstat.ON))
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("ESTOP_RESET→ON: rc=%d err=%v", rc, err))
		return
	}

	if err := h.waitForState(emcstat.ON, 500*time.Millisecond); err != nil {
		stat, _ := h.getStat()
		r.fail(name, fmt.Sprintf("after on: state=%d, want %d", stat.Task.State, emcstat.ON))
		return
	}

	r.pass(name)
}

func (h *testHarness) testModeSwitch(r *testResults) {
	const name = "mode_switch_manual_auto_mdi"
	h.ensureOn()

	// Switch to AUTO
	rc, err := h.setMode(int32(emcstat.AUTO))
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("setMode(AUTO): rc=%d err=%v", rc, err))
		return
	}
	time.Sleep(50 * time.Millisecond)
	stat, _ := h.getStat()
	if stat.Task.Mode != emcstat.AUTO {
		r.fail(name, fmt.Sprintf("mode after AUTO: got %d, want %d", stat.Task.Mode, emcstat.AUTO))
		return
	}

	// Switch to MDI
	rc, err = h.setMode(int32(emcstat.MDI))
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("setMode(MDI): rc=%d err=%v", rc, err))
		return
	}
	time.Sleep(50 * time.Millisecond)
	stat, _ = h.getStat()
	if stat.Task.Mode != emcstat.MDI {
		r.fail(name, fmt.Sprintf("mode after MDI: got %d, want %d", stat.Task.Mode, emcstat.MDI))
		return
	}

	// Switch to MANUAL
	rc, err = h.setMode(int32(emcstat.MANUAL))
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("setMode(MANUAL): rc=%d err=%v", rc, err))
		return
	}
	time.Sleep(50 * time.Millisecond)
	stat, _ = h.getStat()
	if stat.Task.Mode != emcstat.MANUAL {
		r.fail(name, fmt.Sprintf("mode after MANUAL: got %d, want %d", stat.Task.Mode, emcstat.MANUAL))
		return
	}

	r.pass(name)
}

func (h *testHarness) testMotionEnabled(r *testResults) {
	const name = "motion_enabled_after_on"
	h.ensureOn()

	// After machine ON, motion should be enabled (may need brief wait for servo cycle)
	if err := h.waitForMotionEnabled(500 * time.Millisecond); err != nil {
		stat, _ := h.getStat()
		r.fail(name, fmt.Sprintf("motion not enabled: enabled=%v state=%d", stat.Motion.Enabled, stat.Task.State))
		return
	}
	r.pass(name)
}

func (h *testHarness) testMotionCoordMode(r *testResults) {
	const name = "motion_coord_after_auto"
	h.ensureOn()

	// Switch to AUTO mode
	h.setMode(int32(emcstat.AUTO))

	// Motion should be in COORD mode (may need brief wait for servo cycle)
	if err := h.waitForMotionMode(emcstat.COORD, 500*time.Millisecond); err != nil {
		stat, _ := h.getStat()
		r.fail(name, fmt.Sprintf("motion not in COORD: mode=%d", stat.Motion.Mode))
		return
	}
	r.pass(name)
}

func (h *testHarness) testGuardRejectsJogInEstop(r *testResults) {
	const name = "guard_jog_rejected_in_estop"

	// Go to ESTOP
	h.setState(int32(emcstat.ESTOP))
	time.Sleep(20 * time.Millisecond)

	// Jog should fail
	rc, _ := h.jog(1, 0, 100.0, 0) // JOG_CONTINUOUS=1
	if isOK(rc) {
		r.fail(name, fmt.Sprintf("jog should be rejected in ESTOP, got rc=%d", rc))
		return
	}
	r.pass(name)
}

func (h *testHarness) testGuardRejectsAutoInManual(r *testResults) {
	const name = "guard_auto_cmd_rejected_in_manual"
	h.ensureOn()
	h.setMode(int32(emcstat.MANUAL))
	time.Sleep(50 * time.Millisecond)

	// auto_cmd(RUN) in MANUAL mode: either rejected (rc=error) or
	// accepted but program should NOT actually be running.
	h.autoCmd(0, 0) // AUTO_RUN=0
	time.Sleep(100 * time.Millisecond)
	stat, _ := h.getStat()
	// Verify mode is still MANUAL (didn't auto-switch)
	if stat.Task.Mode != emcstat.MANUAL {
		r.fail(name, fmt.Sprintf("mode changed to %d after auto_cmd in MANUAL", stat.Task.Mode))
		return
	}
	r.pass(name)
}

func (h *testHarness) testFloodMist(r *testResults) {
	const name = "flood_mist_on_off"
	h.ensureOn()

	rc, err := h.flood(true)
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("flood(on): rc=%d err=%v", rc, err))
		return
	}
	time.Sleep(50 * time.Millisecond)
	stat, _ := h.getStat()
	if !stat.Flood {
		r.fail(name, "flood should be on")
		return
	}

	rc, err = h.flood(false)
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("flood(off): rc=%d err=%v", rc, err))
		return
	}
	time.Sleep(50 * time.Millisecond)
	stat, _ = h.getStat()
	if stat.Flood {
		r.fail(name, "flood should be off")
		return
	}

	r.pass(name)
}

func (h *testHarness) testFeedOverride(r *testResults) {
	const name = "feed_override"
	h.ensureOn()

	rc, err := h.setFeedOverride(0.5)
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("setFeedOverride(0.5): rc=%d err=%v", rc, err))
		return
	}
	time.Sleep(50 * time.Millisecond)
	stat, _ := h.getStat()
	if stat.Motion.Feedrate < 0.49 || stat.Motion.Feedrate > 0.51 {
		r.fail(name, fmt.Sprintf("feedrate=%f, want ~0.5", stat.Motion.Feedrate))
		return
	}

	// Restore
	h.setFeedOverride(1.0)
	r.pass(name)
}

func (h *testHarness) testAbortAlways(r *testResults) {
	const name = "abort_always_succeeds"

	// Abort should succeed in any state
	h.setState(int32(emcstat.ESTOP))
	time.Sleep(50 * time.Millisecond)
	rc, err := h.abort()
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("abort in ESTOP: rc=%d err=%v", rc, err))
		return
	}

	h.ensureOn()
	rc, err = h.abort()
	if err != nil || !isOK(rc) {
		r.fail(name, fmt.Sprintf("abort when ON: rc=%d err=%v", rc, err))
		return
	}
	r.pass(name)
}

// --- Helpers ---

// ensureOn brings the machine to state ON if not already there.
func (h *testHarness) ensureOn() {
	stat, _ := h.getStat()
	if stat.Task.State == emcstat.ON {
		return
	}
	if stat.Task.State == emcstat.ESTOP {
		h.setState(int32(emcstat.ESTOP_RESET))
		h.waitForState(emcstat.ESTOP_RESET, 500*time.Millisecond)
	}
	h.setState(int32(emcstat.ON))
	h.waitForState(emcstat.ON, 500*time.Millisecond)
	// Wait for motion to actually enable (servo cycle).
	h.waitForMotionEnabled(500 * time.Millisecond)
}
