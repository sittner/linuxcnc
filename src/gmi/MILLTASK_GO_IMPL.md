# Milltask Go Rewrite — Implementation Plan

## Overview

Rewrite milltask from C++ cmod (~14,350 lines) to a Go gomod. The current
implementation routes all commands through NML message structs and three large
switch statements. The new design eliminates NML entirely — GMI methods on
the Task struct become the direct command handlers.

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
    task.go           // Task struct, state types, dependency interfaces
    guards.go         // requireState(), requireMode(), requireHomed(), ...
    commands.go       // GMI method implementations (27 methods)
    sequencer.go      // interpreter queue execution loop
    canon.go          // canon callback implementations (push QueuedCmd)
    interp.go         // cgo wrapper for interpreter C shim
    ini_config.go     // INI reading at startup (joints, axes, traj, spindles)
    hal_pins.go       // HAL pin creation + periodic update
    task_test.go      // guard + state transition tests
    commands_test.go  // command acceptance/rejection matrix (~100 cases)
    sequencer_test.go // queue execution, abort, error handling
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

### Phase 1: Guard matrix (table-driven)
~100 test cases encoding state×mode×command acceptance/rejection. This is the
safety-critical behavior — wrong guards can cause machine damage.

### Phase 2: State transitions
- estop → estop_reset → on (must follow sequence)
- on → estop (direct, always allowed)
- mode switches clear interpreter queue
- abort behavior from each state

### Phase 3: Sequencer
- Queue N commands, verify sequential execution with correct waits
- Abort clears queue and stops current wait
- Error propagation stops execution

### Phase 4: Canon → QueuedCmd
- Each canon callback produces the correct QueuedCmd type
- Coordinate transforms (unit conversion, offsets, rotation)
- Segment chaining for naive CAM

## Effort Estimate

| Subsystem | Lines (current) | Approach | Effort |
|-----------|----------------|----------|--------|
| State machine | 3800 | Redesign as methods | Medium |
| Motion interface | 2032 | Direct motctl calls | Small |
| Canon logic | 3530 | Port math to Go | Medium-Large |
| Canon table | 607 | Eliminated (Go implements directly) | — |
| Task utils | 659 | Thin interp cgo shim | Small |
| IO interface | 283 | Direct emcio calls | Small |
| Command handlers | 351 | Eliminated (methods are handlers) | — |
| Command slot | 78 | Eliminated (direct calls) | — |
| INI config | 631 | inifile.IniFile | Small |
| HAL pins | 444 | gomc HAL package | Small |
| interp_list | 204 | Go channel/slice | — |
| rcs_shim/linklist/etc | 693 | Eliminated (Go stdlib) | — |

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
