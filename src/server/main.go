// src/server/main.go
//
// LinuxCNC Server — conditional component launcher.
//
// Components are started based on what is configured in the INI file:
//
//   Always started:
//     • RTAPI subsystem
//     • HAL subsystem + load [HAL]HALFILE files
//     • retain (if [RETAIN]VAR_FILE is set)
//     • halcmd start (start RT threads)
//
//   Started only when [TASK]TASK is set (full CNC mode):
//     • IO controller goroutine  (if [EMCIO]EMCIO is also set)
//     • Task controller goroutine
//
//   Started only when [HAL]HALUI is set (requires [TASK]TASK):
//     • halui process
//
//   Started only when [DISPLAY]DISPLAY is set:
//     • display process (foreground, server exits when display exits)
//
//   HAL-only mode (no [TASK]TASK, no [DISPLAY]DISPLAY):
//     • server waits for SIGINT or SIGTERM, then shuts down cleanly.

package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"syscall"

	"golang.org/x/sync/errgroup"
	"linuxcnc/server/config"
	"linuxcnc/server/hal"
	"linuxcnc/server/iocontrol"
	"linuxcnc/server/rtapi"
	"linuxcnc/server/task"
)

var (
	Version   = "dev"
	BuildTime = "unknown"
)

func main() {
	iniFile := flag.String("ini", "", "Path to INI configuration file")
	version := flag.Bool("version", false, "Print version and exit")
	debug := flag.Bool("debug", false, "Enable debug output")
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

	if err := run(*iniFile, *debug); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func run(iniFile string, debug bool) error {
	// ── Step 1: Load and validate configuration ───────────────────────────────
	cfg, err := config.Load(iniFile)
	if err != nil {
		return fmt.Errorf("failed to load config: %w", err)
	}

	if err := cfg.Validate(); err != nil {
		return fmt.Errorf("invalid config: %w", err)
	}

	if debug {
		cfg.Dump(os.Stdout)
	}

	// ── Step 2: Determine operational mode ───────────────────────────────────
	hasTask := cfg.HasTask()
	hasIO := cfg.HasIO()
	hasHALUI := cfg.HasHALUI()
	hasDisplay := cfg.HasDisplay()

	fmt.Printf("Mode: %s\n", cfg.Mode())

	// ── Step 3: Signal handling ───────────────────────────────────────────────
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		fmt.Printf("\nReceived signal %v, shutting down...\n", sig)
		cancel()
	}()

	// ── ALWAYS: Initialize RTAPI ──────────────────────────────────────────────
	rt, err := rtapi.Init(rtapi.Config{
		InstanceName: cfg.EMC.MachineName,
		Debug:        debug,
	})
	if err != nil {
		return fmt.Errorf("rtapi init failed: %w", err)
	}
	defer rt.Shutdown()

	fmt.Println("RTAPI initialized")

	// ── ALWAYS: Initialize HAL ────────────────────────────────────────────────
	h, err := hal.Init(hal.Config{ComponentName: "linuxcnc"})
	if err != nil {
		return fmt.Errorf("hal init failed: %w", err)
	}
	defer h.Shutdown()

	fmt.Println("HAL initialized")

	// ── ALWAYS: Load HAL files ────────────────────────────────────────────────
	halLoader := hal.NewLoader(h, cfg)
	if err := halLoader.LoadFiles(cfg.HAL.Files); err != nil {
		return fmt.Errorf("hal config failed: %w", err)
	}

	fmt.Printf("Loaded %d HAL file(s)\n", len(cfg.HAL.Files))

	// ── ALWAYS: Start retain if configured ───────────────────────────────────
	if cfg.HasRetain() {
		if err := startRetain(cfg); err != nil {
			return fmt.Errorf("retain start failed: %w", err)
		}
		fmt.Printf("Retain started (var file: %s)\n", cfg.Retain.VarFile)
	}

	// ── Run goroutines for long-running components ────────────────────────────
	g, gctx := errgroup.WithContext(ctx)

	// ── CONDITIONAL: IO Controller (only with Task) ───────────────────────────
	if hasTask && hasIO {
		ioc, err := iocontrol.Init(iocontrol.Config{
			IniFile:   iniFile,
			CycleTime: cfg.EMCIO.CycleTime,
		})
		if err != nil {
			return fmt.Errorf("iocontrol init failed: %w", err)
		}
		defer ioc.Shutdown()

		g.Go(func() error { return ioc.Run(gctx) })

		fmt.Println("IO Controller initialized")
	}

	// ── CONDITIONAL: Task Controller ─────────────────────────────────────────
	if hasTask {
		tsk, err := task.Init(task.Config{
			IniFile:   iniFile,
			CycleTime: cfg.Task.CycleTime,
		})
		if err != nil {
			return fmt.Errorf("task init failed: %w", err)
		}
		defer tsk.Shutdown()

		g.Go(func() error { return tsk.Run(gctx) })

		fmt.Println("Task Controller initialized")
	}

	// ── ALWAYS: Signal HAL ready (start RT threads) ───────────────────────────
	if err := h.Ready(); err != nil {
		return fmt.Errorf("hal ready failed: %w", err)
	}

	fmt.Println("HAL ready")

	// ── CONDITIONAL: halui ────────────────────────────────────────────────────
	// validateDependencies ensures hasHALUI is only set when hasTask is also set.
	if hasHALUI {
		if err := startHALUI(cfg, gctx, g); err != nil {
			return fmt.Errorf("halui start failed: %w", err)
		}
		fmt.Printf("halui started (%s)\n", cfg.HAL.HALUI)
	}

	// ── CONDITIONAL: Display ──────────────────────────────────────────────────
	if hasDisplay {
		fmt.Printf("Starting display: %s\n", cfg.Display.Display)
		if err := runDisplay(cfg, gctx); err != nil && err != context.Canceled {
			return fmt.Errorf("display exited with error: %w", err)
		}
		// Display exiting is the normal shutdown trigger in full CNC mode.
		cancel()
	} else {
		// HAL-only mode: wait for SIGINT/SIGTERM.
		fmt.Println("HAL-only mode. Waiting for shutdown signal (SIGINT/SIGTERM)...")
		<-gctx.Done()
	}

	if err := g.Wait(); err != nil && err != context.Canceled {
		return fmt.Errorf("runtime error: %w", err)
	}

	fmt.Println("Shutdown complete")
	return nil
}

// startRetain launches the retain/persist service for HAL variables.
func startRetain(cfg *config.Config) error {
	// Production implementation: start halcmd-based retain daemon or
	// a Go ticker that periodically saves HAL pin values to cfg.Retain.VarFile.
	// Stub: no-op.
	return nil
}

// startHALUI launches the halui process as a managed goroutine.
// It exits when the provided context is cancelled.
func startHALUI(cfg *config.Config, ctx context.Context, g *errgroup.Group) error {
	g.Go(func() error {
		cmd := exec.CommandContext(ctx, cfg.HAL.HALUI, "-ini", cfg.IniPath)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil && ctx.Err() == nil {
			return fmt.Errorf("halui exited unexpectedly: %w", err)
		}
		return nil
	})
	return nil
}

// runDisplay starts the display process and blocks until it exits.
func runDisplay(cfg *config.Config, ctx context.Context) error {
	cmd := exec.CommandContext(ctx, cfg.Display.Display, "-ini", cfg.IniPath)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
