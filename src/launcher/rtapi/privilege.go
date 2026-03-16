//go:build linux

// Package rtapi — privilege.go implements the privilege management strategy
// chosen in RTAPI-INPROCESS-DESIGN.md: Option A + D (Linux file capabilities
// + PR_SET_NO_NEW_PRIVS).
//
// The linuxcnc-launcher binary is installed with file capabilities:
//
//	sudo setcap cap_sys_nice,cap_ipc_lock,cap_sys_rawio,cap_sys_resource=eip \
//	    $(EMC2_BIN_DIR)/linuxcnc-launcher
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
//  2. hardenRT() is called from a goroutine locked to an OS thread.
//  3. After RT setup is complete, dropPrivileges() clears all capabilities and
//     installs PR_SET_NO_NEW_PRIVS.
//  4. From that point on, the process is equivalent to a normal user process.
package rtapi

/*
#include <stdlib.h>
#include <errno.h>
#include <sys/prctl.h>

// harden_rt_stub is a STUB for the full harden_rt() implementation.
//
// When the CGO bridge to uspace_rtapi_app.c (refactored as a library) is
// complete, this will be replaced by a direct call to harden_rt().
//
// The full implementation will perform the following syscalls IN ORDER
// (each requires the capabilities listed in brackets):
//
//   1. iopl(3)
//      - Sets the I/O privilege level for this OS thread to 3, granting
//        direct access to all x86 I/O ports (parallel port, etc.).
//      - Requires CAP_SYS_RAWIO.
//      - Per-thread state stored in task_struct.iopl (x86 only).
//      - On non-x86 architectures, iopl() is a no-op.
//      - NOTE: RT pthreads created by task_start() do NOT inherit this
//        setting; they must call iopl(3) independently in their startup
//        function.
//
//   2. setrlimit(RLIMIT_RTPRIO, {RLIM_INFINITY, RLIM_INFINITY})
//      - Allows threads in this process to set RT scheduling priorities
//        without a hard ceiling.
//      - Requires CAP_SYS_RESOURCE.
//      - Process-wide; persists after privilege drop.
//
//   3. setrlimit(RLIMIT_MEMLOCK, {RLIM_INFINITY, RLIM_INFINITY})
//      - Allows unlimited memory locking.
//      - Requires CAP_SYS_RESOURCE.
//      - Process-wide; persists after privilege drop.
//
//   4. mlockall(MCL_CURRENT | MCL_FUTURE)
//      - Locks all current and future memory pages into RAM (no swapping).
//      - Requires CAP_IPC_LOCK (or RLIMIT_MEMLOCK == RLIM_INFINITY, set above).
//      - Process-wide; affects Go's memory allocator (all allocations are
//        pinned in RAM). This increases physical memory footprint but
//        eliminates page-fault latency for RT threads.
//      - Persists after privilege drop.
//
//   5. open("/dev/cpu_dma_latency", O_WRONLY) + write(fd, &zero, 4)
//      - Disables CPU power-management C-states by holding the file open.
//      - Requires root group or udev rule: KERNEL=="cpu_dma_latency",
//        GROUP="realtime", MODE="0660"
//      - The fd is intentionally kept open for the process lifetime.
//      - Note: udev rule is preferred over requiring root for this file.
//
//   6. prctl(PR_SET_DUMPABLE, 1, 0, 0, 0)
//      - Allows core dump generation for debugging RT issues.
//      - No capability required.
//
// Returns 0 on success, -errno on failure.
static int harden_rt_stub(void) {
    // TODO: Replace with call to harden_rt() from uspace_rtapi_app.c once
    // that file is refactored as a linkable library (not a standalone binary).
    //
    // Pseudo-code for full implementation:
    //
    //   #include <sys/io.h>        // iopl()
    //   #include <sys/mman.h>      // mlockall()
    //   #include <sys/resource.h>  // setrlimit()
    //   #include <fcntl.h>         // open()
    //   #include <unistd.h>        // write()
    //   #include <stdint.h>        // int32_t
    //
    //   // 1. x86 I/O port access
    //   #if defined(__i386__) || defined(__x86_64__)
    //   if (iopl(3) != 0) {
    //       // Non-fatal on non-x86 or without CAP_SYS_RAWIO
    //       perror("iopl");
    //   }
    //   #endif
    //
    //   // 2. Allow SCHED_FIFO at any priority
    //   struct rlimit r = { RLIM_INFINITY, RLIM_INFINITY };
    //   if (setrlimit(RLIMIT_RTPRIO, &r) != 0) return -errno;
    //
    //   // 3. Unlimited memory locking
    //   if (setrlimit(RLIMIT_MEMLOCK, &r) != 0) return -errno;
    //
    //   // 4. Lock all current and future pages
    //   if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) return -errno;
    //
    //   // 5. Disable CPU power-management latency
    //   int fd = open("/dev/cpu_dma_latency", O_WRONLY);
    //   if (fd >= 0) {
    //       int32_t latency = 0;
    //       (void)write(fd, &latency, sizeof(latency));
    //       // fd intentionally kept open
    //   }
    //
    //   // 6. Allow core dumps
    //   (void)prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
    //
    //   return 0;
    return 0;
}

// drop_privileges_stub is a STUB for the full privilege drop implementation.
//
// When the CGO bridge is complete and libcap is available as a build
// dependency, this will:
//
//  1. cap_set_proc(cap_init())
//     - Clears ALL Linux capabilities from the process's effective, permitted,
//       and inheritable sets.
//     - After this call, no thread in the process can perform any privileged
//       operation — regardless of which OS thread the goroutine runs on.
//     - Existing RT threads (SCHED_FIFO pthreads) continue with their current
//       scheduling policy; privilege drop does not demote running threads.
//     - Already-locked memory (from mlockall) remains locked.
//
//  2. prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)
//     - An irreversible kernel guarantee: no thread in this process can ever
//       gain new privileges via execve() of setuid/setcap binaries.
//     - Prevents confused-deputy attacks where a compromised component tries
//       to exec a privileged helper.
//
// Returns 0 on success, -errno on failure.
static int drop_privileges_stub(void) {
    // TODO: Replace with libcap calls once libcap-dev is a build dependency:
    //
    //   #include <sys/capability.h>
    //
    //   cap_t empty = cap_init();
    //   if (!empty) return -errno;
    //   if (cap_set_proc(empty) != 0) {
    //       int err = errno;
    //       cap_free(empty);
    //       return -err;
    //   }
    //   cap_free(empty);

    // Install PR_SET_NO_NEW_PRIVS even in the stub — this is safe to call
    // unconditionally and provides defense-in-depth immediately.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return -errno;
    }
    return 0;
}
*/
import "C"
import "fmt"

// hardenRT performs the RT hardening sequence.
//
// This MUST be called from a goroutine that has been locked to an OS thread
// via runtime.LockOSThread(), because iopl() sets per-thread state in the
// Linux task_struct. If the goroutine migrates to another OS thread, the
// iopl() state is lost on the new thread.
//
// Once the CGO bridge to uspace_rtapi_app.c is complete, this function will
// call C.harden_rt() directly. For now, it calls the C stub.
func (e *Engine) hardenRT() error {
	ret := C.harden_rt_stub()
	if ret != 0 {
		return fmt.Errorf("harden_rt failed with errno %d", int(-ret))
	}
	return nil
}

// dropPrivileges drops all Linux capabilities from the process and installs
// PR_SET_NO_NEW_PRIVS.
//
// After this call:
//   - No thread in the process holds any Linux capability.
//   - No thread can gain new privileges via execve() of a setuid/setcap binary.
//   - Existing RT threads continue with their current SCHED_FIFO policy.
//   - Already-locked memory pages (from mlockall) remain locked.
//   - RLIMIT_RTPRIO remains unlimited (set before privilege drop).
//
// Once the CGO bridge to libcap is complete, this will call cap_set_proc()
// to clear all capabilities before installing PR_SET_NO_NEW_PRIVS.
func (e *Engine) dropPrivileges() error {
	ret := C.drop_privileges_stub()
	if ret != 0 {
		return fmt.Errorf("drop_privileges failed with errno %d", int(-ret))
	}
	return nil
}
