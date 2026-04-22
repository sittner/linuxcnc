# go-comp-template

A skeleton Go module for LinuxCNC's gomc-server.

Go modules are compiled directly into the `gomc-server` binary using
`modcompile add-gomod`. They run in the same process and have direct access
to HAL shared memory — no inter-process communication required.

## Overview

The module registers itself at `init()` time via `gomc.RegisterModule()`.
When gomc-server processes a `load mygomodule` HAL command, it calls the
registered factory function to create an instance.

```
# In your .hal file:
load mygomodule [optional-arguments]
```

Arguments after the module name are passed to the factory as `args []string`.

## Requirements

- Go 1.22 or later (same version used to build LinuxCNC)
- CGO enabled (`CGO_ENABLED=1`)
- LinuxCNC development environment (headers + libraries)

## Building and Installing

```bash
# Source the LinuxCNC environment first:
source /path/to/linuxcnc/scripts/rip-environment

# Verify the module compiles:
make

# Install into gomc-server (copies source, rebuilds binary):
make install

# Remove from gomc-server:
make uninstall
```

`make install` runs `modcompile add-gomod .` which:
1. Copies your source into `src/gomc/external/mygomodule/`
2. Registers it in `packages.conf`
3. Regenerates `imports_generated.go`
4. Rebuilds `gomc-server`

Reinstalling from the same directory auto-overwrites the previous copy.
Installing from a different directory requires `modcompile add-gomod --force .`.

## Module Interface

Your module must call `gomc.RegisterModule` in an `init()` function:

```go
func init() {
    gomc.RegisterModule("mygomodule", newMyGoModule)
}
```

The factory function creates and initializes the module (including HAL
component and pin creation):

```go
func newMyGoModule(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomc.Module, error) {
    // Create HAL component, pins, etc.
    return &myModule{...}, nil
}
```

The `Module` interface has three lifecycle methods:

| Method      | Called when                              | Use for                                      |
|-------------|------------------------------------------|----------------------------------------------|
| `Start()`   | After HAL threads start                  | Start goroutines, open network connections   |
| `Stop()`    | During gomc-server cleanup               | Stop goroutines, close connections           |
| `Destroy()` | After all modules are stopped            | Release HAL components and allocated memory  |

## Usage in a HAL file

```
# Load the module with optional arguments:
load mygomodule config=/path/to/config.ini

# After loading, its HAL pins are available for wiring:
net my-signal mygomodule.in-f  some-component.output-pin
net my-signal mygomodule.out-f some-other-component.input-pin
```

## Customizing the Template

1. Rename the package — update `package mygomodule`, the `init()` registration
   name, and `MODULE_NAME` in the Makefile.
2. Replace the `myGoModule` struct and its `Start`/`Stop`/`Destroy` methods
   with your own implementation.
3. Update the factory function to create your module.
4. Update `go.mod` with your own module path.
5. Build with `make`, install with `make install`.
# go-comp-template

A skeleton Go plugin for LinuxCNC's in-process Go module loader.

Go plugins loaded by the launcher run in the same process as `gomc-server`
and have direct access to HAL shared memory — no inter-process communication
or HAL pins across process boundaries is required.

## Overview

The launcher supports a universal `load` HAL command that auto-detects whether
a `.so` file is a Go plugin or a C RT module and dispatches to the appropriate
loader.

```
# In your .hal file:
load /path/to/mygomodule.so <optional-arguments>
```

Everything after the module path is passed to the plugin's `New`
factory function as the `name string` and `args []string` arguments.

## Requirements

> **Important:** The plugin **must** be built with the **exact same Go version**
> and the **exact same dependency versions** as the `gomc-server` binary.
> A version mismatch causes `plugin.Open()` to fail at runtime with a clear
> error message.

- Linux only (Go plugin limitation — matches LinuxCNC's supported platforms)
- Go 1.21 or later
- CGO enabled (`CGO_ENABLED=1`)
- LinuxCNC headers available (`config.h`, `hal.h`)

## Building

```bash
# Source the LinuxCNC environment first:
source /path/to/linuxcnc/scripts/rip-environment

# Build with make (uses modcompile to locate paths):
make

# Install to the gomod directory:
make install
```

The Makefile uses `modcompile --print-make-inc` to obtain all necessary paths
(`GOMC_GO`, `GOMC_LAUNCHER_DIR`, `GOMC_GOMOD_DIR`, `GOMC_LIB_DIR`) automatically.

The resulting `mygomodule.so` can be installed to the gomod directory with
`make install`.

## Plugin Interface

Your plugin must export a symbol named `New` with type `gomodule.Factory`:

```go
var New gomodule.Factory = func(ini *inifile.IniFile, logger *slog.Logger, name string, args []string) (gomodule.Module, error) {
    return &myModule{ini: ini, logger: logger, name: name, args: args}, nil
}
```

The `Module` interface has three lifecycle methods:

| Method      | Called when                              | Use for                                      |
|-------------|------------------------------------------|----------------------------------------------|
| `Start()`   | After HAL threads start                  | Start goroutines, open network connections   |
| `Stop()`    | During launcher cleanup                  | Stop goroutines, close connections           |
| `Destroy()` | After all modules are stopped            | Release HAL components and allocated memory  |

## Usage in a HAL file

```
# Load the plugin — path can be absolute or a module name resolvable via gomod dir
load mygomodule.so config=/path/to/config.ini

# After the plugin is loaded, its HAL pins are available for wiring:
net my-signal go-passthrough.in-f  some-component.output-pin
net my-signal go-passthrough.out-f some-other-component.input-pin
```

## Notes on Go Plugin Limitations

- Go plugins can be **loaded but never unloaded**. `plugin.Open()` has no
  `Close()`. The `Stop()` method handles logical shutdown, but the code stays
  resident in memory until the process exits. This is fine for LinuxCNC, where
  components loaded at startup live for the entire machine session.

- All dependency versions (e.g. `github.com/sittner/linuxcnc/src/gomc/pkg/hal`, standard library, and any
  third-party packages) must match the versions used to build `gomc-server`
  at the time both binaries were compiled. A mismatch causes `plugin.Open()` to
  fail at runtime with a clear message like:
  `plugin was built with a different version of package github.com/sittner/linuxcnc/src/gomc/pkg/hal`

- To verify compatibility, compare the module info of both binaries:
  ```bash
  go version -m /usr/bin/gomc-server
  go version -m mygomodule.so
  ```
  The Go toolchain version and all shared dependency versions must match exactly.

- Building with the same Go toolchain (via `modcompile --go`) and using
  `go.work` to link against the launcher source (via `modcompile --launcher-dir`)
  is the easiest way to ensure version compatibility.

## Customizing the Template

1. Rename the module in `go.mod` to your own module path.
2. Replace the `passthroughModule` struct and its `Start`/`Stop`/`Destroy` methods
   with your own implementation.
3. Update `New` to create and return your module.
4. Build with `make` and load from your HAL file.
