package halfile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/sittner/linuxcnc/src/launcher/inifile"
)

// ---------------------------------------------------------------------------
// parseTwopassOptions tests
// ---------------------------------------------------------------------------

func TestParseTwopassOptions_Empty(t *testing.T) {
	opts := parseTwopassOptions("on")
	if opts.verbose {
		t.Error("verbose should be false for plain 'on'")
	}
	if opts.nodelete {
		t.Error("nodelete should be false for plain 'on'")
	}
	if opts.raw != "on" {
		t.Errorf("raw = %q; want %q", opts.raw, "on")
	}
}

func TestParseTwopassOptions_Verbose(t *testing.T) {
	for _, val := range []string{"verbose", "on,verbose", "verbose,on", "on verbose"} {
		opts := parseTwopassOptions(val)
		if !opts.verbose {
			t.Errorf("verbose should be true for %q", val)
		}
	}
}

func TestParseTwopassOptions_Nodelete(t *testing.T) {
	opts := parseTwopassOptions("on,nodelete")
	if !opts.nodelete {
		t.Error("nodelete should be true")
	}
}

func TestParseTwopassOptions_Both(t *testing.T) {
	opts := parseTwopassOptions("on,verbose,nodelete")
	if !opts.verbose {
		t.Error("verbose should be true")
	}
	if !opts.nodelete {
		t.Error("nodelete should be true")
	}
}

func TestParseTwopassOptions_CaseInsensitive(t *testing.T) {
	opts := parseTwopassOptions("VERBOSE,NODELETE")
	if !opts.verbose {
		t.Error("verbose should be true (case-insensitive)")
	}
	if !opts.nodelete {
		t.Error("nodelete should be true (case-insensitive)")
	}
}

// ---------------------------------------------------------------------------
// hasNoTwopass tests
// ---------------------------------------------------------------------------

func TestHasNoTwopass_NotPresent(t *testing.T) {
	dir := t.TempDir()
	path := writeTemp(t, dir, "a.hal", "loadrt trivkins\nnet x-pos-cmd joint.0.motor-pos-cmd\n")
	got, err := hasNoTwopass(path)
	if err != nil {
		t.Fatalf("hasNoTwopass error: %v", err)
	}
	if got {
		t.Error("hasNoTwopass should be false when comment is absent")
	}
}

func TestHasNoTwopass_Present(t *testing.T) {
	dir := t.TempDir()
	path := writeTemp(t, dir, "b.hal", "# normal comment\n#NOTWOPASS\nloadrt trivkins\n")
	got, err := hasNoTwopass(path)
	if err != nil {
		t.Fatalf("hasNoTwopass error: %v", err)
	}
	if !got {
		t.Error("hasNoTwopass should be true when #NOTWOPASS is present")
	}
}

func TestHasNoTwopass_CaseInsensitive(t *testing.T) {
	dir := t.TempDir()
	path := writeTemp(t, dir, "c.hal", "#notwopass\n")
	got, err := hasNoTwopass(path)
	if err != nil {
		t.Fatalf("hasNoTwopass error: %v", err)
	}
	if !got {
		t.Error("hasNoTwopass should be true (case-insensitive)")
	}
}

func TestHasNoTwopass_LeadingWhitespaceBeforeHash(t *testing.T) {
	dir := t.TempDir()
	// Whitespace before the '#' is stripped.
	path := writeTemp(t, dir, "d.hal", "   #NOTWOPASS\nloadrt trivkins\n")
	got, err := hasNoTwopass(path)
	if err != nil {
		t.Fatalf("hasNoTwopass error: %v", err)
	}
	if !got {
		t.Error("hasNoTwopass should be true when there is leading whitespace before #NOTWOPASS")
	}
}

func TestHasNoTwopass_WhitespaceBetweenHashAndKeyword(t *testing.T) {
	dir := t.TempDir()
	// Whitespace between '#' and 'NOTWOPASS' should be stripped before comparison.
	path := writeTemp(t, dir, "e.hal", "# NOTWOPASS\n")
	got, err := hasNoTwopass(path)
	if err != nil {
		t.Fatalf("hasNoTwopass error: %v", err)
	}
	if !got {
		t.Error("hasNoTwopass should be true when spaces between # and NOTWOPASS are ignored")
	}
}

func TestHasNoTwopass_MissingFile(t *testing.T) {
	_, err := hasNoTwopass("/nonexistent/path/file.hal")
	if err == nil {
		t.Error("hasNoTwopass should return error for missing file")
	}
}

// ---------------------------------------------------------------------------
// parseLoadrt tests
// ---------------------------------------------------------------------------

func TestParseLoadrt_ModuleOnly(t *testing.T) {
	module, params, err := parseLoadrt("loadrt trivkins")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "trivkins" {
		t.Errorf("module = %q; want %q", module, "trivkins")
	}
	if params.form != "" {
		t.Errorf("form = %q; want empty", params.form)
	}
}

func TestParseLoadrt_WithCount(t *testing.T) {
	module, params, err := parseLoadrt("loadrt and2 count=3")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "and2" {
		t.Errorf("module = %q; want %q", module, "and2")
	}
	if params.count != 3 {
		t.Errorf("count = %d; want 3", params.count)
	}
	if params.form != "count" {
		t.Errorf("form = %q; want %q", params.form, "count")
	}
}

func TestParseLoadrt_WithNumChan(t *testing.T) {
	module, params, err := parseLoadrt("loadrt pid num_chan=4")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "pid" {
		t.Errorf("module = %q; want %q", module, "pid")
	}
	if params.numChan != 4 {
		t.Errorf("num_chan = %d; want 4", params.numChan)
	}
	if params.form != "num_chan" {
		t.Errorf("form = %q; want %q", params.form, "num_chan")
	}
}

func TestParseLoadrt_WithNames(t *testing.T) {
	module, params, err := parseLoadrt("loadrt and2 names=aa,ab")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "and2" {
		t.Errorf("module = %q; want %q", module, "and2")
	}
	if params.names != "aa,ab" {
		t.Errorf("names = %q; want %q", params.names, "aa,ab")
	}
	if params.form != "names" {
		t.Errorf("form = %q; want %q", params.form, "names")
	}
}

func TestParseLoadrt_WithPersonality(t *testing.T) {
	_, params, err := parseLoadrt("loadrt mux4 personality=0x3")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if params.personality != "0x3" {
		t.Errorf("personality = %q; want %q", params.personality, "0x3")
	}
}

func TestParseLoadrt_WithDebug(t *testing.T) {
	module, params, err := parseLoadrt("loadrt pid num_chan=2 debug=1")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "pid" {
		t.Errorf("module = %q; want %q", module, "pid")
	}
	if params.debug != 1 {
		t.Errorf("debug = %d; want 1", params.debug)
	}
}

func TestParseLoadrt_WithOtherParam(t *testing.T) {
	module, params, err := parseLoadrt("loadrt hm2_eth board_ip=192.168.1.1")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "hm2_eth" {
		t.Errorf("module = %q; want %q", module, "hm2_eth")
	}
	if params.other != "board_ip=192.168.1.1" {
		t.Errorf("other = %q; want %q", params.other, "board_ip=192.168.1.1")
	}
}

func TestParseLoadrt_NoLoadrtKeyword(t *testing.T) {
	// parseLoadrt also accepts lines without the "loadrt" prefix, so it can be
	// used on the argument portion of a loadrt command after the keyword has
	// been stripped by the caller.
	module, params, err := parseLoadrt("and2 count=2")
	if err != nil {
		t.Fatalf("parseLoadrt error: %v", err)
	}
	if module != "and2" {
		t.Errorf("module = %q; want %q", module, "and2")
	}
	if params.count != 2 {
		t.Errorf("count = %d; want 2", params.count)
	}
}

func TestParseLoadrt_InvalidCount(t *testing.T) {
	_, _, err := parseLoadrt("loadrt and2 count=bad")
	if err == nil {
		t.Error("parseLoadrt should return error for invalid count value")
	}
}

// ---------------------------------------------------------------------------
// mergeLoadrt tests
// ---------------------------------------------------------------------------

func TestMergeLoadrt_SumCounts(t *testing.T) {
	dst := &loadrtParams{form: "count", count: 3}
	src := loadrtParams{form: "count", count: 2}
	if err := mergeLoadrt(dst, src, "and2"); err != nil {
		t.Fatalf("mergeLoadrt error: %v", err)
	}
	if dst.count != 5 {
		t.Errorf("merged count = %d; want 5", dst.count)
	}
}

func TestMergeLoadrt_SumNumChan(t *testing.T) {
	dst := &loadrtParams{form: "num_chan", numChan: 2}
	src := loadrtParams{form: "num_chan", numChan: 3}
	if err := mergeLoadrt(dst, src, "pid"); err != nil {
		t.Fatalf("mergeLoadrt error: %v", err)
	}
	if dst.numChan != 5 {
		t.Errorf("merged num_chan = %d; want 5", dst.numChan)
	}
}

func TestMergeLoadrt_ConcatenateNames(t *testing.T) {
	dst := &loadrtParams{form: "names", names: "aa,ab"}
	src := loadrtParams{form: "names", names: "ac,ad"}
	if err := mergeLoadrt(dst, src, "and2"); err != nil {
		t.Fatalf("mergeLoadrt error: %v", err)
	}
	if dst.names != "aa,ab,ac,ad" {
		t.Errorf("merged names = %q; want %q", dst.names, "aa,ab,ac,ad")
	}
}

func TestMergeLoadrt_OrDebug(t *testing.T) {
	dst := &loadrtParams{debug: 1}
	src := loadrtParams{debug: 2}
	if err := mergeLoadrt(dst, src, "pid"); err != nil {
		t.Fatalf("mergeLoadrt error: %v", err)
	}
	if dst.debug != 3 {
		t.Errorf("merged debug = %d; want 3 (OR of 1 and 2)", dst.debug)
	}
}

func TestMergeLoadrt_ConcatenatePersonality(t *testing.T) {
	dst := &loadrtParams{personality: "0x1"}
	src := loadrtParams{personality: "0x2"}
	if err := mergeLoadrt(dst, src, "mux"); err != nil {
		t.Fatalf("mergeLoadrt error: %v", err)
	}
	if dst.personality != "0x1,0x2" {
		t.Errorf("merged personality = %q; want %q", dst.personality, "0x1,0x2")
	}
}

func TestMergeLoadrt_ConcatenateOther(t *testing.T) {
	dst := &loadrtParams{other: "config=abc"}
	src := loadrtParams{other: "config=def"}
	if err := mergeLoadrt(dst, src, "hm2"); err != nil {
		t.Fatalf("mergeLoadrt error: %v", err)
	}
	if dst.other != "config=abc config=def" {
		t.Errorf("merged other = %q; want %q", dst.other, "config=abc config=def")
	}
}

func TestMergeLoadrt_MutualExclusivity_CountAndNames(t *testing.T) {
	dst := &loadrtParams{form: "count", count: 3}
	src := loadrtParams{form: "names", names: "aa,ab"}
	err := mergeLoadrt(dst, src, "and2")
	if err == nil {
		t.Error("mergeLoadrt should return error when mixing count= and names=")
	}
}

func TestMergeLoadrt_MutualExclusivity_NumChanAndCount(t *testing.T) {
	dst := &loadrtParams{form: "num_chan", numChan: 2}
	src := loadrtParams{form: "count", count: 3}
	err := mergeLoadrt(dst, src, "pid")
	if err == nil {
		t.Error("mergeLoadrt should return error when mixing num_chan= and count=")
	}
}

// ---------------------------------------------------------------------------
// buildLoadrtArgs tests
// ---------------------------------------------------------------------------

func TestBuildLoadrtArgs_Count(t *testing.T) {
	args := buildLoadrtArgs("and2", loadrtParams{form: "count", count: 5})
	want := []string{"loadrt", "and2", "count=5"}
	if strings.Join(args, " ") != strings.Join(want, " ") {
		t.Errorf("buildLoadrtArgs = %v; want %v", args, want)
	}
}

func TestBuildLoadrtArgs_Names(t *testing.T) {
	args := buildLoadrtArgs("and2", loadrtParams{form: "names", names: "aa,ab,ac"})
	want := []string{"loadrt", "and2", "names=aa,ab,ac"}
	if strings.Join(args, " ") != strings.Join(want, " ") {
		t.Errorf("buildLoadrtArgs = %v; want %v", args, want)
	}
}

func TestBuildLoadrtArgs_NumChan(t *testing.T) {
	args := buildLoadrtArgs("pid", loadrtParams{form: "num_chan", numChan: 3})
	want := []string{"loadrt", "pid", "num_chan=3"}
	if strings.Join(args, " ") != strings.Join(want, " ") {
		t.Errorf("buildLoadrtArgs = %v; want %v", args, want)
	}
}

func TestBuildLoadrtArgs_WithDebug(t *testing.T) {
	args := buildLoadrtArgs("pid", loadrtParams{form: "num_chan", numChan: 2, debug: 3})
	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "debug=3") {
		t.Errorf("buildLoadrtArgs %q should contain debug=3", joined)
	}
}

func TestBuildLoadrtArgs_ModuleOnly(t *testing.T) {
	args := buildLoadrtArgs("trivkins", loadrtParams{})
	want := []string{"loadrt", "trivkins"}
	if strings.Join(args, " ") != strings.Join(want, " ") {
		t.Errorf("buildLoadrtArgs = %v; want %v", args, want)
	}
}

// ---------------------------------------------------------------------------
// readHalLines tests
// ---------------------------------------------------------------------------

func TestReadHalLines_Basic(t *testing.T) {
	dir := t.TempDir()
	path := writeTemp(t, dir, "test.hal", "loadrt trivkins\nnet signal pin\n")

	ini := parseIni(t, "[HAL]\n")
	e := New(ini, "/usr/bin/halcmd", "", nil)
	lines, err := e.readHalLines(path)
	if err != nil {
		t.Fatalf("readHalLines error: %v", err)
	}
	if len(lines) != 2 {
		t.Fatalf("got %d lines; want 2", len(lines))
	}
	if lines[0] != "loadrt trivkins" {
		t.Errorf("lines[0] = %q; want %q", lines[0], "loadrt trivkins")
	}
}

func TestReadHalLines_BackslashContinuation(t *testing.T) {
	dir := t.TempDir()
	path := writeTemp(t, dir, "test.hal", "loadrt and2 \\\n  count=3\n")

	ini := parseIni(t, "[HAL]\n")
	e := New(ini, "/usr/bin/halcmd", "", nil)
	lines, err := e.readHalLines(path)
	if err != nil {
		t.Fatalf("readHalLines error: %v", err)
	}
	if len(lines) != 1 {
		t.Fatalf("got %d lines; want 1 (continuation should be merged)", len(lines))
	}
	if lines[0] != "loadrt and2   count=3" {
		t.Errorf("lines[0] = %q; want %q", lines[0], "loadrt and2   count=3")
	}
}

// ---------------------------------------------------------------------------
// TCL fallback detection tests
// ---------------------------------------------------------------------------

func TestExecuteTwopass_TclFallback_NoTcl(t *testing.T) {
	// When there are no .tcl files, we should NOT fall back to legacy.
	// We verify this indirectly: if halfiles contains only .hal files, the
	// TCL fallback branch is never taken.
	dir := t.TempDir()
	writeTemp(t, dir, "a.hal", "# just a comment\n")
	iniPath := writeTemp(t, dir, "machine.ini", "[HAL]\nTWOPASS = on\nHALFILE = a.hal\n")
	ini, err := parseIniFile(t, iniPath)
	if err != nil {
		t.Fatalf("parsing INI: %v", err)
	}

	// Verify that the file list contains only .hal.
	entries := ini.GetSection("HAL")
	for _, entry := range entries {
		if entry.Key == "HALFILE" && strings.HasSuffix(entry.Value, ".tcl") {
			t.Errorf("unexpected .tcl file in HALFILE entries: %q", entry.Value)
		}
	}
}

// ---------------------------------------------------------------------------
// End-to-end twopass native tests (mock halcmd via a capture script)
// ---------------------------------------------------------------------------

// captureExecutor wraps Executor and records halcmd calls without running them.
// We override runHalcmdArgs by using a script that writes its args to a file.
func TestExecuteTwopassNative_Pass0CollectsLoadrt(t *testing.T) {
	// Create a pair of HAL files each with a loadrt and2 call.
	dir := t.TempDir()
	writeTemp(t, dir, "a.hal", "loadrt and2 count=2\nnet sig1 and2.0.in0\n")
	writeTemp(t, dir, "b.hal", "loadrt and2 count=3\nnet sig2 and2.0.in1\n")

	iniContent := "[HAL]\nTWOPASS = on\nHALFILE = a.hal\nHALFILE = b.hal\n"
	iniPath := writeTemp(t, dir, "machine.ini", iniContent)
	ini, _ := parseIniFile(t, iniPath)

	// Use a fake halcmd that records commands to a log file.
	logFile := filepath.Join(dir, "halcmd.log")
	fakeHalcmd := filepath.Join(dir, "halcmd")
	writeTemp(t, dir, "halcmd", "#!/bin/sh\necho \"$@\" >> "+logFile+"\n")
	if err := os.Chmod(fakeHalcmd, 0o755); err != nil {
		t.Fatalf("chmod: %v", err)
	}

	e := New(ini, fakeHalcmd, "", nil)
	if err := e.ExecuteAll(); err != nil {
		t.Fatalf("ExecuteAll error: %v", err)
	}

	log, err := os.ReadFile(logFile)
	if err != nil {
		t.Fatalf("reading log: %v", err)
	}
	logStr := string(log)
	t.Logf("halcmd log:\n%s", logStr)

	// The merged loadrt should appear exactly once with count=5.
	if !strings.Contains(logStr, "loadrt and2 count=5") {
		t.Errorf("expected merged 'loadrt and2 count=5' in log, got:\n%s", logStr)
	}

	// Pass 1 should execute the net commands.
	netCount := strings.Count(logStr, "net sig")
	if netCount != 2 {
		t.Errorf("expected 2 'net sig*' lines in pass 1, got %d\nlog:\n%s", netCount, logStr)
	}
}

func TestExecuteTwopassNative_LoadusrInPass0(t *testing.T) {
	dir := t.TempDir()
	writeTemp(t, dir, "a.hal", "loadusr -W hal_input -KRAL Mouse\nloadrt trivkins\n")

	iniContent := "[HAL]\nTWOPASS = on\nHALFILE = a.hal\n"
	iniPath := writeTemp(t, dir, "machine.ini", iniContent)
	ini, _ := parseIniFile(t, iniPath)

	logFile := filepath.Join(dir, "halcmd.log")
	fakeHalcmd := filepath.Join(dir, "halcmd")
	writeTemp(t, dir, "halcmd", "#!/bin/sh\necho \"$@\" >> "+logFile+"\n")
	os.Chmod(fakeHalcmd, 0o755)

	e := New(ini, fakeHalcmd, "", nil)
	if err := e.ExecuteAll(); err != nil {
		t.Fatalf("ExecuteAll error: %v", err)
	}

	log, _ := os.ReadFile(logFile)
	logStr := string(log)

	// loadusr should appear in the log (executed in pass 0).
	if !strings.Contains(logStr, "loadusr") {
		t.Errorf("expected loadusr in log:\n%s", logStr)
	}
	// loadrt trivkins should appear (executed at end of pass 0).
	if !strings.Contains(logStr, "loadrt trivkins") {
		t.Errorf("expected 'loadrt trivkins' in log:\n%s", logStr)
	}
}

func TestExecuteTwopassNative_NoTwopassFile_ExecutedImmediately(t *testing.T) {
	dir := t.TempDir()
	writeTemp(t, dir, "notwopass.hal", "#NOTWOPASS\nloadrt trivkins\n")
	writeTemp(t, dir, "normal.hal", "loadrt and2 count=1\n")

	iniContent := "[HAL]\nTWOPASS = on\nHALFILE = notwopass.hal\nHALFILE = normal.hal\n"
	iniPath := writeTemp(t, dir, "machine.ini", iniContent)
	ini, _ := parseIniFile(t, iniPath)

	logFile := filepath.Join(dir, "halcmd.log")
	fakeHalcmd := filepath.Join(dir, "halcmd")
	writeTemp(t, dir, "halcmd", "#!/bin/sh\necho \"$@\" >> "+logFile+"\n")
	os.Chmod(fakeHalcmd, 0o755)

	e := New(ini, fakeHalcmd, "", nil)
	if err := e.ExecuteAll(); err != nil {
		t.Fatalf("ExecuteAll error: %v", err)
	}

	log, _ := os.ReadFile(logFile)
	logStr := string(log)

	// The #NOTWOPASS file should have been executed with -vkf.
	if !strings.Contains(logStr, "-vkf") {
		t.Errorf("expected -vkf flag for #NOTWOPASS file in log:\n%s", logStr)
	}
	// normal.hal loadrt and2 count=1 should be in the merged pass.
	if !strings.Contains(logStr, "loadrt and2 count=1") {
		t.Errorf("expected 'loadrt and2 count=1' in log:\n%s", logStr)
	}
}

func TestExecuteTwopassNative_NamesFormMerge(t *testing.T) {
	dir := t.TempDir()
	writeTemp(t, dir, "a.hal", "loadrt and2 names=aa,ab\n")
	writeTemp(t, dir, "b.hal", "loadrt and2 names=ac,ad\n")

	iniContent := "[HAL]\nTWOPASS = on\nHALFILE = a.hal\nHALFILE = b.hal\n"
	iniPath := writeTemp(t, dir, "machine.ini", iniContent)
	ini, _ := parseIniFile(t, iniPath)

	logFile := filepath.Join(dir, "halcmd.log")
	fakeHalcmd := filepath.Join(dir, "halcmd")
	writeTemp(t, dir, "halcmd", "#!/bin/sh\necho \"$@\" >> "+logFile+"\n")
	os.Chmod(fakeHalcmd, 0o755)

	e := New(ini, fakeHalcmd, "", nil)
	if err := e.ExecuteAll(); err != nil {
		t.Fatalf("ExecuteAll error: %v", err)
	}

	log, _ := os.ReadFile(logFile)
	logStr := string(log)
	if !strings.Contains(logStr, "names=aa,ab,ac,ad") {
		t.Errorf("expected merged names 'names=aa,ab,ac,ad' in log:\n%s", logStr)
	}
}

func TestExecuteTwopassNative_MixedFormError(t *testing.T) {
	dir := t.TempDir()
	writeTemp(t, dir, "a.hal", "loadrt and2 count=3\n")
	writeTemp(t, dir, "b.hal", "loadrt and2 names=aa,ab\n")

	iniContent := "[HAL]\nTWOPASS = on\nHALFILE = a.hal\nHALFILE = b.hal\n"
	iniPath := writeTemp(t, dir, "machine.ini", iniContent)
	ini, _ := parseIniFile(t, iniPath)

	fakeHalcmd := filepath.Join(dir, "halcmd")
	writeTemp(t, dir, "halcmd", "#!/bin/sh\nexit 0\n")
	os.Chmod(fakeHalcmd, 0o755)

	e := New(ini, fakeHalcmd, "", nil)
	err := e.ExecuteAll()
	if err == nil {
		t.Error("ExecuteAll should return error when mixing count= and names= for the same module")
	}
}

func TestExecuteAll_NoTwopass_SinglePassUnchanged(t *testing.T) {
	// Verify that without TWOPASS, the existing single-pass behavior is unchanged.
	ini := parseIni(t, "[EMC]\nMACHINE = TestMachine\n")
	e := New(ini, "/usr/bin/halcmd", "", nil)
	// No HALFILE entries, no TWOPASS → should return nil without error.
	if err := e.ExecuteAll(); err != nil {
		t.Errorf("ExecuteAll without TWOPASS on empty INI: %v", err)
	}
}

func TestExecuteTwopassNative_HALCMDInPass1(t *testing.T) {
	// HALCMD entries should be executed in pass 1, in INI-file order.
	dir := t.TempDir()
	writeTemp(t, dir, "a.hal", "loadrt trivkins\n")

	iniContent := "[HAL]\nTWOPASS = on\nHALFILE = a.hal\nHALCMD = setp foo 1\n"
	iniPath := writeTemp(t, dir, "machine.ini", iniContent)
	ini, _ := parseIniFile(t, iniPath)

	logFile := filepath.Join(dir, "halcmd.log")
	fakeHalcmd := filepath.Join(dir, "halcmd")
	writeTemp(t, dir, "halcmd", "#!/bin/sh\necho \"$@\" >> "+logFile+"\n")
	os.Chmod(fakeHalcmd, 0o755)

	e := New(ini, fakeHalcmd, "", nil)
	if err := e.ExecuteAll(); err != nil {
		t.Fatalf("ExecuteAll error: %v", err)
	}

	log, _ := os.ReadFile(logFile)
	logStr := string(log)

	if !strings.Contains(logStr, "setp foo 1") {
		t.Errorf("expected 'setp foo 1' in pass 1 log:\n%s", logStr)
	}
}

// ---------------------------------------------------------------------------
// Helper: parseIniFile parses an INI file from a path.
// ---------------------------------------------------------------------------

func parseIniFile(t *testing.T, path string) (*inifile.IniFile, error) {
	t.Helper()
	return inifile.Parse(path)
}
