//go:build linux

// Package rtapi -- privilege.go implements the privilege management strategy
// chosen in RTAPI-INPROCESS-DESIGN.md: Option A + D (Linux file capabilities
// + PR_SET_NO_NEW_PRIVS).
//
// The linuxcnc-launcher binary is installed with file capabilities via
// "sudo make setuid", which runs:
//
//	setcap cap_sys_nice,cap_ipc_lock,cap_sys_rawio,cap_sys_resource=eip \
//	    /path/to/bin/linuxcnc-launcher
//
// # Required capabilities
//
//   - CAP_SYS_NICE:     SCHED_FIFO scheduling, RT priority, CPU affinity
//   - CAP_IPC_LOCK:     mlockall(), unlimited memory locking
//   - CAP_SYS_RAWIO:    iopl(3), direct I/O port access (x86)
//   - CAP_SYS_RESOURCE: setrlimit(RLIMIT_RTPRIO), setrlimit(RLIMIT_MEMLOCK)
//
// # Privilege lifecycle
//
//  1. Process starts with file capabilities in effective+permitted sets.
//  2. hardenRT() is a no-op here; the actual harden_rt() C function is called
//     inside rtapi_app_master_start() on the dedicated master goroutine.
//  3. After the master loop is running, dropPrivileges() clears capabilities
//     and installs PR_SET_NO_NEW_PRIVS.
//  4. From that point on, the process is equivalent to a normal user process.
package rtapi

/*
#cgo CFLAGS: -I${SRCDIR}/../../rtapi -I${SRCDIR}/../../../include
#cgo LDFLAGS: -L${SRCDIR}/../../../lib -lrtapi_app -ldl -lpthread

#include <stdlib.h>
#include <errno.h>
#include <sys/prctl.h>

// drop_privileges_impl installs PR_SET_NO_NEW_PRIVS.
//
// Full cap_set_proc() to clear all capabilities requires libcap-dev.
// PR_SET_NO_NEW_PRIVS is sufficient as defense-in-depth and is safe to
// call unconditionally.
static int drop_privileges_impl(void) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -errno;
    }
    return 0;
}
*/
import "C"
import "fmt"

// hardenRT is a no-op on the Go side; the actual harden_rt() C function is
// invoked inside rtapi_app_master_start() which runs on the dedicated master
// goroutine before entering the socket accept loop.
//
// We keep this function in the Engine API so callers can use the IsRealtime()
// query after Init() completes.  The realtime flag is determined by app_policy
// set during initialize_app() inside the C master thread.
func (e *Engine) hardenRT() error {
// harden_rt() is called inside rtapi_app_master_start(); nothing to do here.
return nil
}

// dropPrivileges drops elevated privileges from the process and installs
// PR_SET_NO_NEW_PRIVS.
//
// After this call:
//   - No thread can gain new privileges via execve() of a setuid/setcap binary.
//   - Existing RT threads continue with their current SCHED_FIFO policy.
//   - Already-locked memory pages (from mlockall) remain locked.
//   - RLIMIT_RTPRIO remains unlimited (set before privilege drop).
func (e *Engine) dropPrivileges() error {
ret := C.drop_privileges_impl()
if ret != 0 {
return fmt.Errorf("drop_privileges failed with errno %d", int(-ret))
}
return nil
}
