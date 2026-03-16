// Package rtapi implements the in-process RTAPI engine for the LinuxCNC
// launcher.
//
// This package replaces the external rtapi_app process with an in-process
// thread, enabling direct shared-memory access to HAL data structures without
// IPC overhead.
//
// Architecture (see RTAPI-INPROCESS-DESIGN.md for full analysis):
//   - The Engine performs RT hardening (iopl, mlockall, RLIMIT_RTPRIO, etc.)
//     via CGO during Init(), then drops all elevated capabilities.
//   - RT threads are pure C pthreads (created by the rtapi C library), not Go
//     goroutines. They are unaffected by Go's scheduler.
//   - After Init(), all Go goroutines (REST API, display, HAL orchestration)
//     run without any elevated privileges.
//
// The CGO bridging to the C rtapi library (uspace_rtapi_app.c refactored as a
// library) is a follow-up task. This package provides the Go-side API and
// privilege management scaffolding.
package rtapi

import (
	"fmt"
	"log/slog"
	"os"
	"runtime"
	"sync"
)

// Config holds configuration for the in-process RTAPI engine.
type Config struct {
	// InstanceName identifies this RTAPI instance (used in shared-memory naming
	// and component registration).
	InstanceName string

	// DebugLevel sets the RTAPI debug message verbosity (0 = default, 5 = max).
	// Values above 5 are passed through to the underlying RTAPI subsystem which
	// clamps them to its own maximum.
	DebugLevel int
}

// Engine manages the in-process RTAPI subsystem.
//
// It embeds the rtapi_app functionality as a goroutine (backed by a locked OS
// thread) within the Go launcher process, enabling direct shared-memory access
// to HAL data structures without Unix socket IPC.
type Engine struct {
	cfg    Config
	logger *slog.Logger

	mu          sync.Mutex
	initialized bool
	isRT        bool
}

// New creates a new Engine with the given configuration and logger.
// If logger is nil, a default structured logger writing to stderr is used.
func New(cfg Config, logger *slog.Logger) *Engine {
	if logger == nil {
		logger = slog.New(slog.NewTextHandler(os.Stderr, nil))
	}
	return &Engine{cfg: cfg, logger: logger}
}

// Init performs privileged RT setup and then drops all elevated privileges.
//
// This method locks the calling goroutine to its OS thread
// (runtime.LockOSThread) to ensure that per-thread state set by harden_rt()
// (particularly iopl() on x86) remains on a known OS thread.
//
// Startup sequence:
//  1. Lock goroutine to OS thread.
//  2. hardenRT() — calls C harden_rt() equivalent via CGO:
//     - iopl(3) for x86 I/O port access
//     - setrlimit(RLIMIT_RTPRIO, unlimited) to allow SCHED_FIFO
//     - setrlimit(RLIMIT_MEMLOCK, unlimited)
//     - mlockall(MCL_CURRENT | MCL_FUTURE)
//     - open /dev/cpu_dma_latency and write 0
//     - prctl(PR_SET_DUMPABLE, 1)
//  3. Initialize RTAPI subsystem (hal_lib, shared memory) via CGO.
//  4. dropPrivileges() — clears all Linux capabilities and installs
//     PR_SET_NO_NEW_PRIVS. After this call the process is indistinguishable
//     from a normal user process.
//
// If hardenRT() fails (e.g., running without capabilities in a test
// environment), Init() succeeds in non-RT mode (IsRealtime() returns false).
func (e *Engine) Init() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	if e.initialized {
		return fmt.Errorf("rtapi: engine already initialized")
	}

	// Pin this goroutine to a single OS thread for the duration of privileged
	// setup. Per-thread state such as iopl() must not migrate to another thread.
	runtime.LockOSThread()

	e.logger.Info("performing RT hardening")
	if err := e.hardenRT(); err != nil {
		// hardenRT failure is non-fatal: in test/CI environments and machines
		// that do not need direct I/O port access, RT hardening may partially
		// fail. Log a warning and continue in non-RT mode.
		e.logger.Warn("RT hardening failed, running in non-RT mode", "error", err)
		e.isRT = false
	} else {
		e.isRT = true
	}

	// TODO: CGO call to initialize RTAPI subsystem (hal_lib, shared memory).
	// This replaces the master() initialization sequence in uspace_rtapi_app.c.
	// Example (once the C shim is implemented):
	//
	//   cname := C.CString(e.cfg.InstanceName)
	//   defer C.free(unsafe.Pointer(cname))
	//   ret := C.rtapi_app_init(cname, C.int(e.cfg.DebugLevel))
	//   if ret < 0 {
	//       return fmt.Errorf("rtapi: subsystem init failed: %d", ret)
	//   }

	e.logger.Info("dropping elevated privileges")
	if err := e.dropPrivileges(); err != nil {
		// Privilege drop failure is logged but not fatal: on systems where
		// libcap is not available or capabilities were never granted, this
		// call may fail harmlessly.
		e.logger.Warn("privilege drop failed", "error", err)
	}

	e.initialized = true
	e.logger.Info("RTAPI engine initialized", "realtime", e.isRT,
		"instance", e.cfg.InstanceName)
	return nil
}

// Shutdown performs an orderly shutdown of the RTAPI subsystem.
//
// It stops all RT threads, unloads all loaded modules, and releases RTAPI
// resources including shared memory segments.
//
// Shutdown is idempotent: calling it on an uninitialized engine is a no-op.
func (e *Engine) Shutdown() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	if !e.initialized {
		e.logger.Debug("rtapi: engine not initialized, nothing to shut down")
		return nil
	}

	e.logger.Info("shutting down RTAPI engine")

	// TODO: CGO call to shut down RTAPI subsystem.
	// This replaces the cleanup/exit sequence in uspace_rtapi_app.c.
	// Example (once the C shim is implemented):
	//
	//   ret := C.rtapi_app_exit()
	//   if ret < 0 {
	//       return fmt.Errorf("rtapi: shutdown failed: %d", ret)
	//   }

	e.initialized = false
	e.logger.Info("RTAPI engine shut down")
	return nil
}

// LoadModule loads a realtime (.so) module into the RTAPI subsystem.
//
// This replaces the halcmd→rtapi_app Unix socket IPC protocol. Instead of
// halcmd sending a "loadrt" command over a socket to the external rtapi_app
// process, the module is loaded directly via dlopen() in this process.
//
// name is the module name (e.g., "threads", "tpmod", "homemod").
// args are the module parameters (e.g., "name1=servo-thread period1=1000000").
//
// Since the module is loaded in-process, HAL data structures (pins, params,
// signals, threads) are accessible via direct pointer dereference — no IPC.
func (e *Engine) LoadModule(name string, args []string) error {
	e.mu.Lock()
	defer e.mu.Unlock()

	if !e.initialized {
		return fmt.Errorf("rtapi: engine not initialized")
	}

	e.logger.Info("loading RT module", "module", name, "args", args)

	// TODO: CGO call to load module via dlopen().
	// This replaces do_load_cmd() from uspace_rtapi_app.c, which:
	//   1. dlopen()s the .so file from HAL_RTMOD_DIR
	//   2. Calls rtapi_app_main() in the loaded module
	//   3. Creates RT pthreads (SCHED_FIFO) via task_start() if requested
	//
	// Example (once the C shim is implemented):
	//
	//   cname := C.CString(name)
	//   defer C.free(unsafe.Pointer(cname))
	//   // Build C argv from args slice
	//   cArgv := make([]*C.char, len(args)+1)
	//   for i, a := range args {
	//       cArgv[i] = C.CString(a)
	//       defer C.free(unsafe.Pointer(cArgv[i]))
	//   }
	//   cArgv[len(args)] = nil
	//   ret := C.rtapi_app_loadrt(cname, (**C.char)(unsafe.Pointer(&cArgv[0])),
	//                             C.int(len(args)))
	//   if ret < 0 {
	//       return fmt.Errorf("rtapi: loadrt %s failed: %d", name, ret)
	//   }

	return nil
}

// UnloadModule unloads a previously-loaded realtime module.
//
// name is the module name (e.g., "threads").
func (e *Engine) UnloadModule(name string) error {
	e.mu.Lock()
	defer e.mu.Unlock()

	if !e.initialized {
		return fmt.Errorf("rtapi: engine not initialized")
	}

	e.logger.Info("unloading RT module", "module", name)

	// TODO: CGO call to unload module.
	// This replaces do_unload_cmd() from uspace_rtapi_app.c, which:
	//   1. Finds the loaded module by name
	//   2. Calls rtapi_app_exit() in the module
	//   3. dlclose()s the .so file
	//
	// Example (once the C shim is implemented):
	//
	//   cname := C.CString(name)
	//   defer C.free(unsafe.Pointer(cname))
	//   ret := C.rtapi_app_unloadrt(cname)
	//   if ret < 0 {
	//       return fmt.Errorf("rtapi: unloadrt %s failed: %d", name, ret)
	//   }

	return nil
}

// IsRealtime reports whether the engine successfully initialized in realtime
// mode (with SCHED_FIFO threads, mlockall, etc.).
//
// Returns false if hardenRT() failed during Init() (e.g., in a CI environment
// without the necessary capabilities).
func (e *Engine) IsRealtime() bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.isRT
}
