# RTAPI In-Process Design Document

## Overview

This document analyses the viable options for running `rtapi_app` functionality
in-process within the Go `linuxcnc-launcher` binary, while maintaining hard
real-time features and keeping non-RT code (REST services, display
orchestration, etc.) unprivileged.

**Key constraint:** same process = shared address space, enabling direct
shared-memory access to HAL data structures without IPC overhead.

---

## Background: What rtapi_app Needs Root For

`harden_rt()` in `src/rtapi/uspace_rtapi_app.c` performs these privileged
operations:

| Operation | Syscall/API | Capability Needed |
|---|---|---|
| Direct I/O port access | `iopl(3)` | `CAP_SYS_RAWIO` |
| Allow SCHED_FIFO at any priority | `setrlimit(RLIMIT_RTPRIO, ∞)` | `CAP_SYS_RESOURCE` |
| Unlimited memory locking | `setrlimit(RLIMIT_MEMLOCK, ∞)` | `CAP_SYS_RESOURCE` |
| Lock all pages in memory | `mlockall(MCL_CURRENT\|MCL_FUTURE)` | `CAP_IPC_LOCK` |
| Disable CPU power-saving | `open("/dev/cpu_dma_latency")` + `write(0)` | root or group membership |
| Core dumps | `prctl(PR_SET_DUMPABLE, 1)` | none |
| RT thread creation | `pthread_create` + `SCHED_FIFO` | `CAP_SYS_NICE` |

The current `rtapi_app` is **setuid root**: it starts as euid=0, captures both
uids, calls `setfsuid(ruid)` immediately, and only temporarily re-escalates via
`with_root_enter()` / `with_root_exit()` for specific privileged operations.

---

## The Goroutine Scheduler Problem

Go's runtime multiplexes goroutines onto OS threads dynamically. This has
critical implications for per-thread state:

- **`iopl()`** — stored in the thread's `iopl` field in Linux `task_struct`.
  If a goroutine migrates to a new OS thread, the iopl state is **lost** on the
  new thread. Solution: `runtime.LockOSThread()` pins a goroutine to one OS
  thread.

- **`setfsuid()` / `setresuid()`** — these are per-thread in Linux (each thread
  has its own credentials). Using them from Go goroutines is unreliable unless
  the goroutine is locked to an OS thread. **Avoid thread-local UID manipulation
  from Go entirely.**

- **C pthreads created by rtapi** — `pthread_create()` in `task_start()` creates
  pure C pthreads. These are NOT Go goroutines and are not affected by Go's
  scheduler. They run independently and can safely use `SCHED_FIFO`, `iopl()`,
  etc.

- **CGO calls** — When Go calls C via CGO, the C code executes on the calling
  goroutine's current OS thread. If that goroutine is locked via
  `runtime.LockOSThread()`, all CGO calls on it run on the same OS thread.

- **`mlockall()`** — Process-wide. Persists after privilege drop. Affects Go's
  memory allocator (all allocations are pre-locked). This increases memory
  footprint but is desirable for RT.

- **`RLIMIT_RTPRIO`** — Process-wide. Once set to unlimited, all threads in the
  process can use `SCHED_FIFO` at any priority, even after privilege drop.

---

## Option A: Linux Ambient Capabilities (Recommended)

### Mechanism

Install **file capabilities** on the `linuxcnc-launcher` binary instead of
setuid root:

```bash
sudo setcap cap_sys_nice,cap_ipc_lock,cap_sys_rawio,cap_sys_resource=eip \
    $(EMC2_BIN_DIR)/linuxcnc-launcher
```

The binary runs as the normal user but inherits the listed capabilities in its
effective and permitted sets on `execve`.

### RT Hardening Sequence

```
linuxcnc-launcher startup:
────────────────────────────────────────────────────────────
1. Process starts with file capabilities in effective set
2. RT-init goroutine: runtime.LockOSThread()
3. RT-init goroutine: CGO → harden_rt()
     - iopl(3)                    [CAP_SYS_RAWIO]
     - setrlimit(RLIMIT_RTPRIO)   [CAP_SYS_RESOURCE]
     - setrlimit(RLIMIT_MEMLOCK)  [CAP_SYS_RESOURCE]
     - mlockall(MCL_CURRENT|MCL_FUTURE) [CAP_IPC_LOCK]
     - open /dev/cpu_dma_latency  [root or group]
     - SCHED_FIFO for RT threads  [CAP_SYS_NICE]
4. dropPrivileges(): cap_set_proc(empty) — clear all capabilities
5. prctl(PR_SET_NO_NEW_PRIVS, 1) — no future privilege escalation
6. REST API, display, HAL orchestration start (unprivileged)
────────────────────────────────────────────────────────────
```

### Install / Permission Setup

During `make setuid` (or install):
```makefile
setcap cap_sys_nice,cap_ipc_lock,cap_sys_rawio,cap_sys_resource=eip \
    $(DESTDIR)$(EMC2_BIN_DIR)/linuxcnc-launcher
```

No `chown root` or `chmod 4750` needed.

### Privilege Separation

After `dropPrivileges()`:
- No thread in the process has any Linux capability.
- `PR_SET_NO_NEW_PRIVS` prevents re-acquisition via `execve` of setuid binaries.
- Go goroutines for REST, display, HAL file parsing run fully unprivileged.

### Pros

- No root at all — principle of least privilege.
- Capabilities are narrowly scoped (only what `harden_rt()` needs).
- `PR_SET_NO_NEW_PRIVS` provides defense-in-depth.
- After privilege drop, the process is equivalent to a normal user process.
- Works cleanly with Go's goroutine model (no per-thread uid manipulation).
- The RT C pthreads (`task_start()`) are unaffected by Go scheduler.
- `mlockall()` and `RLIMIT_RTPRIO` persist after privilege drop (process-wide).
- `iopl()` persists on the locked RT-init OS thread; RT pthreads created from
  rtapi C code will independently set `iopl()` if needed (or inherit from
  parent thread on Linux — needs verification per kernel version).

### Cons

- Requires `setcap` tool during installation (`libcap2-bin` package).
- `CAP_SYS_RAWIO` is powerful; it enables raw I/O port access which is a
  security risk if the binary is compromised before privilege drop. Mitigated
  by dropping it immediately after `harden_rt()`.
- `/dev/cpu_dma_latency` may require root group membership or `udev` rules
  in addition to capabilities.
- Capabilities are cleared on `execve` to non-capability-aware binaries
  (ambient capabilities needed if the launcher `exec`s a setcap binary that
  itself needs capabilities — not a concern here).

### Security Implications

After `dropPrivileges()` + `PR_SET_NO_NEW_PRIVS`:
- Compromise of the REST API or display code cannot re-escalate privileges.
- The attacker is limited to the normal user's permissions.
- SCHED_FIFO RT threads continue but cannot be manipulated further.

---

## Option B: Setuid Root + Early Drop

### Mechanism

The `linuxcnc-launcher` binary is owned by root with setuid bit set
(like current `rtapi_app`). On startup:

1. Capture `euid=0` and `ruid=<user>`.
2. Lock a goroutine to an OS thread via `runtime.LockOSThread()`.
3. Perform all privileged RT setup: `harden_rt()`, `mlockall()`, `iopl()`,
   open `/dev/cpu_dma_latency`.
4. Call `setresuid(ruid, ruid, ruid)` — permanently drop root for the entire
   process.
5. Start unprivileged goroutines for REST, display, etc.

### RT Hardening Sequence

```
linuxcnc-launcher startup (setuid root):
────────────────────────────────────────────────────────────
1. Process starts as euid=0, ruid=<user>
2. setresuid(0, 0, ruid) — keep euid=0, save ruid
3. RT-init goroutine: runtime.LockOSThread()
4. RT-init goroutine: CGO → harden_rt()
5. setresuid(ruid, ruid, ruid) — permanent privilege drop
6. REST API, display, HAL orchestration start (unprivileged)
────────────────────────────────────────────────────────────
```

### Install / Permission Setup

```makefile
chown root $(DESTDIR)$(EMC2_BIN_DIR)/linuxcnc-launcher
chmod 4750 $(DESTDIR)$(EMC2_BIN_DIR)/linuxcnc-launcher
```

### Pros

- Familiar pattern (same as current `rtapi_app`).
- Does not require `libcap` or `setcap`.
- Works on systems without capability support.

### Cons

- **Setuid root is a larger attack surface** than fine-grained capabilities.
  The entire binary starts as root, not just specific operations.
- `setresuid()` is per-thread in Linux. Calling it from a goroutine that is
  NOT locked to an OS thread will only affect that OS thread, not the whole
  process. This means careful use of `runtime.LockOSThread()` is required,
  and the semantics are subtle.
- Go's `syscall.Setresuid()` or CGO call to `setresuid()` from a goroutine
  locked to an OS thread affects only that thread. To affect all threads,
  you need to call it on every OS thread — Go provides no clean API for this.
  **The usual advice is to avoid setuid in Go programs for this reason.**
- `iopl()` is per-thread: even if the main goroutine calls it while locked,
  the RT C pthreads created later may not have `iopl` set. They need to call
  `iopl(3)` themselves (currently `rtapi_app` handles this under
  `with_root_enter()`).

### Security Implications

- Setuid root is more dangerous than capabilities: the entire binary image
  executes initially as root before privilege drop.
- If there is a vulnerability in the startup path before `setresuid()`, an
  attacker gets full root.
- Not recommended for new code.

---

## Option C: Privileged Helper Thread

### Mechanism

One dedicated OS thread retains root (or capabilities) for the lifetime of the
process. All privileged operations are dispatched to this thread via a channel.
All other goroutines run unprivileged.

```
┌─────────────────────────────────────────────────────┐
│  linuxcnc-launcher process                          │
│                                                     │
│  Privileged OS thread (locked)                      │
│  ┌────────────────────────────────────────────────┐ │
│  │ - Retains CAP_SYS_NICE etc.                    │ │
│  │ - Listens on privileged ops channel            │ │
│  │ - Creates SCHED_FIFO pthreads on request       │ │
│  │ - Opens /dev/cpu_dma_latency                   │ │
│  └────────────────────────────────────────────────┘ │
│                                                     │
│  All other goroutines (unprivileged)                │
│  ┌─────────────────┐  ┌────────────────────────┐   │
│  │ REST API        │  │ HAL file execution      │   │
│  └─────────────────┘  └────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

### Implementation

```go
type PrivilegedOp struct {
    fn     func() error
    result chan error
}

var privOpCh = make(chan PrivilegedOp)

func init() {
    go func() {
        runtime.LockOSThread()
        // Keep capabilities on this thread
        for op := range privOpCh {
            op.result <- op.fn()
        }
    }()
}

func doPrivileged(fn func() error) error {
    result := make(chan error, 1)
    privOpCh <- PrivilegedOp{fn: fn, result: result}
    return <-result
}
```

### Pros

- Fine-grained control: only specific operations are privileged.
- The privileged thread exists for the process lifetime, enabling dynamic
  `loadrt` after initial setup (capability not dropped entirely).
- Mirrors the existing `with_root_enter()` / `with_root_exit()` pattern.

### Cons

- **The privileged thread exists for the entire process lifetime** — a
  compromised goroutine can send arbitrary operations to it.
- Channel-based dispatch adds latency and complexity.
- If the privileged thread crashes, privilege operations are lost.
- Harder to reason about security (when is privilege in use?).
- **Does not satisfy PR_SET_NO_NEW_PRIVS** (privilege is retained, not dropped).
- Maintaining a persistent root thread is an unnecessary security risk when
  Options A or B can be used.

### Security Implications

- The privileged thread is a persistent root inside the process.
- Any code path that can write to the `privOpCh` channel can perform privileged
  operations — this is a broad attack surface.
- Not recommended for security-sensitive deployments.

---

## Option D: Capabilities + PR_SET_NO_NEW_PRIVS (Defense-in-Depth)

### Mechanism

This option is a hardening layer applied **on top of** Option A (or B).
After performing RT hardening:

1. Drop all capabilities using `cap_set_proc(cap_init())`.
2. Call `prctl(PR_SET_NO_NEW_PRIVS, 1)` — this prevents any thread from
   ever gaining new privileges via `execve` of setuid/setcap binaries.
3. Optionally, clear the capability bounding set to prevent capability
   re-acquisition via ambient capabilities.

```c
// After harden_rt():
cap_t empty = cap_init();
cap_set_proc(empty);     // clear all capabilities
cap_free(empty);
prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
```

### Pros

- `PR_SET_NO_NEW_PRIVS` is an **irreversible** kernel-enforced guarantee:
  once set, no thread in the process can gain new privileges, ever.
- Provides defense-in-depth: even if the capability drop is somehow bypassed,
  `PR_SET_NO_NEW_PRIVS` prevents escalation via `execve`.
- Protects against confused deputy attacks where a compromised component
  tries to exec a setuid helper.

### Cons

- By itself, this option doesn't describe a full privilege model — it must
  be combined with Option A or B.
- After `PR_SET_NO_NEW_PRIVS`, `halcmd` (if it were setuid) would not gain
  privileges when exec'd by the launcher. This is intentional — `halcmd`
  should not need setuid in the new architecture.

### Security Implications

- `PR_SET_NO_NEW_PRIVS` + empty capability set is the strongest possible
  post-setup privilege state.
- The combination of Option A + D is recommended.

---

## Recommendation: Option A + D

**Use Linux file capabilities (`setcap`) combined with PR_SET_NO_NEW_PRIVS.**

### Rationale

1. **Minimal privilege from the start**: Unlike setuid root (Option B), the
   binary does not start as root. Only the specific capabilities needed for
   `harden_rt()` are granted.

2. **Clean privilege drop**: After RT setup, all capabilities are removed and
   `PR_SET_NO_NEW_PRIVS` is installed. The process becomes indistinguishable
   from a normal user process.

3. **Go-friendly**: No per-thread UID manipulation (`setresuid`). Capabilities
   are set at the file level and managed at the process level, which integrates
   cleanly with Go's goroutine model.

4. **Defense-in-depth**: The combination of empty capability set +
   `PR_SET_NO_NEW_PRIVS` provides the strongest possible post-setup guarantee.

5. **Forward-looking**: File capabilities are the modern Linux approach to
   privilege management. `setcap` is well-supported across all current Linux
   distributions.

### Startup Sequence (Combined A + D)

```
1. execve(linuxcnc-launcher)
   → kernel: file caps set → effective/permitted = {SYS_NICE, IPC_LOCK,
                                                     SYS_RAWIO, SYS_RESOURCE}
   → process starts as normal user, not root

2. RT-init goroutine (locked to OS thread via runtime.LockOSThread()):
   CGO → harden_rt():
     a. iopl(3)                         [CAP_SYS_RAWIO]
     b. setrlimit(RLIMIT_RTPRIO, ∞)     [CAP_SYS_RESOURCE]
     c. setrlimit(RLIMIT_MEMLOCK, ∞)    [CAP_SYS_RESOURCE]
     d. mlockall(MCL_CURRENT|MCL_FUTURE)[CAP_IPC_LOCK]
     e. open /dev/cpu_dma_latency, write 0
     f. prctl(PR_SET_DUMPABLE, 1)

3. Initialize RTAPI subsystem (hal_lib, shmem) — still has capabilities

4. dropPrivileges():
   a. cap_set_proc(cap_init())          → empty capability set
   b. prctl(PR_SET_NO_NEW_PRIVS, 1)    → irreversible no-new-privs

5. From this point: process == normal user, no capabilities

6. Start unprivileged services:
   - REST API (future)
   - HAL file parsing and execution
   - Display orchestration
   - Task controller via halcmd

RT threads (from step 2 / future loadrt calls):
   - Pure C pthreads, SCHED_FIFO (policy set before privilege drop)
   - Not affected by Go scheduler
   - Memory already locked (mlockall persists)
   - RLIMIT_RTPRIO unlimited (persists)
```

### Trade-offs and Notes

- **`mlockall()` and Go's allocator**: `mlockall(MCL_FUTURE)` will lock all
  future memory allocations. Go's garbage collector allocates frequently.
  This increases physical memory footprint (no swapping) but is desirable for
  RT latency. Document this to end users (memory requirements).

- **`iopl()` and RT threads**: On Linux, `iopl()` affects only the calling
  thread's I/O permission bitmap. The RT pthreads created by `task_start()`
  (via `pthread_create`) are new threads and do NOT inherit `iopl()` state from
  the Go OS thread that called `harden_rt()`. The RT thread initialization code
  in `uspace_rtapi_app.c` must call `iopl(3)` in each RT thread's startup
  function. This is already handled in the existing `task_start()` code.

- **Dynamic `loadrt` after privilege drop**: After `dropPrivileges()`, new
  modules can still be `dlopen()`ed (module loading does not require
  capabilities). New RT threads can still be created with `SCHED_FIFO` because
  `RLIMIT_RTPRIO` was already set to unlimited and persists.

- **`/dev/cpu_dma_latency`**: On many systems this requires group `root` or a
  `udev` rule granting group access. A `udev` rule is the preferred approach:
  ```
  KERNEL=="cpu_dma_latency", GROUP="realtime", MODE="0660"
  ```
  The user running `linuxcnc-launcher` should be in the `realtime` group.

---

## Implementation Summary

The `src/launcher/rtapi/` package implements the Go-side scaffolding for this
design:

- **`rtapi.go`**: `Engine` struct with `Init()` (performs privilege setup +
  drop), `Shutdown()`, `LoadModule()`, `UnloadModule()`, `IsRealtime()`.

- **`privilege.go`**: CGO stubs for `hardenRT()` and `dropPrivileges()`.
  The stubs contain detailed comments documenting the exact syscalls/prctls
  the final implementation will make. The actual CGO bridge to
  `uspace_rtapi_app.c` (refactored as a library) is a follow-up task.

- **`src/launcher/realtime/realtime.go`**: Updated `Manager` that wraps the
  `Engine` and delegates `Start()` → `engine.Init()`, `Stop()` →
  `engine.Shutdown()`. Provides `LoadModule()` / `UnloadModule()` for use by
  the launcher.

The `make setuid` target is updated to call `setcap` on `linuxcnc-launcher`
instead of `chown root / chmod 4750` on `rtapi_app`:

```makefile
setuid:
ifeq ($(BUILD_SYS),uspace)
	setcap cap_sys_nice,cap_ipc_lock,cap_sys_rawio,cap_sys_resource=eip \
	    ../bin/linuxcnc-launcher
endif
```

---

## References

- `src/rtapi/uspace_rtapi_app.c` — current `harden_rt()` and privilege model
- `capabilities(7)` — Linux capability overview
- `prctl(2)` — `PR_SET_NO_NEW_PRIVS`, `PR_SET_DUMPABLE`
- `iopl(2)` — I/O privilege level (x86-specific, per-thread)
- `mlockall(2)` — memory locking
- `setrlimit(2)` — resource limits
- Go `runtime.LockOSThread()` — goroutine→OS thread pinning
