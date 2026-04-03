# go-comp-template

A skeleton Go plugin for LinuxCNC's in-process Go module loader.

Go plugins loaded by the launcher run in the same process as `linuxcnc-launcher`
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

Everything after the module path is passed verbatim to the plugin's `New`
factory function as the `params string` argument.

## Requirements

> **Important:** The plugin **must** be built with the **exact same Go version**
> and the **exact same dependency versions** as the `linuxcnc-launcher` binary.
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
var New gomodule.Factory = func(ini *inifile.IniFile, logger *slog.Logger, params string) (gomodule.Module, error) {
    return &myModule{ini: ini, logger: logger, params: params}, nil
}
```

The `Module` interface has three lifecycle methods:

| Method   | Called when                              | Use for                                      |
|----------|------------------------------------------|----------------------------------------------|
| `Init()` | After `load`, before HAL file wiring     | Create HAL components and pins               |
| `Start()`| After HAL threads start                  | Start goroutines, open network connections   |
| `Stop()` | During launcher cleanup                  | Stop goroutines, close connections           |

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

- All dependency versions (e.g. `github.com/sittner/linuxcnc/src/launcher/pkg/hal`, standard library, and any
  third-party packages) must match the versions used to build `linuxcnc-launcher`
  at the time both binaries were compiled. A mismatch causes `plugin.Open()` to
  fail at runtime with a clear message like:
  `plugin was built with a different version of package github.com/sittner/linuxcnc/src/launcher/pkg/hal`

- To verify compatibility, compare the module info of both binaries:
  ```bash
  go version -m /usr/bin/linuxcnc-launcher
  go version -m mygomodule.so
  ```
  The Go toolchain version and all shared dependency versions must match exactly.

- Building with the same Go toolchain (via `modcompile --go`) and using
  `go.work` to link against the launcher source (via `modcompile --launcher-dir`)
  is the easiest way to ensure version compatibility.

## Customizing the Template

1. Rename the module in `go.mod` to your own module path.
2. Replace the `passthroughModule` struct and its `Init`/`Start`/`Stop` methods
   with your own implementation.
3. Update `New` to create and return your module.
4. Build with `make` and load from your HAL file.
