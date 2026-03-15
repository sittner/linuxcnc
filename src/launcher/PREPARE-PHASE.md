# Prepare Phase: Launcher → hal-go Transition

This document describes the evolution path from the current halcmd-subprocess
architecture to direct HAL library calls via hal-go (cgo), and identifies
which methods in the launcher are the next candidates for replacement.

---

## Current Architecture

```
launcher.go  ──►  halfile.Executor  ──►  exec.Command("halcmd", ...)  ──►  HAL kernel
                                    ──►  exec.Command("haltcl", ...)  ──►  HAL kernel
launcher.go  ──────────────────────────► exec.Command("halcmd", ...)  ──►  HAL kernel
```

Every HAL operation crosses a process boundary: the Go launcher spawns a
`halcmd` subprocess for each command (or for each HAL file in the legacy
`-f` mode).

---

## Target Architecture

```
launcher.go  ──►  halfile.Executor  ──►  hal-go API  ──►  HAL kernel (direct cgo call)
launcher.go  ──────────────────────────► hal-go API  ──►  HAL kernel (direct cgo call)
```

HAL operations become direct cgo calls via the hal-go package
(`linuxcnc.org/hal`), eliminating subprocess overhead and enabling
richer error propagation.

---

## The Single Swap Point: `executeCommand()`

The `halfile` package now routes all HAL commands through a single bottleneck:

```go
// halfile/halfile.go
func (e *Executor) executeCommand(line string) error
```

Today this method spawns `halcmd <args...>`.  When hal-go is ready, only
this method changes — the rest of the file-reading, substitution, and
backslash-joining logic stays the same.

**Planned replacement**:
```go
func (e *Executor) executeCommand(line string) error {
    line = strings.TrimSpace(line)
    if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
        return nil
    }
    parts := strings.Fields(line)
    // TODO: dispatch parts[0] to hal-go:
    //   "loadrt"  → hal.LoadRT(parts[1], parts[2:]...)
    //   "addf"    → hal.AddF(parts[1], parts[2])
    //   "net"     → hal.Net(parts[1], parts[2:]...)
    //   "setp"    → hal.SetP(parts[1], parts[2])
    //   "sets"    → hal.SetS(parts[1], parts[2])
    //   "loadusr" → hal.LoadUSR(parts[1:]...)
    //   ...
    return dispatchHalCommand(parts)
}
```

---

## Methods in `launcher.go` That Become cgo Calls

The following methods in `launcher/launcher.go` currently spawn `halcmd`
subprocesses and are candidates for replacement with hal-go calls, in order
of priority:

| Method | Current mechanism | hal-go replacement |
|---|---|---|
| `halfile.Executor.executeCommand()` | `halcmd <cmd>` per line | `hal.LoadRT()`, `hal.Net()`, etc. |
| `startHalThreads()` | `halcmd start` | `hal.Start()` |
| `loadRetain()` — loadrt/addf calls | `halcmd loadrt retain` | `hal.LoadRT("retain")`, `hal.AddF(...)` |
| `preloadMotionModules()` | `halcmd loadrt <mod>` | `hal.LoadRT(mod)` |
| `startIOControl()` | `halcmd loadusr -Wn iocontrol` | `hal.LoadUSR(...)` |
| `startHalUI()` | `halcmd loadusr -Wn halui` | `hal.LoadUSR(...)` |
| `startTask()` | `halcmd loadusr -Wn inihal` | `hal.LoadUSR(...)` |
| `doCleanup()` — stop/unload | `halcmd stop`, `halcmd unload all` | `hal.Stop()`, `hal.UnloadAll()` |

TCL HAL files (`.tcl`) are handled by `haltcl` and are **not** in scope for
hal-go replacement; they remain as subprocess calls.

---

## Replacement Order

1. **`executeCommand()`** — highest impact, covers all `.hal` file execution.
   Requires implementing a HAL command dispatcher in Go that maps textual
   HAL commands (`loadrt`, `addf`, `net`, etc.) to hal-go API calls.

2. **`startHalThreads()` / cleanup stop+unload** — simple single-command
   replacements once the hal-go component ID is tracked.

3. **`preloadMotionModules()`** — two `loadrt` calls.

4. **`startIOControl()` / `startHalUI()` / `startTask()`** — `loadusr` with
   wait; hal-go's `LoadUSR` with `-W` semantics needs to be verified.

5. **`loadRetain()`** — mixed `loadrt`/`addf`/`loadusr`; defer until the
   simpler cases above are done.

---

## Prerequisites for hal-go Integration

- hal-go (`linuxcnc.org/hal`) must be available as a module dependency.
- The launcher must initialize a HAL component early in `Run()` and pass the
  component ID (or a `*hal.Component`) into the `halfile.Executor`.
- `executeCommand()` needs a HAL command dispatcher (a ~100-line `switch`
  statement mapping command names to hal-go calls).
- TCL twopass (`twopass.go`) continues to use `haltcl` as a subprocess; this
  path is unaffected by the hal-go transition.

---

## What Is Intentionally Not Changed in This Prepare Phase

| Item | Reason |
|---|---|
| `halfile/resolve.go` — `LIB:` path resolution | Stays as-is; needed regardless of execution backend |
| `halfile/twopass.go` — TWOPASS delegation | Stays until twopass is reimplemented natively |
| `halfile/substitute.go` — line substitution | Stays; needed for `[SECTION]KEY` expansion when halcmd is gone |
| `inifile/expand.go` — `WriteExpanded()` | Stays; subprocesses (iocontrol, task) still need the expanded flat file |
| `launcher/cleanup.go` — shutdown sequence | Stays; will migrate to hal-go incrementally (see table above) |
