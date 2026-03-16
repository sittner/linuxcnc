// Package rtapi implements the in-process RTAPI engine for the LinuxCNC
// launcher.
//
// This package replaces the external rtapi_app process with an in-process
// thread, enabling direct shared-memory access to HAL data structures without
// IPC overhead.
//
// Architecture (see RTAPI-INPROCESS-DESIGN.md for full analysis):
//   - Init() starts a goroutine locked to an OS thread that calls
//     C.rtapi_app_master_start().  This function creates the Unix domain
//     socket (the same socket halcmd connects to), loads hal_lib, calls
//     harden_rt(), and then blocks in the accept loop serving halcmd requests.
//   - Init() waits until the socket file exists (master loop is ready), then
//     drops elevated privileges via dropPrivileges().
//   - LoadModule() / UnloadModule() call C.rtapi_app_load() /
//     C.rtapi_app_unload() directly, bypassing the socket IPC.  They are
//     thread-safe because the C implementation uses modules_lock.
//   - halcmd -f <file> connections still go through the Unix socket, but the
//     socket is now served by the master goroutine inside this process, so
//     all modules share the same address space.
package rtapi

/*
#cgo CFLAGS: -I${SRCDIR}/../../rtapi -I${SRCDIR}/../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -lrtapi_app -ldl -lpthread

#include "rtapi_app_lib.h"
#include <stdlib.h>
*/
import "C"
import (
"fmt"
"log/slog"
"os"
"runtime"
"sync"
"time"
"unsafe"
)

// Config holds configuration for the in-process RTAPI engine.
type Config struct {
// InstanceName identifies this RTAPI instance (used in shared-memory naming
// and component registration).
InstanceName string

// DebugLevel sets the RTAPI debug message verbosity (0 = default, 5 = max).
DebugLevel int

// FifoPath overrides the Unix socket path for the master loop.
// If empty, the default ($HOME/.rtapi_fifo or RTAPI_FIFO_PATH env var) is used.
FifoPath string
}

// Engine manages the in-process RTAPI subsystem.
//
// It starts the rtapi_app master loop as a goroutine locked to an OS thread
// within the Go launcher process, enabling direct shared-memory access to HAL
// data structures without Unix socket IPC overhead.
type Engine struct {
cfg    Config
logger *slog.Logger

mu          sync.Mutex
initialized bool

// masterDone is closed when the master goroutine exits.
masterDone chan struct{}
}

// New creates a new Engine with the given configuration and logger.
// If logger is nil, a default structured logger writing to stderr is used.
func New(cfg Config, logger *slog.Logger) *Engine {
if logger == nil {
logger = slog.New(slog.NewTextHandler(os.Stderr, nil))
}
return &Engine{
cfg:        cfg,
logger:     logger,
masterDone: make(chan struct{}),
}
}

// Init starts the in-process RTAPI master loop and waits for it to be ready.
//
// The startup sequence is:
//  1. Start a goroutine locked to an OS thread.
//  2. The goroutine calls C.rtapi_app_master_start() which:
//     - Captures uid/euid.
//     - Creates and binds the Unix socket.
//     - Calls harden_rt() (mlockall, iopl, SCHED_FIFO setup).
//     - Loads hal_lib.
//     - Enters the blocking accept loop (serving halcmd connections).
//  3. Init() polls until the socket file exists, then returns.
//  4. dropPrivileges() installs PR_SET_NO_NEW_PRIVS.
func (e *Engine) Init() error {
e.mu.Lock()
defer e.mu.Unlock()

if e.initialized {
return fmt.Errorf("rtapi: engine already initialized")
}

// Resolve socket path before starting the C thread so we can poll for it.
fifoPath := e.cfg.FifoPath
if fifoPath == "" {
if p := os.Getenv("RTAPI_FIFO_PATH"); p != "" {
fifoPath = p
} else if home := os.Getenv("HOME"); home != "" {
fifoPath = home + "/.rtapi_fifo"
}
}

// Start the master loop on a goroutine locked to its OS thread.
// C.rtapi_app_master_start blocks until rtapi_app_master_stop() is called.
go func() {
runtime.LockOSThread()
// LockOSThread is intentionally not paired with UnlockOSThread here:
// when the goroutine exits Go will destroy the underlying OS thread,
// which is the correct behaviour for a long-lived C blocking call.
defer close(e.masterDone)

var cFifoPath *C.char
if fifoPath != "" {
cFifoPath = C.CString(fifoPath)
defer C.free(unsafe.Pointer(cFifoPath))
}

ret := C.rtapi_app_master_start(cFifoPath)
if ret < 0 {
e.logger.Error("rtapi master loop exited with error", "ret", int(ret))
}
}()

// Wait until the socket file appears (master loop is ready to accept).
if fifoPath != "" {
deadline := time.Now().Add(10 * time.Second)
for time.Now().Before(deadline) {
if _, err := os.Stat(fifoPath); err == nil {
break
}
time.Sleep(10 * time.Millisecond)
}
if _, err := os.Stat(fifoPath); err != nil {
// Signal master to exit so we don't leak the goroutine.
C.rtapi_app_master_stop()
<-e.masterDone
return fmt.Errorf("rtapi: master loop socket %s did not appear: %w", fifoPath, err)
}
} else {
// No fifo path resolved; give C a moment to start up.
time.Sleep(100 * time.Millisecond)
}

e.logger.Info("dropping elevated privileges")
if err := e.dropPrivileges(); err != nil {
e.logger.Warn("privilege drop failed", "error", err)
}

e.initialized = true
e.logger.Info("RTAPI engine initialized",
"realtime", e.IsRealtime(),
"instance", e.cfg.InstanceName)
return nil
}

// Shutdown performs an orderly shutdown of the RTAPI subsystem.
//
// It signals the master loop to exit and waits for it to finish.
// Shutdown is idempotent: calling it on an uninitialized engine is a no-op.
func (e *Engine) Shutdown() error {
e.mu.Lock()
defer e.mu.Unlock()

if !e.initialized {
e.logger.Debug("rtapi: engine not initialized, nothing to shut down")
return nil
}

e.logger.Info("shutting down RTAPI engine")
C.rtapi_app_master_stop()

// Wait for the master goroutine to finish (with a timeout).
select {
case <-e.masterDone:
case <-time.After(5 * time.Second):
e.logger.Warn("rtapi: master loop did not exit within 5 seconds")
}

e.initialized = false
e.logger.Info("RTAPI engine shut down")
return nil
}

// LoadModule loads a realtime (.so) module into the RTAPI subsystem.
//
// This calls C.rtapi_app_load() which calls do_load_cmd() directly,
// bypassing the Unix socket IPC.  The module is dlopen()'d in this process,
// so its symbols are immediately visible to subsequently loaded modules
// (e.g. motmod can resolve symbols from homemod and tpmod).
//
// Thread-safe: protected by the C-side modules_lock mutex.
func (e *Engine) LoadModule(name string, args []string) error {
e.mu.Lock()
defer e.mu.Unlock()

if !e.initialized {
return fmt.Errorf("rtapi: engine not initialized")
}

e.logger.Info("loading RT module", "module", name, "args", args)

// Build C argv: args[0] is the module name; the rest are "key=value" params.
// The C do_load_cmd() function takes (name, args, nargs) where args[0]==name.
cArgv := make([]*C.char, len(args)+1)
cArgv[0] = C.CString(name)
defer C.free(unsafe.Pointer(cArgv[0]))
for i, a := range args {
cArgv[i+1] = C.CString(a)
defer C.free(unsafe.Pointer(cArgv[i+1]))
}

cName := C.CString(name)
defer C.free(unsafe.Pointer(cName))

var argvPtr **C.char
if len(cArgv) > 0 {
argvPtr = &cArgv[0]
}

ret := C.rtapi_app_load(cName, argvPtr, C.int(len(cArgv)))
if ret < 0 {
return fmt.Errorf("rtapi: loadrt %s failed: %d", name, int(ret))
}
return nil
}

// UnloadModule unloads a previously-loaded realtime module.
func (e *Engine) UnloadModule(name string) error {
e.mu.Lock()
defer e.mu.Unlock()

if !e.initialized {
return fmt.Errorf("rtapi: engine not initialized")
}

e.logger.Info("unloading RT module", "module", name)

cName := C.CString(name)
defer C.free(unsafe.Pointer(cName))

ret := C.rtapi_app_unload(cName)
if ret < 0 {
return fmt.Errorf("rtapi: unloadrt %s failed: %d", name, int(ret))
}
return nil
}

// IsRealtime reports whether the engine is running in realtime mode
// (SCHED_FIFO threads, mlockall, etc.).
//
// Returns false if the system does not support realtime (e.g. a CI
// environment without the necessary capabilities).
func (e *Engine) IsRealtime() bool {
	e.mu.Lock()
	defer e.mu.Unlock()
		return false
	}
	return C.rtapi_app_is_realtime() != 0
}
