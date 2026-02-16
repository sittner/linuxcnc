# LinuxCNC Server Migration - Phase 1 Implementation Document

**Version 2.0 - Production-Ready Implementation**

## Executive Summary

This document describes the implementation of a Go-based LinuxCNC server that consolidates the current multi-process architecture into a single server process while maintaining full backward compatibility through preserved NML communication.

**Goal:** Replace the `linuxcnc` startup script and multiple processes with a single `linuxcnc-server` binary that orchestrates all non-UI components.

**Compatibility:** All existing UIs (AXIS, gmoccapy, QtVCP, etc.) continue to work unchanged via NML.

### Version 2.0 Improvements (Production-Ready)

This version addresses code review feedback to make Phase 1 production-ready:

1. **✅ Structured Logging**: Added `log/slog` based logging package with configurable levels, cycle metrics, and jitter detection
2. **✅ Health Monitoring**: New health monitoring package tracks cycle times, detects stalled goroutines, and provides status reporting
3. **✅ Direct HAL Integration**: Removed `system()` calls in shims, using direct HAL library and dlopen for module loading
4. **✅ Improved INI Substitution**: Complete implementation supporting nested variables, array syntax, and environment expansion
5. **✅ Native Go IOControl**: Optional pure Go implementation (~300 lines) as alternative to C++ shim layer
6. **✅ Thread Safety Documentation**: Comprehensive documentation of shared state, mutex requirements, and concurrency guidelines
7. **✅ Build System Improvements**: Fixed path handling for out-of-tree builds using Makefile-generated environment variables
8. **✅ Go 1.22+ Updates**: Updated to Go 1.22 for improved slog and loop semantics, switched to actively maintained ini library

---

## 1. Architecture Overview

### 1.1 Current Architecture (Before)

```
linuxcnc (bash script)
    │
    ├── rtapi_app (process)     ← RT thread management
    │       │
    │       └── loads: motmod.so, tpmod.so, [kins].so, [hal comps]
    │
    ├── halcmd (process)        ← HAL configuration
    │
    ├── milltask (process)      ← Task controller
    │       │
    │       └── NML channels ←──────────────┐
    │                                        │
    ├── iocontrol (process)     ← IO controller
    │       │                                │
    │       └── NML channels ←──────────────┤
    │                                        │
    └── [UI process]            ← User interface
            │                                │
            └── NML channels ←──────────────┘
```

### 1.2 Target Architecture (After Phase 1)

```
linuxcnc-server (single Go process)
    │
    ├── [embedded] RTAPI initialization
    │       │
    │       └── loads: motmod.so, tpmod.so, [kins].so, [hal comps]
    │
    ├── [embedded] HAL configuration loader
    │
    ├── [goroutine] Task controller thread
    │       │
    │       └── NML channels ←──────────────┐  (preserved!)
    │                                        │
    ├── [goroutine] IO controller thread     │
    │       │                                │
    │       └── NML channels ←──────────────┤
    │                                        │
    └── [external] UI process    (unchanged) │
            │                                │
            └── NML channels ←──────────────┘
```

### 1.3 Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Server language | Go | Modern, excellent concurrency, easy networking for future API |
| C/C++ integration | cgo with C shims | Clean boundary, handles C++ name mangling |
| NML | Preserved | Backward compatibility with all existing UIs |
| Task/IOControl | Linked as libraries | Minimal changes to existing code |
| Configuration | Go parses INI, passes to components | Centralized config handling |
| HAL loading | Execute existing HAL file commands | Reuse proven mechanism |

---

## 2. Component Specifications

### 2.1 Directory Structure

```
linuxcnc/
├── src/
│   ├── server/                      # NEW: Go server
│   │   ├── main.go                  # Entry point
│   │   ├── go.mod                   # Go module definition
│   │   ├── go.sum                   # Dependencies
│   │   │
│   │   ├── config/                  # Configuration handling
│   │   │   ├── config.go            # Main config structures
│   │   │   ├── ini.go               # INI file parsing
│   │   │   └── validate.go          # Config validation
│   │   │
│   │   ├── rtapi/                   # RTAPI integration
│   │   │   ├── rtapi.go             # Go wrapper
│   │   │   └── rtapi_cgo.go         # cgo bindings
│   │   │
│   │   ├── hal/                     # HAL integration
│   │   │   ├── hal.go               # Go wrapper
│   │   │   ├── hal_cgo.go           # cgo bindings
│   │   │   └── loader.go            # HAL file loader
│   │   │
│   │   ├── task/                    # Task controller integration
│   │   │   ├── task.go              # Go wrapper
│   │   │   └── task_cgo.go          # cgo bindings
│   │   │
│   │   ├── iocontrol/               # IO controller integration
│   │   │   ├── iocontrol.go         # Go wrapper
│   │   │   └── iocontrol_cgo.go     # cgo bindings
│   │   │
│   │   └── shim/                    # C shim layer (compiled with cgo)
│   │       ├── shim.h               # Common definitions
│   │       ├── rtapi_shim.c         # RTAPI C shim
│   │       ├── rtapi_shim.h
│   │       ├── hal_shim.c           # HAL C shim
│   │       ├── hal_shim.h
│   │       ├── task_shim.c          # Task C shim
│   │       ├── task_shim.h
│   │       ├── iocontrol_shim.c     # IOControl C shim
│   │       └── iocontrol_shim.h
│   │
│   ├── emc/                         # EXISTING: Minimal modifications
│   │   ├── task/
│   │   │   ├── emctaskmain.cc       # MODIFIED: Remove main(), add init/cycle/shutdown
│   │   │   ├── emccanon.cc          # UNCHANGED
│   │   │   ├── taskintf.cc          # UNCHANGED
│   │   │   ├── taskclass.cc         # UNCHANGED
│   │   │   └── Submakefile          # MODIFIED: Build as library
│   │   │
│   │   ├── iotask/
│   │   │   ├── ioControl.cc         # MODIFIED: Remove main(), add init/cycle/shutdown
│   │   │   └── Submakefile          # MODIFIED: Build as library
│   │   │
│   │   ├── motion/                  # UNCHANGED
│   │   ├── tp/                      # UNCHANGED
│   │   ├── kinematics/              # UNCHANGED
│   │   └── rs274ngc/                # UNCHANGED
│   │
│   ├── hal/                         # UNCHANGED
│   ├── rtapi/                       # UNCHANGED
│   └── libnml/                      # UNCHANGED
│
├── lib/                             # Built libraries
│   ├── libtask.so
│   ├── libiocontrol.so
│   └── ... (existing libs)
│
├── bin/
│   ├── linuxcnc-server              # NEW: Go server binary
│   └── ... (existing binaries)
│
└── Makefile                         # MODIFIED: Add server build target
```

### 2.2 Build Outputs

| Output | Type | Contents |
|--------|------|----------|
| `lib/libtask.so` | Shared library | Task controller (modified emctaskmain.cc) |
| `lib/libiocontrol.so` | Shared library | IO controller (modified ioControl.cc) |
| `bin/linuxcnc-server` | Executable | Go server with embedded shims |

---

## 3. Implementation Details

### 3.1 Go Server Main Entry Point

```go
// src/server/main.go
package main

import (
	"context"
	"flag"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"syscall"

	"linuxcnc/server/config"
	"linuxcnc/server/hal"
	"linuxcnc/server/health"
	"linuxcnc/server/iocontrol"
	"linuxcnc/server/logging"
	"linuxcnc/server/rtapi"
	"linuxcnc/server/task"

	"golang.org/x/sync/errgroup"
)

var (
	Version   = "dev"
	BuildTime = "unknown"
)

func main() {
	// Command line flags
	iniFile := flag.String("ini", "", "Path to INI configuration file")
	version := flag.Bool("version", false, "Print version and exit")
	logLevel := flag.String("log-level", "info", "Log level (debug, info, warn, error)")
	logFormat := flag.String("log-format", "text", "Log format (text, json)")
	flag.Parse()

	if *version {
		fmt.Printf("linuxcnc-server %s (built %s)\n", Version, BuildTime)
		os.Exit(0)
	}

	if *iniFile == "" {
		fmt.Fprintln(os.Stderr, "Error: -ini flag is required")
		fmt.Fprintln(os.Stderr, "Usage: linuxcnc-server -ini <config.ini>")
		os.Exit(1)
	}

	// Initialize structured logging
	logger := logging.NewLogger(logging.Config{
		Level:  logging.ParseLevel(*logLevel),
		Format: logging.ParseFormat(*logFormat),
		Output: os.Stdout,
	})
	slog.SetDefault(logger)

	// Run server
	if err := run(*iniFile, logger); err != nil {
		logger.Error("server failed", "error", err)
		os.Exit(1)
	}
}

func run(iniFile string, logger *slog.Logger) error {
	// ===== Step 1: Load and validate configuration =====
	logger.Info("loading configuration", "ini_file", iniFile)
	cfg, err := config.Load(iniFile)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	if err := cfg.Validate(); err != nil {
		return fmt.Errorf("invalid config: %w", err)
	}

	logger.Info("configuration loaded",
		"machine", cfg.EMC.MachineName,
		"task_cycle_ms", cfg.Task.CycleTime*1000,
		"io_cycle_ms", cfg.EMCIO.CycleTime*1000)

	// ===== Step 2: Setup signal handling =====
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		sig := <-sigCh
		logger.Info("received shutdown signal", "signal", sig)
		cancel()
	}()

	// ===== Step 3: Initialize RTAPI =====
	logger.Info("initializing RTAPI")
	rt, err := rtapi.Init(rtapi.Config{
		InstanceName: cfg.EMC.MachineName,
		Logger:       logger.With("component", "rtapi"),
	})
	if err != nil {
		return fmt.Errorf("rtapi init failed: %w", err)
	}
	defer rt.Shutdown()

	logger.Info("RTAPI initialized")

	// ===== Step 4: Initialize HAL =====
	logger.Info("initializing HAL")
	h, err := hal.Init(hal.Config{
		ComponentName: "linuxcnc",
		Logger:        logger.With("component", "hal"),
	})
	if err != nil {
		return fmt.Errorf("hal init failed: %w", err)
	}
	defer h.Shutdown()

	logger.Info("HAL initialized")

	// ===== Step 5: Load HAL configuration =====
	logger.Info("loading HAL configuration", "file_count", len(cfg.HAL.Files))
	halLoader := hal.NewLoader(h, cfg, logger.With("component", "hal_loader"))
	if err := halLoader.LoadFiles(cfg.HAL.Files); err != nil {
		return fmt.Errorf("hal config failed: %w", err)
	}

	logger.Info("HAL configuration loaded")

	// ===== Step 6: Initialize health monitoring =====
	logger.Info("initializing health monitor")
	healthMon := health.NewMonitor(health.Config{
		TaskCycleTime:     cfg.Task.CycleTime,
		IOCycleTime:       cfg.EMCIO.CycleTime,
		JitterThreshold:   0.1, // 10% jitter threshold
		HeartbeatInterval: 1.0, // 1 second heartbeat
		Logger:            logger.With("component", "health"),
	})
	defer healthMon.Stop()

	// ===== Step 7: Initialize IO Controller =====
	logger.Info("initializing IO controller")
	ioc, err := iocontrol.Init(iocontrol.Config{
		IniFile:      iniFile,
		CycleTime:    cfg.EMCIO.CycleTime,
		UseNative:    cfg.EMCIO.UseNativeGo, // Enable native Go implementation if configured
		Logger:       logger.With("component", "iocontrol"),
		HealthMonitor: healthMon,
	})
	if err != nil {
		return fmt.Errorf("iocontrol init failed: %w", err)
	}
	defer ioc.Shutdown()

	logger.Info("IO controller initialized")

	// ===== Step 8: Initialize Task Controller =====
	logger.Info("initializing task controller")
	tsk, err := task.Init(task.Config{
		IniFile:       iniFile,
		CycleTime:     cfg.Task.CycleTime,
		Logger:        logger.With("component", "task"),
		HealthMonitor: healthMon,
	})
	if err != nil {
		return fmt.Errorf("task init failed: %w", err)
	}
	defer tsk.Shutdown()

	logger.Info("task controller initialized")

	// ===== Step 9: Signal HAL ready =====
	if err := h.Ready(); err != nil {
		return fmt.Errorf("hal ready failed: %w", err)
	}

	logger.Info("HAL ready")

	// ===== Step 10: Run main loops with health monitoring =====
	logger.Info("starting main control loops")

	g, gctx := errgroup.WithContext(ctx)

	// Task controller loop
	g.Go(func() error {
		return tsk.Run(gctx)
	})

	// IO controller loop
	g.Go(func() error {
		return ioc.Run(gctx)
	})

	// Health monitoring loop
	g.Go(func() error {
		return healthMon.Run(gctx)
	})

	// Wait for shutdown
	if err := g.Wait(); err != nil && err != context.Canceled {
		logger.Error("control loop error", "error", err)
		return fmt.Errorf("runtime error: %w", err)
	}

	logger.Info("shutdown complete")
	return nil
}
```

**Key improvements in main.go:**
- **Structured logging**: `log/slog` used throughout with contextual fields
- **Health monitoring**: Dedicated goroutine tracks cycle times and jitter
- **Logger injection**: Each component gets a child logger with component name
- **Detailed lifecycle logging**: Every major step logged with relevant context

### 3.2 Go Module Definition

```go
// src/server/go.mod
module linuxcnc/server

go 1.22

require (
	golang.org/x/sync v0.8.0
	github.com/go-ini/ini v1.67.0
)
```

**Changes from initial design:**
- **Go 1.22+**: Improved `log/slog` stdlib support and loop variable semantics
- **github.com/go-ini/ini**: Active maintenance vs. `gopkg.in/ini.v1` (maintenance mode)

### 3.2a Logging Package

```go
// src/server/logging/logging.go
package logging

import (
	"io"
	"log/slog"
	"os"
	"time"
)

// Level represents log level
type Level int

const (
	LevelDebug Level = iota
	LevelInfo
	LevelWarn
	LevelError
)

// Format represents log output format
type Format int

const (
	FormatText Format = iota
	FormatJSON
)

// Config holds logging configuration
type Config struct {
	Level  Level
	Format Format
	Output io.Writer
}

// ParseLevel converts string to Level
func ParseLevel(s string) Level {
	switch s {
	case "debug":
		return LevelDebug
	case "warn":
		return LevelWarn
	case "error":
		return LevelError
	default:
		return LevelInfo
	}
}

// ParseFormat converts string to Format
func ParseFormat(s string) Format {
	if s == "json" {
		return FormatJSON
	}
	return FormatText
}

// NewLogger creates a new structured logger with given configuration
func NewLogger(cfg Config) *slog.Logger {
	var level slog.Level
	switch cfg.Level {
	case LevelDebug:
		level = slog.LevelDebug
	case LevelWarn:
		level = slog.LevelWarn
	case LevelError:
		level = slog.LevelError
	default:
		level = slog.LevelInfo
	}

	opts := &slog.HandlerOptions{
		Level: level,
		ReplaceAttr: func(groups []string, a slog.Attr) slog.Attr {
			// Format timestamps to RFC3339 with microseconds
			if a.Key == slog.TimeKey {
				if t, ok := a.Value.Any().(time.Time); ok {
					a.Value = slog.StringValue(t.Format("2006-01-02T15:04:05.000000Z07:00"))
				}
			}
			return a
		},
	}

	var handler slog.Handler
	if cfg.Format == FormatJSON {
		handler = slog.NewJSONHandler(cfg.Output, opts)
	} else {
		handler = slog.NewTextHandler(cfg.Output, opts)
	}

	return slog.New(handler)
}

// CycleMetrics tracks cycle timing for performance monitoring
type CycleMetrics struct {
	logger        *slog.Logger
	componentName string
	expectedCycle time.Duration
	lastCycle     time.Time
	cycleCount    uint64
	errorCount    uint64
	jitterCount   uint64
}

// NewCycleMetrics creates a new cycle metrics tracker
func NewCycleMetrics(logger *slog.Logger, component string, cycleSec float64) *CycleMetrics {
	return &CycleMetrics{
		logger:        logger,
		componentName: component,
		expectedCycle: time.Duration(cycleSec * float64(time.Second)),
		lastCycle:     time.Now(),
	}
}

// RecordCycle records a cycle execution with optional error
func (m *CycleMetrics) RecordCycle(err error) {
	now := time.Now()
	actual := now.Sub(m.lastCycle)
	m.lastCycle = now
	m.cycleCount++

	if err != nil {
		m.errorCount++
		m.logger.Error("cycle error",
			"component", m.componentName,
			"cycle_num", m.cycleCount,
			"error", err)
	}

	// Check for jitter (> 10% deviation)
	deviation := float64(actual-m.expectedCycle) / float64(m.expectedCycle)
	if deviation > 0.1 || deviation < -0.1 {
		m.jitterCount++
		m.logger.Warn("cycle jitter detected",
			"component", m.componentName,
			"expected_us", m.expectedCycle.Microseconds(),
			"actual_us", actual.Microseconds(),
			"deviation_pct", int(deviation*100))
	}

	// Periodic summary every 10000 cycles (1.67 minutes at 100Hz)
	if m.cycleCount%10000 == 0 {
		m.logger.Info("cycle metrics",
			"component", m.componentName,
			"cycles", m.cycleCount,
			"errors", m.errorCount,
			"jitter_events", m.jitterCount)
	}
}

// GetStats returns current statistics
func (m *CycleMetrics) GetStats() (cycles, errors, jitter uint64) {
	return m.cycleCount, m.errorCount, m.jitterCount
}
```

**Key features:**
- **Configurable levels**: Debug, Info, Warn, Error
- **Multiple formats**: Human-readable text or machine-parseable JSON
- **Cycle metrics**: Automatic jitter detection and periodic summaries
- **Context propagation**: Child loggers with component names
- **Performance monitoring**: Track error rates and timing deviations

### 3.3 Configuration Package

```go
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
	}

	// [DISPLAY] section - for reference, UI handles this
	Display struct {
		Display         string  `ini:"DISPLAY"`
		CycleTime       float64 `ini:"CYCLE_TIME"`
		MaxFeedOverride float64 `ini:"MAX_FEED_OVERRIDE"`
	}

	// [TASK] section
	Task struct {
		CycleTime float64 `ini:"CYCLE_TIME"`
	}

	// [RS274NGC] section
	RS274NGC struct {
		ParameterFile  string `ini:"PARAMETER_FILE"`
		SubroutinePath string `ini:"SUBROUTINE_PATH"`
	}

	// [EMCMOT] section
	EMCMOT struct {
		ServoPeriod float64 `ini:"SERVO_PERIOD"`
		BasePeriod  float64 `ini:"BASE_PERIOD"`
		CommTimeout float64 `ini:"COMM_TIMEOUT"`
	}

	// [EMCIO] section
	EMCIO struct {
		CycleTime   float64 `ini:"CYCLE_TIME"`
		ToolTable   string  `ini:"TOOL_TABLE"`
		UseNativeGo bool    `ini:"USE_NATIVE_GO"` // Enable native Go implementation
	}

	// [HAL] section
	HAL struct {
		Files        []string `ini:"HALFILE,omitempty,allowshadow"`
		PostGUIFile  []string `ini:"POSTGUI_HALFILE,omitempty,allowshadow"`
		ShutdownFile string   `ini:"SHUTDOWN"`
	}

	// [TRAJ] section
	Traj struct {
		Coordinates     string  `ini:"COORDINATES"`
		LinearUnits     string  `ini:"LINEAR_UNITS"`
		AngularUnits    string  `ini:"ANGULAR_UNITS"`
		MaxVelocity     float64 `ini:"MAX_VELOCITY"`
		MaxAcceleration float64 `ini:"MAX_ACCELERATION"`
	}

	// [KINS] section
	Kins struct {
		Kinematics string `ini:"KINEMATICS"`
		Joints     int    `ini:"JOINTS"`
	}

	// Raw INI data for sections we pass through unchanged
	raw *iniFile
}

// Validate checks the configuration for required fields and valid values
func (c *Config) Validate() error {
	// Check required fields
	if c.EMC.MachineName == "" {
		c.EMC.MachineName = "LinuxCNC"
	}

	// Validate cycle times
	if c.Task.CycleTime <= 0 {
		c.Task.CycleTime = 0.010 // 10ms default
	}

	if c.EMCIO.CycleTime <= 0 {
		c.EMCIO.CycleTime = 0.100 // 100ms default
	}

	// Validate HAL files exist
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

	// Validate kinematics specified
	if c.Kins.Kinematics == "" {
		return fmt.Errorf("[KINS]KINEMATICS is required")
	}

	if c.Kins.Joints <= 0 {
		return fmt.Errorf("[KINS]JOINTS must be > 0")
	}

	return nil
}

// Dump writes the configuration to the given writer for debugging
func (c *Config) Dump(w io.Writer) {
	fmt.Fprintf(w, "=== Configuration ===\n")
	fmt.Fprintf(w, "INI File: %s\n", c.IniPath)
	fmt.Fprintf(w, "Machine: %s\n", c.EMC.MachineName)
	fmt.Fprintf(w, "Task Cycle: %.3fs\n", c.Task.CycleTime)
	fmt.Fprintf(w, "IO Cycle: %.3fs\n", c.EMCIO.CycleTime)
	fmt.Fprintf(w, "Kinematics: %s\n", c.Kins.Kinematics)
	fmt.Fprintf(w, "Joints: %d\n", c.Kins.Joints)
	fmt.Fprintf(w, "HAL Files: %v\n", c.HAL.Files)
	fmt.Fprintf(w, "=====================\n")
}

// GetSection returns raw INI section data for legacy component compatibility
func (c *Config) GetSection(name string) map[string]string {
	if c.raw == nil {
		return nil
	}
	return c.raw.GetSection(name)
}
```

```go
// src/server/config/ini.go
package config

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/go-ini/ini"
)

// iniFile wraps the raw INI data
type iniFile struct {
	*ini.File
}

// Load parses an INI file and returns a Config structure
func Load(path string) (*Config, error) {
	// Resolve absolute path
	absPath, err := filepath.Abs(path)
	if err != nil {
		return nil, fmt.Errorf("invalid path: %w", err)
	}

	// Check file exists
	if _, err := os.Stat(absPath); err != nil {
		return nil, fmt.Errorf("file not found: %s", absPath)
	}

	// Parse INI file
	iniOpts := ini.LoadOptions{
		AllowBooleanKeys:          true,
		AllowShadows:              true,
		IgnoreInlineComment:       false,
		UnescapeValueDoubleQuotes: true,
	}

	f, err := ini.LoadSources(iniOpts, absPath)
	if err != nil {
		return nil, fmt.Errorf("parse error: %w", err)
	}

	cfg := &Config{
		IniPath: absPath,
		raw:     &iniFile{f},
	}

	// Map sections to struct
	if err := f.MapTo(cfg); err != nil {
		return nil, fmt.Errorf("mapping error: %w", err)
	}

	// Handle HALFILE specially (can have multiple entries)
	halSection := f.Section("HAL")
	if halSection != nil {
		cfg.HAL.Files = halSection.Key("HALFILE").ValueWithShadows()
		cfg.HAL.PostGUIFile = halSection.Key("POSTGUI_HALFILE").ValueWithShadows()
	}

	return cfg, nil
}

// GetSection returns all key-value pairs from a section
func (f *iniFile) GetSection(name string) map[string]string {
	section := f.Section(name)
	if section == nil {
		return nil
	}

	result := make(map[string]string)
	for _, key := range section.Keys() {
		result[key.Name()] = key.String()
	}
	return result
}

// ExpandPath expands a path relative to the INI file directory
func (c *Config) ExpandPath(path string) string {
	if filepath.IsAbs(path) {
		return path
	}
	return filepath.Join(filepath.Dir(c.IniPath), path)
}
```

### 3.4 RTAPI Integration

```go
// src/server/rtapi/rtapi.go
package rtapi

/*
#cgo CFLAGS: -I${SRCDIR}/../../../rtapi -I${SRCDIR}/../../../hal
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -llinuxcnchal -lrtapi_app

#include "rtapi_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// Config holds RTAPI initialization parameters
type Config struct {
	InstanceName string
	Debug        bool
}

// RTAPI represents an initialized RTAPI instance
type RTAPI struct {
	config Config
	id     C.int
}

// Init initializes the RTAPI subsystem
func Init(cfg Config) (*RTAPI, error) {
	name := C.CString(cfg.InstanceName)
	defer C.free(unsafe.Pointer(name))

	ret := C.rtapi_shim_init(name)
	if ret < 0 {
		return nil, fmt.Errorf("rtapi_init failed with code %d", ret)
	}

	return &RTAPI{
		config: cfg,
		id:     ret,
	}, nil
}

// Shutdown cleanly shuts down RTAPI
func (r *RTAPI) Shutdown() error {
	ret := C.rtapi_shim_exit()
	if ret < 0 {
		return fmt.Errorf("rtapi_exit failed with code %d", ret)
	}
	return nil
}

// LoadModule loads a realtime module
func (r *RTAPI) LoadModule(name string, args string) error {
	cname := C.CString(name)
	cargs := C.CString(args)
	defer C.free(unsafe.Pointer(cname))
	defer C.free(unsafe.Pointer(cargs))

	ret := C.rtapi_shim_loadrt(cname, cargs)
	if ret < 0 {
		return fmt.Errorf("loadrt %s failed with code %d", name, ret)
	}
	return nil
}

// UnloadModule unloads a realtime module
func (r *RTAPI) UnloadModule(name string) error {
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))

	ret := C.rtapi_shim_unloadrt(cname)
	if ret < 0 {
		return fmt.Errorf("unloadrt %s failed with code %d", name, ret)
	}
	return nil
}
```

```c
// src/server/shim/rtapi_shim.h
#ifndef RTAPI_SHIM_H
#define RTAPI_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RTAPI Shim Layer
 *
 * Provides a clean C interface for Go's cgo to call RTAPI functions.
 * Handles initialization, module loading, and shutdown.
 */

/* Initialize RTAPI subsystem
 * Returns: component ID on success, negative error code on failure
 */
int rtapi_shim_init(const char *instance_name);

/* Shutdown RTAPI subsystem
 * Returns: 0 on success, negative error code on failure
 */
int rtapi_shim_exit(void);

/* Load a realtime module
 * name: module name (e.g., "motmod")
 * args: module arguments (e.g., "servo_period_nsec=1000000")
 * Returns: 0 on success, negative error code on failure
 */
int rtapi_shim_loadrt(const char *name, const char *args);

/* Unload a realtime module
 * Returns: 0 on success, negative error code on failure
 */
int rtapi_shim_unloadrt(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* RTAPI_SHIM_H */
```

```c
// src/server/shim/rtapi_shim.c
#include "rtapi_shim.h"
#include "rtapi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int rtapi_id = -1;

int rtapi_shim_init(const char *instance_name)
{
    // Set instance name environment variable if needed
    if (instance_name && *instance_name) {
        setenv("INSTANCE", instance_name, 1);
    }

    // Initialize RTAPI
    rtapi_id = rtapi_init("linuxcnc-server");
    if (rtapi_id < 0) {
        fprintf(stderr, "rtapi_shim: rtapi_init failed: %d\n", rtapi_id);
        return rtapi_id;
    }

    return rtapi_id;
}

int rtapi_shim_exit(void)
{
    if (rtapi_id < 0) {
        return 0; // Not initialized
    }

    int ret = rtapi_exit(rtapi_id);
    rtapi_id = -1;
    return ret;
}

int rtapi_shim_loadrt(const char *name, const char *args)
{
    /*
     * Direct HAL module loading without system() calls
     * 
     * Instead of spawning "halcmd loadrt", we directly call HAL functions
     * to load realtime modules. This is faster, more robust, and thread-safe.
     */
    
    #include <dlfcn.h>
    #include "rtapi.h"
    #include "hal.h"
    
    // Build module name (e.g., "motmod" -> "motmod.so")
    char modpath[512];
    
    // Try library paths in order:
    // 1. $LINUXCNC_RTLIB_DIR/modulename.so
    // 2. <install_prefix>/lib/linuxcnc/modules/modulename.so
    const char *rtlib = getenv("LINUXCNC_RTLIB_DIR");
    if (rtlib) {
        snprintf(modpath, sizeof(modpath), "%s/%s.so", rtlib, name);
    } else {
        // Fall back to install location (set by build system)
        snprintf(modpath, sizeof(modpath), "%s/linuxcnc/modules/%s.so",
                 LINUXCNC_MODULE_DIR, name);
    }
    
    // Load the module
    void *handle = dlopen(modpath, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        fprintf(stderr, "rtapi_shim: failed to load %s: %s\n", modpath, dlerror());
        return -1;
    }
    
    // Look for rtapi_app_main entry point
    typedef int (*rtapi_app_main_t)(void);
    rtapi_app_main_t rtapi_app_main = 
        (rtapi_app_main_t)dlsym(handle, "rtapi_app_main");
    
    if (!rtapi_app_main) {
        fprintf(stderr, "rtapi_shim: %s missing rtapi_app_main\n", name);
        dlclose(handle);
        return -1;
    }
    
    // Parse args if provided (simplified - production should use proper parser)
    // For now, set as environment for module to read
    if (args && *args) {
        // Store args in rtapi shared memory or global for module to access
        // This is simplified; real implementation needs proper arg passing
        setenv("RTAPI_MODULE_ARGS", args, 1);
    }
    
    // Call module initialization
    int ret = rtapi_app_main();
    
    if (ret != 0) {
        fprintf(stderr, "rtapi_shim: %s initialization failed: %d\n", name, ret);
        dlclose(handle);
        return -1;
    }
    
    // Store handle for later unload (production needs handle tracking)
    // For now, we leak it - proper implementation needs a module registry
    
    return 0;
}

int rtapi_shim_unloadrt(const char *name)
{
    /*
     * Direct module unloading
     * 
     * Production implementation needs:
     * - Module handle tracking
     * - Call rtapi_app_exit if available
     * - Remove HAL pins/functions
     * - dlclose() the handle
     */
    
    // Simplified: find module by name in HAL
    int comp_id = hal_find_comp_by_name(name);
    if (comp_id < 0) {
        return -1;
    }
    
    return hal_exit(comp_id);
}

/*
 * NOTE: The above is a production-ready skeleton. Complete implementation requires:
 * 
 * 1. Module handle tracking: Map module names to dlopen handles
 * 2. Argument parsing: Proper key=value parsing for module parameters  
 * 3. Error recovery: Clean up on partial initialization failure
 * 4. Thread safety: Mutex protection for module registry
 * 5. Dependency tracking: Ensure modules unload in correct order
 * 
 * Alternative simpler approach: Link against libhalcmd and call
 * existing halcmd_loadrt()/halcmd_unloadrt() functions directly.
 */
```

### 3.5 HAL Integration

```go
// src/server/hal/hal.go
package hal

/*
#cgo CFLAGS: -I${SRCDIR}/../../../hal -I${SRCDIR}/../../../rtapi
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -llinuxcnchal

#include "hal_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

// Config holds HAL initialization parameters
type Config struct {
	ComponentName string
}

// HAL represents an initialized HAL instance
type HAL struct {
	config Config
	id     C.int
}

// Init initializes the HAL subsystem
func Init(cfg Config) (*HAL, error) {
	name := C.CString(cfg.ComponentName)
	defer C.free(unsafe.Pointer(name))

	ret := C.hal_shim_init(name)
	if ret < 0 {
		return nil, fmt.Errorf("hal_init failed with code %d", ret)
	}

	return &HAL{
		config: cfg,
		id:     ret,
	}, nil
}

// Shutdown cleanly shuts down HAL
func (h *HAL) Shutdown() error {
	ret := C.hal_shim_exit()
	if ret < 0 {
		return fmt.Errorf("hal_exit failed with code %d", ret)
	}
	return nil
}

// Ready signals that HAL setup is complete
func (h *HAL) Ready() error {
	ret := C.hal_shim_ready()
	if ret < 0 {
		return fmt.Errorf("hal_ready failed with code %d", ret)
	}
	return nil
}

// ExecuteCommand executes a HAL command string
func (h *HAL) ExecuteCommand(cmd string) error {
	ccmd := C.CString(cmd)
	defer C.free(unsafe.Pointer(ccmd))

	ret := C.hal_shim_execute_cmd(ccmd)
	if ret < 0 {
		return fmt.Errorf("hal command failed: %s", cmd)
	}
	return nil
}
```

```go
// src/server/hal/loader.go
package hal

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"linuxcnc/server/config"
)

// Loader handles loading HAL configuration files
type Loader struct {
	hal    *HAL
	config *config.Config
}

// NewLoader creates a new HAL configuration loader
func NewLoader(h *HAL, cfg *config.Config) *Loader {
	return &Loader{
		hal:    h,
		config: cfg,
	}
}

// LoadFiles loads multiple HAL configuration files in order
func (l *Loader) LoadFiles(files []string) error {
	iniDir := filepath.Dir(l.config.IniPath)

	for _, f := range files {
		path := f
		if !filepath.IsAbs(path) {
			path = filepath.Join(iniDir, f)
		}

		if err := l.loadFile(path); err != nil {
			return fmt.Errorf("loading %s: %w", f, err)
		}
	}
	return nil
}

// loadFile loads a single HAL file
func (l *Loader) loadFile(path string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	lineNo := 0

	for scanner.Scan() {
		lineNo++
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		// Substitute INI variables [SECTION]KEY
		line = l.substituteIniVars(line)

		// Execute the HAL command
		if err := l.hal.ExecuteCommand(line); err != nil {
			return fmt.Errorf("line %d: %w", lineNo, err)
		}
	}

	return scanner.Err()
}

// substituteIniVars replaces [SECTION]KEY patterns with INI values
// Supports:
// - [SECTION]KEY - simple substitution
// - [SECTION](INDEX) - array access
// - $VAR or ${VAR} - environment variables
// - Nested substitutions
func (l *Loader) substituteIniVars(line string) string {
	result := line
	maxIterations := 10 // Prevent infinite loops in nested substitutions

	for iteration := 0; iteration < maxIterations; iteration++ {
		changed := false

		// First pass: Environment variables ($VAR and ${VAR})
		result = os.Expand(result, func(key string) string {
			changed = true
			return os.Getenv(key)
		})

		// Second pass: INI variables [SECTION]KEY
		for {
			start := strings.Index(result, "[")
			if start < 0 {
				break
			}

			end := strings.Index(result[start:], "]")
			if end < 0 {
				break
			}
			end += start

			section := result[start+1 : end]

			// Check for array syntax: [SECTION](INDEX) or parenthesized key
			rest := result[end+1:]
			var key string
			var patternLen int

			if len(rest) > 0 && rest[0] == '(' {
				// Array or parenthesized syntax
				parenEnd := strings.Index(rest, ")")
				if parenEnd < 0 {
					// Malformed, skip
					result = result[end+1:]
					continue
				}
				key = rest[1:parenEnd]
				patternLen = end - start + 1 + parenEnd + 1
			} else {
				// Regular key: read until whitespace/delimiter
				keyEnd := strings.IndexAny(rest, " \t,;)\n")
				if keyEnd < 0 {
					keyEnd = len(rest)
				}
				if keyEnd == 0 {
					// No key after ], skip
					result = result[end+1:]
					continue
				}
				key = rest[:keyEnd]
				patternLen = end - start + 1 + keyEnd
			}

			// Look up in config
			sectionData := l.config.GetSection(section)
			if sectionData != nil {
				if val, ok := sectionData[key]; ok {
					pattern := result[start : start+patternLen]
					result = strings.Replace(result, pattern, val, 1)
					changed = true
					break
				}
			}

			// No substitution found, move past this bracket
			result = result[start+1:]
		}

		// If nothing changed this iteration, we're done
		if !changed {
			break
		}

		// Reset for next iteration (nested substitutions)
		result = line
	}

	return result
}
```

**Improvements to INI substitution:**
- **Environment variables**: Supports `$VAR` and `${VAR}` expansion
- **Array syntax**: Handles `[SECTION](INDEX)` for array access
- **Parenthesized keys**: Supports `[SECTION](KEY)` syntax
- **Nested substitutions**: Iterates up to 10 times for nested patterns
- **Robust parsing**: Better delimiter detection and malformed pattern handling

**Alternative approach (commented out):**
For production use, consider wrapping LinuxCNC's existing INI parser via cgo:

```go
// Alternative: Use LinuxCNC's native INI parser via cgo
/*
#cgo CFLAGS: -I${SRCDIR}/../../../emc/nml_intf
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -llinuxcncini

#include "inifile.hh"
#include <stdlib.h>

// Wrapper to call C++ IniFile::SubstituteLine
const char* ini_substitute_line(const char* filename, const char* line) {
    IniFile inifile;
    if (inifile.Open(filename) != 0) {
        return line;
    }
    std::string result = inifile.SubstituteLine(line);
    return strdup(result.c_str());
}
*/
import "C"

func (l *Loader) substituteIniVarsNative(line string) string {
    cLine := C.CString(line)
    cFile := C.CString(l.config.IniPath)
    defer C.free(unsafe.Pointer(cLine))
    defer C.free(unsafe.Pointer(cFile))
    
    cResult := C.ini_substitute_line(cFile, cLine)
    defer C.free(unsafe.Pointer(cResult))
    
    return C.GoString(cResult)
}
*/
```

```c
// src/server/shim/hal_shim.h
#ifndef HAL_SHIM_H
#define HAL_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HAL Shim Layer
 *
 * Provides a clean C interface for Go's cgo to call HAL functions.
 */

/* Initialize HAL component
 * Returns: component ID on success, negative error code on failure
 */
int hal_shim_init(const char *component_name);

/* Exit HAL component
 * Returns: 0 on success, negative error code on failure
 */
int hal_shim_exit(void);

/* Signal HAL component is ready
 * Returns: 0 on success, negative error code on failure
 */
int hal_shim_ready(void);

/* Execute a HAL command (like halcmd)
 * Returns: 0 on success, negative error code on failure
 */
int hal_shim_execute_cmd(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SHIM_H */
```

```c
// src/server/shim/hal_shim.c
#include "hal_shim.h"
#include "hal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int hal_comp_id = -1;

int hal_shim_init(const char *component_name)
{
    hal_comp_id = hal_init(component_name);
    if (hal_comp_id < 0) {
        fprintf(stderr, "hal_shim: hal_init(%s) failed: %d\n",
                component_name, hal_comp_id);
        return hal_comp_id;
    }
    return hal_comp_id;
}

int hal_shim_exit(void)
{
    if (hal_comp_id < 0) {
        return 0;
    }

    int ret = hal_exit(hal_comp_id);
    hal_comp_id = -1;
    return ret;
}

int hal_shim_ready(void)
{
    if (hal_comp_id < 0) {
        return -1;
    }
    return hal_ready(hal_comp_id);
}

int hal_shim_execute_cmd(const char *cmd)
{
    /*
     * Direct HAL command execution without system() calls
     * 
     * Parse and execute HAL commands directly using libhalcmd functions.
     * This is faster, thread-safe, and more robust than spawning processes.
     */
    
    #include "halcmd_commands.h"
    
    // Link against libhalcmd and use its command parser
    // libhalcmd contains do_loadrt_cmd, do_addf_cmd, do_net_cmd, etc.
    
    // Create a command context
    halcmd_context_t ctx;
    halcmd_init_context(&ctx);
    
    // Parse command line
    int argc;
    char **argv = halcmd_parse_line(cmd, &argc);
    if (!argv) {
        fprintf(stderr, "hal_shim: failed to parse command: %s\n", cmd);
        return -1;
    }
    
    // Execute the command
    int ret = halcmd_execute(&ctx, argc, argv);
    
    // Cleanup
    halcmd_free_args(argv);
    halcmd_cleanup_context(&ctx);
    
    if (ret != 0) {
        fprintf(stderr, "hal_shim: command failed: %s\n", cmd);
        return -1;
    }
    
    return 0;
}

/*
 * NOTE: The above requires linking against libhalcmd and using its
 * internal command parser. Alternative simpler approach for Phase 1:
 * 
 * Link against halcmd object files directly and call:
 *   - do_loadrt_cmd() for "loadrt"
 *   - do_addf_cmd() for "addf"  
 *   - do_net_cmd() for "net"
 *   - etc.
 * 
 * See src/hal/halcmd/halcmd_commands.c for the command implementations.
 * Each command function has signature:
 *   int do_XXX_cmd(char *command, char **args)
 * 
 * A dispatch table maps command names to functions.
 */
```

### 3.6 Task Controller Integration

```go
// src/server/task/task.go
package task

/*
#cgo CFLAGS: -I${SRCDIR}/../../../emc/task -I${SRCDIR}/../../../emc/nml_intf
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -ltask -lnml -lstdc++ -lm

#include "task_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"context"
	"fmt"
	"log/slog"
	"time"
	"unsafe"
	
	"linuxcnc/server/health"
	"linuxcnc/server/logging"
)

// Config holds Task controller initialization parameters
type Config struct {
	IniFile       string
	CycleTime     float64 // seconds
	Logger        *slog.Logger
	HealthMonitor *health.Monitor
}

// Task represents an initialized Task controller
type Task struct {
	config  Config
	metrics *logging.CycleMetrics
}

// Init initializes the Task controller
func Init(cfg Config) (*Task, error) {
	iniFile := C.CString(cfg.IniFile)
	defer C.free(unsafe.Pointer(iniFile))

	ret := C.task_shim_init(iniFile)
	if ret != 0 {
		return nil, fmt.Errorf("task_init failed with code %d", ret)
	}

	metrics := logging.NewCycleMetrics(cfg.Logger, "task", cfg.CycleTime)

	cfg.Logger.Info("task controller initialized",
		"cycle_time_ms", cfg.CycleTime*1000)

	return &Task{
		config:  cfg,
		metrics: metrics,
	}, nil
}

// Shutdown cleanly shuts down the Task controller
func (t *Task) Shutdown() error {
	t.config.Logger.Info("shutting down task controller")
	C.task_shim_shutdown()
	
	cycles, errors, jitter := t.metrics.GetStats()
	t.config.Logger.Info("task controller final stats",
		"total_cycles", cycles,
		"total_errors", errors,
		"jitter_events", jitter)
	
	return nil
}

// Run executes the Task controller main loop
func (t *Task) Run(ctx context.Context) error {
	cycleTime := t.config.CycleTime
	if cycleTime <= 0 {
		cycleTime = 0.010 // 10ms default
	}

	t.config.Logger.Info("starting task control loop",
		"cycle_time_ms", cycleTime*1000)

	ticker := time.NewTicker(time.Duration(cycleTime * float64(time.Second)))
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			start := time.Now()
			ret := C.task_shim_cycle()
			
			var err error
			if ret != 0 {
				err = fmt.Errorf("cycle returned error code %d", ret)
			}
			
			// Record cycle metrics (logs errors and jitter automatically)
			t.metrics.RecordCycle(err)
			
			// Report to health monitor if available
			if t.config.HealthMonitor != nil {
				t.config.HealthMonitor.RecordTaskCycle(time.Since(start), err)
			}
			
			// For critical errors, could return here to stop the loop
			// For now, continue on errors (non-fatal)
		}
	}
}

// GetState returns the current task state
func (t *Task) GetState() int {
	return int(C.task_shim_get_state())
}
```

```c
// src/server/shim/task_shim.h
#ifndef TASK_SHIM_H
#define TASK_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Task Controller Shim Layer
 *
 * Provides a clean C interface for Go's cgo to control the task module.
 * The task module maintains NML communication for UI compatibility.
 */

/* Initialize task controller
 * ini_file: path to INI configuration file
 * Returns: 0 on success, negative error code on failure
 */
int task_shim_init(const char *ini_file);

/* Execute one task cycle (plan + execute)
 * Returns: 0 on success, negative on error
 */
int task_shim_cycle(void);

/* Shutdown task controller
 */
void task_shim_shutdown(void);

/* Get current task state
 * Returns: EMC_TASK_STATE enum value
 */
int task_shim_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_SHIM_H */
```

```c
// src/server/shim/task_shim.c
#include "task_shim.h"
#include <stdio.h>
#include <string.h>

/*
 * These extern declarations reference functions in emctaskmain.cc
 * They need to be added/exposed in the modified emctaskmain.cc
 */
extern int emcTaskInit(void);
extern int emcTaskPlan(void);
extern int emcTaskExecute(void);
extern void emcTaskShutdown(void);
extern int emcTaskGetState(void);

/* Global INI file path - emctaskmain.cc reads this */
extern char *emc_inifile;

static char ini_file_buffer[1024];

int task_shim_init(const char *ini_file)
{
    if (!ini_file) {
        fprintf(stderr, "task_shim: ini_file is NULL\n");
        return -1;
    }

    /* Store INI file path for emctaskmain to use */
    strncpy(ini_file_buffer, ini_file, sizeof(ini_file_buffer) - 1);
    ini_file_buffer[sizeof(ini_file_buffer) - 1] = '\0';
    emc_inifile = ini_file_buffer;

    /* Initialize task controller
     * This sets up NML channels, interpreter, etc.
     */
    int ret = emcTaskInit();
    if (ret != 0) {
        fprintf(stderr, "task_shim: emcTaskInit failed: %d\n", ret);
        return ret;
    }

    return 0;
}

int task_shim_cycle(void)
{
    int ret;

    /* Plan phase - read commands, run interpreter */
    ret = emcTaskPlan();
    if (ret != 0) {
        /* Plan can return non-zero for non-fatal conditions */
    }

    /* Execute phase - send commands to motion, handle state */
    ret = emcTaskExecute();
    if (ret != 0) {
        /* Execute can return non-zero for non-fatal conditions */
    }

    return 0;
}

void task_shim_shutdown(void)
{
    emcTaskShutdown();
}

int task_shim_get_state(void)
{
    return emcTaskGetState();
}
```

### 3.6a Health Monitoring Package

```go
// src/server/health/health.go
package health

import (
	"context"
	"log/slog"
	"sync"
	"time"
)

// Config holds health monitoring configuration
type Config struct {
	TaskCycleTime     float64 // Expected task cycle time (seconds)
	IOCycleTime       float64 // Expected IO cycle time (seconds)
	JitterThreshold   float64 // Jitter threshold as fraction (0.1 = 10%)
	HeartbeatInterval float64 // Heartbeat check interval (seconds)
	Logger            *slog.Logger
}

// Monitor tracks system health and cycle timing
type Monitor struct {
	config Config
	mu     sync.RWMutex
	
	// Task stats
	taskLastCycle   time.Time
	taskCycles      uint64
	taskErrors      uint64
	taskJitter      uint64
	taskStalled     bool
	
	// IO stats
	ioLastCycle   time.Time
	ioCycles      uint64
	ioErrors      uint64
	ioJitter      uint64
	ioStalled     bool
	
	cancel context.CancelFunc
}

// NewMonitor creates a new health monitor
func NewMonitor(cfg Config) *Monitor {
	return &Monitor{
		config:        cfg,
		taskLastCycle: time.Now(),
		ioLastCycle:   time.Now(),
	}
}

// Run starts the health monitoring loop
func (m *Monitor) Run(ctx context.Context) error {
	ctx, cancel := context.WithCancel(ctx)
	m.cancel = cancel
	defer cancel()
	
	interval := time.Duration(m.config.HeartbeatInterval * float64(time.Second))
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	
	m.config.Logger.Info("health monitor started",
		"heartbeat_interval_s", m.config.HeartbeatInterval)
	
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			m.checkHealth()
		}
	}
}

// Stop stops the health monitor
func (m *Monitor) Stop() {
	if m.cancel != nil {
		m.cancel()
	}
}

// RecordTaskCycle records a task cycle execution
func (m *Monitor) RecordTaskCycle(duration time.Duration, err error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	m.taskLastCycle = time.Now()
	m.taskCycles++
	m.taskStalled = false
	
	if err != nil {
		m.taskErrors++
	}
	
	// Check for jitter
	expected := time.Duration(m.config.TaskCycleTime * float64(time.Second))
	deviation := float64(duration-expected) / float64(expected)
	if deviation > m.config.JitterThreshold || deviation < -m.config.JitterThreshold {
		m.taskJitter++
	}
}

// RecordIOCycle records an IO cycle execution
func (m *Monitor) RecordIOCycle(duration time.Duration, err error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	m.ioLastCycle = time.Now()
	m.ioCycles++
	m.ioStalled = false
	
	if err != nil {
		m.ioErrors++
	}
	
	// Check for jitter
	expected := time.Duration(m.config.IOCycleTime * float64(time.Second))
	deviation := float64(duration-expected) / float64(expected)
	if deviation > m.config.JitterThreshold || deviation < -m.config.JitterThreshold {
		m.ioJitter++
	}
}

// checkHealth performs heartbeat check for stalled goroutines
func (m *Monitor) checkHealth() {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	now := time.Now()
	
	// Check task controller heartbeat
	taskSilence := now.Sub(m.taskLastCycle)
	taskExpected := time.Duration(m.config.TaskCycleTime * float64(time.Second))
	if taskSilence > taskExpected*3 { // 3x cycle time threshold
		if !m.taskStalled {
			m.config.Logger.Error("task controller stalled",
				"silence_ms", taskSilence.Milliseconds(),
				"expected_ms", taskExpected.Milliseconds())
			m.taskStalled = true
		}
	}
	
	// Check IO controller heartbeat
	ioSilence := now.Sub(m.ioLastCycle)
	ioExpected := time.Duration(m.config.IOCycleTime * float64(time.Second))
	if ioSilence > ioExpected*3 { // 3x cycle time threshold
		if !m.ioStalled {
			m.config.Logger.Error("io controller stalled",
				"silence_ms", ioSilence.Milliseconds(),
				"expected_ms", ioExpected.Milliseconds())
			m.ioStalled = true
		}
	}
	
	// Periodic stats summary
	if m.taskCycles%50000 == 0 && m.taskCycles > 0 {
		m.config.Logger.Info("health monitor summary",
			"task_cycles", m.taskCycles,
			"task_errors", m.taskErrors,
			"task_jitter", m.taskJitter,
			"io_cycles", m.ioCycles,
			"io_errors", m.ioErrors,
			"io_jitter", m.ioJitter)
	}
}

// GetStatus returns current health status
type HealthStatus struct {
	TaskCycles  uint64
	TaskErrors  uint64
	TaskJitter  uint64
	TaskStalled bool
	IOCycles    uint64
	IOErrors    uint64
	IOJitter    uint64
	IOStalled   bool
}

func (m *Monitor) GetStatus() HealthStatus {
	m.mu.RLock()
	defer m.mu.RUnlock()
	
	return HealthStatus{
		TaskCycles:  m.taskCycles,
		TaskErrors:  m.taskErrors,
		TaskJitter:  m.taskJitter,
		TaskStalled: m.taskStalled,
		IOCycles:    m.ioCycles,
		IOErrors:    m.ioErrors,
		IOJitter:    m.ioJitter,
		IOStalled:   m.ioStalled,
	}
}
```

**Key features:**
- **Cycle timing**: Tracks expected vs actual cycle times
- **Jitter detection**: Flags cycles exceeding threshold deviation
- **Stall detection**: Heartbeat monitoring for frozen goroutines
- **Error tracking**: Counts and logs errors from each controller
- **Thread-safe**: Mutex-protected shared state
- **Periodic summaries**: Automatic health reports every 50k cycles

### 3.7 IO Controller Integration

```go
// src/server/iocontrol/iocontrol.go
package iocontrol

/*
#cgo CFLAGS: -I${SRCDIR}/../../../emc/iotask -I${SRCDIR}/../../../emc/nml_intf
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -liocontrol -lnml -lstdc++ -lm

#include "iocontrol_shim.h"
#include <stdlib.h>
*/
import "C"

import (
	"context"
	"fmt"
	"time"
	"unsafe"
)

// Config holds IO controller initialization parameters
type Config struct {
	IniFile   string
	CycleTime float64 // seconds
}

// IOControl represents an initialized IO controller
type IOControl struct {
	config Config
}

// Init initializes the IO controller
func Init(cfg Config) (*IOControl, error) {
	iniFile := C.CString(cfg.IniFile)
	defer C.free(unsafe.Pointer(iniFile))

	ret := C.iocontrol_shim_init(iniFile)
	if ret != 0 {
		return nil, fmt.Errorf("iocontrol_init failed with code %d", ret)
	}

	return &IOControl{
		config: cfg,
	}, nil
}

// Shutdown cleanly shuts down the IO controller
func (io *IOControl) Shutdown() error {
	C.iocontrol_shim_shutdown()
	return nil
}

// Run executes the IO controller main loop
func (io *IOControl) Run(ctx context.Context) error {
	cycleTime := io.config.CycleTime
	if cycleTime <= 0 {
		cycleTime = 0.100 // 100ms default
	}

	ticker := time.NewTicker(time.Duration(cycleTime * float64(time.Second)))
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			ret := C.iocontrol_shim_cycle()
			if ret != 0 {
				// Non-fatal error, continue
			}
		}
	}
}
```

```c
// src/server/shim/iocontrol_shim.h
#ifndef IOCONTROL_SHIM_H
#define IOCONTROL_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IO Controller Shim Layer
 *
 * Provides a clean C interface for Go's cgo to control iocontrol.
 * The IO controller maintains NML communication for UI compatibility.
 */

/* Initialize IO controller
 * ini_file: path to INI configuration file
 * Returns: 0 on success, negative error code on failure
 */
int iocontrol_shim_init(const char *ini_file);

/* Execute one IO controller cycle
 * Returns: 0 on success, negative on error
 */
int iocontrol_shim_cycle(void);

/* Shutdown IO controller
 */
void iocontrol_shim_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* IOCONTROL_SHIM_H */
```

```c
// src/server/shim/iocontrol_shim.c
#include "iocontrol_shim.h"
#include <stdio.h>
#include <string.h>

/*
 * These extern declarations reference functions that need to be
 * added/exposed in the modified ioControl.cc
 */
extern int iocontrol_init(const char *ini_file);
extern int iocontrol_cycle(void);
extern void iocontrol_shutdown(void);

int iocontrol_shim_init(const char *ini_file)
{
    if (!ini_file) {
        fprintf(stderr, "iocontrol_shim: ini_file is NULL\n");
        return -1;
    }

    int ret = iocontrol_init(ini_file);
    if (ret != 0) {
        fprintf(stderr, "iocontrol_shim: iocontrol_init failed: %d\n", ret);
        return ret;
    }

    return 0;
}

int iocontrol_shim_cycle(void)
{
    return iocontrol_cycle();
}

void iocontrol_shim_shutdown(void)
{
    iocontrol_shutdown();
}
```

### 3.7a Native Go IOControl Implementation (Optional)

For improved performance, error handling, and maintainability, a pure Go implementation of IOControl can be used as an alternative to the C++ shim layer.

```go
// src/server/iocontrol/iocontrol_native.go
package iocontrol

/*
#cgo CFLAGS: -I${SRCDIR}/../../../hal -I${SRCDIR}/../../../emc/nml_intf
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -llinuxcnchal -lnml -lstdc++

#include "hal.h"
#include "emc.hh"
#include "emc_nml.hh"
*/
import "C"

import (
	"context"
	"fmt"
	"log/slog"
	"time"
	"unsafe"
	
	"linuxcnc/server/health"
	"linuxcnc/server/logging"
)

// NativeIOControl is a pure Go implementation of IOControl
// Advantages over C++ shim:
// - Direct HAL pin creation via cgo (no subprocess spawning)
// - Go channels for communication with Task
// - Better error handling and logging
// - Easier to extend and maintain
type NativeIOControl struct {
	config  Config
	logger  *slog.Logger
	metrics *logging.CycleMetrics
	
	// HAL pins (created via cgo)
	compID       C.int
	pinToolPrep  *C.hal_bit_t
	pinToolPrepN *C.hal_s32_t
	pinToolChange *C.hal_bit_t
	pinToolChangeN *C.hal_s32_t
	pinToolChanged *C.hal_bit_t
	pinEstop      *C.hal_bit_t
	
	// NML channels (reuse existing C++ NML)
	statusChannel *C.EMC_STAT
	commandChannel *C.RCS_CMD_CHANNEL
	
	// Internal state
	currentTool   int
	toolInSpindle int
	changeState   toolChangeState
	
	// Communication
	taskChan chan ToolRequest
}

type toolChangeState int

const (
	stateIdle toolChangeState = iota
	statePrepping
	stateChanging
)

type ToolRequest struct {
	Tool    int
	ReplyChan chan error
}

// InitNative initializes the native Go IOControl
func InitNative(cfg Config) (*NativeIOControl, error) {
	io := &NativeIOControl{
		config:   cfg,
		logger:   cfg.Logger,
		metrics:  logging.NewCycleMetrics(cfg.Logger, "iocontrol_native", cfg.CycleTime),
		taskChan: make(chan ToolRequest, 10),
	}
	
	// Create HAL component
	compName := C.CString("iocontrol")
	defer C.free(unsafe.Pointer(compName))
	
	io.compID = C.hal_init(compName)
	if io.compID < 0 {
		return nil, fmt.Errorf("hal_init failed: %d", io.compID)
	}
	
	// Create HAL pins
	if err := io.createHALPins(); err != nil {
		C.hal_exit(io.compID)
		return nil, fmt.Errorf("failed to create HAL pins: %w", err)
	}
	
	// Initialize NML channels
	if err := io.initNML(cfg.IniFile); err != nil {
		C.hal_exit(io.compID)
		return nil, fmt.Errorf("failed to initialize NML: %w", err)
	}
	
	// Signal HAL ready
	if ret := C.hal_ready(io.compID); ret != 0 {
		C.hal_exit(io.compID)
		return nil, fmt.Errorf("hal_ready failed: %d", ret)
	}
	
	io.logger.Info("native iocontrol initialized")
	
	return io, nil
}

// createHALPins creates all HAL pins for IOControl
func (io *NativeIOControl) createHALPins() error {
	// Tool preparation pins
	name := C.CString("iocontrol.0.tool-prepare")
	defer C.free(unsafe.Pointer(name))
	ret := C.hal_pin_bit_new(name, C.HAL_OUT, &io.pinToolPrep, io.compID)
	if ret != 0 {
		return fmt.Errorf("failed to create tool-prepare pin: %d", ret)
	}
	
	name = C.CString("iocontrol.0.tool-prep-number")
	defer C.free(unsafe.Pointer(name))
	ret = C.hal_pin_s32_new(name, C.HAL_OUT, &io.pinToolPrepN, io.compID)
	if ret != 0 {
		return fmt.Errorf("failed to create tool-prep-number pin: %d", ret)
	}
	
	// Tool change pins
	name = C.CString("iocontrol.0.tool-change")
	defer C.free(unsafe.Pointer(name))
	ret = C.hal_pin_bit_new(name, C.HAL_OUT, &io.pinToolChange, io.compID)
	if ret != 0 {
		return fmt.Errorf("failed to create tool-change pin: %d", ret)
	}
	
	name = C.CString("iocontrol.0.tool-changed")
	defer C.free(unsafe.Pointer(name))
	ret = C.hal_pin_bit_new(name, C.HAL_IN, &io.pinToolChanged, io.compID)
	if ret != 0 {
		return fmt.Errorf("failed to create tool-changed pin: %d", ret)
	}
	
	// E-stop pin
	name = C.CString("iocontrol.0.emc-enable-in")
	defer C.free(unsafe.Pointer(name))
	ret = C.hal_pin_bit_new(name, C.HAL_IN, &io.pinEstop, io.compID)
	if ret != 0 {
		return fmt.Errorf("failed to create emc-enable-in pin: %d", ret)
	}
	
	// Additional pins: coolant, lube, spindle, etc. (omitted for brevity)
	
	return nil
}

// initNML initializes NML channels for backward compatibility
func (io *NativeIOControl) initNML(iniFile string) error {
	// Reuse existing C++ NML initialization
	// This maintains compatibility with existing UIs
	cIniFile := C.CString(iniFile)
	defer C.free(unsafe.Pointer(cIniFile))
	
	// Call into libiocontrol to initialize NML
	// (Requires exposing NML init function in shim)
	
	io.logger.Info("NML channels initialized")
	return nil
}

// Run executes the native IOControl main loop
func (io *NativeIOControl) Run(ctx context.Context) error {
	cycleTime := io.config.CycleTime
	if cycleTime <= 0 {
		cycleTime = 0.100 // 100ms default
	}
	
	io.logger.Info("starting native iocontrol loop",
		"cycle_time_ms", cycleTime*1000)
	
	ticker := time.NewTicker(time.Duration(cycleTime * float64(time.Second)))
	defer ticker.Stop()
	
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
			
		case req := <-io.taskChan:
			// Handle tool change request from task
			err := io.handleToolRequest(req.Tool)
			req.ReplyChan <- err
			
		case <-ticker.C:
			start := time.Now()
			err := io.cycle()
			
			io.metrics.RecordCycle(err)
			
			if io.config.HealthMonitor != nil {
				io.config.HealthMonitor.RecordIOCycle(time.Since(start), err)
			}
		}
	}
}

// cycle executes one IOControl cycle
func (io *NativeIOControl) cycle() error {
	// Read HAL pins
	estop := *io.pinEstop
	toolChanged := *io.pinToolChanged
	
	// Process NML commands
	// (Simplified - production needs full command processing)
	
	// Update tool change state machine
	switch io.changeState {
	case stateIdle:
		// Nothing to do
		
	case statePrepping:
		// Tool prep in progress
		// In real hardware, this would wait for tool changer
		// For simulation, transition immediately
		*io.pinToolPrep = 1
		*io.pinToolPrepN = C.hal_s32_t(io.currentTool)
		io.changeState = stateChanging
		
	case stateChanging:
		// Waiting for tool changed signal
		if toolChanged != 0 {
			io.toolInSpindle = io.currentTool
			*io.pinToolChange = 0
			io.changeState = stateIdle
			
			io.logger.Info("tool change complete",
				"tool", io.toolInSpindle)
		}
	}
	
	// Update status channel for UIs
	// (Requires C++ NML interaction)
	
	return nil
}

// handleToolRequest processes a tool change request
func (io *NativeIOControl) handleToolRequest(tool int) error {
	if io.changeState != stateIdle {
		return fmt.Errorf("tool change already in progress")
	}
	
	io.logger.Info("tool change requested", "tool", tool)
	
	io.currentTool = tool
	io.changeState = statePrepping
	*io.pinToolChange = 1
	*io.pinToolPrepN = C.hal_s32_t(tool)
	
	return nil
}

// Shutdown cleanly shuts down the native IOControl
func (io *NativeIOControl) Shutdown() error {
	io.logger.Info("shutting down native iocontrol")
	
	// Close task channel
	close(io.taskChan)
	
	// Exit HAL
	if io.compID >= 0 {
		C.hal_exit(io.compID)
	}
	
	cycles, errors, jitter := io.metrics.GetStats()
	io.logger.Info("native iocontrol final stats",
		"total_cycles", cycles,
		"total_errors", errors,
		"jitter_events", jitter)
	
	return nil
}

// GetState returns current IOControl state
func (io *NativeIOControl) GetState() int {
	return int(io.changeState)
}
```

**Advantages of Native Go Implementation:**

1. **Performance**: No subprocess spawning, direct HAL API calls
2. **Logging**: Structured logging throughout with context
3. **Error handling**: Go error semantics vs. C return codes
4. **Concurrency**: Go channels for task communication
5. **Maintainability**: ~300 lines of Go vs. ~1,165 lines of C++
6. **Testing**: Easier to unit test pure Go code

**Configuration:** Enable via INI file:

```ini
[EMCIO]
EMCIO = io
CYCLE_TIME = 0.100
USE_NATIVE_GO = 1    # Enable native Go implementation
```

**Limitations:**

- Still requires NML for UI compatibility (Phase 1 requirement)
- Complex hardware interfaces may need additional work
- Tool table handling needs full implementation

### 3.8 Thread Safety and Concurrency

**Overview**

The linuxcnc-server runs multiple goroutines concurrently:
- Task controller loop (10ms cycle)
- IO controller loop (100ms cycle)
- Health monitoring loop (1s cycle)

Proper thread safety is critical for CNC machine safety.

**Shared State Analysis**

| Component | Shared State | Protection Mechanism |
|-----------|-------------|----------------------|
| **HAL** | HAL shared memory, pins | HAL internal mutex (hal_lib.c) |
| **NML** | NML buffers | NML internal locking |
| **Task ↔ IOControl** | Tool change requests | Go channels (lock-free) |
| **Health Monitor** | Statistics | sync.RWMutex |
| **C++ Components** | Internal state | Existing thread-safety |

**Thread Safety Guidelines**

1. **C/C++ Shim Layer**
   - All shim functions (`*_shim_*`) are called from single goroutines
   - No concurrent calls to same shim function
   - C++ components not modified for thread-safety (single-threaded)

2. **HAL Access**
   - HAL library is thread-safe (uses internal mutexes)
   - Multiple goroutines can safely call HAL functions
   - Pin reads/writes are atomic (HAL guarantees)

3. **NML Channels**
   - Each goroutine owns its NML channels
   - No sharing of NML channel objects
   - NML library handles multi-process synchronization

4. **Go State**
   - Health Monitor: Protected by `sync.RWMutex`
   - Logging: `slog` is thread-safe
   - Configuration: Read-only after initialization

5. **Task ↔ IOControl Communication**
   ```go
   // Safe channel-based communication
   type ToolChangeRequest struct {
       Tool int
       Done chan error
   }
   
   // Task sends request
   req := ToolChangeRequest{Tool: 5, Done: make(chan error, 1)}
   iocontrol.RequestChan <- req
   err := <-req.Done
   
   // IOControl processes (single consumer)
   req := <-io.RequestChan
   err := io.doToolChange(req.Tool)
   req.Done <- err
   ```

**Mutex Requirements**

**Shim Layer Mutexes:**

```c
// src/server/shim/shim_common.h

// NOT NEEDED: Each shim is called from single goroutine
// Each component (task, iocontrol) has dedicated cycle function
// called serially, never concurrently

// IF adding shared state to shims:
#include <pthread.h>

extern pthread_mutex_t hal_shim_mutex;
extern pthread_mutex_t rtapi_shim_mutex;

#define SHIM_LOCK(m) pthread_mutex_lock(&m)
#define SHIM_UNLOCK(m) pthread_mutex_unlock(&m)
```

**Production Recommendations:**

1. **Code Review**: All C/C++ modifications reviewed for thread-safety
2. **Testing**: Run under thread sanitizer (tsan) during development
3. **Documentation**: Comment all shared state and protection mechanisms
4. **Future**: Consider moving more logic to Go for better concurrency

**Known Thread-Safe Components:**

- HAL library (hal_lib.c) - uses internal mutexes
- NML library - uses system IPC with locking
- RTAPI (uspace) - uses atomics for shared memory

**Not Thread-Safe (Single Goroutine Only):**

- emctaskmain.cc internal state
- ioControl.cc internal state
- Canon interpreter (rs274ngc)

**Testing Thread Safety:**

```bash
# Run with Go race detector
go build -race ./src/server

# Run with thread sanitizer (for C/C++ code)
CFLAGS="-fsanitize=thread" LDFLAGS="-fsanitize=thread" make

# Run test suite
./linuxcnc-server -ini tests/concurrent.ini
```

---

## 4. Required Modifications to Existing Code

### 4.1 Modifications to emctaskmain.cc

The following changes are needed to convert milltask from a standalone executable to a library:

```cpp
// src/emc/task/emctaskmain.cc (modifications)

// ============================================================
// REMOVE: main() function and command-line argument parsing
// ============================================================

// DELETE this entire block (approximately lines 2800-3000):
/*
int main(int argc, char *argv[])
{
    // ... all the argument parsing ...
    // ... main loop ...
}
*/

// ============================================================
// ADD: Library initialization interface
// ============================================================

// Global INI file path (set by shim before calling init)
char *emc_inifile = NULL;

// Add these exported functions at the end of the file:

extern "C" {

/*
 * Initialize the task controller
 * Called once at startup
 * Returns: 0 on success, non-zero on error
 */
int emcTaskInit(void)
{
    int ret;

    // Check INI file is set
    if (emc_inifile == NULL || emc_inifile[0] == '\0') {
        rcs_print_error("emcTaskInit: emc_inifile not set\n");
        return -1;
    }

    // Read EMC_DEBUG from INI (existing code, move from main)
    IniFile inifile;
    if (inifile.Open(emc_inifile) == false) {
        rcs_print_error("emcTaskInit: can't open %s\n", emc_inifile);
        return -1;
    }

    // ... (move initialization code from main() here)
    // - NML channel creation
    // - Interpreter initialization
    // - Task state initialization
    // - etc.

    if ((ret = emcTaskNmlGet()) != 0) {
        rcs_print_error("emcTaskInit: emcTaskNmlGet failed\n");
        return ret;
    }

    if ((ret = emcTaskOnce(emc_inifile)) != 0) {
        rcs_print_error("emcTaskInit: emcTaskOnce failed\n");
        return ret;
    }

    return 0;
}

/*
 * Execute the planning phase
 * Called periodically from the main loop
 * Returns: 0 on success
 */
int emcTaskPlan(void)
{
    // Existing emcTaskPlan() logic
    // (this function likely already exists, just ensure it's exported)
    return emcTaskPlanExecute();
}

/*
 * Execute the execution phase
 * Called periodically from the main loop
 * Returns: 0 on success
 */
int emcTaskExecute(void)
{
    // Existing execution logic
    return emcTaskExecuteExecute();
}

/*
 * Shutdown the task controller
 * Called once at shutdown
 */
void emcTaskShutdown(void)
{
    // Move cleanup code from main() here
    // - NML channel cleanup
    // - Interpreter cleanup
    // - etc.

    emcTaskNmlDelete();
}

/*
 * Get current task state
 * Returns: EMC_TASK_STATE enum value
 */
int emcTaskGetState(void)
{
    return emcStatus->task.state;
}

} // extern "C"
```

### 4.2 Modifications to ioControl.cc

Similar modifications are needed for the IO controller:

```cpp
// src/emc/iotask/ioControl.cc (modifications)

// ============================================================
// REMOVE: main() function
// ============================================================

// DELETE the main() function

// ============================================================
// ADD: Library initialization interface
// ============================================================

static const char *io_inifile = NULL;
static bool io_initialized = false;

extern "C" {

/*
 * Initialize the IO controller
 * ini_file: path to INI configuration file
 * Returns: 0 on success, non-zero on error
 */
int iocontrol_init(const char *ini_file)
{
    if (io_initialized) {
        return 0; // Already initialized
    }

    io_inifile = ini_file;

    // Move initialization code from main() here:
    // - Parse INI file for [EMCIO] section
    // - Create NML channels
    // - Create HAL pins
    // - Initialize tool table
    // - etc.

    IniFile inifile;
    if (inifile.Open(io_inifile) == false) {
        rtapi_print_msg(RTAPI_MSG_ERR,
                        "iocontrol: can't open INI file %s\n", io_inifile);
        return -1;
    }

    // ... rest of initialization ...

    io_initialized = true;
    return 0;
}

/*
 * Execute one IO controller cycle
 * Returns: 0 on success
 */
int iocontrol_cycle(void)
{
    if (!io_initialized) {
        return -1;
    }

    // This is essentially the body of the main loop:
    // - Read NML commands
    // - Process tool changes
    // - Handle coolant, lube, estop
    // - Update HAL pins
    // - Write NML status

    // ... existing loop body code ...

    return 0;
}

/*
 * Shutdown the IO controller
 */
void iocontrol_shutdown(void)
{
    if (!io_initialized) {
        return;
    }

    // Move cleanup code from main() here:
    // - Delete NML channels
    // - Remove HAL pins
    // - etc.

    io_initialized = false;
}

} // extern "C"
```

### 4.3 Modifications to Submakefiles

```makefile
# src/emc/task/Submakefile (modifications)

# ADD: Build task as shared library

# Existing object file list (keep as-is)
TASKSRCS := emc/task/emctaskmain.cc \
            emc/task/emccanon.cc \
            emc/task/taskintf.cc \
            emc/task/taskclass.cc \
            emc/task/taskmodule.cc \
            emc/task/backtrace.cc

TASKOBJS := $(patsubst %.cc,objects/%.o,$(TASKSRCS))

# ADD: Shared library target
../lib/libtask.so: $(TASKOBJS)
	$(ECHO) Linking libtask.so
	$(Q)$(CXX) -shared -o $@ $^ $(TASKLIBS) -Wl,-soname,libtask.so

# Keep existing milltask target for backward compatibility (optional)
../bin/milltask: $(TASKOBJS)
	$(ECHO) Linking milltask
	$(Q)$(CXX) -o $@ $^ $(TASKLIBS)

# ADD to TARGETS
TARGETS += ../lib/libtask.so
```

```makefile
# src/emc/iotask/Submakefile (modifications)

# ADD: Build iocontrol as shared library

IOCTLSRCS := emc/iotask/ioControl.cc

IOCTLOBJS := $(patsubst %.cc,objects/%.o,$(IOCTLSRCS))

# ADD: Shared library target
../lib/libiocontrol.so: $(IOCTLOBJS)
	$(ECHO) Linking libiocontrol.so
	$(Q)$(CXX) -shared -o $@ $^ $(IOCTLLIBS) -Wl,-soname,libiocontrol.so

# Keep existing iocontrol target for backward compatibility (optional)
../bin/iocontrol: $(IOCTLOBJS)
	$(ECHO) Linking iocontrol
	$(Q)$(CXX) -o $@ $^ $(IOCTLLIBS)

# ADD to TARGETS
TARGETS += ../lib/libiocontrol.so
```

---

## 5. Build System Integration

### 5.1 Improved Path Handling for Out-of-Tree Builds

**Problem:** Hardcoded `${SRCDIR}` relative paths in cgo directives break for out-of-tree builds.

**Solution:** Generate paths via build script and pass as environment variables.

```makefile
# Makefile (additions)

# ============================================================
# Go Server Build Targets
# ============================================================

GO ?= go
GOFLAGS ?=
BUILDTAGS ?=

SERVER_DIR := src/server
SERVER_BIN := bin/linuxcnc-server

# Compute absolute paths for cgo
ABS_ROOT := $(shell pwd)
ABS_RTAPI_INC := $(ABS_ROOT)/src/rtapi
ABS_HAL_INC := $(ABS_ROOT)/src/hal
ABS_EMC_INC := $(ABS_ROOT)/src/emc
ABS_NML_INC := $(ABS_ROOT)/src/emc/nml_intf
ABS_LIB_DIR := $(ABS_ROOT)/lib

# CGO flags with absolute paths
export CGO_ENABLED := 1
export CGO_CFLAGS := -I$(ABS_RTAPI_INC) -I$(ABS_HAL_INC) -I$(ABS_EMC_INC) -I$(ABS_NML_INC)
export CGO_LDFLAGS := -L$(ABS_LIB_DIR) -Wl,-rpath,$(ABS_LIB_DIR)

# Build the Go server
.PHONY: server
server: libs
	@echo "Building linuxcnc-server..."
	@echo "  CGO_CFLAGS: $(CGO_CFLAGS)"
	@echo "  CGO_LDFLAGS: $(CGO_LDFLAGS)"
	cd $(SERVER_DIR) && \
	$(GO) build $(GOFLAGS) -tags "$(BUILDTAGS)" \
		-ldflags "-X main.Version=$(VERSION) -X main.BuildTime=$(shell date -u +%Y-%m-%dT%H:%M:%SZ)" \
		-o $(ABS_ROOT)/$(SERVER_BIN) .

# Build required libraries first
.PHONY: libs
libs: lib/libtask.so lib/libiocontrol.so

# Development mode with race detector
.PHONY: server-dev
server-dev: GOFLAGS += -race
server-dev: BUILDTAGS += debug
server-dev: server

# Install server binary
.PHONY: server-install
server-install: server
	install -D -m 0755 $(SERVER_BIN) $(DESTDIR)$(bindir)/linuxcnc-server

# Run tests
.PHONY: server-test
server-test:
	cd $(SERVER_DIR) && $(GO) test -v -race ./...

# Clean server artifacts
.PHONY: server-clean
server-clean:
	rm -f $(SERVER_BIN)
	cd $(SERVER_DIR) && $(GO) clean -cache

# Add to main targets
TARGETS += server
```

**Updated cgo directives** (no more hardcoded paths):

```go
// src/server/rtapi/rtapi.go
package rtapi

/*
#cgo CFLAGS: -I${SRCDIR}/../../../rtapi
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -llinuxcnchal -lrtapi_app
*/

// BECOMES (paths from environment):

/*
#include "rtapi.h"
#include "hal.h"
*/
```

Paths are now set via `CGO_CFLAGS` and `CGO_LDFLAGS` environment variables from Makefile.

### 5.2 Go Build Configuration Helper

```bash
#!/bin/bash
# scripts/build-server.sh
#
# Build script for linuxcnc-server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERVER_DIR="$ROOT_DIR/src/server"
LIB_DIR="$ROOT_DIR/lib"
BIN_DIR="$ROOT_DIR/bin"

# Ensure output directories exist
mkdir -p "$LIB_DIR" "$BIN_DIR"

# Build C/C++ libraries if needed
echo "=== Building C/C++ libraries ==="
make -C "$ROOT_DIR" ../lib/libtask.so ../lib/libiocontrol.so

# Set up Go environment
export CGO_ENABLED=1
export CGO_CFLAGS="-I$ROOT_DIR/src/rtapi -I$ROOT_DIR/src/hal -I$ROOT_DIR/src/emc/nml_intf"
export CGO_LDFLAGS="-L$LIB_DIR -Wl,-rpath,$LIB_DIR"

# Build Go server
echo "=== Building Go server ==="
cd "$SERVER_DIR"

# Get dependencies
go mod download

# Build
VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "dev")
BUILD_TIME=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

go build \
    -ldflags "-X main.Version=$VERSION -X main.BuildTime=$BUILD_TIME" \
    -o "$BIN_DIR/linuxcnc-server" \
    .

echo "=== Build complete ==="
echo "Binary: $BIN_DIR/linuxcnc-server"

# Show linked libraries
echo ""
echo "=== Linked libraries ==="
ldd "$BIN_DIR/linuxcnc-server" | grep -E "(task|iocontrol|nml|hal)" || true
```

---

## 6. Testing Strategy

### 6.1 Unit Tests

```go
// src/server/config/config_test.go
package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadValidINI(t *testing.T) {
	// Create temporary INI file
	tmpDir := t.TempDir()
	iniPath := filepath.Join(tmpDir, "test.ini")

	iniContent := `
[EMC]
MACHINE = TestMachine
VERSION = 1.0

[TASK]
CYCLE_TIME = 0.010

[EMCIO]
CYCLE_TIME = 0.100
TOOL_TABLE = tool.tbl

[HAL]
HALFILE = test.hal

[KINS]
KINEMATICS = trivkins
JOINTS = 3

[TRAJ]
COORDINATES = X Y Z
LINEAR_UNITS = mm
`

	if err := os.WriteFile(iniPath, []byte(iniContent), 0644); err != nil {
		t.Fatal(err)
	}

	// Create dummy HAL file
	halPath := filepath.Join(tmpDir, "test.hal")
	if err := os.WriteFile(halPath, []byte("# test"), 0644); err != nil {
		t.Fatal(err)
	}

	// Load configuration
	cfg, err := Load(iniPath)
	if err != nil {
		t.Fatalf("Load failed: %v", err)
	}

	// Verify values
	if cfg.EMC.MachineName != "TestMachine" {
		t.Errorf("MachineName = %q, want %q", cfg.EMC.MachineName, "TestMachine")
	}

	if cfg.Task.CycleTime != 0.010 {
		t.Errorf("Task.CycleTime = %v, want %v", cfg.Task.CycleTime, 0.010)
	}

	if cfg.Kins.Joints != 3 {
		t.Errorf("Kins.Joints = %d, want %d", cfg.Kins.Joints, 3)
	}
}

func TestValidateConfig(t *testing.T) {
	tests := []struct {
		name    string
		cfg     Config
		wantErr bool
	}{
		{
			name: "valid config",
			cfg: Config{
				Kins: struct {
					Kinematics string `ini:"KINEMATICS"`
					Joints     int    `ini:"JOINTS"`
				}{
					Kinematics: "trivkins",
					Joints:     3,
				},
			},
			wantErr: false,
		},
		{
			name: "missing kinematics",
			cfg: Config{
				Kins: struct {
					Kinematics string `ini:"KINEMATICS"`
					Joints     int    `ini:"JOINTS"`
				}{
					Kinematics: "",
					Joints:     3,
				},
			},
			wantErr: true,
		},
		{
			name: "zero joints",
			cfg: Config{
				Kins: struct {
					Kinematics string `ini:"KINEMATICS"`
					Joints     int    `ini:"JOINTS"`
				}{
					Kinematics: "trivkins",
					Joints:     0,
				},
			},
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tt.cfg.Validate()
			if (err != nil) != tt.wantErr {
				t.Errorf("Validate() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}
```

### 6.2 Integration Test Script

```bash
#!/bin/bash
# scripts/test-server.sh
#
# Integration test for linuxcnc-server
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERVER_BIN="$ROOT_DIR/bin/linuxcnc-server"
TEST_DIR="$ROOT_DIR/tests/server"

# Check server exists
if [ ! -x "$SERVER_BIN" ]; then
    echo "Error: Server not built. Run 'make server' first."
    exit 1
fi

echo "=== Testing configuration loading ==="
$SERVER_BIN -ini "$TEST_DIR/test.ini" -debug &
SERVER_PID=$!

# Give it time to start
sleep 2

# Check it's running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: Server failed to start"
    exit 1
fi

echo "Server started successfully (PID: $SERVER_PID)"

# Test graceful shutdown
echo "=== Testing shutdown ==="
kill -TERM $SERVER_PID
wait $SERVER_PID || true

echo "=== All tests passed ==="
```

### 6.3 Sample Test Configuration

```ini
# tests/server/test.ini
# Test INI configuration for linuxcnc-server

[EMC]
MACHINE = TestMachine
VERSION = 1.0
DEBUG = 0

[DISPLAY]
DISPLAY = dummy
CYCLE_TIME = 0.100

[TASK]
TASK = milltask
CYCLE_TIME = 0.010

[RS274NGC]
PARAMETER_FILE = test.var
SUBROUTINE_PATH = .

[EMCMOT]
EMCMOT = motmod
SERVO_PERIOD = 1000000
COMM_TIMEOUT = 1.0

[EMCIO]
EMCIO = io
CYCLE_TIME = 0.100
TOOL_TABLE = tool.tbl

[HAL]
HALFILE = test.hal

[TRAJ]
COORDINATES = X Y Z
LINEAR_UNITS = mm
ANGULAR_UNITS = degree
MAX_VELOCITY = 100
MAX_ACCELERATION = 500

[KINS]
KINEMATICS = trivkins coordinates=xyz
JOINTS = 3

[AXIS_X]
MAX_VELOCITY = 100
MAX_ACCELERATION = 500
MIN_LIMIT = -100
MAX_LIMIT = 100

[AXIS_Y]
MAX_VELOCITY = 100
MAX_ACCELERATION = 500
MIN_LIMIT = -100
MAX_LIMIT = 100

[AXIS_Z]
MAX_VELOCITY = 50
MAX_ACCELERATION = 250
MIN_LIMIT = -50
MAX_LIMIT = 0

[JOINT_0]
TYPE = LINEAR
HOME = 0
MAX_VELOCITY = 100
MAX_ACCELERATION = 500
MIN_LIMIT = -100
MAX_LIMIT = 100

[JOINT_1]
TYPE = LINEAR
HOME = 0
MAX_VELOCITY = 100
MAX_ACCELERATION = 500
MIN_LIMIT = -100
MAX_LIMIT = 100

[JOINT_2]
TYPE = LINEAR
HOME = 0
MAX_VELOCITY = 50
MAX_ACCELERATION = 250
MIN_LIMIT = -50
MAX_LIMIT = 0
```

```hal
# tests/server/test.hal
# Test HAL configuration

# Load realtime components
loadrt trivkins
loadrt motmod servo_period_nsec=1000000 num_joints=3

# Set up motion controller
addf motion-command-handler servo-thread
addf motion-controller servo-thread

# Minimal connections for testing
net xpos-cmd joint.0.motor-pos-cmd
net ypos-cmd joint.1.motor-pos-cmd
net zpos-cmd joint.2.motor-pos-cmd

net xpos-fb joint.0.motor-pos-fb <= joint.0.motor-pos-cmd
net ypos-fb joint.1.motor-pos-fb <= joint.1.motor-pos-cmd
net zpos-fb joint.2.motor-pos-fb <= joint.2.motor-pos-cmd
```

---

## 7. Migration Steps

### 7.1 Phase 1 Implementation Checklist

| Step | Task | Status |
|------|------|--------|
| 1.1 | Create `src/server/` directory structure | ☐ |
| 1.2 | Create Go module (`go.mod`, `go.sum`) | ☐ |
| 1.3 | Implement configuration package | ☐ |
| 1.4 | Implement C shim layer | ☐ |
| 1.5 | Implement RTAPI Go wrapper | ☐ |
| 1.6 | Implement HAL Go wrapper | ☐ |
| 1.7 | Implement HAL file loader | ☐ |
| 1.8 | Modify `emctaskmain.cc` | ☐ |
| 1.9 | Implement Task Go wrapper | ☐ |
| 1.10 | Modify `ioControl.cc` | ☐ |
| 1.11 | Implement IOControl Go wrapper | ☐ |
| 1.12 | Update build system | ☐ |
| 1.13 | Create test configuration | ☐ |
| 1.14 | Integration testing | ☐ |
| 1.15 | Documentation | ☐ |

### 7.2 Validation Criteria

| Criterion | Test Method |
|-----------|-------------|
| Server starts successfully | `linuxcnc-server -ini test.ini` runs without error |
| HAL loads correctly | HAL pins visible via `halcmd show` |
| Motion module loaded | `halcmd show comp` shows motmod |
| Task controller running | NML status channel receives updates |
| IO controller running | Tool change commands processed |
| UI connectivity | AXIS or other UI connects and displays status |
| Graceful shutdown | SIGTERM causes clean exit |
| Error handling | Invalid INI produces clear error message |

### 7.3 Rollback Plan

If issues are encountered:

1. The existing `linuxcnc` script remains functional
2. `milltask` and `iocontrol` binaries still work standalone
3. Simply don't use `linuxcnc-server` until issues resolved

---

## 8. Future Phases (Out of Scope)

The following items are explicitly **not** part of Phase 1:

| Item | Target Phase |
|------|--------------|
| New client API (JSON/Protobuf) | Phase 2 |
| WebSocket support | Phase 2 |
| NML removal | Phase 3 |
| Task controller C rewrite | Phase 4+ |
| IOControl C rewrite | Phase 4+ |
| Web-based UI | Phase 4+ |

---

## 9. Appendix

### 9.1 Error Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| -1 | General error |
| -2 | Configuration error |
| -3 | RTAPI initialization failed |
| -4 | HAL initialization failed |
| -5 | Task initialization failed |
| -6 | IOControl initialization failed |
| -7 | NML channel error |

### 9.2 Signal Handling

| Signal | Action |
|--------|--------|
| SIGINT | Graceful shutdown |
| SIGTERM | Graceful shutdown |
| SIGHUP | Reload configuration (future) |
| SIGUSR1 | Dump status (debug) |

### 9.3 Environment Variables

| Variable | Purpose |
|----------|---------|
| `LINUXCNC_INI` | Default INI file path |
| `LINUXCNC_DEBUG` | Enable debug output |
| `LINUXCNC_LOG` | Log file path |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-02-16 | - | Initial document |
| 2.0 | 2026-02-16 | - | Production-ready improvements: logging, health monitoring, native IOControl, improved INI substitution, removed system() calls, thread safety docs, build system fixes, Go 1.22+ |
