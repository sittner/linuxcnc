# Dynamic API Design

This document describes the dynamic inter-module communication system for LinuxCNC,
intended to replace NML with a modern, type-safe approach.

## Current Status (April 2026)

| Step | Status | Tests |
|------|--------|-------|
| 0: Core Module Migration | ✅ Complete | — |
| 1: apiserver Package | ✅ Complete | 37 |
| 2: `--server-go` | ✅ Complete | 3 |
| 3: `--server-c` + cgo | ✅ Complete | 5 |
| 4: Client Generation | ⚠️ Partial | — |
| 5: Python Client | ❌ Not Started | — |
| 6: Polish | ❌ Not Started | — |

**Total: 56 tests passing**

Step 4 status: `--client-c` internal and REST complete; `--client-go` not started.

## Overview

The system enables modules (cmod/gomod) to:
1. Register API implementations (expose functionality)
2. Lookup and call APIs (consume functionality)
3. Optionally expose APIs via REST for external clients

All module code works with native structs (C or Go). JSON/REST handling is
centralized in the launcher and invisible to modules.

## Architecture

```
                     External REST Client
                            │
                            │ JSON/HTTP (localhost:port)
                            ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                           LAUNCHER (Go)                                  │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                      HTTP Server Layer                             │  │
│  │  - Binds to localhost only (no auth initially)                     │  │
│  │  - JSON ↔ Go struct marshaling (ONLY external boundary)            │  │
│  │  - Started after HAL setup, stopped after UI shutdown              │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                    │                                     │
│                                    │ Go struct                           │
│                                    ▼                                     │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                      Dispatcher / Registry                         │  │
│  │  - Stores APIMeta (from IDL) per instance                          │  │
│  │  - Holds opaque Callbacks pointer (Go interface or C struct ptr)   │  │
│  │  - HTTP server matches path → funcIndex → Funcs[i].Dispatch       │  │
│  │  - Generated dispatch wrappers handle all marshaling               │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│              │                                      │                    │
│              │ Go struct                            │ C struct (cgo)     │
│              ▼                                      ▼                    │
│  ┌─────────────────────┐                ┌─────────────────────┐          │
│  │       gomod         │◀──────────────▶│       cmod          │          │
│  │  (Go struct API)    │   Go↔C conv    │  (C struct API)     │          │
│  │  Always non-RT      │                │  Can be RT-safe     │          │
│  └─────────────────────┘                └─────────────────────┘          │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

## Key Design Principles

### 1. Modules Know Nothing About REST/JSON

Modules implement pure struct-based APIs:
- **cmod**: C structs, C function callbacks
- **gomod**: Go structs, Go function callbacks

All protocol handling (HTTP, JSON) is centralized in the launcher.

### 2. JSON Only at External Boundary

| Call Path | Data Serialization |
|-----------|-------------------|
| External REST → any module | JSON → DispatchFunc → callback (via dispatch table) |
| gomod → gomod | Go struct (direct interface call) |
| gomod → cmod | Go struct → C struct (via generated client) |
| cmod → cmod | C struct (direct function pointer call, RT-safe) |
| cmod → gomod | C struct → Go struct (via cgo export) |

### 3. Single Port, Path-Based Routing

All REST services share one HTTP server:
```
http://localhost:8080/api/v1/{instance}/{path}

Examples:
  GET  /api/v1/hal/pins
  GET  /api/v1/hal/pin/axis.0.pos-cmd
  POST /api/v1/halcmd/signal
```

### 4. RT-Safe Marking

- `@rt_safe "true"` in IDL means the callback CAN be called from RT context
- All callbacks can be called from non-RT context (REST, gomod)
- gomod is always non-RT (Go has garbage collection)
- cmod→gomod calls are always non-RT

## IDL Definition (.gmi files)

Interface definitions live in `src/gmi/idl/`. Example:

```
@api hal
@version 1
@prefix "hal"
@rest_export false  // Not exposed via REST

const MAX_PINS = 256

enum PinDir {
    HAL_IN = 16
    HAL_OUT = 32
    HAL_IO = 48
}

type PinInfo {
    name: string
    dir: PinDir
    type: PinType
    value: f64
}

@rt_safe "true"
func pin_read(name: string) -> PinInfo
```

### IDL Language Features (Implemented)

| Feature | Syntax | Example |
|---------|--------|---------|
| Constants | `const NAME = N` | `const MAX_JOINTS = 16` |
| Enums | `enum Name { A = 1, B = 2 }` | `enum PinDir { IN = 16 }` |
| Types | `type Name { field: T }` | `type PinInfo { name: string }` |
| Primitives | `bool`, `i8`-`i64`, `u8`-`u64`, `f32`, `f64`, `string` | |
| Fixed arrays | `[N]T` or `[CONST]T` | `[16]f64`, `[MAX_JOINTS]f64` |
| Slices | `[]T` | `[]PinInfo` |
| Nullable | `T?` | `string?`, `PinInfo?` |
| By-ref params | `byref name: T` | `byref joints: [16]f64` |
| Functions | `func name(params) -> ReturnType` | `func forward(...) -> i32` |

**Directives:**
- `@api name` — API name (required)
- `@version N` — API version (required)
- `@prefix "str"` — C/REST prefix
- `@rest_export true/false` — Enable REST exposure
- `@rt_safe "true"` — Mark function as RT-safe

## Code Generation (gmicompile)

### Generated C Server Code (`--server-c`)

```c
// hal_api.h - Generated by gmicompile

typedef struct {
    const char *name;
    hal_pin_dir_t dir;
    hal_pin_type_t type;
    double value;
} hal_pin_info_t;

typedef int (*hal_pin_read_fn)(const char *name, hal_pin_info_t *out);

typedef struct {
    hal_pin_read_fn pin_read;
    // ... other callbacks
} hal_callbacks_t;

// Register this API implementation with the launcher.
// Internally generates Go dispatch wrappers (via cgo) embedded in FuncMeta.
int hal_api_register(const char *instance_name, const hal_callbacks_t *callbacks);
```

### Generated Go Dispatch Wrappers (internal, from `--server-c`)

These are generated alongside the C header and compiled into the launcher.
They bridge the gap between the uniform `DispatchFunc` signature and C callbacks:

```go
// hal_dispatch.go - Generated by gmicompile (not hand-written)

func halDispatchPinRead(cb unsafe.Pointer, req []byte) ([]byte, error) {
    // JSON → Go
    var params struct { Name string `json:"name"` }
    json.Unmarshal(req, &params)
    // Go → C
    cname := C.CString(params.Name)
    defer C.free(unsafe.Pointer(cname))
    var out C.hal_pin_info_t
    rc := C.call_pin_read((*C.hal_callbacks_t)(cb), cname, &out)
    if rc != 0 { return nil, syscall.Errno(-rc) }
    // C → JSON
    return json.Marshal(pinInfoFromC(&out))
}

var halMeta = &APIMeta{
    Name: "hal", Version: 1, RESTExport: true, Prefix: "hal",
    Funcs: []FuncMeta{
        {Name: "pin_read", Method: "GET", Path: "/pin/{name}", Dispatch: halDispatchPinRead},
        // ...
    },
}
```

### Generated Go Server Code (`--server-go`)

```go
// hal_api.go - Generated by gmicompile

type PinInfo struct {
    Name  string  `json:"name"`
    Dir   PinDir  `json:"dir"`
    Type  PinType `json:"type"`
    Value float64 `json:"value"`
}

type HalCallbacks interface {
    PinRead(name string) (*PinInfo, error)
    // ... other callbacks
}

// Register populates FuncMeta.Dispatch for each function and calls
// the generic registry.Register().
func RegisterHalAPI(instance string, impl HalCallbacks) error
```

### Generated C Client Code (`--client-c`)

For cmod calling other APIs (lookup at startup, direct calls at runtime):

```c
// halcmd_client.h - for cmod to call halcmd API

// Lookup API at module init (fails if not found or version mismatch).
// Returns opaque pointer — cast to halcmd_callbacks_t* for direct calls.
halcmd_callbacks_t *halcmd_api_get(const char *instance, int required_version);

// Direct function calls via callbacks struct (no dispatch, no overhead):
// halcmd_callbacks_t *api = halcmd_api_get("halcmd0", 1);
// int rc = api->list_pins("*", &result, &len);
```

### Generated C REST Client Code (`--client-c` with `@rest_export true`)

For external (non-launcher) C programs calling APIs over REST.
Each client instance owns a persistent CURL handle for connection pooling
(TCP keep-alive, TLS session reuse). Not thread-safe — create one per thread:

```c
// halcmd_rest_client.h - for standalone C programs

typedef struct halcmd_rest_client halcmd_rest_client_t;

// Create client — owns CURL handle, reuses connections across calls.
// For multi-threaded use, create one client per thread.
halcmd_rest_client_t *halcmd_rest_connect(const char *base_url);
void halcmd_rest_disconnect(halcmd_rest_client_t *client);

int halcmd_rest_list_pins(halcmd_rest_client_t *client, const char *pattern,
                          halcmd_pin_info_t **out, size_t *out_len);
```

### Generated Go Client Code (`--client-go`)

For gomod calling other APIs (lookup at startup):

```go
// halcmd_client.go - for gomod to call halcmd API

// Lookup at module init - returns typed interface wrapping the callbacks.
// Returns error if not found or version mismatch.
func GetHalcmdAPI(instance string, requiredVersion int) (HalcmdCallbacks, error)

// Direct calls via returned interface (no dispatch table overhead):
// api, _ := GetHalcmdAPI("halcmd0", 1)
// pins, err := api.ListPins("*")
```

## Call Flow Examples

### External REST → cmod (or gomod — identical path)

```
1. HTTP request: GET /api/v1/hal0/pin/axis.0.pos-cmd
2. HTTP server looks up "hal0" in registry → RegisteredAPI
3. HTTP server matches (GET, "/pin/{name}") → funcIndex
4. HTTP server calls api.Meta.Funcs[funcIndex].Dispatch(api.Callbacks, body)
5. Generated dispatch wrapper (Go):
   a. Unmarshals JSON request body → Go values
   b. For cmod: converts Go values → C values, calls C callback via cgo
      For gomod: calls Go interface method directly
   c. Converts result → JSON response bytes
6. HTTP server writes JSON response
```

Note: The HTTP server is completely generic. It does not know whether the
API is backed by a cmod or gomod — both produce the same `DispatchFunc` table.

### gomod → cmod (internal, at runtime)

```
Prerequisites (done at module init):
  - gomod called GetHalAPI("hal0", 1)
  - Returned HalCallbacks wraps resolved C callback pointers

At runtime:
1. gomod calls: api.PinRead("axis.0.pos-cmd")
2. Generated client converts Go values → C values
3. Direct C callback call via cgo (no dispatch table, no lookup!)
4. C callback executes, fills result struct, returns errno
5. Generated client converts C result → Go values
6. Return Go struct to caller
```

### cmod → gomod (internal, non-RT only, at runtime)

```
Prerequisites (done at module init):
  - cmod called halcmd_api_get("halcmd0", 1)
  - Returned opaque pointer is cast to halcmd_callbacks_t*

At runtime:
1. cmod calls: api->list_pins("*", &result, &len)
2. Generated C stub enters Go via cgo export
3. Direct Go interface call (no dispatch table, no registry lookup!)
4. Go callback executes, returns Go values + error
5. Generated stub converts Go result → C struct
6. Return to C caller with errno
```

### cmod → cmod (internal, RT-safe possible, at runtime)

```
Prerequisites (done at module init):
  - cmod called hal_api_get("hal0", 1)
  - Returned opaque pointer is cast to hal_callbacks_t*

At runtime (can be from RT context if callback is RT-safe):
1. cmod calls: api->pin_read("axis.0.pos-cmd", &info)
2. Direct C function pointer call (no cgo, no dispatch table!)
3. C callback executes, fills result struct, returns errno
4. Return to caller
```

## Registry Design

### Core Types

```go
// DispatchFunc is the uniform signature for all generated dispatch wrappers.
// Both cmod and gomod generate functions with this signature.
// The HTTP server calls these — it never touches callbacks directly.
type DispatchFunc func(callbacks unsafe.Pointer, req []byte) ([]byte, error)

// FuncMeta holds static metadata + dispatch for one API function (generated).
// Routing info and dispatch wrapper live together — no parallel arrays.
type FuncMeta struct {
    Name     string       // "pin_read"
    Method   string       // "GET", "POST", etc. (empty if not REST-exported)
    Path     string       // "/pin/{name}" (empty if not REST-exported)
    RTSafe   bool
    Dispatch DispatchFunc // generated wrapper (nil if not REST-exported)
}

// APIMeta holds static metadata for an entire API (generated, read-only).
type APIMeta struct {
    Name       string     // "hal"
    Version    int
    RESTExport bool
    Prefix     string     // REST path prefix
    Funcs      []FuncMeta // routing + dispatch in one place
}

// RegisteredAPI is one registered API instance in the registry.
type RegisteredAPI struct {
    APIName   string         // "tp" — API name from registration
    Version   int            // API version from registration
    Meta      *APIMeta       // optional — REST routing/dispatch (nil for pure C-to-C)
    Instance  string         // "default" — unique instance name within an API
    Callbacks unsafe.Pointer // opaque — *tp_callbacks_t (cmod) or Go interface
}
```

### Language-Agnostic Dispatch

Both cmod and gomod generate Go-side dispatch functions with identical `DispatchFunc`
signatures. Since cgo already requires Go wrapper functions to call C function
pointers, the REST path always goes through generated Go code — no special handling
for cmod vs gomod:

```go
// Generated for cmod (--server-c):
func halDispatchPinRead(cb unsafe.Pointer, req []byte) ([]byte, error) {
    name := unmarshalString(req)            // JSON → Go
    cname := C.CString(name)               // Go → C
    defer C.free(unsafe.Pointer(cname))
    var out C.hal_pin_info_t
    rc := C.hal_pin_read_call(             // cgo call to C callback
        (*C.hal_callbacks_t)(cb).pin_read, cname, &out)
    if rc != 0 { return nil, syscall.Errno(-rc) }
    return marshalPinInfo(&out), nil        // C → Go → JSON
}

// Generated for gomod (--server-go):
func halDispatchPinRead(cb unsafe.Pointer, req []byte) ([]byte, error) {
    name := unmarshalString(req)            // JSON → Go
    impl := (*goHalCallbacks)(cb)           // type assertion
    result, err := impl.PinRead(name)       // direct Go call
    if err != nil { return nil, err }
    return marshalPinInfo(result), nil      // Go → JSON
}

// Both populate Dispatch in the same FuncMeta slice:
var halMeta = &APIMeta{
    Funcs: []FuncMeta{
        {Name: "init", Dispatch: halDispatchInit},
        {Name: "pin_read", Method: "GET", Path: "/pin/{name}", Dispatch: halDispatchPinRead},
        // ...
    },
}
```

### API Registration (at module load)

```go
// Register is called by generated code during module init.
// Only apiName, version, instance, and callbacks are required — all supplied
// by the C module at runtime. If an APIMeta with matching name+version was
// registered (e.g. via a generated Go package init()), it is automatically
// attached for REST dispatch.
func Register(apiName string, version int, instance string, callbacks unsafe.Pointer) error {
    key := registryKey(apiName, instance)
    if registry.Has(key) {
        return syscall.EEXIST
    }
    // Attach REST metadata if available (optional — nil is fine for C-to-C)
    meta := GetMeta(apiName, version)
    registry.Put(key, &RegisteredAPI{
        APIName:   apiName,
        Version:   version,
        Meta:      meta,
        Instance:  instance,
        Callbacks: callbacks,
    })
    return nil
}
```

### API Lookup (at module init, not runtime)

```go
// GetAPI returns the callbacks pointer for direct inter-module calls.
// For gomod clients — called during module init.
func GetAPI(apiName, instance string, requiredVersion int) (unsafe.Pointer, error) {
    key := registryKey(apiName, instance)
    api := registry.Get(key)
    if api == nil {
        return nil, syscall.ENOENT
    }
    if api.Version != requiredVersion {
        return nil, syscall.EINVAL
    }
    // Return opaque callbacks pointer — client casts to concrete type
    return api.Callbacks, nil
}

// For cmod clients — called during module init via gomc_api_t callback table:
// void* get_api(void *ctx, const char *api_name, int version, const char *instance);
// Returns NULL on not found or version mismatch.
// The cmod casts the result to kins_callbacks_t* and calls members directly.
```

### REST Dispatch (HTTP server)

The HTTP server is completely generic — no per-API code:

```go
func (s *Server) handleAPIRequest(w http.ResponseWriter, r *http.Request) {
    // 1. Extract instance + path from URL
    instance, path := parseURL(r.URL.Path)

    // 2. Find registered API
    api := registry.Get(instance)
    if api == nil || !api.Meta.RESTExport {
        http.NotFound(w, r)
        return
    }

    // 3. Match path against FuncMeta to get funcIndex
    funcIndex := matchFunc(api.Meta.Funcs, r.Method, path)
    if funcIndex < 0 {
        http.NotFound(w, r)
        return
    }

    // 4. Dispatch — uniform call, no cmod/gomod awareness
    body, _ := io.ReadAll(r.Body)
    resp, err := api.Meta.Funcs[funcIndex].Dispatch(api.Callbacks, body)

    // 5. Write response
    if err != nil {
        writeError(w, err)
        return
    }
    w.Header().Set("Content-Type", "application/json")
    w.Write(resp)
}
```

### Direct Inter-Module Calls (no dispatch table)

For cmod→cmod and gomod→gomod, the dispatch table is NOT used.
Clients call through the callbacks struct directly — zero overhead:

```c
// cmod→cmod: direct C function pointer call (RT-safe)
hal_callbacks_t *api = (hal_callbacks_t *)gmi_get_api("hal0", 1);
hal_pin_info_t info;
int rc = api->pin_read("axis.0.pos-cmd", &info);
```

```go
// gomod→gomod: direct Go interface call
api, _ := GetHalAPI("hal0", 1)  // returns HalCallbacks interface
info, err := api.PinRead("axis.0.pos-cmd")
```

The dispatch table only exists for REST and cross-language calls.

## Struct Conversion (Go ↔ C)

Options:

1. **Generated converters**: gmicompile generates conversion functions
2. **Reflection-based**: Generic converter using struct tags
3. **Hybrid**: Generated for performance-critical paths, reflection for rest

Recommended: **Generated converters** for type safety and performance.

```go
// Generated by gmicompile
func pinInfoGoToC(src *PinInfo, dst *C.hal_pin_info_t) {
    dst.name = C.CString(src.Name)
    dst.dir = C.hal_pin_dir_t(src.Dir)
    dst.type_ = C.hal_pin_type_t(src.Type)
    dst.value = C.double(src.Value)
}

func pinInfoCToGo(src *C.hal_pin_info_t) *PinInfo {
    return &PinInfo{
        Name:  C.GoString(src.name),
        Dir:   PinDir(src.dir),
        Type:  PinType(src.type_),
        Value: float64(src.value),
    }
}
```

## Lifecycle Integration

### Startup Sequence

```
1. Parse config, load modules (existing)
2. Execute HAL file setup (existing)
3. All modules register their APIs
4. Start HTTP server (new)
5. Start UI (existing)
```

### Shutdown Sequence

```
1. UI exits
2. Stop HTTP server (new) 
3. Unload modules (existing)
4. Cleanup (existing)
```

## Security Considerations

### Current (Phase 1)
- Bind to localhost only
- No authentication
- Suitable for single-machine use

### Future (Phase 2+)
- Optional TLS
- Token-based authentication
- Permission system per API/function
- Optional remote access

## File Structure

```
src/gmi/
├── idl/                    # Interface definitions (.gmi files)
│   ├── hal.gmi             # HAL component API (not compilable, uses opaque ptrs)
│   ├── halcmd.gmi          # HAL command API (@rest_export true)
│   ├── home.gmi            # Homing API (19 callbacks)
│   ├── kins.gmi            # Kinematics API (5 callbacks)
│   ├── mot.gmi             # Motion reverse-callbacks (83 callbacks)
│   ├── tp.gmi              # Trajectory planner API (29 callbacks)
│   └── README.md
├── lib/                    # C runtime library (libgmi) for REST clients
│   ├── gmi.h               # Main include
│   ├── gmi_http.c/h        # HTTP client (libcurl wrapper)
│   ├── gmi_json.c/h        # JSON utilities (cJSON wrapper)
│   ├── gmi_error.c/h       # Error codes (GMI_ERR_*)
│   ├── gmi_types.c/h       # Type utilities
│   └── Submakefile
├── DYNAMIC_API_DESIGN.md   # This document
└── README.md

src/launcher/
├── cmd/
│   └── gmicompile/         # Code generator CLI
│       └── main.go
├── generated/              # Generated code (gitignored)
│   └── gmi/
│       ├── home/           # home_api.h, home_cgo.go
│       ├── kins/           # kins_api.h, kins_cgo.go
│       ├── mot/            # mot_api.h, mot_cgo.go
│       └── tp/             # tp_api.h, tp_cgo.go
├── internal/
│   ├── apiserver/          # REST server (Step 1)
│   │   ├── types.go        # DispatchFunc, FuncMeta, APIMeta, RegisteredAPI
│   │   ├── registry.go     # Register(), GetAPI(), thread-safe map
│   │   ├── server.go       # HTTP handler, path matching
│   │   ├── *_test.go       # 37 tests
│   │   └── directtest/     # cmod direct-call simulation tests
│   └── gmicompile/         # Code generator (parses .gmi → C/Go)
│       ├── ast/            # AST types
│       ├── parser/         # IDL parser (8 tests)
│       └── cgen/           # Code generators
│           ├── server.go       # --server-c: C header generation
│           ├── dispatch_c.go   # --server-c: Go cgo dispatch wrappers
│           ├── server_go.go    # --server-go: Go server generation
│           └── client.go       # --client-c: C REST client generation
└── ...

src/emc/kinematics/         # Kinematics modules (cmod .so plugins)
├── trivkins.c              # Each .c is a self-contained cmod
├── 5axiskins.c
├── genhexkins.c
├── genserkins.c
├── ...                     # 17 kins modules total
├── switchkins_cmod.h       # Shared header for switchable kins cmods
├── Submakefile             # Build rules for Python modules + cmod rules
└── ...

src/emc/tp/                 # Trajectory planner (cmod .so plugin)
├── tp.c                    # Self-contained cmod: owns TP_STRUCT, registers tp API
├── tc.c, tcq.c, ...       # Support files compiled into tpmod.so
└── ...

src/emc/motion/             # Motion controller
├── homing.c                # Self-contained cmod: registers home API
├── motion.c                # motmod (consumes tp + home APIs)
├── control.c               # RT control loop (calls motmod_tp_api->*, motmod_home_api->*)
├── command.c               # Command handler
└── ...
```

## Implementation Plan

### Step 0: Core Module Migration (COMPLETE)

Migrate the three core motion modules (kins, tp, homing) from legacy RTAPI loadable
modules to self-contained cmods using the GMI dynamic API.

**Deliverables:**
- [x] IDL definitions: `mot.gmi` (83 callbacks), `home.gmi` (19), `tp.gmi` (29), `kins.gmi` (5)
- [x] Code generator (`gmicompile`): `--server-c` producing C headers + Go CGO wrappers
- [x] Codegen: functions return values directly (not via out-pointer)
- [x] Codegen: enums passed by value (not pointer)
- [x] Kinematics: 17 kins modules ported to cmod (in `emc/kinematics/`)
- [x] Trajectory planner: `tp.c` is self-contained cmod (owns `TP_STRUCT`, registers tp API)
- [x] Homing: `homing.c` is self-contained cmod (registers home API)
- [x] Motion controller (`motmod`): consumes tp + home APIs via direct pointer calls
- [x] Bridge layer removed — `control.c`/`command.c` call `motmod_tp_api->*` directly
- [x] All wrapper layers eliminated (tpmod.c, homemod.c deleted)
- [x] Build system: cmod rules in Makefile/Submakefiles, rtlib rules removed
- [x] Generated code properly gitignored (`src/launcher/.gitignore`)
- [x] Kins round-trip Go tests: forward→inverse→compare (trivkins, pumakins)
- [x] RPY convention test: verifies j1 rotation maps to yaw (C), not roll (A)

**Migration findings:**

- **Self-contained cmod pattern**: Each module implements `New()` entry point,
  returns a `cmod_t` with Init/Start/Destroy. Init calls `*_api_get()` to
  resolve dependencies. This replaces the old RTAPI module_init + EXPORT_SYMBOL pattern.

- **Wrapper elimination**: Initial migration used intermediate wrapper files
  (tpmod.c, homemod.c) that forwarded calls to the original implementation.
  These were eliminated by merging directly — making original functions `static`
  and appending `gmi_*` callback wrappers + cmod lifecycle to the same file.

- **Forward declarations needed**: After making functions static, some are called
  before their definition. Forward declarations are required (added at top of file).

- **Internal self-calls**: When public wrapper functions are removed, internal code
  that previously called those wrappers must be updated to call `base_*` functions
  directly (e.g., `get_allhomed()` → `base_get_allhomed()` inside homing.c).

- **Kinematics are trivial cmods**: Each is a single .c file with `New()` that
  registers kins callbacks. No complex lifecycle. The `switchkins_cmod.h` header
  provides common infrastructure for switchable kins modules.

- **Posemath convention standardized**: Modules that inline rotation helpers must
  use the same convention as `posemath.h`. Storage: `R.AB` = column A, row B
  (matching `PmRotationMatrix` where `m->x.y` = column x, row y). RPY:
  `R = Rz(yaw) * Ry(pitch) * Rx(roll)`, where roll=A (about X), pitch=B (about Y),
  yaw=C (about Z). Two modules (`pumakins.c`, `pentakins.c`) had roll/yaw swapped
  in their inline helpers — fixed to match legacy posemath behavior.

- **No separate directories needed**: Source lives in standard locations
  (`emc/kinematics/`, `emc/tp/`, `emc/motion/`). Only the IDL definitions and
  runtime library need a dedicated `gmi/` directory.

### Step 1: apiserver Package (COMPLETE)

Foundation for everything else. Fully testable in isolation.

**Deliverables:**
- [x] `types.go` — `DispatchFunc`, `FuncMeta`, `APIMeta`, `RegisteredAPI`
- [x] `registry.go` — `Register()`, `GetAPI()`, thread-safe instance map
- [x] `server.go` — generic HTTP handler, path matching, JSON error responses

**Tests:** 37 passing
- [x] Unit: registry Register/GetAPI, duplicate rejection, version mismatch (9 tests)
- [x] Unit: path matching (static, parameterized, wildcard, method filtering) (5 tests)
- [x] Unit: dispatch with mock `DispatchFunc` (success, error, not found) (11 tests)
- [x] Integration: `httptest.Server` → register fake API → REST roundtrip → verify JSON (8 tests)
- [x] Direct call tests: cmod→cmod lookup and call simulation (4 tests)

**No dependencies on:** gmicompile, cgo, generated code. Hand-written mock APIs only.

### Step 2: gmicompile `--server-go` (COMPLETE)

Generate Go code that plugs into the apiserver from Step 1.

**Deliverables:**
- [x] Generate `APIMeta` literal with `FuncMeta` entries (including `Dispatch`)
- [x] Generate Go callbacks interface (e.g., `HalCallbacks`)
- [x] Generate Go dispatch wrappers (`DispatchFunc` per function)
- [x] Generate `Register*API()` wrapper calling `apiserver.Register()`
- [x] Generate Go struct types from IDL `type` declarations

**Tests:** 3 passing
- [x] Unit: golden-file comparison of generated .go output vs expected
- [x] Unit: keyword escape handling (Go reserved words)
- [x] Unit: non-REST API generation (no dispatch wrappers)

### Step 3: gmicompile `--server-c` + cgo Bridge (COMPLETE)

Generate C callbacks struct + Go dispatch wrappers that cross the cgo boundary.

**Deliverables:**
- [x] Generate C header (callbacks struct, register function, types)
- [x] Generate Go dispatch wrappers (cgo: Go → C callback calls)
- [x] Generate Go↔C struct converters (both directions)
- [x] Generate cgo-exported `Register()` callable from C
- [x] REST dispatch wrappers (JSON → Go → C → errno → JSON)

**Tests:** 5 passing
- [x] Unit: golden-file comparison of generated .h and .go output
- [x] Unit: cgo keyword field escaping (`type` → `_type`)
- [x] Unit: void return functions, primitive return functions

**Generated files:** `kins_api.h` + `kins_cgo.go`, `tp_api.h` + `tp_cgo.go`,
`home_api.h` + `home_cgo.go`, `mot_api.h` + `mot_cgo.go` (in `generated/gmi/`)

### Step 4: Client Generation (PARTIAL)

Enable inter-module calls (direct) and external REST clients.

**Deliverables:**
- [ ] `--client-go` — typed Go wrapper around `apiserver.GetAPI()` + type assertion
- [x] `--client-c` internal — C header with `<api>_api_get()` for cmod→cmod/gomod (direct callback)
- [x] `--client-c` REST — C REST client using libgmi (for external programs)

**Tests:**
- [ ] Unit: golden-file comparison of generated client code
- [ ] Integration: gomod→gomod direct call
- [ ] Integration: gomod→cmod direct call (via cgo)
- [x] Integration: cmod→cmod direct call (pure C function pointers) — motmod→tp, motmod→home
- [ ] Integration: C REST client → HTTP server → cmod roundtrip

**Runtime library (libgmi):** Complete in `src/gmi/lib/`
- `gmi.h` — main include
- `gmi_http.c/h` — HTTP client (libcurl wrapper)
- `gmi_json.c/h` — JSON utilities (cJSON wrapper)
- `gmi_error.c/h` — error codes (GMI_ERR_*)
- `gmi_types.c/h` — type utilities

### Step 5: Python Client Generation (NOT STARTED)

REST client for Python UIs (axis, gmoccapy, etc.).

**Deliverables:**
- [ ] `--client-py` — generate Python REST client module using `requests`/`urllib`
- [ ] Generate typed Python classes from IDL `type`/`enum` declarations
- [ ] Generate method wrappers with path/query param handling

**Tests:**
- [ ] Unit: golden-file comparison of generated .py output
- [ ] Integration: generated Python client → HTTP server → roundtrip (pytest)

### Step 6: Polish (NOT STARTED)
- [ ] Error handling standardization
- [ ] Logging/tracing
- [ ] Performance optimization
- [ ] Documentation

## Open Questions

1. ~~**Versioning strategy**: How to handle API version mismatches?~~ **Resolved**: Exact match required, fail at lookup
2. ~~**Hot reload**: Can APIs be re-registered while running?~~ **Resolved**: No, lookup at startup only
3. ~~**Timeout handling**: Per-call timeouts? Global?~~ **Resolved**: No function timeouts, only HTTP transport
4. ~~**Error codes**: Standardize across Go/C boundary?~~ **Resolved**: errno for inter-module callbacks, GMI_ERR_* for client library

## Design Decisions

### API Lookup at Startup (Not Runtime)

API lookup happens during module initialization, not at function call time:

```c
// In cmod init function (receives gomc_api_t* from launcher):
int my_module_init(const gomc_api_t *api) {
    // Lookup happens here - fails fast if API unavailable or version mismatch
    // Returns opaque pointer, cast to typed callbacks struct
    kins_api = kins_api_get(api, "default");  // wrapper for api->get_api()
    if (!kins_api) {
        return -ENOENT;  // Fail module load
    }
    
    // All callback pointers now resolved
    // Runtime calls are direct pointer calls - RT safe
    return 0;
}

// At runtime - direct call, no lookup, no dispatch table:
kins_pose_t pose;
int rc = kins_api->forward(joints, &pose, fflags, &iflags);
```

**Benefits:**
- Fail-fast: Version/availability issues caught at startup
- RT-safe: No allocation or lookup in call path
- Predictable: All dependencies resolved before operation

### Version Matching

Exact version match required at lookup time:

```c
// Generated wrapper in <api>_api.h calls through gomc_api_t:
static inline const kins_callbacks_t *kins_api_get(
    const gomc_api_t *api,
    const char *instance_name)
{
    return (const kins_callbacks_t *)api->get_api(
        api->ctx, "kins", 1, instance_name);
}

// Returns NULL if:
// - Instance not found
// - Version mismatch (registered != required)
```

No backward/forward compatibility - keeps things simple and safe.

### No Function Timeouts

API function calls behave like normal C/Go function calls:
- No internal timeout handling
- Caller is responsible for not blocking inappropriately
- HTTP transport layer has its own timeouts (for external REST only)

**Rationale:** 
- RT callbacks must be deterministic - no timeout machinery
- Simplifies implementation
- Matches normal function call semantics

### Error Handling: Two Domains

There are two separate error code domains:

**Inter-module callback API** (cmod↔cmod, cmod↔gomod): Use standard Linux errno
codes for consistency with C ecosystem:

```c
// Callback return values:
//   0         = success
//   -EINVAL   = invalid argument
//   -ENOENT   = not found (pin, signal, etc.)
//   -ENOMEM   = allocation failed
//   -EBUSY    = resource busy
//   -EPERM    = permission denied (future auth)
//   -ENOSYS   = function not implemented
//   -EEXIST   = already exists
//   -ERANGE   = value out of range

// Example callback signature:
typedef int (*hal_pin_read_fn)(const char *name, hal_pin_info_t *out);
// Returns 0 on success, -errno on failure
```

**In Go:**
```go
import "syscall"

func (api *HalAPI) PinRead(name string) (*PinInfo, error) {
    // ...
    if notFound {
        return nil, syscall.ENOENT
    }
    return &info, nil
}
```

**Client library (libgmi)**: Uses custom `GMI_ERR_*` codes for HTTP/JSON/curl
domain-specific errors. These do not overlap with errno:

```c
// Client library error codes (negative, libgmi-specific):
//   GMI_OK            =  0   // Success
//   GMI_ERR_ALLOC     = -1   // Memory allocation failed
//   GMI_ERR_CURL      = -2   // CURL operation failed
//   GMI_ERR_JSON      = -3   // JSON parse/encode error
//   GMI_ERR_OVERFLOW  = -4   // Buffer overflow
//   GMI_ERR_INVALID   = -5   // Invalid argument
//   GMI_ERR_NOT_FOUND = -6   // Resource not found
//   GMI_ERR_TIMEOUT   = -7   // Operation timed out
//   GMI_ERR_IO        = -8   // I/O error
//   >= 100                   // HTTP status code (returned as-is)
```

**For detailed errors** (when simple errno insufficient):
```c
typedef struct {
    const char *message;  // Human-readable error detail (optional, can be NULL)
    // ... response fields
} hal_response_t;

// Caller checks return code first, then response.message if needed
```
