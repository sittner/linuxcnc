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
| 4: Client Generation | ✅ Complete | 14 |
| 4.5: halcmd REST Tool | ✅ Complete | — |
| 5: Python Client | ✅ Complete | 3 |
| 5.1: Manualtoolchange REST | ✅ Complete | — |
| 5.2: AXIS UI Watch Channel | ✅ Complete | 2 |
| 5.3: PyVCP REST/WebSocket | ✅ Complete | — |
| 5.4: INI REST Migration | ✅ Complete | 6 |
| 6: Polish | ❌ Not Started | — |
| 7: Remove Go Plugins | ✅ Complete | — |

**Total: 81 tests passing**

**Inter-module call patterns tested:**
- cmod→cmod ✅ (directtest)
- gomod→cmod ✅ (directtest)  
- gomod→gomod ✅ (gomodtest)
- cmod→gomod ✅ (cmodtogomod)

## Overview

The system enables modules (cmod/gomod) to:
1. Register API implementations (expose functionality)
2. Lookup and call APIs (consume functionality)
3. Optionally expose APIs via REST for external clients

All module code works with native structs (C or Go). JSON/REST handling is
centralized in the gomc-server binary and invisible to modules.

## Architecture

```
                     External REST Client
                            │
                            │ JSON/HTTP (localhost:port)
                            ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                         GOMC-SERVER (Go)                                 │
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

All protocol handling (HTTP, JSON) is centralized in the gomc-server binary.

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

// Register this API implementation with the gomc-server.
// Internally generates Go dispatch wrappers (via cgo) embedded in FuncMeta.
int hal_api_register(const char *instance_name, const hal_callbacks_t *callbacks);
```

### Generated Go Dispatch Wrappers (internal, from `--server-c`)

These are generated alongside the C header and compiled into the gomc-server binary.
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
// halcmd_callbacks_t *api = halcmd_api_get("halcmd", 1);
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
// api, _ := GetHalcmdAPI("halcmd", 1)
// pins, err := api.ListPins("*")
```

## Call Flow Examples

### External REST → cmod (or gomod — identical path)

```
1. HTTP request: GET /api/v1/hal/pin/axis.0.pos-cmd
2. HTTP server looks up "hal" in registry → RegisteredAPI
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
  - gomod called GetHalAPI("hal", 1)
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
  - cmod called halcmd_api_get("halcmd", 1)
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
  - cmod called hal_api_get("hal", 1)
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
hal_callbacks_t *api = (hal_callbacks_t *)gmi_get_api("hal", 1);
hal_pin_info_t info;
int rc = api->pin_read("axis.0.pos-cmd", &info);
```

```go
// gomod→gomod: direct Go interface call
api, _ := GetHalAPI("hal", 1)  // returns HalCallbacks interface
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

src/gomc/                   # Go module: github.com/sittner/linuxcnc/src/gomc
├── go.mod.in               # Tracked base go.mod (copied to go.mod on fresh build)
├── go.mod                   # Runtime go.mod (gitignored, managed by modcompile)
├── go.sum                   # Runtime go.sum (gitignored, managed by go toolchain)
├── packages.conf.in         # Tracked base package registry
├── packages.conf            # Runtime package registry (gitignored, managed by modcompile)
├── .gitignore               # Ignores runtime files: go.mod, go.sum, packages.conf, imports_generated.go
├── Submakefile              # Build rules for all Go targets
├── cmd/
│   ├── gomc-server/         # Server binary (compiled-in architecture)
│   │   ├── main.go
│   │   └── imports_generated.go  # Generated blank imports (gitignored)
│   ├── modcompile/          # Unified tool: .comp + .gmi + gomod management
│   │   └── main.go
│   ├── ads-xml-gen/         # ADS XML generator
│   │   └── main.go
│   └── halcmd/              # Go REST-based halcmd replacement (Step 4.5)
│       └── main.go
├── generated/               # Generated code (gitignored)
│   └── gmi/
│       ├── halcmd/          # halcmd_client.go (Go REST client)
│       ├── home/            # home_api.h, home_cgo.go
│       ├── kins/            # kins_api.h, kins_cgo.go
│       ├── mot/             # mot_api.h, mot_cgo.go
│       ├── manualtoolchange/ # manualtoolchange_api.h, manualtoolchange_cgo.go
│       └── tp/              # tp_api.h, tp_cgo.go
├── external/                # Installed external Go packages (gitignored)
│   └── <name>/              # Copied source + .origin marker file
├── internal/
│   ├── ads/                 # ADS server (moved from external plugin, Phase 2)
│   ├── adsbridge/           # ADS bridge layer
│   ├── adsconfig/           # ADS configuration
│   ├── adsmodule/           # init() registers "ads-server" with gomc registry
│   ├── apiserver/           # REST server (Step 1)
│   │   ├── types.go         # DispatchFunc, FuncMeta, APIMeta, RegisteredAPI
│   │   ├── registry.go      # Register(), GetAPI(), thread-safe map
│   │   ├── server.go        # HTTP handler, path matching
│   │   ├── *_test.go        # 37 tests
│   │   └── directtest/      # cmod direct-call simulation tests
│   ├── config/              # Compile-time config (paths injected via -ldflags)
│   │   └── paths.go         # EMC2GomcDir, EMC2BinDir, etc.
│   ├── gomc/                # Server lifecycle
│   │   ├── launcher.go      # Main server struct + startup
│   │   ├── rest_server.go   # REST API server start/stop ([GMC]REST_ADDR)
│   │   └── cleanup.go       # Shutdown sequence
│   ├── halrest/             # Server-side REST handler for halcmd API (Step 4.5)
│   │   └── halrest.go       # Dispatches REST calls to internal/halcmd
│   ├── inirest/             # Server-side REST handler for INI file access (Step 5.4)
│   │   ├── inirest.go       # POST /query dispatch, reads from launcher's parsed INI
│   │   └── inirest_test.go  # 6 tests (single, missing, empty, findall, bulk)
│   └── gmicompile/          # Code generator (parses .gmi → C/Go)
│       ├── ast/             # AST types
│       ├── parser/          # IDL parser (8 tests)
│       └── cgen/            # Code generators
│           ├── server.go        # --server-c: C header generation
│           ├── dispatch_c.go    # --server-c: Go cgo dispatch wrappers
│           ├── server_go.go     # --server-go: Go server generation
│           ├── client.go        # --client-c: C REST client generation
│           ├── client_go.go     # --client-go: Go REST client generation
│           └── client_py.go     # --client-python: Python REST client generation
├── pkg/
│   ├── cmodule/             # C module headers (gomc_*.h)
│   ├── gomc/                # Public registration interface for external packages
│   │   └── gomc.go          # RegisterModule(), RegisterMeta(), GetFactory(), HasModule()
│   ├── inifile/             # INI file parser
│   └── hal/                 # Go HAL bindings
│       ├── examples/        # passthrough example
│       └── tests/           # str-sender, str-receiver
└── pkgreg/                  # Package registry (packages.conf reader/writer)
    └── registry.go          # Registry type, GenerateImports()
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
- [x] Generated code properly gitignored (`src/gomc/.gitignore`)
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

### Step 4: Client Generation (COMPLETE)

Enable inter-module calls (direct) and external REST clients.

**Deliverables:**
- [x] `--client-go` — Go REST client for external programs (halcmd replacement)
- [x] `--client-c` internal — C header with `<api>_api_get()` for cmod→cmod/gomod (direct callback)
- [x] `--client-c` REST — C REST client using libgmi (for external programs)

**Tests:** All four calling patterns tested (14 tests total)
- [x] cmod→cmod: `directtest/` — C callbacks registered, Go looks up, calls via cgo (4 tests)
- [x] gomod→cmod: `directtest/` — same mechanism, Go code calling C function pointers
- [x] gomod→gomod: `gomodtest/` — pure Go interface registration and lookup (5 tests)
- [x] cmod→gomod: `cmodtogomod/` — C code calling `//export` Go functions (5 tests)

**Runtime library (libgmi):** Complete in `src/gmi/lib/`
- `gmi.h` — main include
- `gmi_http.c/h` — HTTP client (libcurl wrapper)
- `gmi_json.c/h` — JSON utilities (cJSON wrapper)
- `gmi_error.c/h` — error codes (GMI_ERR_*)
- `gmi_types.c/h` — type utilities

### Step 4.5: halcmd REST Tool (COMPLETE)

Replace the legacy C halcmd/halrmt with a new Go-based halcmd using the REST API.

**Motivation:**
- Validates `--client-go` in real-world usage
- Removes ~8k lines of old C halcmd code
- halrmt becomes redundant (REST is inherently remote-capable)
- Consistent architecture: all external tools use REST

**Deliverables:**
- [x] New `cmd/halcmd/` in launcher — Go CLI using generated halcmd client
- [x] Environment variable `GMC_REST_URL` (default: `http://localhost:5080/`)
- [x] Full command compatibility (show, list, getp, setp, gets, sets, newsig, delsig, net, loadrt, etc.)
- [x] Disable old halcmd/halrmt in build system (`BUILD_GOLANG=yes` guard)

**Commands mapped to REST:**
| halcmd command | REST API call |
|----------------|---------------|
| `show pin [pattern]` | GET /pins?pattern= |
| `show sig [pattern]` | GET /signals?pattern= |
| `show param [pattern]` | GET /params?pattern= |
| `show comp [pattern]` | GET /components?pattern= |
| `show funct [pattern]` | GET /functions?pattern= |
| `show thread [pattern]` | GET /threads?pattern= |
| `status` | GET /status |
| `getp <pin>` | GET /pin/{name} |
| `gets <signal>` | GET /signal/{name} |
| `setp <pin> <value>` | PUT /pin/{name} |
| `sets <signal> <value>` | PUT /signal/{name} |
| `newsig <name> <type>` | POST /signal |
| `delsig <name>` | DELETE /signal/{name} |
| `net <signal> <pins>` | POST /net |
| `linksp <signal> <pin>` | POST /link |
| `linkpp <pin1> <pin2>` | POST /linkpp |
| `unlinkp <pin>` | DELETE /link/{pin} |
| `loadrt <module> [args]` | POST /loadrt |
| `unloadrt <module>` | DELETE /loadrt/{module} |
| `loadusr [-W] <cmd>` | POST /loadusr |
| `unloadusr <name>` | DELETE /loadusr/{name} |
| `waitusr <name>` | POST /waitusr/{name} |
| `newthread <n> <period>` | POST /thread |
| `delthread <name>` | DELETE /thread/{name} |
| `addf <func> <thread>` | POST /thread/{thread}/function |
| `delf <func> <thread>` | DELETE /thread/{thread}/function/{func} |
| `start` | POST /start |
| `stop` | POST /stop |
| `alias pin <n> <a>` | POST /pin/{n}/alias |
| `unalias pin <n>` | DELETE /pin/{n}/alias |
| `lock [level]` | POST /lock |
| `unlock [level]` | POST /unlock |
| `debug <level>` | PUT /debug |
| `save [type]` | GET /save |

### Step 5: Python Client Generation (COMPLETE)

REST client for Python UIs (axis, gmoccapy, etc.).

**Deliverables:**
- [x] `--client-python` — generate Python REST client module using `urllib` (stdlib only, no external deps)
- [x] Generate `@dataclass` classes from IDL `type` declarations (with `from_dict()`/`to_dict()`)
- [x] Generate `IntEnum` subclasses from IDL `enum` declarations
- [x] Generate `<Api>Client` class with typed methods, path/query param handling, JSON body
- [x] `APIError` exception class for HTTP error responses
- [x] Wired into gmicompile CLI (`--client-python` mode with `@rest_export` validation)
- [x] Generated halcmd Python client (621 lines, valid Python syntax)

**Implementation:** `internal/gmicompile/cgen/client_py.go`

**Tests:** 3 passing (`client_py_test.go`)
- [x] Unit: full API generation (types, enums, constants, client class, methods)
- [x] Unit: multiple path parameter substitution
- [x] Unit: primitive return types and void methods

### Step 5.1: Manualtoolchange REST Migration (COMPLETE)

First real consumer of the GMI pipeline: replace `hal_manualtoolchange.py`
(HAL userspace component in Python that directly accesses HAL pins) with a
cmod + REST API architecture.

**Architecture:**
- `manualtoolchange.comp` — cmod (C, RT-capable) handling HAL pins + iocontrol handshake
- `manualtoolchange.gmi` — IDL defining REST API (GET /state, POST /confirm)
- Generated dispatch (`manualtoolchange_cgo.go`) — compiled into gomc-server
- Generated Python client (`manualtoolchange_client.py`) — used by UI
- `manualtoolchange_ui.py` — Tkinter UI, polls REST, replaces old `hal_manualtoolchange.py`

**Completed:**
- [x] `gmi/idl/manualtoolchange.gmi` — IDL with `@rest_export true`, two endpoints
- [x] `hal/components/manualtoolchange.comp` — cmod with `gmi_provide manualtoolchange`,
      HAL pins (change, number, change_button, changed), thread function,
      GMI callbacks (`gmi_manualtoolchange_get_state`, `gmi_manualtoolchange_confirm`)
- [x] Generated `manualtoolchange_api.h` + `manualtoolchange_cgo.go` in
      `gomc/generated/gmi/manualtoolchange/`
- [x] Generated `lib/python/gmi/manualtoolchange_client.py` — Python REST client
- [x] `manualtoolchange_ui.py` (133 lines) — Tkinter UI using generated REST client
- [x] `gmi/codegen/Submakefile` — build rules for API header, cgo dispatch, Python client
- [x] `hal/components/Submakefile` — cmod build rule with GMI header dependency
- [x] `bin/manualtoolchange_ui` — installed UI script
- [x] Generated cgo uses `internal/apiserver` (correct import path)
- [x] `packages.conf` entry present — cgo dispatch compiled into gomc-server
- [x] `lib/python/gmi/__init__.py` — hand-written source in `src/gmi/python/`, copied at build time
- [x] All HAL configs migrated from `hal_manualtoolchange` to `manualtoolchange` cmod
- [x] `sim_lib.tcl` `use_hal_manualtoolchange` proc updated (3-line cmod form)
- [x] `stepconf/build_HAL.py` and `pncconf/build_HAL.py` updated
- [x] `qtvcp/widgets/dialog_widget.py` updated to use `manualtoolchange` prefix

**Notes:**
- The proc/option name `use_hal_manualtoolchange` / `-no_use_hal_manualtoolchange`
  is kept for backward compatibility with user INI files. Only the implementation
  changed (loads cmod instead of `loadusr`).
- `hal_manualtoolchange.py` (old Python component) is kept but deprecated — to be
  removed in a future cleanup pass.

### Step 5.2: AXIS UI Watch Channel (COMPLETE)

Migrate AXIS GUI's HAL pins to a WebSocket-based watch channel, eliminating the
UI's dependency on HAL shared memory. This is a preparation step for removing
all shared memory access from UI processes.

**Motivation:**
- Mid-term goal: UI processes must not access HAL shared memory
- AXIS currently exports ~25 HAL pins (jog, status, notifications, sliders)
- REST polling is adequate for slow state (notifications, errors) but insufficient
  for jog buttons (~10ms) and position updates (~50ms)
- NML status polling (current mechanism for machine position) will also need
  replacement — the watch channel infrastructure serves both needs

**Architecture: GMI Watch Channel (WebSocket)**

A single persistent WebSocket connection between UI and gomc-server:

```
axis.py (Tk)                    gomc-server
    │                               │
    ├─ REST (1s) ──────────────────►│  tool change, notifications
    │                               │
    └─ WebSocket (persistent) ◄────►│  position/status push (50ms)
         jog commands (immediate) ──►│  jog start/stop, abort
         slider values ────────────►│  feed override, spindle override
```

**Server→Client (push):** Server calls `@watch`-annotated functions at the
subscribed rate and pushes results as JSON over the WebSocket.

**Client→Server (commands):** Jog start/stop, abort, slider values sent as
JSON command messages over the same connection. Minimal framing overhead
(~2 bytes WebSocket header vs ~200 bytes HTTP per request).

**Why WebSocket:**
- Bidirectional on one persistent TCP connection
- ~1-5ms LAN latency (limited only by TCP + Go scheduling)
- Native Python support (`websockets` / `asyncio`)
- Tk integration: run in thread, post events to mainloop
- Works over network / reverse proxies → remote UI for free
- SSE is push-only (still need REST for commands); gRPC is heavy for Python/Tk;
  long-poll has per-request overhead defeating 50ms updates

**GMI IDL Extensions:**

`@watch` is a function-level annotation. Watchable functions can return any
GMI type (structs, enums, arrays, nested structs). The framework serializes
whatever the function returns.

```gmi
@api axis

type Position {
    x: f64
    y: f64
    z: f64
    a: f64
    b: f64
    c: f64
}

type JogState {
    active_axis: string
    increment: f64
    disabled: bool
}

type MachineStatus {
    position: Position
    jog: JogState
    is_running: bool
    has_error: bool
    has_notifications: bool
}

type Notification {
    level: NotifyLevel
    message: string
}

enum NotifyLevel {
    Info = 0
    Error = 1
}

enum NotifyClearMask {
    All = 0
    Info = 1
    Error = 2
}

# Push at 50ms — client subscribes, server calls at requested rate
@watch true
@watch_default_rate 50ms
func get_status() -> MachineStatus

# Slower status, separate subscription
@watch true
@watch_default_rate 1000ms
func get_notifications() -> []Notification

# Commands — not watchable, dispatched immediately over same WebSocket
func jog_start(axis: string, speed: f64) -> bool
func jog_stop(axis: string) -> bool
func set_jog_increment(value: f64) -> bool
func clear_notifications(which: NotifyClearMask) -> bool
func abort() -> bool
```

**Generated Code (gmicompile):**

| Flag | Output | Purpose |
|------|--------|---------|
| `--server-ws` | Go WebSocket handler | Subscribe/dispatch loop, per-client goroutine |
| `--client-python-ws` | Python async client | Typed callbacks, same dataclasses as REST client |

Server-side handler:
- Accepts subscription messages: `{"subscribe": "get_status", "rate_ms": 50}`
- Runs a goroutine per client per subscription that calls the GMI function
  at the requested rate and pushes the serialized result
- Receives command messages: `{"call": "jog_start", "args": {"axis": "x", "speed": 100.0}}`
- Optional delta optimization: only send fields that changed since last push

Python client:
```python
client = AxisWatchClient("ws://localhost:5080/api/v1/watch")
client.subscribe_get_status(rate_ms=50, callback=on_status_update)
client.subscribe_get_notifications(rate_ms=1000, callback=on_notifications)
# Commands go through the same connection
client.jog_start(axis="x", speed=100.0)
```

**Pin Migration Map (axis.py → axis.gmi):**

| Old HAL Pin | Direction | New GMI Mechanism |
|-------------|-----------|-------------------|
| `is-running` | OUT | `get_status().is_running` (watch @50ms) |
| `error` | OUT | `get_status().has_error` (watch @50ms) |
| `has-notifications` | OUT | `get_status().has_notifications` (watch @50ms) |
| `abort` | OUT | `abort()` command |
| `jog.{x..w}` | OUT | `get_status().jog.active_axis` (watch @50ms) |
| `jog.increment` | OUT | `get_status().jog.increment` (watch @50ms) |
| `jog.{x..w}-plus/minus` | IN | `jog_start()`/`jog_stop()` commands |
| `jog.disable` | IN | `get_status().jog.disabled` (watch @50ms) |
| `notifications-clear*` | IN | `clear_notifications()` command |
| `resume-inhibit` | IN | Part of status or separate command |
| `sliders.scale*` | IN | Slider commands (future) |

**Implementation Plan:**

- [x] GMI parser: `@watch`, `@watch_default_rate` annotations on functions
- [x] gmicompile `--server-ws`: Go WebSocket subscribe/push handler
- [x] gmicompile `--client-python-ws`: Python async watch client
- [x] gomc-server: WebSocket endpoint at `/api/v1/watch`
- [x] `axisui.gmi`: IDL with jog/slider/notification watch + set commands
- [x] axis.py: replace HAL `comp` with WebSocket client (WSCompat + AxisuiWatchThread)
- [x] axisui.comp: cmod with HAL pins and GMI callbacks
- [x] HAL config: `axisui.hal` + INI entries for sim configs
- [x] WatchFactory registry: auto-registration via init() + packages.conf
- [x] `modcompile add-gmi`: auto-registration during codegen
- [x] Python `gmi` package: `rest_url()`/`ws_url()` central URL helpers
- [x] Debian packaging: install rules for `gmi/` Python package and `cmod/*.so`
- [x] `loadusr` PID tracking: proper SIGTERM on shutdown for non-HAL child processes

**Future (out of scope for 5.2):**
- NML status channel replacement (same watch infrastructure, different GMI API)
- Multiple simultaneous UI clients (watch supports this by design)
- Remote UI over network (WebSocket works through reverse proxies)

### Step 5.3: PyVCP REST/WebSocket Migration (COMPLETE)

Migrated PyVCP from direct HAL shared memory access to REST + WebSocket,
eliminating the UI's dependency on HAL shared memory. PyVCP panels are now
pure display/input frontends communicating through the gomc-server.

**Architecture:**

A Go module (pyvcpmodule) inside gomc-server owns the HAL component.
The Python frontend is a pure display client:

```
pyvcp (Tk frontend)              gomc-server
    │                               │
    ├─ GET /panel/{name} ─────────►│  Fetch panel info + pin defs
    │                               │
    ├─ WebSocket (persistent) ◄────►│  Pin value push (100ms)
    │   set_pin commands ──────────►│  Write output pin values
    │                               │
    │                          pyvcpmodule (gomod)
    │                               │
    │                          HAL component (real pins)
    │                          via pkg/hal/ Go bindings
```

**Server-Side: pyvcpmodule**

```
src/gomc/internal/pyvcpmodule/
├── module.go        # init() → RegisterModule("pyvcp", factory)
│                    # REST dispatch (get_panel), WS watch (watch_pins, set_pin)
│                    # Panel registry, WatchRegistry for subscriptions
└── panel.go         # XML parsing for 20 widget types → HAL pin creation
                     # autoName counters match Python pyvcp_widgets.py naming
```

Startup flow:
1. gomc-server loads pyvcpmodule via `[HAL]GOMOD = pyvcp` INI directive
2. Module reads XML path from module config → parses XML → extracts pin definitions
3. Creates real HAL component with required pins via `pkg/hal/` Go bindings
4. Registers REST + WebSocket endpoints as API instance "pyvcp"
5. `comp.Ready()` — pins visible to halcmd, connectable in HAL files

**REST Endpoints (registered on apiserver as instance "pyvcp"):**

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/v1/pyvcp/panel/{name}` | Fetch panel info (name, XML, pin defs) |

**WebSocket Commands (via `/api/v1/watch`):**

| Command | Purpose |
|---------|---------|
| `watch_pins` | Subscribe to pin value push at 100ms |
| `set_pin` | Write a pin value (name + string-encoded value) |

**Client-Side: PyVCPCompat + pyvcp_compat.py**

Hand-written (not generated) drop-in replacement for `hal.component`:

```
src/gmi/python/pyvcp_compat.py → lib/python/gmi/pyvcp_compat.py (build copy)
```

- `PyVCPCompat` class: `__getitem__`/`__setitem__` interface, backed by WebSocket
- `_WatchThread`: asyncio WebSocket client in background thread
- `_flush_loop()`: batches outgoing `set_pin` writes
- Fetches panel info via REST on init, subscribes to watch_pins
- Widget code (`pyvcp_widgets.py`) requires zero changes

**AXIS Integration:**

`vcpparse.py` has a new `create_vcp_rest()` function that:
1. Fetches panel info from REST (`GET /api/v1/pyvcp/panel/{name}`)
2. Creates `PyVCPCompat` instead of `hal.component`
3. Builds widget tree from XML (existing code path)

AXIS calls `vcpparse.create_vcp_rest(f, compname="pyvcp")` — the return
value is unused since `PyVCPCompat` is self-contained (WebSocket thread).

**HAL Module Elimination from axis.py:**

With PyVCP migrated, the `hal` Python module is no longer imported by axis.py.
All HAL queries now go through the gomc REST API via the `gmi` package:

| Old (hal module, needs shared memory) | New (gmi REST) |
|---------------------------------------|----------------|
| `hal.component("axisui-display")` | Removed (axisui cmod owns pins) |
| `hal.component_exists(name)` | `gmi.component_exists(name)` |
| `hal.pin_has_writer(name)` | `gmi.pin_has_writer(name)` |

**gmi Python Package (`src/gmi/python/__init__.py`):**

Now a hand-written source file (previously an empty `@touch` build artifact).
Contains central helpers used by axis.py and other UI code:

- `rest_url()` / `ws_url()` — URL helpers from `GMC_REST_URL` env var
- `component_exists(name)` — `GET /api/v1/halcmd/components?pattern={name}`
- `pin_has_writer(name)` — `GET /api/v1/halcmd/pins?pattern={name}`, checks `has_writer` field

**halcmd REST Enhancement:**

Added `has_writer` field to the pins endpoint to support `pin_has_writer()`:

- `hal_shim_pin_info_t` C struct: new `int has_writer` field
- `hal_shim_show_pins`: sets `has_writer = (sig->writers > 0)` when pin is linked
- `PinInfo` Go struct: new `HasWriter bool` field (JSON: `"has_writer"`)
- Exposed via `GET /api/v1/halcmd/pins?pattern={name}` response

**Build System:**

- `gmi/codegen/Submakefile`: dedicated copy rule for `__init__.py` (source → build)
- Other GMI Python targets depend on `$(GMI_INIT_DST)` instead of `@touch`
- `pyvcp_compat.py` copy rule (same pattern)
- `packages.conf.in`: pyvcpmodule compiled into gomc-server

**INI Configuration:**

```ini
[HAL]
GOMOD = pyvcp xml=pyvcp_demo1.xml
HALFILE = pyvcp_rest.hal
HALFILE = custom.hal    # was POSTGUI_HALFILE — moved since gomod creates pins before GUI

[DISPLAY]
PYVCP = pyvcp_demo1.xml
```

No `POSTGUI_HALFILE` needed — the Go module creates HAL pins during module
init (before AXIS starts), so `custom.hal` can `net` pins as a regular HALFILE.

**Completed:**
- [x] `internal/pyvcpmodule/module.go` — gomod: REST dispatch, WebSocket watch, panel registry
- [x] `internal/pyvcpmodule/panel.go` — XML parser for 20 widget types, HAL pin creation
- [x] `src/gmi/python/pyvcp_compat.py` — PyVCPCompat + WatchThread (WebSocket client)
- [x] `src/gmi/python/__init__.py` — package init with `rest_url()`, `ws_url()`,
      `component_exists()`, `pin_has_writer()` helpers
- [x] `gmi/codegen/Submakefile` — build rules for `__init__.py` + `pyvcp_compat.py` copy
- [x] `vcpparse.py` — `create_vcp_rest()` function for REST/WS panel creation
- [x] `axis.py` — uses `gmi.component_exists()`, `gmi.pin_has_writer()`, no `import hal`
- [x] `halcmd/cgo.go` — `has_writer` in `hal_shim_pin_info_t` + `PinInfo`
- [x] `halcmd/halcmd.go` — `HasWriter bool` field on `PinInfo`
- [x] `packages.conf.in` — pyvcpmodule entry
- [x] `configs/sim/pyvcp_demo/pyvcp_rest.ini` — sim config (no POSTGUI_HALFILE)
- [x] Pin naming: dial/spinbox/scale always use `autoName()` counters (matches Python)

**Notes:**
- Widget code (`pyvcp_widgets.py`) required zero changes
- No GMI IDL file — pyvcpmodule uses hand-written REST/WS dispatch (not gmicompile-generated)
- The 100ms WebSocket push rate matches existing PyVCP polling interval
- Panel XML is parsed server-side; widgets are built client-side from the same XML
- HAL pins are real HAL pins, fully visible to halcmd and connectable in HAL files
- `axis.py` no longer imports the `hal` Python module at all

### Step 5.4: INI File REST Migration (COMPLETE)

Replace direct INI file parsing in axis.py (`linuxcnc.ini()`) with REST
queries to gomc-server, eliminating the `liblinuxcnc` C extension dependency
for INI access.

**Motivation:**
- axis.py currently uses `linuxcnc.ini(sys.argv[2])` to parse the INI file
  directly from disk via the C `liblinuxcnc` extension
- With HAL removed (Step 5.3), INI is the next `liblinuxcnc` dependency to eliminate
- Remaining `liblinuxcnc` deps (`linuxcnc.stat()`, `linuxcnc.command()`,
  `linuxcnc.error_channel()`) depend on NML and will be removed after the
  server-internal NML→GMI migration

**Scope:**
- 82 `inifile.find()` / `inifile.findall()` calls in axis.py across
  sections: DISPLAY, TRAJ, EMC, RS274NGC, EMCIO, KINS, FILTER, TASK,
  JOINT_N, AXIS_X/A/B/C, GMC
- Read-only — axis.py never writes to INI

**Architecture:**

gomc-server already has a full INI parser (`pkg/inifile/`) and the parsed
INI is available in the launcher. A new REST module exposes it:

```
axis.py                          gomc-server
    │                               │
    ├─ POST /api/v1/ini/query ─────►│  Bulk INI lookup
    │  [{section, key}, ...]        │  returns all values in one response
    │◄──────────────────────────────┤
    │  [{value: "..."}, ...]        │
    │                               │
    │                          internal/inirest/
    │                               │
    │                          pkg/inifile/ (already parsed)
```

**REST Endpoint:**

```
POST /api/v1/ini/query
Content-Type: application/json

[
  {"section": "DISPLAY", "key": "GEOMETRY"},
  {"section": "DISPLAY", "key": "MAX_FEED_OVERRIDE"},
  {"section": "FILTER", "key": "PROGRAM_EXTENSION", "all": true}
]

→ 200 OK
[
  {"value": "XYZABCUVW"},
  {"value": "1.5"},
  {"values": [".nc", ".ngc"]}
]
```

- `all: true` → uses `findall()` semantics, returns `values` array
- Missing keys return `{}` (no `value` field — `omitempty` on the pointer)
- Empty-value keys return `{"value": ""}` (key exists but value is empty)
- Single round-trip for all ~82 lookups at startup (~1-2ms local)

**Implementation Plan:**

1. **Go: `internal/inirest/`** — new REST module, registers as "ini" instance on
   apiserver, single `POST /query` dispatch function, reads from launcher's
   parsed `inifile.INI` (no re-parsing). Registered in `launcher.go` right
   after INI parsing, before `startAPIServer()`.
2. **Python: `gmi.IniFile` class** — lazy per-call REST with local cache.
   Each `.find()` / `.findall()` call issues a single-item POST on first
   access, caches the result, and returns from cache on subsequent calls.
   Separate caches for find (single value) and findall (value list).
3. **axis.py** — replace `inifile = linuxcnc.ini(sys.argv[2])` with
   `inifile = gmi.IniFile()`, all 82 `.find()`/`.findall()` calls work unchanged
4. **Remove `import linuxcnc` for INI** — but keep it for `stat()`, `command()`,
   `error_channel()` until NML migration (future step)

**Python Client (implemented):**

```python
class IniFile:
    def __init__(self):
        self._cache = {}      # (section, key) -> str or None
        self._cache_all = {}  # (section, key) -> list[str]

    def find(self, section, key):
        """Return first value for section/key, or None if not found."""
        cache_key = (section, key)
        if cache_key in self._cache:
            return self._cache[cache_key]
        result = self._query([{"section": section, "key": key}])
        if result and len(result) == 1:
            val = result[0].get("value")
            self._cache[cache_key] = val
            return val
        self._cache[cache_key] = None
        return None

    def findall(self, section, key):
        """Return all values for section/key as a list."""
        cache_key = (section, key)
        if cache_key in self._cache_all:
            return self._cache_all[cache_key]
        result = self._query([{"section": section, "key": key, "all": True}])
        if result and len(result) == 1:
            vals = result[0].get("values", [])
            self._cache_all[cache_key] = vals
            return vals
        self._cache_all[cache_key] = []
        return []

    def _query(self, items):
        """Issue a bulk query to the INI REST endpoint."""
        url = rest_url() + "/api/v1/ini/query"
        data = json.dumps(items).encode("utf-8")
        req = urllib.request.Request(url, data=data,
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
```

The lazy approach was chosen over bulk prefetch for simplicity — each call
is a single round-trip (~0.1ms localhost), cached after first access.
The `_query()` method accepts a list for future bulk optimization if needed.

**Deliverables:**
- [x] `internal/inirest/inirest.go` — Go REST module with `POST /query`
- [x] `src/gmi/python/__init__.py` — `IniFile` class with bulk fetch + `.find()`/`.findall()`
- [x] `axis.py` — replace `linuxcnc.ini()` with `gmi.IniFile()`, remove INI-related `import`
- [x] Tests for inirest endpoint

**Notes:**
- No GMI IDL file — inirest uses hand-written REST dispatch (same pattern as
  halrest and pyvcpmodule), not gmicompile-generated code
- Other UIs (touchy, gmoccapy, gscreen) also use `linuxcnc.ini()` — the REST
  endpoint benefits them all, but migration is per-UI
- The endpoint is read-only by design (INI is parsed once at startup)
- The `IniFile` class matches `linuxcnc.ini` return types exactly:
  `find()` returns `str | None`, `findall()` returns `list[str]`
- The Go dispatch distinguishes "key not found" (empty JSON object) from
  "key exists with empty value" (`{"value": ""}`) by checking `GetSection()`
  entries when `Get()` returns empty string
- Instance name is `"ini"` (no numeric suffix — instance identity is the name itself,
  consistent with halcmd, pyvcp, and all other singleton APIs)

### Step 6: Polish (NOT STARTED)
- [ ] Error handling standardization
- [ ] Logging/tracing
- [ ] Performance optimization
- [ ] Documentation
- [x] Launcher REST server reads listen URL from INI file (`[GMC]REST_ADDR`, default `localhost:5080`)

## Step 7: Remove Go Plugins — Compile-In Architecture (COMPLETE)

### Motivation

Go's `plugin.Open` mechanism has fundamental problems:
- **Version fragility**: Plugin must be built with exact same Go version and
  dependency versions as the host binary. Any mismatch → runtime panic.
- **No unload**: `plugin.Open` has no `Close`. Memory grows, can't hot-swap.
- **Runtime duplication**: Each plugin embeds parts of the Go runtime (~5MB baseline).
- **Size overhead**: A trivial plugin like manualtoolchange is 5.9MB (vs ~60KB as cmod).

The solution: **eliminate Go plugins entirely**. All Go packages (internal like
ads-server, generated GMI dispatch, external like galv-formula) are compiled
directly into the server binary. Adding a package = rebuild the server.

This is the same pattern used by Caddy, Traefik, and other Go-based extensible
servers. It trades dynamic loading for build-time composition — which is the
natural Go approach.

### Terminology Changes (Implemented)

| Old | New | Reason |
|-----|-----|--------|
| `linuxcnc-launcher` | `gomc-server` | Reflects role: server process, not UI launcher |
| `src/launcher/` | `src/gomc/` | Directory matches binary name |
| gomod (.so plugin) | gomod (compiled-in Go package) | Same name, but compiled in, not dynamic |
| `pkg/gomodule/` | `pkg/gomc/` | Registration interface, no plugin machinery |
| `gomodules.go` | registry lookup | No more `plugin.Open`, `loadGoPlugin` |
| `gomod/*.so` | (nothing) | No more plugin .so files |
| `gmicompile` (standalone) | `modcompile gmi` | Merged into unified tool |
| `LAUNCHER_*` vars | `GOMC_*` vars | Consistent naming in Submakefile |
| `EMC2LauncherDir` | `EMC2GomcDir` | Config field matches directory |

### What Was Removed

- `pkg/gomodule/gomodule.go` — Module interface, Factory type
- `internal/gomc/gomodules.go` — `loadGoPlugin`, `resolveGoModulePath`, etc.
- `gomod/` directory — no more plugin .so outputs
- `gomod/` top-level directory — stale build artifacts
- `share/gomodule/` — stale share directory
- `hal/proto/ads-server/` — moved to `internal/ads/`
- `EMC2_GOMOD_DIR` — no more gomod path in config
- `-buildmode=plugin` build rules in Submakefile
- `go.work` files in individual plugin dirs
- `cmd/gmicompile/` — merged into `cmd/modcompile/`

### New Architecture

#### Server Source Layout (Implemented)

The gomc module serves as both development tree and installable build directory.
For RIP, `EMC2_GOMC_DIR` points to the source tree directly. For installed
systems, the source is copied to a share directory.

Runtime files (`go.mod`, `go.sum`, `packages.conf`, `imports_generated.go`) are
gitignored and auto-generated from tracked `.in` base files on fresh checkouts.
This keeps the git tree clean after `add-gomod`/`rm-gomod` operations.

```
src/gomc/                               # = EMC2_GOMC_DIR (RIP)
├── go.mod.in                           # Tracked base (no external deps)
├── go.mod                              # Runtime (gitignored, copied from .in or managed by modcompile)
├── go.sum                              # Runtime (gitignored)
├── packages.conf.in                    # Tracked base (core gmi + internal adsmodule)
├── packages.conf                       # Runtime (gitignored, managed by modcompile)
├── cmd/
│   ├── gomc-server/
│   │   ├── main.go
│   │   └── imports_generated.go        # Generated blank imports (gitignored)
│   ├── modcompile/                     # Unified tool
│   │   └── main.go
│   ├── ads-xml-gen/
│   │   └── main.go
│   └── halcmd/
│       └── main.go
├── generated/
│   └── gmi/                            # Generated dispatch packages
│       ├── kins/                       # kins_api.h + kins_cgo.go
│       ├── tp/
│       ├── home/
│       ├── mot/
│       ├── manualtoolchange/           # REST dispatch for manualtoolchange cmod
│       └── halcmd/                     # halcmd Go REST client
├── external/                           # Installed external Go packages (gitignored)
│   └── <name>/                         # Copied source + .origin marker, go.mod stripped
├── internal/
│   ├── ads/                            # ads-server (moved in-tree, Phase 2)
│   ├── adsmodule/                      # init() registers "ads-server" with gomc
│   ├── apiserver/                      # REST server + registry
│   ├── config/                         # Compile-time paths (injected via -ldflags -X)
│   ├── gomc/                           # Server lifecycle
│   └── gmicompile/                     # .gmi codegen (parser + C/Go/Python generators)
├── pkg/
│   ├── cmodule/                        # C module headers (gomc_*.h)
│   ├── gomc/                           # Public registration: RegisterModule(), RegisterMeta()
│   ├── inifile/                        # INI file parser
│   └── hal/                            # Go HAL bindings
└── pkgreg/                             # Package registry reader/writer
    └── registry.go
```

#### Package Registry (`packages.conf`)

A simple text file that `modcompile` reads and writes. Tracks all Go packages
compiled into the server — both GMI dispatch packages and full Go modules.

Two copies exist:
- **`packages.conf.in`** — tracked in git, contains the base set (core gmi + internal modules)
- **`packages.conf`** — gitignored runtime copy, modified by `add-gomod`/`rm-gomod`

On fresh checkout, the Makefile copies `.in` → runtime if missing. `modcompile`
also calls `ensureRuntimeFiles()` before any registry operation.

```ini
# packages.conf — managed by modcompile. DO NOT EDIT MANUALLY.
#
# Format: TYPE IMPORT_PATH
#
# TYPE:
#   gmi     — generated GMI dispatch package (compiled into gomc-server)
#   gomod   — Go module compiled into gomc-server
#
# IMPORT_PATH — relative path within the gomc module for blank import

# Core GMI dispatch (generated, part of this module)
gmi generated/gmi/kins
gmi generated/gmi/tp
gmi generated/gmi/home
gmi generated/gmi/mot
gmi generated/gmi/manualtoolchange
gmi generated/gmi/halcmd

# Go modules (in-tree and installed external)
gomod internal/adsmodule
```

#### Generated Files

`modcompile` regenerates `imports_generated.go` from `packages.conf`:

**`imports_generated.go`** — blank imports that pull packages into the binary:

```go
// Code generated by modcompile. DO NOT EDIT.
package main

import (
    // GMI dispatch packages
    _ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/kins"
    _ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/tp"
    _ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/home"
    _ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/mot"
    _ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/manualtoolchange"
    _ "github.com/sittner/linuxcnc/src/gomc/generated/gmi/halcmd"

    // Go modules
    _ "github.com/sittner/linuxcnc/src/gomc/internal/adsmodule"
)
```

External packages are copied into `external/<name>/` (with their `go.mod`
stripped) and referenced as relative paths in the module. Third-party
dependencies from external packages are merged into the main `go.mod` via
`go get`.

### Unified `modcompile` Tool (Implemented)

`modcompile` is the single entry point for all module operations:

```
modcompile [global-flags] <command> [command-flags] [args...]

Build environment queries:
  --cflags        Print C compiler flags for cmod builds
  --ldflags       Print linker flags for cmod builds
  --cmod-dir      Print cmod installation directory
  --include-dir   Print cmod include directory
  --go            Print Go binary path
  --print-make-inc  Print Makefile include snippet (GOMC_DIR variable)

.comp compiler:
  --parse FILE         Parse .comp and dump AST (JSON)
  --preprocess FILE    Generate C source from .comp
  --document FILE      Generate documentation from .comp
  --view-doc FILE      Generate and display documentation
  --compile FILE       Compile .comp to cmod .so
  --install FILE       Compile and install .comp to EMC2_CMOD_DIR
  --uninstall NAME     Remove an installed cmod

GMI code generator:
  gmi [flags] FILE     Generate dispatch code from .gmi file
    --server-c         Generate C header + Go cgo dispatch
    --client-c         Generate C REST client
    --client-go        Generate Go REST client
    --client-python    Generate Python REST client

Package registry:
  list                 List registered packages from packages.conf
  rebuild              Regenerate imports + rebuild gomc-server
  regenerate-imports   Regenerate imports_generated.go only (for Makefile use)
  add-gomod [-f] DIR   Copy external Go package and rebuild
  rm-gomod NAME        Remove external Go package and rebuild

Examples:
  # Compile and install a HAL component:
  modcompile --install mycomponent.comp

  # Generate GMI dispatch from IDL:
  modcompile gmi --server-c src/gmi/idl/manualtoolchange.gmi

  # Add external Go package + rebuild server:
  modcompile add-gomod ~/source/galv-mqtt-receiver/galv-formula

  # Remove a Go package:
  modcompile rm-gomod galv-formula

  # Rebuild after manual edits:
  modcompile rebuild
```

#### `modcompile gmi` Workflow

```
1. Parse the .gmi file
2. Generate C header + Go cgo dispatch to generated/gmi/<api>/
3. If @rest_export true: also generate Python client to lib/python/gmi/
4. (Submakefile handles adding gmi entry to packages.conf and regenerating imports)
```

#### `modcompile add-gomod` Workflow (Implemented)

```
1. Validate: target directory has go.mod
2. Name = directory basename; destination = external/<name>/
3. dirMirror: pure Go directory copy (replaces rsync), excludes go.mod
4. Write .origin marker file (records source path for reinstall detection)
5. mergeGoDeps: parse external go.mod via "go mod edit -json",
   filter out local replaces and self-references,
   run "go get <dep>@<version>" for remaining third-party deps
6. Add "gomod external/<name>" to packages.conf
7. Regenerate imports_generated.go
8. Rebuild gomc-server binary
```

Key implementation details:
- External packages' `go.mod` is **not** copied — the package becomes a
  sub-directory of the gomc module, not a separate module
- Third-party dependencies are merged into the main `go.mod` via `go get`
- Same-source reinstall is auto-detected via `.origin` file (no `--force` needed)
- `dirMirror()` is a pure Go replacement for `rsync --delete`, avoiding
  the external tool dependency

#### `modcompile rm-gomod` Workflow (Implemented)

```
1. Delete external/<name>/ directory
2. Remove "gomod external/<name>" from packages.conf
3. Regenerate imports_generated.go
4. Run "go mod tidy" to clean up orphaned dependencies
5. Rebuild gomc-server binary
```

#### `modcompile rebuild` Workflow

```
1. Read packages.conf
2. Regenerate imports_generated.go
3. cd EMC2_GOMC_DIR && go build -ldflags "..." -o $BIN/gomc-server ./cmd/gomc-server/
```

#### `modcompile regenerate-imports` Workflow

```
1. Read packages.conf
2. Regenerate imports_generated.go only (no build)
```

This subcommand exists specifically for Makefile use to avoid a race condition:
with parallel builds (`make -j8`), the `imports_generated.go` rule must not
trigger a `go build` because GMI codegen targets may not have finished yet.
The actual build is handled by the `../bin/gomc-server` Makefile target which
has proper GMI dependencies.

### Build System Integration (Implemented)

#### Makefile (`gomc/Submakefile`)

```makefile
# Runtime files: .in → working copy (on fresh checkout)
gomc/packages.conf: gomc/packages.conf.in
    $(Q)test -f $@ || cp $< $@

gomc/go.mod: gomc/go.mod.in
    $(Q)test -f $@ || cp $< $@

# imports_generated.go uses regenerate-imports (NOT rebuild) to avoid
# race conditions with parallel builds — GMI codegen may still be running.
$(IMPORTS_GENERATED): gomc/packages.conf ../bin/modcompile
    $(Q)test -f $@ || cd gomc && $(TOP)/bin/modcompile regenerate-imports

# GOMC_SRC_BASE includes gomc/go.mod (generated) — forces copy from .in.
# filter-out excludes imports_generated.go to break circular dependency
# (modcompile depends on GOMC_SRC_BASE, imports_generated depends on modcompile).
GOMC_SRC_BASE := $(filter-out $(IMPORTS_GENERATED),...) gomc/go.mod.in gomc/go.mod

# gomc-server: depends on generated GMI files + imports + source
../bin/gomc-server: $(GOMC_SRC) $(GMI_KINS_GEN_GO) $(IMPORTS_GENERATED) \
                    gomc/packages.conf ../lib/liblinuxcnchal.so
    cd gomc && CGO_LDFLAGS="..." $(GO) build -ldflags "$(GOMC_LDFLAGS)" \
        -o $(TOP)/bin/gomc-server ./cmd/gomc-server
```

Key Makefile design decisions:
- **`gomc/go.mod` in `GOMC_SRC_BASE`**: All Go targets automatically depend on
  it, triggering the copy-from-`.in` rule on fresh checkouts
- **`filter-out` for `IMPORTS_GENERATED`**: Breaks circular dependency where
  modcompile → GOMC_SRC_BASE → imports_generated → modcompile
- **`regenerate-imports` not `rebuild`**: The Makefile rule only generates the
  imports file, not the binary. The `../bin/gomc-server` target handles the
  actual build with proper prerequisite ordering
- **`test -f` guards**: Idempotent — don't overwrite existing files

#### Environment Variables

Set by `scripts/rip-environment` (RIP) or read from installed paths:

| Variable | RIP Value | Installed Value |
|----------|-----------|-----------------|
| `EMC2_GOMC_DIR` | `$EMC2_HOME/src/gomc` | `$prefix/share/linuxcnc/gomc` |
| `EMC2_CMOD_DIR` | `$EMC2_HOME/cmod` | `$prefix/lib/linuxcnc/cmod` |

#### Submakefile Variables (GOMC_* namespace)

| Variable | Purpose |
|----------|---------|
| `GOMC_LDFLAGS_PKG` | Go package path for `-ldflags -X` injection |
| `EMC2_GOMC_DIR` | Install destination for gomc source tree |
| `GOMC_LDFLAGS` | All `-X` flags for compile-time config |
| `GOMC_SRC_BASE` | Hand-written Go sources + go.mod (no generated files) |
| `GOMC_SRC` | Full source list including generated files |
| `GOMC_PKG_FILES` | Public package files copied to `share/` for RIP |

Set by `scripts/rip-environment` (RIP) or read from installed paths:

| Variable | RIP Value | Installed Value |
|----------|-----------|-----------------|
| `EMC2_GOMC_DIR` | `$EMC2_HOME/src/gomc` | `$prefix/share/linuxcnc/gomc` |
| `EMC2_CMOD_DIR` | `$EMC2_HOME/cmod` | `$prefix/lib/linuxcnc/cmod` |

### Migration Phases (All Complete)

#### Phase 1: Rename + Remove Plugin Infrastructure ✅

- Renamed `linuxcnc-launcher` binary to `gomc-server` (scripts, Submakefile, docs)
- Renamed `src/launcher/` directory to `src/gomc/`
- Renamed all `LAUNCHER_*` Submakefile variables to `GOMC_*`
- Renamed `EMC2LauncherDir` config field to `EMC2GomcDir`
- Created `pkg/gomc/gomc.go` — registration interface (`RegisterModule`, `GetFactory`, `HasModule`)
- Replaced `plugin.Open` with `pkg/gomc` registry lookup in `gomodules.go`
- Removed `-buildmode=plugin` build rules
- Removed `EMC2_GOMOD_DIR` from config

#### Phase 2: Move ads-server In-Tree ✅

- Moved `hal/proto/ads-server/` → `internal/ads/`, `internal/adsbridge/`, `internal/adsconfig/`
- Created `internal/adsmodule/module.go` with `init()` that registers "ads-server" factory
- Removed ads-server's separate `go.mod` and `go.work`
- Blank import in `cmd/gomc-server/main.go` triggers registration

#### Phase 3: Package Registry ✅

- Created `packages.conf` format (`TYPE IMPORT_PATH`)
- Created `pkgreg/registry.go` — registry reader/writer with `GenerateImports()`
- Created `imports_generated.go` generator
- Implemented `modcompile list`, `add-gomod`, `rm-gomod`, `rebuild`, `regenerate-imports`
- Implemented `dirMirror()` — pure Go directory copy replacing rsync
- Implemented `mergeGoDeps()` — parses external go.mod, runs `go get` for third-party deps
- Implemented `goModTidy()` — cleanup helper used by both add and rm paths
- Implemented `ensureRuntimeFiles()` — copies `.in` → runtime if missing

#### Phase 4: Merge gmicompile into modcompile ✅

- Moved gmicompile's CLI logic into `modcompile gmi` subcommand
- Updated `gmi/codegen/Submakefile` to use `modcompile gmi` instead of standalone `gmicompile`
- Removed standalone `cmd/gmicompile/` directory

#### Phase 5: Installed Build Support ✅

- `modcompile` uses `EMC2GomcDir` (injected via `-ldflags`) for all paths
- `--gomc-dir` CLI flag with `--launcher-dir` backward compat alias
- `--print-make-inc` emits `GOMC_DIR` variable for external Makefiles
- Compile-time config propagated to rebuilt binaries via `-ldflags -X`

### Implementation Findings

These are lessons learned during implementation that weren't anticipated in the
original design.

#### go.work Was Not Needed

The original design called for a `go.work` file to give external packages access
to the gomc module. In practice, `go.work` was unnecessary because:
- External packages are copied into `external/<name>/` inside the module tree
- Their `go.mod` is stripped — they become regular sub-packages of the module
- Third-party dependencies are merged into the main `go.mod` via `go get`

This is simpler and avoids `go.work` complexities (e.g., it prevents `go mod tidy`
from working normally).

#### Parallel Build Race Condition

With `make -j8`, the `imports_generated.go` rule originally called
`modcompile rebuild` which triggered `go build ./cmd/gomc-server`. This raced
with GMI codegen targets — `go build` would fail because generated packages
(like `generated/gmi/tp`) didn't exist yet.

**Fix**: Split into `regenerate-imports` (file generation only, used by Makefile)
and `rebuild` (generation + build, used interactively). The Makefile's
`../bin/gomc-server` target has explicit GMI prerequisites and handles the build.

#### Circular Makefile Dependencies

`GOMC_SRC_BASE` uses `$(wildcard gomc/cmd/*/*.go)` which, after the first build,
picks up `imports_generated.go`. This creates a cycle:
`modcompile` → `GOMC_SRC_BASE` → `imports_generated.go` → `modcompile`.

**Fix**: Define `IMPORTS_GENERATED` before `GOMC_SRC_BASE` and filter it out:
```makefile
GOMC_SRC_BASE := $(filter-out $(IMPORTS_GENERATED),$(wildcard gomc/cmd/*/*.go) ...) ...
```

#### Git Dirty State from Runtime Files

`packages.conf`, `go.mod`, `go.sum`, and `imports_generated.go` are modified by
`add-gomod`/`rm-gomod` and by `go build` (`go.sum` updates). Having these tracked
meant the git tree was always dirty after external module operations.

**Fix**: `.in` base file pattern:
- `packages.conf.in` and `go.mod.in` are tracked (minimal base state)
- Runtime copies are gitignored
- Makefile rules copy `.in` → runtime if missing (`test -f $@ || cp $< $@`)
- `modcompile ensureRuntimeFiles()` does the same before registry operations
- `git rm --cached` was needed to un-track previously committed runtime files

#### External Package go.mod Stripping

External packages can't keep their own `go.mod` inside the gomc module tree —
Go would treat them as separate modules. Instead:
- `dirMirror()` copies everything except `go.mod` and `go.sum`
- Third-party dependencies are extracted from the external `go.mod` via
  `go mod edit -json` and merged with `go get <dep>@<version>`
- Local `replace` directives and self-references are filtered out

#### `test -f` Guards for Idempotency

The `.in` → runtime copy rules use `test -f $@ || cp $< $@` rather than plain
`cp`. This ensures that `packages.conf` modified by `add-gomod` is not
overwritten on subsequent `make` invocations — the copy only happens when the
file doesn't exist at all.

#### modcompile Path After `cd gomc`

Submakefile rules that `cd gomc` before running commands need `$(TOP)/bin/modcompile`
(absolute path), not `../bin/modcompile` (which resolves relative to the new CWD,
giving `src/bin/modcompile` which doesn't exist).

### Go Package Requirements for `add-gomod` (Implemented)

External Go packages must follow these conventions to be compiled into
gomc-server:

1. **`go.mod`** at package root with proper module path
2. **`init()` function** that registers the package with the server via
   `gomc.RegisterModule(name, factory)` — the factory returns a `gomc.Module`
   with Start/Stop/Cleanup lifecycle hooks
3. **No `main` package** — the package is imported, not executed
4. **Compatible dependencies** — must build with the gomc module's Go version

Example minimal gomod:

```go
package mymodule

import "github.com/sittner/linuxcnc/src/gomc/pkg/gomc"

func init() {
    gomc.RegisterModule("mymodule", func(cfg gomc.ModuleConfig) (gomc.Module, error) {
        return &myModule{cfg: cfg}, nil
    })
}

type myModule struct {
    cfg gomc.ModuleConfig
}

func (m *myModule) Start() error { /* ... */ return nil }
func (m *myModule) Stop()        { /* ... */ }
func (m *myModule) Cleanup()     { /* ... */ }
```

The `gomc-stub/` directory pattern (from `go-comp-template`) provides local
development with a stub `pkg/gomc` so the package can be developed independently
and only needs the real gomc module when compiled into gomc-server.

#### External Makefile Integration

External packages use `modcompile --print-make-inc` to get the `GOMC_DIR` variable:

```makefile
$(eval $(shell modcompile --print-make-inc))
# Now GOMC_DIR is set to the gomc source directory

install:
    cd $(GOMC_DIR) && $(GOMC_DIR)/../bin/modcompile add-gomod $(CURDIR)

uninstall:
    cd $(GOMC_DIR) && $(GOMC_DIR)/../bin/modcompile rm-gomod $(notdir $(CURDIR))
```

### Configure Support for In-Tree Gomods

In-tree Go modules (like ads-server) should be selectable at configure time:

```
./configure --enable-ads-server    # default: enabled
./configure --disable-ads-server   # exclude from build
```

Configure writes the selection to a config file. The build system reads it to
determine which in-tree gomods are added to `packages.conf` and compiled into
gomc-server. This mirrors how optional C components (e.g., `--enable-pncconf`)
work today.

### Dependency Conflicts Between External Go Packages

All compiled-in packages share one dependency tree. When two external packages
require different versions of the same dependency, Go's MVS (Minimum Version
Selection) picks the highest version. This usually works, but can break if the
higher version has breaking API changes without a module path bump.

Mitigation (implemented):
- `modcompile add-gomod` merges dependencies via `go get` and then builds.
  If the combined build fails, the error is reported and the package is still
  added (the user can fix dependencies and run `modcompile rebuild`).
- `modcompile rm-gomod` runs `go mod tidy` to clean up orphaned dependencies.

Known constraint: *"All compiled-in packages share one dependency tree.
Adding a package that requires an incompatible version of a shared
dependency will fail at build time."*

### Impact on Existing Components

| Component | Change |
|-----------|--------|
| manualtoolchange (.comp with gmi_provide) | No change — cmod still loaded via dlopen, GMI dispatch compiled into server via generated package |
| ads-server | Moved from external plugin to `internal/ads/`, compiled in |
| kins/tp/home/mot | No change — already compiled into server via generated cgo packages |
| halcmd | No change — already compiled into server |
| External user modules (.comp) | No change — still compiled to cmod .so via `modcompile install` |
| External Go packages | `modcompile add-gomod` instead of building separate .so |
| Python UIs | No change — still REST clients |

## Open Questions

1. ~~**Versioning strategy**: How to handle API version mismatches?~~ **Resolved**: Exact match required, fail at lookup
2. ~~**Hot reload**: Can APIs be re-registered while running?~~ **Resolved**: No, lookup at startup only
3. ~~**Timeout handling**: Per-call timeouts? Global?~~ **Resolved**: No function timeouts, only HTTP transport
4. ~~**Error codes**: Standardize across Go/C boundary?~~ **Resolved**: errno for inter-module callbacks, GMI_ERR_* for client library
5. ~~**apiserver visibility**: Should `apiserver` move to `pkg/` for external Go packages, or use a thin `pkg/` registration interface?~~ **Resolved**: Thin `pkg/gomc` registration interface. External packages import `pkg/gomc` for types (`APIMeta`, `FuncMeta`, `RegisterMeta()`) + lifecycle hooks (`RegisterModule()`). `internal/apiserver` stays internal.
6. ~~**Module lifecycle for gomods**: External Go packages may need Start/Stop lifecycle (like ads-server). Define a registration mechanism in `pkg/` similar to the old `gomodule.Module` but without the plugin baggage?~~ **Resolved**: Mirror the cmod lifecycle. `pkg/gomc.RegisterModule()` registers Start/Stop/Cleanup hooks. The gomc-server calls these at the same lifecycle points it calls cmod equivalents. No plugin.Open — just init()-time registry lookup.

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
