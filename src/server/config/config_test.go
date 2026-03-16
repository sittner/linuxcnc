// src/server/config/config_test.go
package config

import (
	"os"
	"path/filepath"
	"testing"
)

// writeFile is a helper to write content to a file, failing the test on error.
func writeFile(t *testing.T, path, content string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}
}

// ─── Load tests ──────────────────────────────────────────────────────────────

func TestLoadValidFullCNCINI(t *testing.T) {
	tmpDir := t.TempDir()
	halPath := filepath.Join(tmpDir, "test.hal")
	writeFile(t, halPath, "# test")

	iniContent := `
[EMC]
MACHINE = TestMachine
VERSION = 1.0

[DISPLAY]
DISPLAY = axis

[TASK]
TASK = milltask
CYCLE_TIME = 0.010

[RS274NGC]
PARAMETER_FILE = test.var

[EMCMOT]
SERVO_PERIOD = 1000000

[EMCIO]
EMCIO = io
CYCLE_TIME = 0.100
TOOL_TABLE = tool.tbl

[HAL]
HALFILE = test.hal
HALUI = halui

[TRAJ]
COORDINATES = X Y Z
LINEAR_UNITS = mm

[KINS]
KINEMATICS = trivkins
JOINTS = 3
`
	iniPath := filepath.Join(tmpDir, "test.ini")
	writeFile(t, iniPath, iniContent)

	cfg, err := Load(iniPath)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	if cfg.EMC.MachineName != "TestMachine" {
		t.Errorf("MachineName = %q, want %q", cfg.EMC.MachineName, "TestMachine")
	}
	if cfg.Task.Task != "milltask" {
		t.Errorf("Task.Task = %q, want %q", cfg.Task.Task, "milltask")
	}
	if cfg.EMCIO.EMCIO != "io" {
		t.Errorf("EMCIO.EMCIO = %q, want %q", cfg.EMCIO.EMCIO, "io")
	}
	if cfg.HAL.HALUI != "halui" {
		t.Errorf("HAL.HALUI = %q, want %q", cfg.HAL.HALUI, "halui")
	}
	if cfg.Display.Display != "axis" {
		t.Errorf("Display.Display = %q, want %q", cfg.Display.Display, "axis")
	}
	if len(cfg.HAL.Files) != 1 || cfg.HAL.Files[0] != "test.hal" {
		t.Errorf("HAL.Files = %v, want [test.hal]", cfg.HAL.Files)
	}
	if cfg.Task.CycleTime != 0.010 {
		t.Errorf("Task.CycleTime = %v, want 0.010", cfg.Task.CycleTime)
	}
	if cfg.Kins.Joints != 3 {
		t.Errorf("Kins.Joints = %d, want 3", cfg.Kins.Joints)
	}
	if !cfg.HasTask() {
		t.Error("HasTask() = false, want true")
	}
	if !cfg.HasDisplay() {
		t.Error("HasDisplay() = false, want true")
	}
	if !cfg.HasHALUI() {
		t.Error("HasHALUI() = false, want true")
	}
	if !cfg.HasIO() {
		t.Error("HasIO() = false, want true")
	}
}

func TestLoadHALOnlyINI(t *testing.T) {
	tmpDir := t.TempDir()
	halPath := filepath.Join(tmpDir, "my-hardware.hal")
	writeFile(t, halPath, "# hal-only test")

	iniContent := `
[EMC]
MACHINE = MyHALMachine

[HAL]
HALFILE = my-hardware.hal
`
	iniPath := filepath.Join(tmpDir, "hal-only.ini")
	writeFile(t, iniPath, iniContent)

	cfg, err := Load(iniPath)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	if cfg.EMC.MachineName != "MyHALMachine" {
		t.Errorf("MachineName = %q, want %q", cfg.EMC.MachineName, "MyHALMachine")
	}
	if cfg.HasTask() {
		t.Error("HasTask() = true, want false for HAL-only INI")
	}
	if cfg.HasDisplay() {
		t.Error("HasDisplay() = true, want false for HAL-only INI")
	}
	if cfg.HasHALUI() {
		t.Error("HasHALUI() = true, want false for HAL-only INI")
	}
	if cfg.HasIO() {
		t.Error("HasIO() = true, want false for HAL-only INI")
	}
	if cfg.Mode() != "HAL-only" {
		t.Errorf("Mode() = %q, want %q", cfg.Mode(), "HAL-only")
	}
}

func TestLoadMultipleHALFiles(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "hw.hal"), "# hw")
	writeFile(t, filepath.Join(tmpDir, "logic.hal"), "# logic")

	iniContent := `
[EMC]
MACHINE = Multi

[HAL]
HALFILE = hw.hal
HALFILE = logic.hal
`
	iniPath := filepath.Join(tmpDir, "multi.ini")
	writeFile(t, iniPath, iniContent)

	cfg, err := Load(iniPath)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}
	if len(cfg.HAL.Files) != 2 {
		t.Errorf("HAL.Files len = %d, want 2", len(cfg.HAL.Files))
	}
}

func TestLoadRetainSection(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	iniContent := `
[EMC]
MACHINE = RetainMachine

[HAL]
HALFILE = test.hal

[RETAIN]
VAR_FILE = persist.hal
POLL_PERIOD = 1000
`
	iniPath := filepath.Join(tmpDir, "retain.ini")
	writeFile(t, iniPath, iniContent)

	cfg, err := Load(iniPath)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}
	if !cfg.HasRetain() {
		t.Error("HasRetain() = false, want true")
	}
	if cfg.Retain.VarFile != "persist.hal" {
		t.Errorf("Retain.VarFile = %q, want %q", cfg.Retain.VarFile, "persist.hal")
	}
}

func TestLoadFileNotFound(t *testing.T) {
	_, err := Load("/nonexistent/path/test.ini")
	if err == nil {
		t.Error("Load should fail for non-existent file")
	}
}

// ─── Validate tests ───────────────────────────────────────────────────────────

func TestValidateHALOnlyMode(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
		},
	}

	if err := cfg.Validate(); err != nil {
		t.Errorf("HAL-only config should be valid, got: %v", err)
	}
}

func TestValidateHALOnlyMissingHALFile(t *testing.T) {
	cfg := &Config{
		IniPath: "/tmp/test.ini",
	}
	if err := cfg.Validate(); err == nil {
		t.Error("Validate should fail when no HALFILE is set")
	}
}

func TestValidateFullCNCModeRequiresKins(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
		},
		Task: struct {
			Task      string  `ini:"TASK"`
			CycleTime float64 `ini:"CYCLE_TIME"`
		}{
			Task: "milltask",
		},
		EMCMOT: struct {
			ServoPeriod float64 `ini:"SERVO_PERIOD"`
			BasePeriod  float64 `ini:"BASE_PERIOD"`
			CommTimeout float64 `ini:"COMM_TIMEOUT"`
		}{
			ServoPeriod: 1000000,
		},
		Traj: struct {
			Coordinates     string  `ini:"COORDINATES"`
			LinearUnits     string  `ini:"LINEAR_UNITS"`
			AngularUnits    string  `ini:"ANGULAR_UNITS"`
			MaxVelocity     float64 `ini:"MAX_VELOCITY"`
			MaxAcceleration float64 `ini:"MAX_ACCELERATION"`
		}{
			Coordinates: "X Y Z",
		},
	}
	// Kins.Kinematics is empty → should fail
	if err := cfg.Validate(); err == nil {
		t.Error("Validate should fail when [TASK]TASK is set but [KINS]KINEMATICS is missing")
	}
}

func TestValidateFullCNCModeRequiresTraj(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
		},
		Task: struct {
			Task      string  `ini:"TASK"`
			CycleTime float64 `ini:"CYCLE_TIME"`
		}{
			Task: "milltask",
		},
		Kins: struct {
			Kinematics string `ini:"KINEMATICS"`
			Joints     int    `ini:"JOINTS"`
		}{
			Kinematics: "trivkins",
			Joints:     3,
		},
		EMCMOT: struct {
			ServoPeriod float64 `ini:"SERVO_PERIOD"`
			BasePeriod  float64 `ini:"BASE_PERIOD"`
			CommTimeout float64 `ini:"COMM_TIMEOUT"`
		}{
			ServoPeriod: 1000000,
		},
		// Traj.Coordinates is empty → should fail
	}
	if err := cfg.Validate(); err == nil {
		t.Error("Validate should fail when [TASK]TASK is set but [TRAJ]COORDINATES is missing")
	}
}

func TestValidateFullCNCModeRequiresEMCMOT(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
		},
		Task: struct {
			Task      string  `ini:"TASK"`
			CycleTime float64 `ini:"CYCLE_TIME"`
		}{
			Task: "milltask",
		},
		Kins: struct {
			Kinematics string `ini:"KINEMATICS"`
			Joints     int    `ini:"JOINTS"`
		}{
			Kinematics: "trivkins",
			Joints:     3,
		},
		Traj: struct {
			Coordinates     string  `ini:"COORDINATES"`
			LinearUnits     string  `ini:"LINEAR_UNITS"`
			AngularUnits    string  `ini:"ANGULAR_UNITS"`
			MaxVelocity     float64 `ini:"MAX_VELOCITY"`
			MaxAcceleration float64 `ini:"MAX_ACCELERATION"`
		}{
			Coordinates: "X Y Z",
		},
		// EMCMOT.ServoPeriod is 0 → should fail
	}
	if err := cfg.Validate(); err == nil {
		t.Error("Validate should fail when [TASK]TASK is set but [EMCMOT]SERVO_PERIOD is missing")
	}
}

// ─── Dependency validation tests ─────────────────────────────────────────────

func TestValidateHALUIWithoutTaskRejected(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
			HALUI: "halui",
		},
		// No Task set
	}
	if err := cfg.Validate(); err == nil {
		t.Error("Validate should fail when [HAL]HALUI is set but [TASK]TASK is missing")
	}
}

func TestValidateEMCIOWithoutTaskRejected(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
		},
		EMCIO: struct {
			EMCIO     string  `ini:"EMCIO"`
			CycleTime float64 `ini:"CYCLE_TIME"`
			ToolTable string  `ini:"TOOL_TABLE"`
		}{
			EMCIO: "io",
		},
		// No Task set
	}
	if err := cfg.Validate(); err == nil {
		t.Error("Validate should fail when [EMCIO]EMCIO is set but [TASK]TASK is missing")
	}
}

func TestValidateFullCNCModeValid(t *testing.T) {
	tmpDir := t.TempDir()
	writeFile(t, filepath.Join(tmpDir, "test.hal"), "# hal")

	cfg := &Config{
		IniPath: filepath.Join(tmpDir, "test.ini"),
		HAL: struct {
			Files        []string `ini:"HALFILE,omitempty,allowshadow"`
			PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
			ShutdownFile string   `ini:"SHUTDOWN"`
			HALUI        string   `ini:"HALUI"`
		}{
			Files: []string{"test.hal"},
			HALUI: "halui",
		},
		Task: struct {
			Task      string  `ini:"TASK"`
			CycleTime float64 `ini:"CYCLE_TIME"`
		}{
			Task: "milltask",
		},
		EMCIO: struct {
			EMCIO     string  `ini:"EMCIO"`
			CycleTime float64 `ini:"CYCLE_TIME"`
			ToolTable string  `ini:"TOOL_TABLE"`
		}{
			EMCIO: "io",
		},
		Kins: struct {
			Kinematics string `ini:"KINEMATICS"`
			Joints     int    `ini:"JOINTS"`
		}{
			Kinematics: "trivkins",
			Joints:     3,
		},
		Traj: struct {
			Coordinates     string  `ini:"COORDINATES"`
			LinearUnits     string  `ini:"LINEAR_UNITS"`
			AngularUnits    string  `ini:"ANGULAR_UNITS"`
			MaxVelocity     float64 `ini:"MAX_VELOCITY"`
			MaxAcceleration float64 `ini:"MAX_ACCELERATION"`
		}{
			Coordinates: "X Y Z",
		},
		EMCMOT: struct {
			ServoPeriod float64 `ini:"SERVO_PERIOD"`
			BasePeriod  float64 `ini:"BASE_PERIOD"`
			CommTimeout float64 `ini:"COMM_TIMEOUT"`
		}{
			ServoPeriod: 1000000,
		},
	}
	if err := cfg.Validate(); err != nil {
		t.Errorf("Valid full CNC config failed validation: %v", err)
	}
}

// ─── Mode helper tests ────────────────────────────────────────────────────────

func TestModeString(t *testing.T) {
	halOnly := &Config{}
	if halOnly.Mode() != "HAL-only" {
		t.Errorf("empty config Mode() = %q, want %q", halOnly.Mode(), "HAL-only")
	}

	fullCNC := &Config{}
	fullCNC.Task.Task = "milltask"
	if fullCNC.Mode() != "Full CNC (Task + HAL)" {
		t.Errorf("task config Mode() = %q, want %q", fullCNC.Mode(), "Full CNC (Task + HAL)")
	}
}
