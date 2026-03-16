// src/server/config/config.go
package config

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
)

// Config holds the complete server configuration parsed from INI file
type Config struct {
	// Path to the INI file (for passing to legacy components)
	IniPath string

	// [EMC] section
	EMC struct {
		MachineName string  `ini:"MACHINE"`
		Debug       int     `ini:"DEBUG"`
		Version     string  `ini:"VERSION"`
	} `ini:"EMC"`

	// [DISPLAY] section - for reference, UI handles this
	Display struct {
		Display         string  `ini:"DISPLAY"`
		CycleTime       float64 `ini:"CYCLE_TIME"`
		MaxFeedOverride float64 `ini:"MAX_FEED_OVERRIDE"`
	} `ini:"DISPLAY"`

	// [TASK] section
	Task struct {
		Task      string  `ini:"TASK"`      // e.g., "milltask" — present means CNC mode
		CycleTime float64 `ini:"CYCLE_TIME"`
	} `ini:"TASK"`

	// [RS274NGC] section
	RS274NGC struct {
		ParameterFile  string `ini:"PARAMETER_FILE"`
		SubroutinePath string `ini:"SUBROUTINE_PATH"`
	} `ini:"RS274NGC"`

	// [EMCMOT] section
	EMCMOT struct {
		ServoPeriod float64 `ini:"SERVO_PERIOD"`
		BasePeriod  float64 `ini:"BASE_PERIOD"`
		CommTimeout float64 `ini:"COMM_TIMEOUT"`
	} `ini:"EMCMOT"`

	// [EMCIO] section
	EMCIO struct {
		EMCIO     string  `ini:"EMCIO"`     // e.g., "io" — only used when Task is set
		CycleTime float64 `ini:"CYCLE_TIME"`
		ToolTable string  `ini:"TOOL_TABLE"`
	} `ini:"EMCIO"`

	// [HAL] section
	HAL struct {
		Files        []string `ini:"HALFILE,omitempty,allowshadow"`
		PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
		ShutdownFile string   `ini:"SHUTDOWN"`
		HALUI        string   `ini:"HALUI"` // e.g., "halui" — requires Task
	} `ini:"HAL"`

	// [TRAJ] section
	Traj struct {
		Coordinates     string  `ini:"COORDINATES"`
		LinearUnits     string  `ini:"LINEAR_UNITS"`
		AngularUnits    string  `ini:"ANGULAR_UNITS"`
		MaxVelocity     float64 `ini:"MAX_VELOCITY"`
		MaxAcceleration float64 `ini:"MAX_ACCELERATION"`
	} `ini:"TRAJ"`

	// [KINS] section
	Kins struct {
		Kinematics string `ini:"KINEMATICS"`
		Joints     int    `ini:"JOINTS"`
	} `ini:"KINS"`

	// [RETAIN] section — for retain/persist HAL variable storage
	Retain struct {
		VarFile    string `ini:"VAR_FILE"`
		PollPeriod string `ini:"POLL_PERIOD"`
	} `ini:"RETAIN"`

	// Raw INI data for sections we pass through unchanged
	raw *iniFile
}

// HasTask returns true if the Task controller should be started ([TASK]TASK is set).
func (c *Config) HasTask() bool {
	return c.Task.Task != ""
}

// HasDisplay returns true if a display program should be started ([DISPLAY]DISPLAY is set).
func (c *Config) HasDisplay() bool {
	return c.Display.Display != ""
}

// HasHALUI returns true if halui should be started ([HAL]HALUI is set).
func (c *Config) HasHALUI() bool {
	return c.HAL.HALUI != ""
}

// HasIO returns true if the IO controller should be started ([EMCIO]EMCIO is set).
func (c *Config) HasIO() bool {
	return c.EMCIO.EMCIO != ""
}

// HasRetain returns true if retain/persist is configured.
func (c *Config) HasRetain() bool {
	return c.Retain.VarFile != ""
}

// Validate checks the configuration for required fields and applies defaults.
// It enforces conditional requirements based on operational mode:
//   - HAL-only mode (no [TASK]TASK): only [HAL]HALFILE is required.
//   - Full CNC mode ([TASK]TASK set): also requires [KINS], [TRAJ], [EMCMOT], [RS274NGC].
func (c *Config) Validate() error {
	// Apply defaults
	if c.EMC.MachineName == "" {
		c.EMC.MachineName = "LinuxCNC"
	}
	if c.Task.CycleTime <= 0 {
		c.Task.CycleTime = 0.010 // 10ms default
	}
	if c.EMCIO.CycleTime <= 0 {
		c.EMCIO.CycleTime = 0.100 // 100ms default
	}

	// HAL files are always required
	if len(c.HAL.Files) == 0 {
		return fmt.Errorf("at least one [HAL]HALFILE is required")
	}

	// Validate that HAL files exist on disk
	iniDir := filepath.Dir(c.IniPath)
	for _, f := range c.HAL.Files {
		path := f
		if !filepath.IsAbs(path) {
			path = filepath.Join(iniDir, f)
		}
		if _, err := os.Stat(path); err != nil {
			return fmt.Errorf("HAL file not found: %s", f)
		}
	}

	// Full CNC mode: validate sections required by Task controller
	if c.HasTask() {
		if c.Kins.Kinematics == "" {
			return fmt.Errorf("[TASK]TASK is set but [KINS]KINEMATICS is missing")
		}
		if c.Kins.Joints <= 0 {
			return fmt.Errorf("[TASK]TASK is set but [KINS]JOINTS must be > 0")
		}
		if c.Traj.Coordinates == "" {
			return fmt.Errorf("[TASK]TASK is set but [TRAJ]COORDINATES is missing")
		}
		if c.EMCMOT.ServoPeriod <= 0 {
			return fmt.Errorf("[TASK]TASK is set but [EMCMOT]SERVO_PERIOD is missing or zero")
		}
	}

	// Cross-section dependency validation
	return c.validateDependencies()
}

// validateDependencies checks cross-section dependency rules and rejects
// contradictory configurations with a clear error message.
func (c *Config) validateDependencies() error {
	// [HAL]HALUI requires [TASK]TASK
	if c.HasHALUI() && !c.HasTask() {
		return fmt.Errorf("[HAL]HALUI is set but [TASK]TASK is missing — halui requires the task controller")
	}

	// [EMCIO]EMCIO requires [TASK]TASK (IO communicates with Task via NML)
	if c.HasIO() && !c.HasTask() {
		return fmt.Errorf("[EMCIO]EMCIO is set but [TASK]TASK is missing — IO controller requires the task controller")
	}

	return nil
}

// Mode returns a human-readable string describing the operational mode.
func (c *Config) Mode() string {
	if c.HasTask() {
		return "Full CNC (Task + HAL)"
	}
	return "HAL-only"
}

// Dump writes the configuration to the given writer for debugging
func (c *Config) Dump(w io.Writer) {
	fmt.Fprintf(w, "=== Configuration ===\n")
	fmt.Fprintf(w, "INI File:    %s\n", c.IniPath)
	fmt.Fprintf(w, "Machine:     %s\n", c.EMC.MachineName)
	fmt.Fprintf(w, "Mode:        %s\n", c.Mode())
	fmt.Fprintf(w, "Task:        %q\n", c.Task.Task)
	fmt.Fprintf(w, "EMCIO:       %q\n", c.EMCIO.EMCIO)
	fmt.Fprintf(w, "HALUI:       %q\n", c.HAL.HALUI)
	fmt.Fprintf(w, "Display:     %q\n", c.Display.Display)
	fmt.Fprintf(w, "Task Cycle:  %.3fs\n", c.Task.CycleTime)
	fmt.Fprintf(w, "IO Cycle:    %.3fs\n", c.EMCIO.CycleTime)
	if c.HasTask() {
		fmt.Fprintf(w, "Kinematics:  %s\n", c.Kins.Kinematics)
		fmt.Fprintf(w, "Joints:      %d\n", c.Kins.Joints)
	}
	fmt.Fprintf(w, "HAL Files:   %v\n", c.HAL.Files)
	if c.HasRetain() {
		fmt.Fprintf(w, "Retain File: %s\n", c.Retain.VarFile)
	}
	fmt.Fprintf(w, "=====================\n")
}

// GetSection returns raw INI section data for legacy component compatibility
func (c *Config) GetSection(name string) map[string]string {
	if c.raw == nil {
		return nil
	}
	return c.raw.GetSection(name)
}
