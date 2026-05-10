# Milltask Cleanup: Remove Python, Enable Multi-Instance Interpreter

## Goal

Remove all Python/Boost.Python/CPython dependencies from milltask and the
RS274NGC interpreter. Replace external process execution (fork/exec for M1xx)
with cmod/gomod registration. Make the interpreter multi-instance capable by
eliminating global state and introducing a canon callback table.

## Motivation

- CPython GIL creates deadlocks when milltask and pyproxy share the runtime
- Boost.Python bindings deeply couple the interpreter to a single process context
- fork() for M100-M199 won't work without shared memory
- Multi-instance interpreter is required for concurrent G-code preview/execution
- Python extension features (remap, oword, task override) are used by <5% of configs

## Current State

### Python Extension Points in Milltask

| Feature | Where | What it does |
|---------|-------|-------------|
| PythonPlugin singleton | `rs274ngc_pre.cc` Interp ctor | Always initialized, even without `[PYTHON]` |
| Boost.Python modules | `interpmodule.cc`, `taskmodule.cc`, `emccanon.cc` | Expose ~100+ internals to Python |
| Remap prolog/body/epilog | `interp_o_word.cc`, `interp_python.cc` | Custom G/M code handlers via Python |
| Python O-word subs | `interp_o_word.cc` | O\<name\> call dispatches to Python |
| Python Task override | `taskclass.cc`, `taskmodule.cc` | Override tool change, coolant, etc. |
| Inline `(py,...)` | `interp_convert.cc` | Execute Python from G-code comments |
| PLUGIN\_CALL | `emccanon.cc`, `taskclass.cc` | Queue Python for task context (unused) |
| IO\_PLUGIN\_CALL | `emccanon.cc`, `taskclass.cc` | Queue Python for IO context (unused) |

### External Process Execution

| Mechanism | Where | How |
|-----------|-------|-----|
| M100-M199 | `emctask.cc`, `emctaskmain_gomc.cc` | `fork()+execvp()`, async `waitpid` polling |
| EMC\_SYSTEM\_CMD | `emctaskmain_gomc.cc` | NML message → same fork/exec (only for M1xx) |
| POSTTASK\_HALFILE | `taskclass.cc` | `vfork()+execlp("halcmd")`, synchronous |

### Multi-Instance Blockers (~35 globals)

| Category | Variables | Fix |
|----------|-----------|-----|
| Canon state | `static CanonConfig_t canon`, `_tag`, `quat`, `chained_points`, `probefile`, `logfile` | Move into canon context struct |
| Canon API | ~110 free functions accessing globals | Replace with callback table |
| Command queue | `interp_list` (global NML queue) | Per-instance via canon context |
| Machine status | `emcStatus` pointer | Per-instance via canon context |
| Interpreter | `pinterp`, `_is` (setup pointer) | Already per-instance in `_setup` |
| Python plugin | `python_plugin` singleton | Remove entirely |
| NURBS state | `nurbs_order`, `nurbs_control_points` | Move into `_setup` |
| Error buffer | `savedError` | Move into `_setup` |
| M-code dirs | `user_defined_fmt[]`, `user_defined_function_dirindex[]` | Replace with registration |
| Tool table | `the_table` (global) | Per-instance via canon context |

## Architecture

### Implementation via GMI

All inter-module APIs (`interp_canon_t`, `interp_ext_t`, `task_ext_t`) are
defined as GMI interfaces. The GMI code generator produces:
- C struct definitions (callback tables)
- Go wrapper types with idiomatic method signatures
- Marshalling code for cgo boundary crossing
- Version-tagged structs for ABI compatibility

This ensures consistent interface evolution and eliminates hand-written
cgo boilerplate.

### Two Separate Extension APIs

The interpreter and task have different lifecycles and scopes. Separate APIs
enforce that the interpreter can be used standalone (preview, simulation)
without any task dependency.

```
┌─────────────────────────────────────────────────────────┐
│ gomc-server                                             │
│                                                         │
│  ┌──────────────────────┐   ┌────────────────────────┐  │
│  │ interp_canon_t       │   │ task_ext_t             │  │
│  │ (canon callback tbl) │   │ (task method overrides)│  │
│  │                      │   │                        │  │
│  │ Per-instance:        │   │ Singleton per milltask:│  │
│  │ - motion commands    │   │ - tool_prepare         │  │
│  │ - state queries      │   │ - tool_change          │  │
│  │ - IO control         │   │ - coolant_mist/flood   │  │
│  │ - interp_list ref    │   │ - lube                 │  │
│  │ - emcStatus ref      │   │ - estop                │  │
│  └──────┬───────────────┘   │ - mcode_handler[100]   │  │
│         │                   └────────────────────────┘  │
│         ▼                                               │
│  ┌──────────────────────┐                               │
│  │ interp_ext_t         │                               │
│  │ (extension callbacks)│                               │
│  │                      │                               │
│  │ Per-instance:        │                               │
│  │ - remap prolog/epilog│                               │
│  │ - remap body         │                               │
│  │ - oword handler      │                               │
│  │ - inline ext handler │                               │
│  └──────────────────────┘                               │
│                                                         │
│  cmod/gomod register handlers via gomc_interp_ext_t     │
│  and gomc_task_ext_t in cmod_env_t                      │
└─────────────────────────────────────────────────────────┘
```

### interp\_canon\_t — Canon Callback Table

Replaces the ~110 free functions in `canon.hh`. Each Interp instance receives
its own `interp_canon_t` at construction. The table carries a `void *ctx` that
the implementation uses to reach its private state (CanonConfig, interp\_list,
emcStatus, etc.).

```c
typedef struct interp_canon {
    void *ctx;

    // --- Motion ---
    void (*straight_traverse)(void *ctx, int lineno,
                               double x, double y, double z,
                               double a, double b, double c,
                               double u, double v, double w);
    void (*straight_feed)(void *ctx, int lineno,
                           double x, double y, double z,
                           double a, double b, double c,
                           double u, double v, double w);
    void (*arc_feed)(void *ctx, int lineno,
                      double first_end, double second_end,
                      double first_axis, double second_axis,
                      int rotation, double axis_end_point,
                      double a, double b, double c,
                      double u, double v, double w);
    void (*rigid_tap)(void *ctx, int lineno,
                       double x, double y, double z, double scale);
    void (*straight_probe)(void *ctx, int lineno,
                            double x, double y, double z,
                            double a, double b, double c,
                            double u, double v, double w,
                            unsigned char probe_type);
    void (*stop)(void *ctx);
    void (*dwell)(void *ctx, double seconds);
    void (*finish)(void *ctx);

    // --- Feed/motion control ---
    void (*set_feed_rate)(void *ctx, double rate);
    void (*set_feed_mode)(void *ctx, int spindle, int mode);
    void (*set_motion_control_mode)(void *ctx, int mode, double tolerance);
    void (*set_naivecam_tolerance)(void *ctx, double tolerance);
    void (*set_traverse_rate)(void *ctx, double rate);

    // --- Coordinate system ---
    void (*set_g5x_offset)(void *ctx, int origin,
                            double x, double y, double z,
                            double a, double b, double c,
                            double u, double v, double w);
    void (*set_g92_offset)(void *ctx,
                            double x, double y, double z,
                            double a, double b, double c,
                            double u, double v, double w);
    void (*set_xy_rotation)(void *ctx, double t);
    void (*use_length_units)(void *ctx, int units);
    void (*select_plane)(void *ctx, int plane);

    // --- Cutter compensation ---
    void (*set_cutter_radius_compensation)(void *ctx, double radius);
    void (*start_cutter_radius_compensation)(void *ctx, int direction);
    void (*stop_cutter_radius_compensation)(void *ctx);

    // --- Speed-feed sync ---
    void (*start_speed_feed_synch)(void *ctx, int spindle,
                                    double feed_per_rev, int vel_mode);
    void (*stop_speed_feed_synch)(void *ctx);

    // --- Spindle ---
    void (*set_spindle_mode)(void *ctx, int spindle, double mode);
    void (*set_spindle_speed)(void *ctx, int spindle, double rpm);
    void (*start_spindle_cw)(void *ctx, int spindle, int wait);
    void (*start_spindle_ccw)(void *ctx, int spindle, int wait);
    void (*stop_spindle)(void *ctx, int spindle);
    void (*orient_spindle)(void *ctx, int spindle,
                            double orientation, int mode);
    void (*wait_spindle_orient_complete)(void *ctx, int spindle,
                                         double timeout);

    // --- Tool ---
    void (*select_tool)(void *ctx, int tool);
    void (*start_change)(void *ctx);
    void (*change_tool)(void *ctx, int slot);
    void (*change_tool_number)(void *ctx, int number);
    void (*reload_tooldata)(void *ctx);
    void (*set_tool_table_entry)(void *ctx, int pocket, int toolno,
                                  double ox, double oy, double oz,
                                  double oa, double ob, double oc,
                                  double ou, double ov, double ow,
                                  double diameter,
                                  double frontangle, double backangle,
                                  int orientation);
    void (*use_tool_length_offset)(void *ctx,
                                    double x, double y, double z,
                                    double a, double b, double c,
                                    double u, double v, double w);

    // --- Coolant ---
    void (*flood_on)(void *ctx);
    void (*flood_off)(void *ctx);
    void (*mist_on)(void *ctx);
    void (*mist_off)(void *ctx);

    // --- Overrides ---
    void (*enable_feed_override)(void *ctx);
    void (*disable_feed_override)(void *ctx);
    void (*enable_speed_override)(void *ctx, int spindle);
    void (*disable_speed_override)(void *ctx, int spindle);
    void (*enable_feed_hold)(void *ctx);
    void (*disable_feed_hold)(void *ctx);
    void (*enable_adaptive_feed)(void *ctx);
    void (*disable_adaptive_feed)(void *ctx);

    // --- IO digital/analog ---
    void (*set_motion_output_bit)(void *ctx, int index);
    void (*clear_motion_output_bit)(void *ctx, int index);
    void (*set_aux_output_bit)(void *ctx, int index);
    void (*clear_aux_output_bit)(void *ctx, int index);
    void (*set_motion_output_value)(void *ctx, int index, double value);
    void (*set_aux_output_value)(void *ctx, int index, double value);
    int  (*wait_input)(void *ctx, int index, int input_type,
                       int wait_type, double timeout);

    // --- Clamping ---
    void (*clamp_axis)(void *ctx, int axis);
    void (*unclamp_axis)(void *ctx, int axis);
    int  (*lock_rotary)(void *ctx, int lineno, int joint);
    int  (*unlock_rotary)(void *ctx, int lineno, int joint);

    // --- Program flow ---
    void (*program_stop)(void *ctx);
    void (*optional_program_stop)(void *ctx);
    void (*program_end)(void *ctx);
    void (*pallet_shuttle)(void *ctx);

    // --- Messages/logging ---
    void (*comment)(void *ctx, const char *s);
    void (*message)(void *ctx, const char *s);
    void (*log_msg)(void *ctx, const char *s);
    void (*logopen)(void *ctx, const char *s);
    void (*logappend)(void *ctx, const char *s);
    void (*logclose)(void *ctx);
    void (*canon_error)(void *ctx, const char *msg);

    // --- Probe ---
    void (*turn_probe_on)(void *ctx);
    void (*turn_probe_off)(void *ctx);

    // --- Block delete / optional stop ---
    void (*set_block_delete)(void *ctx, int enabled);
    int  (*get_block_delete)(void *ctx);
    void (*set_optional_program_stop)(void *ctx, int enabled);
    int  (*get_optional_program_stop)(void *ctx);

    // --- State tag ---
    void (*update_tag)(void *ctx, int fields[16]);

    // --- Parameter file ---
    void (*set_parameter_file_name)(void *ctx, const char *name);
    void (*on_reset)(void *ctx);
    void (*canon_update_end_point)(void *ctx,
                                    double x, double y, double z,
                                    double a, double b, double c,
                                    double u, double v, double w);

    // --- Getters (interpreter reads machine state) ---
    double (*get_external_feed_rate)(void *ctx);
    double (*get_external_traverse_rate)(void *ctx);
    int    (*get_external_length_unit_type)(void *ctx);
    double (*get_external_length_units)(void *ctx);
    double (*get_external_angle_units)(void *ctx);
    int    (*get_external_motion_control_mode)(void *ctx);
    double (*get_external_motion_control_tolerance)(void *ctx);
    double (*get_external_motion_control_naivecam_tolerance)(void *ctx);
    int    (*get_external_flood)(void *ctx);
    int    (*get_external_mist)(void *ctx);
    void   (*get_external_position)(void *ctx, double *pos);  // 9-element array
    void   (*get_external_probe_position)(void *ctx, double *pos);
    double (*get_external_probe_value)(void *ctx);
    int    (*get_external_probe_tripped_value)(void *ctx);
    double (*get_external_speed)(void *ctx, int spindle);
    int    (*get_external_spindle)(void *ctx, int spindle);
    void   (*get_external_tool_length_offset)(void *ctx, double *off);
    int    (*get_external_tool_slot)(void *ctx);
    int    (*get_external_selected_tool_slot)(void *ctx);
    int    (*get_external_tool_table)(void *ctx, int pocket,
                                      int *toolno, double *offset,
                                      double *diameter,
                                      double *frontangle,
                                      double *backangle,
                                      int *orientation);
    int    (*get_external_digital_input)(void *ctx, int index, int def);
    double (*get_external_analog_input)(void *ctx, int index, double def);
    int    (*get_external_queue_empty)(void *ctx);
    int    (*get_external_axis_mask)(void *ctx);
    int    (*get_external_feed_override_enable)(void *ctx);
    int    (*get_external_spindle_override_enable)(void *ctx, int spindle);
    int    (*get_external_adaptive_feed_enable)(void *ctx);
    int    (*get_external_feed_hold_enable)(void *ctx);
    int    (*get_external_plane)(void *ctx);
    void   (*get_external_parameter_file_name)(void *ctx,
                                                char *buf, int max);
    int    (*get_external_tc_fault)(void *ctx);
    int    (*get_external_tc_reason)(void *ctx);
    int    (*get_external_offset_applied)(void *ctx);
    void   (*get_external_offsets)(void *ctx, double *offsets);

} interp_canon_t;
```

Enum parameters (`CANON_UNITS`, `CANON_PLANE`, etc.) are passed as `int` in the
C table to avoid header coupling. Constants defined in a shared header.

Complex struct parameters (`EmcPose`, `StateTag`) are flattened to primitive
arrays. `EmcPose` → 9 doubles. `StateTag` → `int fields[16]`.

`NURBS_FEED` and the NURBS math helpers are interpreter-internal (they compute
line segments and call `straight_feed`). They do NOT go in the canon table.

`PLUGIN_CALL` and `IO_PLUGIN_CALL` are removed (dead feature).

`USER_DEFINED_FUNCTION_ADD` is removed (replaced by task\_ext registration).

### interp\_ext\_t — Interpreter Extension API

Registered by cmod/gomod. The interpreter dispatches to these instead of Python.

```c
// Words parsed from a G-code block, passed to extension callbacks
typedef struct {
    unsigned int flags;     // bitmask: which words are present
    double x, y, z, a, b, c, u, v, w;
    double i, j, k;
    double p, q, r, l;
    double e, f, s, d, h;
    int    t_number;
    int    line_number;
} interp_block_words_t;

// Interpreter context passed to extension callbacks
typedef struct {
    void *interp;   // opaque interpreter handle

    // Read/write interpreter parameters (#1, #<named>, etc.)
    double (*get_param)(void *interp, const char *name);
    int    (*set_param)(void *interp, const char *name, double val);

    // Tool table queries
    int (*find_tool_pocket)(void *interp, int tool_number);

    // Error reporting
    void (*set_error)(void *interp, const char *msg);

    // Canon access (same table the interpreter uses)
    const interp_canon_t *canon;

    // Resume phase counter (0 = first call, 1+ = after INTERP_EXECUTE_FINISH)
    int phase;
} interp_ext_ctx_t;

// Return values (match existing INTERP_OK/ERROR/EXECUTE_FINISH)
#define INTERP_EXT_OK              0
#define INTERP_EXT_ERROR          -1
#define INTERP_EXT_EXECUTE_FINISH  3  // pause, flush motion, call again

// --- Per-type registration functions ---

// O-word sub: O<name> call [#1] [#2] ...
typedef int (*interp_oword_fn)(interp_ext_ctx_t *ctx,
                                const char *name,
                                const double *args, int n_args,
                                double *retval);

// Remap prolog: validate args, set params before body
typedef int (*interp_remap_prolog_fn)(interp_ext_ctx_t *ctx,
                                       const interp_block_words_t *words);

// Remap body: the handler itself (alternative to NGC sub)
typedef int (*interp_remap_body_fn)(interp_ext_ctx_t *ctx,
                                     const interp_block_words_t *words);

// Remap epilog: commit results after NGC body returns
typedef int (*interp_remap_epilog_fn)(interp_ext_ctx_t *ctx,
                                       double return_value,
                                       int value_returned);

// Inline extension: (ext, name args) in G-code comments
typedef int (*interp_inline_fn)(interp_ext_ctx_t *ctx,
                                 const char *args);
```

Registration is done through a GMI interface, not direct function calls.
The cmod/gomod registers handlers by name:

```c
typedef struct gomc_interp_ext {
    void *ctx;

    int (*register_oword)(void *ctx, const char *name,
                           interp_oword_fn fn, void *user);
    int (*register_remap_prolog)(void *ctx, const char *name,
                                  interp_remap_prolog_fn fn, void *user);
    int (*register_remap_body)(void *ctx, const char *name,
                                interp_remap_body_fn fn, void *user);
    int (*register_remap_epilog)(void *ctx, const char *name,
                                  interp_remap_epilog_fn fn, void *user);
    int (*register_inline)(void *ctx, const char *name,
                            interp_inline_fn fn, void *user);
} gomc_interp_ext_t;
```

The `void *user` is stored alongside the function pointer and passed back
through `interp_ext_ctx_t` (or a separate field) so the cmod/gomod can
maintain its own state.

### task\_ext\_t — Task Extension API

Replaces Python Task method overrides and M100-M199 fork/exec.

Task method overrides (tool change, coolant, etc.) are **synchronous** — they
run inline in the task loop and return immediately. These are fast operations
that set HAL pins or send NML commands.

M100-M199 handlers run on a **separate thread** with an `abort_fd` for clean
cancellation. This keeps the task loop responsive for status updates, abort
processing, and estop handling while the M-code executes.

```c
typedef struct {
    void *task;     // opaque task handle

    // Access to emcStatus fields needed by task callbacks
    // (specific accessor functions TBD based on actual usage)
} task_ext_ctx_t;

// Task method override callbacks (synchronous, run in task loop)
typedef int (*task_tool_prepare_fn)(task_ext_ctx_t *ctx,
                                     int tool, int pocket);
typedef int (*task_tool_change_fn)(task_ext_ctx_t *ctx, int pocket);
typedef int (*task_coolant_fn)(task_ext_ctx_t *ctx, int on);
typedef int (*task_lube_fn)(task_ext_ctx_t *ctx, int on);
typedef int (*task_estop_fn)(task_ext_ctx_t *ctx, int on);
typedef int (*task_io_init_fn)(task_ext_ctx_t *ctx);
typedef int (*task_io_halt_fn)(task_ext_ctx_t *ctx);

// M100-M199 handler context (passed to handler on its own thread)
typedef struct {
    int    abort_fd;    // eventfd, becomes readable on abort/estop
    int    mcode;       // the M-code number (100-199)
    double p;           // P argument from G-code
    double q;           // Q argument from G-code
    double result;      // handler writes result here (read by task)
    void  *user;        // per-registration user data
} task_mcode_ctx_t;

// M-code handler — runs on its own thread, must poll abort_fd.
// Returns 0 = success, -1 = error, -2 = aborted.
typedef int (*task_mcode_fn)(task_mcode_ctx_t *ctx);

typedef struct gomc_task_ext {
    void *ctx;

    int (*register_tool_prepare)(void *ctx, task_tool_prepare_fn fn,
                                  void *user);
    int (*register_tool_change)(void *ctx, task_tool_change_fn fn,
                                 void *user);
    int (*register_coolant_mist)(void *ctx, task_coolant_fn fn,
                                  void *user);
    int (*register_coolant_flood)(void *ctx, task_coolant_fn fn,
                                   void *user);
    int (*register_lube)(void *ctx, task_lube_fn fn, void *user);
    int (*register_estop)(void *ctx, task_estop_fn fn, void *user);
    int (*register_io_init)(void *ctx, task_io_init_fn fn, void *user);
    int (*register_io_halt)(void *ctx, task_io_halt_fn fn, void *user);

    // Register handler for specific M-code (100-199)
    int (*register_mcode)(void *ctx, int mcode, task_mcode_fn fn,
                           void *user);
} gomc_task_ext_t;
```

#### M-code Execution Flow

```
1. Interpreter reads M1xx → queues internal mcode command on interp_list
2. Task loop dequeues command, looks up registered handler for mcode N
3. If no handler registered → error "M1xx: no handler registered"
4. Task creates eventfd (abort_fd), populates task_mcode_ctx_t
5. Task spawns thread calling handler(ctx)
6. Task enters WAITING_FOR_MCODE_HANDLER state
7. Each task loop cycle:
   a. Process NML commands (including abort/estop)
   b. Update and publish status
   c. Check handler thread (pthread_tryjoin_np / non-blocking)
   d. If abort requested: write to abort_fd, wait with timeout, force-cancel
8. Handler finishes → task reads ctx->result, sets execState = DONE
```

#### M-code Handler Example (cmod)

```c
// Wait for a pneumatic clamp sensor, respecting abort
int clamp_mcode(task_mcode_ctx_t *ctx) {
    // Activate clamp via HAL pin
    hal_pin_set_bit("clamp.activate", 1);

    // Poll sensor, checking abort_fd
    struct pollfd pfd = { .fd = ctx->abort_fd, .events = POLLIN };
    while (!hal_pin_get_bit("clamp.clamped")) {
        if (poll(&pfd, 1, 100) > 0) {   // 100ms poll timeout
            hal_pin_set_bit("clamp.activate", 0);  // cleanup
            return -2;  // aborted
        }
    }
    ctx->result = 0;
    return 0;
}
```

On the Go/gomod side, the abort_fd maps to a `context.Context` cancellation
or a channel read, providing idiomatic Go abort handling.

### POSTTASK\_HALFILE

Handled by gomc-server launcher calling internal halcmd directly. No API needed.

### INI Configuration Changes

```ini
# BEFORE (Python remap):
[RS274NGC]
REMAP = T   prolog=prepare_prolog ngc=prepare epilog=prepare_epilog
REMAP = M6  modalgroup=6 prolog=change_prolog ngc=change epilog=change_epilog

[PYTHON]
TOPLEVEL = python/toplevel.py
PATH_PREPEND = python

# AFTER (cmod/gomod remap):
[RS274NGC]
REMAP = T   prolog=prepare_prolog ngc=prepare epilog=prepare_epilog
REMAP = M6  modalgroup=6 prolog=change_prolog ngc=change epilog=change_epilog
# prolog/epilog names resolve to registered cmod/gomod handlers
# ngc= still works (NGC subroutine files, no Python needed)
# py= removed

# [PYTHON] section no longer needed
```

The `REMAP` syntax stays the same. The `prolog=`/`epilog=` names now resolve
to cmod/gomod-registered handlers instead of Python functions. The `py=` keyword
is removed. `ngc=` continues to work for NGC subroutine bodies.

## Implementation Phases

### Phase 1: Canon Callback Table

**Goal:** Replace ~110 global canon functions with `interp_canon_t`.
No behavior change — pure refactoring.

1. Define `interp_canon_t` struct in new header `src/emc/nml_intf/interp_canon.h`
2. Implement `emccanon_make_table()` in `emccanon.cc` — creates an
   `interp_canon_t` whose callbacks call the existing global implementations.
   The `ctx` holds pointers to `canon`, `interp_list`, `emcStatus`, etc.
3. Add `interp_canon_t *canon` parameter to `Interp::init()`. Store as member.
4. Mechanically replace all `STRAIGHT_FEED(...)` calls in `rs274ngc/*.cc` with
   `canon->straight_feed(canon->ctx, ...)`. This is ~250 call sites, but each
   is a trivial search-and-replace.
5. Similarly replace all `GET_EXTERNAL_*()` calls.
6. Move `static CanonConfig_t canon` and related statics into a struct allocated
   per-instance by `emccanon_make_table()`.
7. Remove the free-function declarations from `canon.hh`.
8. SAI (`saicanon.cc`) and gcodemodule (`gcodemodule.cc`) get their own table
   implementations — they already have their own canon function bodies.

**Validation:** All existing configs work unchanged. Two Interp instances can
coexist (tested with preview + execution).

### Phase 2: Remove Python from Interpreter

**Goal:** Strip all Python/Boost.Python from rs274ngc.

1. Remove `PythonPlugin::instantiate()` from `Interp::Interp()`.
2. Remove `python_plugin` references from `interp_python.cc`.
3. In `pycall()` dispatch: if handler name is registered via `interp_ext_t`,
   call the registered C function. If not registered, return error
   "handler not found" (instead of calling Python).
4. Remove `interpmodule.cc` (the Boost.Python binding module).
5. Remove `from interpreter import this` setup in constructor.
6. Remove `PYUSABLE` checks throughout — replace with ext registration checks.
7. Remove `python_plugin.hh` / `python_plugin.cc` from milltask link.
8. Remove `boost_python` and `python3-embed` from milltask build deps.

**Validation:** Configs without `[PYTHON]`/`REMAP` work unchanged.
Configs with `REMAP ngc=` work (NGC subroutine bodies don't need Python).
Configs with `REMAP py=` fail with clear error (expected).

### Phase 3: Interpreter Extension API

**Goal:** Implement `interp_ext_t` / `gomc_interp_ext_t` so cmod/gomod can
register remap prologs/epilogs, oword handlers, and inline extensions.

1. Define `interp_ext_t` header.
2. Implement registration storage in Interp (a name→function-pointer map).
3. Wire `pycall()` replacement to look up registered handlers.
4. Implement the `interp_ext_ctx_t` population (params, tool queries, canon).
5. Implement `phase` counter for yield/resume (INTERP\_EXECUTE\_FINISH).
6. Add `gomc_interp_ext_t` to `cmod_env_t`.
7. Implement Go-side registration wrappers.
8. Port `stdglue.py` (prepare\_prolog, change\_prolog, change\_epilog) to a
   reference `stdglue` cmod.

**Validation:** Remap configs using stdglue functions work with the cmod.
Custom remap prologs/epilogs can be written as gomods.

### Phase 4: Task Extension API + M-code Registration

**Goal:** Replace Python task overrides and M100-M199 fork/exec.

1. Define `task_ext_t` / `gomc_task_ext_t` header.
2. Replace `TaskWrap` (Boost.Python) with C callback dispatch in
   `taskclass.cc` — each Task virtual method checks for registered handler,
   falls through to default C++ if none.
3. Replace `emcSystemCmd()` fork/exec with threaded mcode handler dispatch.
4. Remove `EMC_SYSTEM_CMD` NML message type.
5. Replace `WAITING_FOR_SYSTEM_CMD` with `WAITING_FOR_MCODE_HANDLER` task
   exec state — polls handler thread completion each cycle.
6. Implement abort path: on abort/estop, write to handler's `abort_fd`,
   wait with timeout, then force-cancel thread.
6. Remove `user_defined_fmt[]` / `user_defined_function_dirindex[]` from
   `emctask.cc`.
7. Remove `taskmodule.cc` (Boost.Python bindings).
8. POSTTASK\_HALFILE: call halcmd internally from gomc-server launcher.
9. Remove PLUGIN\_CALL / IO\_PLUGIN\_CALL entirely.
10. Port iocontrol-v2 Python task class to a cmod (if anyone uses it).

**Validation:** M1xx codes work via registered gomods.
Default tool change (iocontrol-based) works without Python.

### Phase 5: Multi-Instance Interpreter + Server-Side Preview

**Goal:** Enable multiple concurrent Interp instances. Primary driver:
server-side G-code preview that runs concurrently with execution.

1. Move remaining file-scoped statics into `_setup`:
   - `nurbs_order`, `nurbs_control_points` → `_setup`
   - `savedError` → `_setup`
2. Each Interp instance gets its own `interp_canon_t` at construction.
3. Each `interp_canon_t` instance has its own CanonConfig, interp\_list,
   and status reference.
4. Tool table access goes through canon getters (already done in Phase 1).
5. Implement **preview canon** (`preview_canon_t`): a `interp_canon_t`
   implementation that records geometry as JSON (line segments, arcs,
   rapid/feed classification, tool changes, coordinate system). No NML,
   no HAL, no interp\_list — pure data recording.
6. Preview Interp instance runs with `interp_ext = NULL`. Extensions are
   skipped entirely — remapped codes that have `ngc=` subs still execute
   (normal NGC sub call, correct geometry), but prolog/epilog/body
   extension callbacks are not invoked. This is correct because preview
   doesn't need side effects, only geometry.
7. gomc-server exposes preview via REST/WebSocket endpoint. Client sends
   file path (or G-code text), server creates preview Interp + preview
   canon, runs interpretation, streams JSON geometry to client. Client
   is a pure renderer — no G-code parsing needed.
8. Test: create two Interp instances — one for execution, one for preview.
   They must not interfere. Preview must produce identical geometry to
   execution canon for the same program (modulo side-effect-only codes).

### Phase 6: Cleanup

1. Remove `#include <Python.h>` and `#include <boost/python.hpp>` throughout.
2. Remove `python3-embed` from `packages.conf`.
3. Remove `libboost_python` from link flags.
4. Remove `src/emc/pythonplugin/` directory.
5. Remove `emccanon.cc` Boost.Python canon bindings (`canonmodule.cc`).
6. Update documentation.

## Files Modified/Removed

### Removed
- `src/emc/pythonplugin/python_plugin.cc`
- `src/emc/pythonplugin/python_plugin.hh`
- `src/emc/rs274ngc/interpmodule.cc`
- `src/emc/rs274ngc/canonmodule.cc`
- `src/emc/task/taskmodule.cc`
- `src/emc/rs274ngc/interp_python.cc` (rewritten as `interp_ext.cc`)

### New
- `src/emc/nml_intf/interp_canon.h` — canon callback table struct
- `src/emc/task/emccanon_table.cc` — table implementation wrapping existing canon
- `src/emc/rs274ngc/interp_ext.h` — extension API types
- `src/emc/rs274ngc/interp_ext.cc` — extension dispatch (replaces interp\_python.cc)
- `src/emc/task/task_ext.h` — task extension API types
- `src/emc/task/task_ext.cc` — task extension dispatch
- `src/gomc/pkg/cmodule/gomc_interp_ext.h` — cmod header for interp extensions
- `src/gomc/pkg/cmodule/gomc_task_ext.h` — cmod header for task extensions
- `cmod/stdglue.so` — reference remap handlers (port of stdglue.py)

### Modified (major)
- `src/emc/rs274ngc/rs274ngc_pre.cc` — remove Python init, add canon table
- `src/emc/rs274ngc/rs274ngc_interp.hh` — add `interp_canon_t*`, `interp_ext_t*` members
- `src/emc/rs274ngc/interp_convert.cc` — replace canon calls, move NURBS statics
- `src/emc/rs274ngc/interp_o_word.cc` — replace pycall with ext dispatch
- `src/emc/task/emccanon.cc` — wrap in table factory
- `src/emc/task/emctask.cc` — remove user\_defined\_function file scanning
- `src/emc/task/emctaskmain_gomc.cc` — remove EMC\_SYSTEM\_CMD, Python refs
- `src/emc/task/taskclass.cc` — replace TaskWrap with task\_ext dispatch
- `src/emc/sai/saicanon.cc` — implement its own interp\_canon\_t
- `src/emc/rs274ngc/gcodemodule.cc` — implement its own interp\_canon\_t

## Resolved Questions

1. ~~**stdglue scope**~~: **Resolved.** stdglue is a replaceable cmod.
   The `interp_ext_ctx_t` accessor API exposes what conceptually makes
   sense for any remap handler — not just what today's stdglue.py uses.
   Design for the general case: tool state, pocket selection, coordinate
   systems, motion mode, etc. Accessors are read-only so there's no risk
   in exposing more than currently needed. A future replacement handler
   shouldn't be limited by a too-narrow API.

2. ~~**G-code preview multi-instance**~~: **Resolved.** Preview runs
   server-side with a recording canon that emits JSON geometry. Extensions
   (`interp_ext`) are NULL for preview — NGC sub bodies still execute for
   correct geometry, but extension callbacks are skipped. Client is a
   pure renderer receiving geometry via REST/WS. See Phase 5.

3. ~~**NGC sub bodies with remap**~~: **Not a question.** NGC sub bodies
   (`ngc=prepare`) work today without Python and continue unchanged.
   The prolog sets named params (#\<tool\>, #\<pocket\>), the NGC sub
   reads them. This path never touches Python and needs no migration.

4. ~~**Inline `(ext, ...)` vs O-word**~~: **Resolved.** Drop inline
   extension support entirely. O-word registration covers the same
   functionality. No `(ext, ...)` comment syntax.
