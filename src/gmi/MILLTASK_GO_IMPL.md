# Milltask Go Rewrite — Implementation Plan

## Current Status (2026-05-23)

Integration test: **58 pass, 0 fail, 7 xfail** (configs/sim/test/tasktest.ini)

### Done
- ✅ Task struct + dependency interfaces (motctl, emcio, motstat clients)
- ✅ INI config loading (loadConfig → motctl calls for traj/joint/axis/spindle)
- ✅ HAL pins (inihal component — runtime INI parameter override)
- ✅ Module registration (`gomc.RegisterModule("milltask", factory)`)
- ✅ Lifecycle fix: API lookups in Start() (not factory/New)
- ✅ Integration: loads via `load milltask` in lib/hallib/linuxcnc.hal
- ✅ Launcher cleanup: no more hasTask special handling
- ✅ emccmd API (27 command handlers) — registered for C callers (halui) and WS
- ✅ emcstat API (GetStat → StatFull) — registered + WebSocket watch with delta push
- ✅ State machine (estop/estop_reset/off/on transitions)
- ✅ Mode switching (manual/auto/mdi) with guards
- ✅ All 27 emccmd handlers implemented (commands.go)
- ✅ Interpreter integration (CInterp wrapping librs274.so via C shim)
- ✅ Canon callbacks in Go (straight_traverse, straight_feed, arc_feed, dwell, spindle, coolant, tool-length, offsets)
- ✅ Sequencer goroutine (executes QueuedCmd from interpQueue)
- ✅ Readahead with backpressure (waitSequencerDrain on EXECUTE_FINISH)
- ✅ Pause/resume with channel signaling
- ✅ Program run (goroutine reads interpreter lines, enqueues canon commands)
- ✅ MDI execution (single command — synch, execute, interpDoneCmd)
- ✅ Continuous jog + jog stop
- ✅ Homing / unhoming (delegates to motctl)
- ✅ Spindle on/off/increase/decrease
- ✅ Coolant flood/mist on/off
- ✅ Overrides: feed, spindle, rapid, max velocity
- ✅ Teleop enable/disable, override limits
- ✅ Optional stop, block delete flags
- ✅ Position logger (poslog.go — ring buffer + WS push)
- ✅ Tools REST API (tooldata shim + GET/PUT/DELETE endpoints)
- ✅ CGO bridge error propagation (all exports return -1 on error)
- ✅ ProgramOpen works in any state/mode (matches C milltask)

### XFAILs (known issues, not regressions)
1. **jog/incremental** — wrong distance (units/scale bug in motctl or motion)
2. **homing/unhome** — homed flag not clearing in motstat after unhome
3. **program/step** — not implemented (TODO in AutoCommand)
4. **program/run_requires_file** — interpreter retains file from previous test
5. **spindle/forward+reverse** — spindle enabled flag not reflected in motstat
6. **misc/load_tool_table** — not implemented (returns errNotReady)

### Pending Work — Priority Order

#### Tier 1: Breaks real usage
| # | Item | Description | Effort |
|---|------|-------------|--------|
| 1 | Abort cleanup | Stop spindle, coolant off, clear interp queue, IO abort | Small |
| 2 | Tool change cycle | M6 canon → ToolPrepare → wait IO → ToolLoad → update offsets | Medium |
| 3 | M-code handler worker | Goroutine for M100-199, abort-aware (eventfd/channel) | Medium |
| 4 | MDI queue | Buffer multiple MDI commands, abort mid-queue | Small |
| 5 | Load tool table | Reload from file, notify interpreter | Small |
| 6 | NO_FORCE_HOMING | Block MDI/AUTO run if not all homed (unless INI override) | Small |
| 7 | Single step | AutoStep reads one line, pauses before next | Small |

#### Tier 2: Stat accuracy (UI shows wrong values)
| # | Item | Description |
|---|------|-------------|
| 8 | Line tracking | Set currentLine/readLine/motionLine from interp + sequencer |
| 9 | Active G/M codes | Read from interpreter after each line, publish in stat |
| 10 | Spindle state | Read spindle direction/enabled from motstat properly |
| 11 | Unhome flag | Ensure motstat reflects unhome (may be motion-side bug) |
| 12 | Interp state on program end | Properly reset to IDLE after M2/M30 completes |

#### Tier 3: Edge cases / advanced
| # | Item | Description |
|---|------|-------------|
| 13 | Incremental jog fix | Debug units/scale in jog_incr path |
| 14 | Task plan synch | Sync interpreter position with motion actual |
| 15 | Wait complete | Poll execState until ExecDone or timeout |
| 16 | Readahead exec states | WAITING_FOR_IO, WAITING_FOR_DELAY, SPINDLE_ORIENT |
| 17 | Probe result | Publish probed_position in stat from motstat |
| 18 | Operator error ring | Push errors to emcerror ring (not just slog) |
| 19 | Feed hold / adaptive feed | Support motion adaptive feed override |
| 20 | Program end rewind | Reset interpreter to line 0 on M2/M30 |

### Milestone targets
- **Tier 1 complete** → usable for basic machining with Axis UI
- **Tier 1+2 complete** → UI shows correct state, suitable for daily use
- **All tiers** → full parity with C milltask, can delete cmod/milltask.so

## Overview

Rewrite milltask from C++ cmod (~3,580 lines in emctaskmain_gomc.cc) to a Go
gomod. The C version routes commands through NML message structs and three
large switch statements. The Go design eliminates NML — GMI methods on the
Task struct are the direct command handlers.

The Go milltask is already the default (loaded via `load milltask` in
linuxcnc.hal). The old C milltask.so exists only as fallback reference.

## Architecture

```
UI (Go/REST/WebSocket)
  │
  ▼
emccmd GMI method → Task.Jog() / Task.Home() / ...
  │
  ├─ guard check: requireState(On), requireMode(Manual), ...
  ├─ execute: t.motctl.JogCont(...)
  └─ return error or nil
```

For interpreter-generated commands (AUTO/MDI):

```
Interpreter (C++ librs274.so via thin C shim)
  │  calls canon callbacks
  ▼
Canon (Go) → pushes QueuedCmd to interpQueue
  │
  ▼
Sequencer goroutine:
  for cmd := range t.interpQueue {
      cmd.Execute(t)
      t.waitFor(cmd.WaitKind())
  }
```

## Key Design Decisions

### 1. No NML message types

The ~80 NML structs used as internal command tokens are replaced by:
- **UI commands**: direct method calls on `Task` struct (27 emccmd GMI methods)
- **Interpreter commands**: typed `QueuedCmd` interface values (~20 concrete types)

### 2. No big switch statements

Current `emcTaskPlan()` (720 lines, state×mode×command filter) becomes guard
methods called at the top of each Task method:

```go
func (t *Task) Jog(...) error {
    if err := t.requireState(StateOn); err != nil { return err }
    if err := t.requireMode(ModeManual); err != nil { return err }
    return t.motctl.JogCont(...)
}
```

Current `emcTaskIssueCommand()` (770 lines, type→function dispatch) is
eliminated — each GMI method IS the dispatch.

### 3. Canon callbacks implemented in Go

The `canon_callbacks_t` vtable (from canon.gmi) already supports Go
implementations. The interpreter calls canon through C function pointers;
these point into Go via cgo exports. No C++ canon code needed.

### 4. INI via inifile.IniFile

Go modules receive `*inifile.IniFile` directly in their Factory. No custom
INI parsing — use `ini.Get("TRAJ", "COORDINATES")` etc.

### 5. Interpreter stays C++

A thin C shim (~100 lines) wraps `InterpBase*` virtual calls:
- `interp_init()`, `interp_open(file)`, `interp_read()`
- `interp_execute(cmd)`, `interp_synch()`, `interp_close()`

The shim is the ONLY C++ code in the Go milltask.

## Package Structure

```
src/gomc/internal/task/
    task.go            // Task struct, state types, dependency interfaces
    guards.go          // requireOn(), requireMode(), requireInterpIdle(), ...
    commands.go        // 27 emccmd method implementations
    sequencer.go       // interpreter queue execution loop (goroutine)
    canon.go           // canon callback implementations (push QueuedCmd)
    interp.go          // cgo wrapper for interpreter C shim (CInterp)
    module.go          // gomc.Module lifecycle (factory, Start, Stop, Destroy)
    api_provider.go    // EmccmdCallbacks + EmcstatCallbacks implementations
    api_cbridge.go     // CGO //export functions for C callers (halui)
    ini_config.go      // INI reading at startup (joints, axes, traj, spindles)
    hal_pins.go        // inihal HAL component (runtime parameter override)
    stat.go            // GetStat() — fills StatFull from motstat + internal state
    watches.go         // WebSocket watch registration (emcstat, poslogger)
    poslog.go          // Position logger (ring buffer, WS push)
    tools.go           // Tool table REST endpoints
    task_test.go       // Unit tests (guards, state transitions, commands)
```

## Dependency Interfaces (mockable for tests)

```go
type MotionController interface {
    JogCont(joint int, vel float64, jjogmode int) error
    HomeJoint(joint int) error
    TrajLinearMove(...) error
    TrajCircularMove(...) error
    TrajAbort() error
    // ... (matches motctl GMI)
}

type IOController interface {
    FloodOn() error
    FloodOff() error
    MistOn() error
    MistOff() error
    ToolPrepare(pocket, tool int) error
    ToolChange() error
    // ... (matches emcio GMI)
}

type Interpreter interface {
    Init() error
    Open(file string) error
    Read() (int, error)
    Execute(cmd string) (int, error)
    Synch() error
    Close() error
}
```

## QueuedCmd Interface

```go
type WaitType int
const (
    WaitNone WaitType = iota
    WaitMotion
    WaitIO
    WaitMotionAndIO
    WaitDelay
    WaitSpindleOriented
)

type QueuedCmd interface {
    Execute(t *Task) error
    WaitKind() WaitType
}
```

Concrete types: LinearMove, CircularMove, SpindleOn, SpindleOff,
ToolPrepare, ToolChange, Dwell, SetOffset, Probe, RigidTap, etc.

## Guard Matrix (from emcTaskPlan)

The acceptance rules extracted from the current implementation:

### Always accepted (any state, any mode)
- set_state, set_mode, abort, set_debug, set_optional_stop, set_block_delete
- set_feed_override, set_spindle_override, set_rapid_override, set_max_velocity

### Requires StateOn + ModeManual
- jog, jog_stop, home, unhome, override_limits

### Requires StateOn + ModeAuto
- auto_cmd(RUN) — also requires: program loaded, not external_offset_applied
- auto_cmd(PAUSE/RESUME/STEP/REVERSE/FORWARD)

### Requires StateOn + ModeMDI
- mdi

### Requires StateOn (any mode)
- spindle, flood, mist, brake, lube, teleop_enable

### Special: jog in AUTO/MDI
- jog/jog_stop accepted in AUTO IDLE and MDI IDLE (allow_while_idle_type)

### Queued (go through IO)
- load_tool_table

## Test Strategy

### Unit tests (src/gomc/internal/task/task_test.go)
- Guard matrix: state×mode×command acceptance/rejection
- State transitions: estop→on sequence, idempotency
- ProgramOpen: works in any mode/state
- Run with mock interpreter + mock motion

### Integration tests (configs/sim/test/tasktest.ini)
Python test harness that starts the full system (gomc-server + sim HAL config)
and exercises all commands via REST API with assertion on stat changes.

Categories: state, mode, motion, jog, homing, program, mdi, spindle, coolant,
override, option, abort, misc. Currently 65 tests (58 pass, 7 xfail).

### How to run
```bash
# Unit tests
cd src/gomc && LD_LIBRARY_PATH=../../lib go test ./internal/task/

# Integration tests
./scripts/linuxcnc configs/sim/test/tasktest.ini

# Build
cd src && make ../bin/gomc-server
```

## Effort Estimate (remaining work)

| Subsystem | Status | Effort |
|-----------|--------|--------|
| Abort cleanup (spindle/coolant/IO/queue) | Not started | Small |
| Tool change cycle (M6) | Not started | Medium |
| M-code handler worker (M100-199) | Not started | Medium |
| MDI queue | Not started | Small |
| Line tracking + active G/M codes | Not started | Small |
| NO_FORCE_HOMING + load_tool_table | Not started | Small |
| Single step | Not started | Small |
| Stat fixes (spindle/unhome/probe) | Not started | Small |
| Incremental jog fix | Not started | Small (debug) |
| Operator error ring push | Not started | Small |

Total remaining: ~800-1200 lines of Go code for full C milltask parity.

## What Gets Eliminated Entirely

- NML message types (~80 structs with type discriminants)
- emcTaskPlan() nested switch (720 lines)
- emcTaskIssueCommand() switch (770 lines)
- emccmd_handlers.cc (351 lines — NML struct construction)
- emccmd_slot.cc (78 lines — condvar handoff)
- interp_list linked list (204 lines)
- rcs_shim.cc (131 lines)
- linklist.cc (~200 lines)
- emc_symbol_lookup.cc (297 lines)
- backtrace.cc (65 lines)
- canon_position.cc (278 lines — becomes Go struct)
- emccanon_table.cc (607 lines — Go implements canon directly)

## References

- `src/gmi/idl/emccmd.gmi` — 27 UI command methods
- `src/gmi/idl/canon.gmi` — canon callback interface (Go binding exists)
- `src/gmi/idl/motctl.gmi` — motion controller commands
- `src/gmi/idl/motstat.gmi` — motion status readback
- `src/gmi/idl/ini.gmi` — INI query API
- `src/gomc/pkg/inifile/` — pure Go INI parser
- `src/emc/task/emctaskmain_gomc.cc` — current implementation (reference)
- `src/emc/rs274ngc/canon_interface.hh` — interpreter's canon vtable wrapper
